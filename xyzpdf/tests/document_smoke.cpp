// Opening a document, and everything the rest of the program asks of one.
//
// The fixtures are opened by relative path, which is why tools/build_all_tests.py
// runs the tests with the source directory as the working directory: a test
// that only passes from one particular directory is a test that cannot be run
// by hand when it fails.
//
// The damaged fixture is the one that matters most. A PDF reader that trusts
// its parser is a reader that crashes on the first file somebody downloaded
// badly, and "it does not crash" is a claim worth checking every time.

#include "../src/document/qtpdfbackend.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QString>

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

bool close(double left, double right, double tolerance = 0.5)
{
    return std::fabs(left - right) <= tolerance;
}

// How many pixels the page actually painted.
//
// A rendered page is ink at full alpha over *transparent* black, because a PDF
// page has no background of its own -- the white paper is supplied by whoever
// shows the result. So "is the image empty" has to be asked as "is anything
// opaque in it": a transparent image has the right size, is not null, and has
// nothing in it at all, which is exactly the shape of the bug that got through
// this test once.
int inkPixels(const QImage &image)
{
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                ++ink;
        }
    }
    return ink;
}

QRect inkBoxOf(const QImage &image)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                box = box.united(QRect(x, y, 1, 1));
        }
    }
    return box;
}

void checkSize(const QSizeF &actual, const QSizeF &expected, const QString &what)
{
    if (close(actual.width(), expected.width()) && close(actual.height(), expected.height()))
        return;
    std::cerr << "FAIL: " << what.toStdString() << " -- expected " << expected.width() << "x"
              << expected.height() << ", got " << actual.width() << "x" << actual.height()
              << '\n';
    ++failures;
}

