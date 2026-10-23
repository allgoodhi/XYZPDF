// Measures one document: how long its pages take to render, how many pixels
// that is, how many of them fit in the render cache's budget, what the text
// layer of one page looks like, and what a search for a phrase would find.
//
//     xyzpdf_pdfprobe <file.pdf> [samples] [viewport width] [text page] [needle]
//
// Not a test -- it prints numbers rather than passing or failing, which is why
// tools/build_all_tests.py skips anything with "probe" in its name. It exists
// because "the program is slow with my document" is not a question that can be
// answered from a description of the document: whether the time goes on
// rendering or on memory depends on whether the pages are text or scans, how
// big they are, and what the screen is, and the numbers have to come off the
// machine where it happens.
//
// Run it with the built program beside it: the Qt runtime is deployed next to
// xyzpdf.exe, and this needs the same libraries.

// Relative, like every other include from outside the project root: the root
// itself is not on the include path.
#include "../src/document/document.h"
#include "../src/document/documentfactory.h"
#include "../src/document/pagemetrics.h"
#include "../src/document/textsearch.h"
#include "../src/render/rendercache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

QString megabytes(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2) {
        std::cerr << "usage: pdfprobe <file.pdf> [samples] [viewport width]\n";
        return 2;
    }

    const QString path = arguments.at(1);
    const int samples = arguments.size() > 2 ? arguments.at(2).toInt() : 12;
    const int viewportWidth = arguments.size() > 3 ? arguments.at(3).toInt() : 2500;

    // Owned through the interface and read through a reference: which backend
    // this is belongs to the factory, and the measurements below are the same
    // questions whichever one answers them.
    const std::unique_ptr<Document> owned = createDocument();
    Document &document = *owned;
    const Document::Status status = document.load(path, QString());
    if (status != Document::Status::Ready) {
        std::cerr << "could not open " << path.toStdString() << ": "
                  << document.lastError().toStdString() << '\n';
        return 1;
    }

    const int pages = document.pageCount();
    std::cout << "file:   " << path.toStdString() << '\n';
    std::cout << "pages:  " << pages << '\n';
    if (pages == 0)
        return 0;
    const QSizeF first = document.pageSize(0);
    std::cout << "page 1: " << first.width() << " x " << first.height() << " pt\n";

    // The zoom the view opens at: fit width for a maximised window.
    const double fitWidth = (viewportWidth - 2 * 12.0) / first.width();
    std::cout << "\nfit width for a " << viewportWidth << " px viewport: "
              << QStringLiteral("%1%").arg(fitWidth * 100, 0, 'f', 1).toStdString() << '\n';

    // Sample across the document rather than the first pages: a scan at the
    // end and a text page at the front have nothing in common.
    std::vector<int> chosen;
    for (int index = 0; index < samples; ++index) {
        const int page = pages == 1 ? 0 : int(std::llround(double(index) * (pages - 1)
                                                          / (samples - 1)));
        if (chosen.empty() || chosen.back() != page)
            chosen.push_back(page);
    }

    std::cout << "\npage      size (px)          bytes      render\n";
    std::vector<double> milliseconds;
    qint64 firstPageBytes = 0;
    for (const int page : chosen) {
        const QSize pixels = PageMetrics::pixelsFor(document.pageSize(page), fitWidth, 1.0);
        QElapsedTimer timer;
        timer.start();
        const QImage image = document.render(page, pixels, RenderOptions{});
        const double elapsed = timer.elapsed();
        milliseconds.push_back(elapsed);
        if (page == 0)
            firstPageBytes = image.isNull() ? 0 : qint64(image.sizeInBytes());
        std::cout << QStringLiteral("%1%2%3%4")
                         .arg(page + 1, 4)
                         .arg(QStringLiteral("%1x%2").arg(pixels.width()).arg(pixels.height()), 16)
                         .arg(image.isNull() ? QStringLiteral("  (none)") : megabytes(image.sizeInBytes()), 10)
                         .arg(QStringLiteral("%1 ms").arg(elapsed, 0, 'f', 0), 12)
                         .toStdString()
                  << '\n';
    }

    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "\nrender time: median " << milliseconds.at(milliseconds.size() / 2) << " ms"
              << ", worst " << milliseconds.back() << " ms\n";
    std::cout << "one page at this zoom: " << megabytes(firstPageBytes).toStdString() << '\n';
    if (firstPageBytes > 0) {
        std::cout << "a " << megabytes(RenderCache::DefaultMaximumBytes).toStdString()
                  << " cache holds " << RenderCache::DefaultMaximumBytes / firstPageBytes
                  << " pages of that size\n";
    }

    // ---- the text layer, which is what a mouse drag selects from ----
    //
    // Added because "selection does not work in this document" is a question
    // that cannot be answered from outside the program: what matters is whether
    // the page has text at all, how long it is, where its boxes are, and what
    // index a point on it comes back as -- and every one of those differs
    // between a Latin page and a CJK one, which is where the difference showed
    // up. The numbers here are what turned "Chinese cannot be selected" into a
    // cause.
    {
        const int page = arguments.size() > 4 ? arguments.at(4).toInt() - 1 : 0;
        if (page < 0 || page >= pages) {
            std::cout << "\nno page " << page + 1 << " to report the text of\n";
        } else {
            const QString text = document.text(page);
            QString sample = text.left(60);
            sample.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            std::cout << "\ntext on page " << page + 1 << ": " << text.length()
                      << " characters, first 60: \"" << sample.toStdString() << "\"\n";

            const QVector<TextRun> runs = document.textRuns(page);
            std::cout << "text runs (line boxes): " << runs.size() << '\n';
            for (int index = 0; index < runs.size() && index < 3; ++index) {
                const QRectF &box = runs.at(index).box;
                std::cout << "  run " << index << ": " << box.x() << ',' << box.y() << ' '
                          << box.width() << 'x' << box.height() << '\n';
            }

            // Per-character boxes, over the first few characters and over the
            // first few that are not Latin. Both, because the two come back
            // differently: what is measured here is that a CJK character's box
            // is empty from this API, and what the character itself comes back
            // as, which is where the index space was suspect.
            const auto describeCharacter = [&document, page, &text](int index) {
                const PageSelection one = document.pageSelection(page, index, index + 1);
                QString box = QStringLiteral("(none)");
                if (!one.boxes.isEmpty()) {
                    box = QStringLiteral("%1,%2 %3x%4")
                              .arg(one.boxes.first().x())
                              .arg(one.boxes.first().y())
                              .arg(one.boxes.first().width())
                              .arg(one.boxes.first().height());
                }
                const QString code =
                    QStringLiteral("U+%1")
                        .arg(int(text.at(index).unicode()), 4, 16, QLatin1Char('0'));
                std::cout << "  " << index << ". " << code.toStdString() << " ["
                          << QString(text.at(index)).toStdString() << "] -> "
                          << int(one.boxes.size()) << " box(es), first " << box.toStdString()
                          << ", api text [" << one.text.toStdString() << "]\n";
            };

            std::cout << "per-character boxes, first 6:\n";
            for (int index = 0; index < qMin(6, text.length()); ++index)
                describeCharacter(index);

            std::cout << "per-character boxes, first 6 that are not Latin:\n";
            int shown = 0;
            for (int index = 0; index < text.length() && shown < 6; ++index) {
                if (text.at(index).unicode() > 0x2E80) {
                    describeCharacter(index);
                    ++shown;
                }
            }

            // And the characters the API has no box for at all -- a newline is
            // the usual one. They are the reason a binary search over "is this
            // character before the point" lands somewhere arbitrary: they are
            // holes in the middle of the answer.
            int withoutBox = 0;
            QString firstWithout;
            for (int index = 0; index < text.length(); ++index) {
                if (document.pageSelection(page, index, index + 1).boxes.isEmpty()) {
                    ++withoutBox;
                    if (firstWithout.size() < 12) {
                        firstWithout += QStringLiteral("%1[%2] ")
                                            .arg(index)
                                            .arg(QString(text.at(index)).replace('\n',
                                                                                 QStringLiteral("\\n"))
                                                     .replace('\r', QStringLiteral("\\r")));
                    }
                }
            }
            std::cout << "characters with no box: " << withoutBox << " of " << text.length()
                      << "; first: " << firstWithout.toStdString() << '\n';

            // Where the characters sit, every so many of them. Whether this list
            // goes down the page in order is the question a bisection over the
            // index depends on: the search assumes that a character later in the
            // text is never higher up or further left than an earlier one, and a
            // document that lays its text out in another order breaks that
            // silently -- it does not fail, it answers somewhere else.
            std::cout << "index -> box, every " << qMax(1, text.length() / 12) << " characters:\n";
            const int step = qMax(1, text.length() / 12);
            for (int index = 0; index < text.length(); index += step) {
                const QRectF box = document.pageSelection(page, index, index + 1).boxes.isEmpty()
                                       ? QRectF()
                                       : document.pageSelection(page, index, index + 1)
                                             .boxes.first();
                std::cout << QStringLiteral("  %1: %2,%3 %4x%5")
                                 .arg(index, 4)
                                 .arg(box.x(), 6, 'f', 0)
                                 .arg(box.y(), 6, 'f', 0)
                                 .arg(box.width(), 5, 'f', 0)
                                 .arg(box.height(), 5, 'f', 0)
                                 .toStdString()
                          << '\n';
            }

            // What a click at the centre of each of the first few lines answers
            // with. This is the measurement the "Chinese cannot be selected"
            // report turned into: the answer has to be an index of *that* line,
            // and before the fix it was an index of another one.
            for (int index = 0; index < runs.size() && index < 6; ++index) {
                const QPointF at = runs.at(index).box.center();
                const int answered = document.characterIndexAt(page, at);
                std::cout << "  centre of run " << index << " (" << at.x() << ',' << at.y()
                          << ") -> index " << answered << ", hasTextAt "
                          << (document.hasTextAt(page, at) ? "yes" : "no") << '\n';
                if (answered > 0 && answered <= text.length()) {
                    const PageSelection near = document.pageSelection(page, answered - 1, answered);
                    if (!near.boxes.isEmpty()) {
                        std::cout << "      which is the character before [" << near.text.toStdString()
                                  << "] at " << near.boxes.first().center().y() << " (the line is at "
                                  << runs.at(index).box.center().y() << ")\n";
                    }
                }
            }

            // What the per-character geometry costs, since it is what a click
            // needs: one PDFium lookup per character. Timed apart, because the
            // answer decides whether it can be built on the thread that draws.
            {
                QElapsedTimer timer;
                timer.start();
                const QString whole = document.text(page);
                std::cout << "the page's text: " << timer.elapsed() << " ms (" << whole.length()
                          << " characters)\n";
                timer.restart();
                for (int index = 0; index < 20; ++index)
                    document.pageSelection(page, index, index + 1);
                std::cout << "20 single-character selections: " << timer.elapsed() << " ms\n";

                // Which is why the index at a point is found by bisection --
                // about twelve of those calls per click, and the page's text is
                // already cached by then.
                if (!runs.isEmpty()) {
                    timer.restart();
                    document.characterIndexAt(page, runs.first().box.center());
                    std::cout << "one click at a point (bisection): " << timer.elapsed() << " ms\n";
                    timer.restart();
                    document.hasTextAt(page, runs.first().box.center());
                    std::cout << "one hover test (line box only): " << timer.elapsed() << " ms\n";
                }
            }
        }
    }

    // ---- what a search for a phrase would find ----
    //
    // The same matching the program does, page by page, which answers the two
    // questions a reader asks about a document they cannot search: does it have
    // a text layer at all, and is the phrase in it? A scan answers "0
    // characters" and "0 hits" -- and the answer to that is the recognition
    // tool, not a better search.
    if (arguments.size() > 5) {
        const QString needle = arguments.at(5);
        int total = 0;
        int pagesWithText = 0;
        std::cout << "\nsearching for \"" << needle.toStdString() << "\"\n";
        for (int page = 0; page < pages; ++page) {
            const QString pageText = document.text(page);
            if (!pageText.isEmpty())
                ++pagesWithText;
            const QVector<TextSearch::Match> found =
                TextSearch::matchesInPage(page, pageText, needle, false);
            if (found.isEmpty())
                continue;
            total += found.size();
            std::cout << "  page " << page + 1 << ": " << found.size() << " hit(s), first at "
                      << found.first().from << " ["
                      << pageText.mid(found.first().from, needle.length()).toStdString() << "]\n";
        }
        std::cout << total << " hit(s) in " << pages << " page(s); " << pagesWithText
                  << " of them have any text at all\n";
    }

    // And what a band of a zoomed-in page costs, which is what the view does
    // above a few hundred percent.
    {
        const QSize whole = PageMetrics::pixelsFor(document.pageSize(0), 4.0, 1.0);
        const QRect band(0, 0, std::min(whole.width(), 2500), std::min(whole.height(), 1400));
        RenderOptions options;
        options.clip = band;
        options.scaledSize = whole;
        QElapsedTimer timer;
        timer.start();
        const QImage image = document.render(0, band.size(), options);
        std::cout << "\nat 400%: whole page would be "
                  << megabytes(qint64(whole.width()) * whole.height() * 4).toStdString()
                  << "; the band on screen is " << megabytes(image.sizeInBytes()).toStdString()
                  << " and took " << timer.elapsed() << " ms\n";
    }

    return 0;
}