QString fixture(const QString &directory, const QString &name)
{
    return directory + QLatin1Char('/') + name;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");

    // ---- an ordinary document ----
    {
        QtPdfBackend document;
        const Document::Status status = document.load(fixture(directory, QStringLiteral("text.pdf")),
                                                     QString());
        check(status == Document::Status::Ready, "a well-formed PDF opens");
        check(document.path().endsWith(QStringLiteral("text.pdf")), "and remembers where from");

        check(document.pageCount() == 2, "it has two pages");
        checkSize(document.pageSize(0), QSizeF(595, 842), "page 1 is A4");
        // The second page is deliberately a different size: a viewer that
        // assumes every page is the first one's is wrong on documents nobody
        // made on purpose, and it is the mistake this fixture exists for.
        checkSize(document.pageSize(1), QSizeF(400, 500), "page 2 is a different size");

        // ---- text ----
        const QString first = document.text(0);
        check(first.contains(QStringLiteral("XYZPDF one")), "the text of page 1 comes out");
        check(document.text(1).contains(QStringLiteral("XYZPDF two")),
              "and the text of page 2 as well");

        // ---- text boxes ----
        //
        // The boxes are what a selection and a search highlight are drawn from,
        // so they have to be in the page's own coordinates and inside it.
        const QVector<TextRun> runs = document.textRuns(0);
        check(!runs.isEmpty(), "page 1 has text boxes");
        const QRectF pageRect(QPointF(0, 0), document.pageSize(0));
        for (const TextRun &run : runs) {
            check(pageRect.intersects(run.box) || pageRect.contains(run.box),
                  "every text box is on the page");
            check(run.box.width() > 0 && run.box.height() > 0, "and has a size");
        }

        // ---- the two things a selection is made of ----
        //
        // A click is a point turned into a character index; a drag is two
        // indices turned back into text and boxes. Both directions are the
        // backend's, and both are checked here: a selection that is off by one
        // character selects the wrong word, and nothing else in the program can
        // tell that it did.
        {
            const QRectF run = runs.first().box;

            // The middle of the line. Qt PDF's own point query answers only when
            // the point lands on the ink of a glyph -- measured, and a point in
            // the gap between two letters comes back empty -- so the index is
            // found by searching the characters' boxes, and this is the check
            // that the answer is a character of the line the point is on.
            const int index = document.characterIndexAt(0, run.center());
            check(index >= 0 && index < first.length(),
                  QStringLiteral("a point on the line has an index into its text (%1 of %2)")
                      .arg(index)
                      .arg(first.length()));

            // Past the end of the text the answer is the end, not nothing: that
            // is where a selection dragged into the margin stops, and where a
            // click below the last line puts the cursor.
            const int beyond = document.characterIndexAt(0, QPointF(560, run.center().y()));
            check(beyond == first.length(),
                  QStringLiteral("a point past the end of the line is the end of the text (%1)")
                      .arg(beyond));
            check(document.characterIndexAt(0, QPointF(300, 5)) == 0,
                  "a point above the text is its beginning");
            check(document.characterIndexAt(0, QPointF(300, 830)) == first.length(),
                  "and a point below it is its end");
            check(document.characterIndexAt(9, run.center()) == -1,
                  "while a page with no text at all has no index to give");

            // ---- and back the other way ----
            const PageSelection part = document.pageSelection(0, index, index + 4);
            check(!part.text.isEmpty(), "a range at that index comes back with text");
            check(part.text == first.mid(index, 4),
                  QStringLiteral("which is the page's own text there (\"%1\")").arg(part.text));
            check(!part.boxes.isEmpty(), "with boxes to draw it with");
            for (const QRectF &box : part.boxes)
                check(pageRect.intersects(box) || pageRect.contains(box), "all of them on the page");

            const PageSelection line = document.pageSelection(0, 0, first.length());
            check(line.text == first, "the whole line comes back whole");
            check(!line.boxes.isEmpty(), "with a box to draw it in");

            // ---- over text, or merely on the page ----
            //
            // The question the pointer asks, and a different one from "which
            // character is at this point": that is answered for every point on
            // a page that has any text at all, which is where a drag past the
            // end of a line has to stop. This one is true only near the ink.
            check(document.hasTextAt(0, run.center()), "the middle of the line is over text");
            check(!document.hasTextAt(0, QPointF(300, 5)), "the top margin is not");
            check(!document.hasTextAt(0, QPointF(run.right() + 200, run.center().y())),
                  "and neither is the right margin");
            check(!document.hasTextAt(0, QPointF(300, 830)), "nor the space below the text");
            // Just off a glyph is still on the line: the tolerance is about the
            // width of a space, because a cursor that drops back to an arrow
            // inside every word space looks like the program cannot decide.
            check(document.hasTextAt(0, QPointF(run.right() + 2, run.center().y())),
                  "but the far end of a glyph's own line is");
            check(!document.hasTextAt(2, run.center()), "and a page with no text has none");

            // A range that is not a range. These are the difference between a
            // click and a select-all, and the backend has to answer nothing
            // rather than everything.
            check(document.pageSelection(0, 5, 5).text.isEmpty(), "an empty range selects nothing");
            check(document.pageSelection(0, 5, 4).text.isEmpty(),
                  "and neither does a backwards one");
            check(document.pageSelection(9, 0, 5).text.isEmpty(), "nor a page that is not there");
        }

        // ---- the things this fixture does not have ----
        check(document.outline().isEmpty(), "a document with no bookmarks has an empty outline");
        check(document.links(0).isEmpty(), "and no links");

        // ---- metadata: whatever the file says, asked without crashing ----
        const DocMetadata metadata = document.metadata();
        check(!metadata.encrypted, "an unencrypted document does not claim to be encrypted");

        // ---- rendering, synchronously ----
        //
        // The view does not use this path -- it goes through PageRenderer -- but
        // the export and print paths will, and so does this test: it is the one
        // way to check that a page comes back at the size that was asked for.
        const QImage image = document.render(0, QSize(200, 283), RenderOptions{});
        check(!image.isNull(), "a page renders");
        check(image.size() == QSize(200, 283), "at exactly the size it was asked for");
        // And it is not empty. A QImage with nothing drawn in it is not null,
        // and is the same size as one with the page in it -- so for a long time
        // this test passed on a fully transparent image, which is what a
        // recognition crop turned out to be: "it renders" is not the same
        // question as "there is a page in it".
        check(inkPixels(image) > 0,
              QStringLiteral("with the page's ink in it (%1 opaque pixels)").arg(inkPixels(image)));

        const QImage turned = document.render(0, QSize(283, 200),
                                             RenderOptions{90, true, false});
        check(turned.size() == QSize(283, 200),
              "and a rotated render is the size that was asked for, not a rotated one");

        // ---- a band of a page, which is what a zoomed-in page costs ----
        //
        // Three different sizes are involved and Qt will not tell you that they
        // are different: the page is scaled to `scaledSize`, the band is named
        // in that scale, and the QSize asked for is the size of the image that
        // comes back. Asking for a page-sized image and a small clip gets a
        // page-sized image with the band drawn in one corner -- the whole
        // memory the banding was for. This is measured rather than read out of
        // the documentation, because there is no Qt source on this machine.
        // The band is over the line of text the fixture draws near the top left
        // of the page, and deliberately so: a band from an empty part of the
        // page is a band that compares equal to another empty one, which is how
        // this check passed for two milestones without comparing anything at
        // all.
        const QSize wholePage(600, 849);
        const QRect band(60, 60, 200, 40);
        const QImage whole = document.render(0, wholePage, RenderOptions{});

        RenderOptions banded;
        banded.clip = band;
        banded.scaledSize = wholePage;
        const QImage part = document.render(0, band.size(), banded);
        check(part.size() == band.size(),
              QStringLiteral("a band comes back the size of the band (%1x%2)")
                  .arg(part.width())
                  .arg(part.height()));
        check(!part.isNull(), "with something in it");

        // And it is the same part of the page: a band that came back the right
        // size but from somewhere else would put a strip of the wrong text on
        // screen, which is worse than no band at all.
        //
        // Compared by where its ink is and how much of it there is, not byte by
        // byte, and the difference is measured rather than assumed: the two
        // paths rasterise the same glyphs from slightly different subpixel
        // origins (the band has its own clip origin), so every antialiased edge
        // pixel differs -- on this fixture all ~1200 ink pixels do, while the
        // ink's bounding box is one pixel off in two directions. Byte equality
        // was never the question, and asserting it passed only because the band
        // used to be taken from an empty part of the page, where two empty
        // pictures are of course identical.
        //
        // A strip from the wrong part of the page moves that box by far more
        // than a pixel, or changes the amount of ink entirely.
        const QRect partInk = inkBoxOf(part);
        const QRect wholeInk = inkBoxOf(whole.copy(band));
        const auto within = [](int left, int right) { return std::abs(left - right) <= 2; };
        check(within(partInk.x(), wholeInk.x()) && within(partInk.y(), wholeInk.y())
                  && within(partInk.width(), wholeInk.width())
                  && within(partInk.height(), wholeInk.height()),
              QStringLiteral("and its ink is where the page's ink is (band %1,%2 %3x%4 against "
                             "%5,%6 %7x%8)")
                  .arg(partInk.x())
                  .arg(partInk.y())
                  .arg(partInk.width())
                  .arg(partInk.height())
                  .arg(wholeInk.x())
                  .arg(wholeInk.y())
                  .arg(wholeInk.width())
                  .arg(wholeInk.height()));

        const int partInkPixels = inkPixels(part);
        const int wholeInkPixels = inkPixels(whole.copy(band));
        check(std::abs(partInkPixels - wholeInkPixels) * 10 < qMax(1, wholeInkPixels),
              QStringLiteral("with about as much of it (%1 against %2 opaque pixels)")
                  .arg(partInkPixels)
                  .arg(wholeInkPixels));

        // Both images being empty would make every comparison above pass
        // without comparing anything, which is what used to happen.
        check(inkPixels(whole) > 0 && partInkPixels > 0,
              QStringLiteral("with ink in both of them (%1 and %2 opaque pixels)")
                  .arg(inkPixels(whole))
                  .arg(partInkPixels));
    }

    // ---- a point on a page with more than one line ----
    //
    // The bug this is here for, and the reason it is about line breaks rather
    // than about scripts: the index at a point used to be found by bisecting on
    // "is this character before the point", which stops being monotone the
    // moment a page has a line break in it -- a line break has no box, and the
    // comparison treated it as being above the point, so it counted as "before"
    // wherever the point was. Every line but the first answered with an index
    // from somewhere else in the document.
    //
    // It was reported as "selection works in English and not in Chinese". The
    // document was Chinese and had more than one line per page; every fixture
    // here had exactly one line per page, so the line break -- the actual cause
    // -- was in none of them.
    {
        QtPdfBackend document;
        const QString path = fixture(directory, QStringLiteral("lines.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the multi-line fixture opens");

            const QString pageText = document.text(0);
            const QVector<TextRun> lines = document.textRuns(0);
            check(lines.size() == 3,
                  QStringLiteral("it has three lines (got %1)").arg(lines.size()));

            const QStringList expected = {QStringLiteral("XYZPDF first line"),
                                          QStringLiteral("second line here"),
                                          QStringLiteral("third line")};
            for (int line = 0; line < qMin(lines.size(), expected.size()); ++line) {
                const QRectF box = lines.at(line).box;
                const int start = pageText.indexOf(expected.at(line));
                check(start >= 0,
                      QStringLiteral("line %1 is in the page's text").arg(line + 1));
                if (start < 0)
                    continue;

                // A point in the middle of the line, and the index it answers
                // with -- which has to be an index *of that line*. Any index
                // inside the page passes a weaker test; that is exactly what the
                // bisection returned.
                const QPointF centre = box.center();
                const int index = document.characterIndexAt(0, centre);
                check(index >= start && index <= start + expected.at(line).length(),
                      QStringLiteral("a click at the centre of line %1 lands in line %1 "
                                     "(index %2, line runs %3 to %4)")
                          .arg(line + 1)
                          .arg(index)
                          .arg(start)
                          .arg(start + expected.at(line).length()));
                check(document.hasTextAt(0, centre),
                      QStringLiteral("and line %1 is reported as text under the pointer").arg(line + 1));

                // And the character just before that index is on that line, by
                // its own box: the index being in range is not enough on its own
                // if the characters it points at are somewhere else.
                const int before = qBound(0, index - 1, pageText.length() - 1);
                const PageSelection one = document.pageSelection(0, before, before + 1);
                if (!one.boxes.isEmpty()) {
                    const double y = one.boxes.first().center().y();
                    check(y >= box.top() - 2 && y <= box.bottom() + 2,
                          QStringLiteral("and the character before it (index %1, y %2) is on that line "
                                         "(y %3 to %4)")
                              .arg(before)
                              .arg(y)
                              .arg(box.top())
                              .arg(box.bottom()));
                }

                // The margins are still not text: below the last line and beside
                // the first one, the pointer is over paper.
                check(!document.hasTextAt(0, QPointF(box.right() + 60, centre.y())),
                      QStringLiteral("line %1: the margin beside it is not text").arg(line + 1));
            }

            // A page whose text is one line long was never the problem, and has
            // to keep working: the fixture the rest of this file uses.
            check(!document.hasTextAt(0, QPointF(300, 800)),
                  "nor is the space below the last line");
        }
    }

    // ---- a page stored portrait and marked 90 degrees ----
    //
    // Measured rather than assumed. The fixture's MediaBox is 300x400 and its
    // /Rotate is 90, and Qt PDF reports 400x300: the page's own rotation is
    // already folded in. So QtPdfBackend::pageSize must not apply it again,
    // which would turn a page marked 90 into a page marked 180 -- a mistake
    // that is invisible until somebody opens a scan that was fed in sideways.
    {
        QtPdfBackend document;
        check(document.load(fixture(directory, QStringLiteral("rotated.pdf")), QString())
                  == Document::Status::Ready,
              "the rotated fixture opens");
        check(document.pageCount() == 1, "it has one page");
        checkSize(document.pageSize(0), QSizeF(400, 300),
                  "a page marked 90 is reported the way up it is read");
        check(document.pageSize(0) == document.pageSize(0), "and reports the same thing twice");
    }

    // ---- a file that is not a PDF ----
    {
        QtPdfBackend document;
        const Document::Status status =
            document.load(fixture(directory, QStringLiteral("damaged.pdf")), QString());
        check(status != Document::Status::Ready, "a truncated PDF does not open");
        check(status == Document::Status::InvalidFormat
                  || status == Document::Status::FileError,
              "and is reported as unreadable rather than as needing a password");
        check(!document.lastError().isEmpty(), "with something to tell the user");
        check(document.pageCount() == 0, "and no pages");

        // Everything below is asked of a document that is not open, which is
        // where a backend that missed a guard crashes.
        check(document.text(0).isEmpty(), "text of a document that is not open is empty");
        check(document.textRuns(0).isEmpty(), "and so are its text boxes");
        check(document.outline().isEmpty(), "and its outline");
        check(document.links(0).isEmpty(), "and its links");
        check(document.render(0, QSize(100, 100), RenderOptions{}).isNull(),
              "and it does not render");
    }

    // ---- a file that is not there ----
    {
        QtPdfBackend document;
        const Document::Status status =
            document.load(fixture(directory, QStringLiteral("no-such-file.pdf")), QString());
        check(status == Document::Status::FileError, "a missing file is a file error");
        check(!document.lastError().isEmpty(), "with a sentence about it");
    }

    // ---- pages that are not there ----
    {
        QtPdfBackend document;
        document.load(fixture(directory, QStringLiteral("text.pdf")), QString());
        check(document.pageSize(-1).isEmpty(), "there is no page before the first");
        check(document.pageSize(2).isEmpty(), "and none after the last");
        check(document.render(-1, QSize(100, 100), RenderOptions{}).isNull(),
              "which does not render either");
        check(document.render(99, QSize(100, 100), RenderOptions{}).isNull(), "nor does that");
        check(document.render(0, QSize(0, 0), RenderOptions{}).isNull(),
              "and a page asked for at no size at all comes back empty rather than null-crashing");
    }

    // ---- opening a second document in the same backend ----
    //
    // The window keeps one backend per tab and reuses it when the tab is given
    // another file, so this is a real path rather than a hypothetical one:
    // whatever was open has to be gone, and nothing of it may be reported as
    // belonging to the new one.
    {
        QtPdfBackend document;
        document.load(fixture(directory, QStringLiteral("text.pdf")), QString());
        check(document.pageCount() == 2, "the first document is open");
        check(document.load(fixture(directory, QStringLiteral("rotated.pdf")), QString())
                  == Document::Status::Ready,
              "the second one opens in the same backend");
        check(document.pageCount() == 1, "and the first one's pages are gone");
        check(document.path().endsWith(QStringLiteral("rotated.pdf")), "and so is its path");

        // Now a failure, which has to leave the backend empty rather than
        // half-believing it still has the previous document.
        check(document.load(fixture(directory, QStringLiteral("damaged.pdf")), QString())
                  != Document::Status::Ready,
              "a failure after a success fails");
        check(document.pageCount() == 0, "and leaves no pages behind");
        check(document.path().isEmpty(), "and no path");
    }

    if (failures == 0) {
        std::cout << "the document backend holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
