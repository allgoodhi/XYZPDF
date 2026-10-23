// The document engine, asserted on its own rather than against a second one.
//
// ---- what this file is now ----
//
// It began in F7-B as the comparison docs/pdfium-plan.md sec.10 asked for: the
// same file read through both backends, page count, every page size, the text of
// every page, and a render of every page compared pixel by pixel. F7-E retired
// the engine on the left of that comparison, and with it the columns that needed
// two answers to be read against. What is left is what a single engine can be
// held to without one: every page of every committed fixture opens, is sized, is
// read and is drawn; the geometry the view depends on (rotation, and a band of a
// page at 2x) lands where the page itself lands; a failed load gets a status and
// a sentence; and the file handle behaves the way F7 changed it to.
//
// The pixel numbers that used to live in the comparison are not lost: they are in
// docs/pdfium-plan.md sec.5.1, measured by the F5 probe, and the ones that decided
// the swap are quoted from there. This file no longer prints them, because there
// is nothing here to print them against.
//
// ---- the second half is the reason the file opened at all ----
//
// docs/handoff-03.md F7 item 3 / docs/pdfium-plan.md R7 changes how the file is
// opened: our own handle with FILE_SHARE_READ | WRITE | DELETE, so that a save by
// replacement (F8) can move a new file over the one on screen. Those are
// assertions about the PDFium backend with no other engine to compare them to:
// the control for the handle half is in the record -- removing FILE_SHARE_DELETE
// from pdfiumbackend.cpp turns the delete and the replace checks red (the
// watcher's own file is not held open, so it stays green: it is asserting
// FileChangeNotice, not our handle).
//
// Run by tools/build_all_tests.py like every other test.

#include "../src/document/pdfiumbackend.h"
#include "../src/services/filechangenotice.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

void note(const QString &what)
{
    std::cout << what.toStdString() << '\n';
}

QString fixture(const QString &directory, const QString &name)
{
    return directory + QLatin1Char('/') + name;
}

bool close(double left, double right, double tolerance = 0.01)
{
    return std::fabs(left - right) <= tolerance;
}

// The ink on a page: how much of it, and where. A page that renders to a blank
// rectangle of the right size passes every size check in this file, which is how
// the F5 probe first counted the paper itself as ink (docs/pdfium-plan.md sec.3,
// C8) and how render_smoke's transparent-image bug survived two milestones.
int inkPixels(const QImage &image, int threshold = 250)
{
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qGray(line[x]) < threshold)
                ++ink;
    }
    return ink;
}

QRect inkBox(const QImage &image)
{
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(line[x]) >= 250)
                continue;
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    if (right < left || bottom < top)
        return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

// How two pictures of the same thing differ, which is asked here of one engine
// with itself: a band of a page against the same page rendered whole and cropped.
struct Difference
{
    bool sameSize = false;
    int differing = 0;
    int maxChannel = 0;
};

Difference comparePixels(const QImage &left, const QImage &right)
{
    Difference result;
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return result;
    result.sameSize = true;
    const QImage a = left.convertToFormat(QImage::Format_RGB32);
    const QImage b = right.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < a.height(); ++y) {
        const auto *lineA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *lineB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int worst = qMax(qRed(lineA[x]) - qRed(lineB[x]),
                                   qMax(qGreen(lineA[x]) - qGreen(lineB[x]),
                                        qBlue(lineA[x]) - qBlue(lineB[x])));
            const int distance = qAbs(worst);
            if (distance != 0)
                ++result.differing;
            result.maxChannel = qMax(result.maxChannel, distance);
        }
    }
    return result;
}

QString describeDifference(const Difference &difference)
{
    return QStringLiteral("%1 pixels differ, max channel %2")
        .arg(difference.differing)
        .arg(difference.maxChannel);
}

// ---- the page-by-page walk ----

struct Sample
{
    const char *name;
    // Pages with a text layer. A scan and a blank page answer "" for every page,
    // and a reader that says otherwise is reading something that is not there.
    bool hasText;
    // Rendered pages, spread across the document. All of them for a small
    // fixture; a sample for huge.pdf, where a full render per page is minutes of
    // work for a number that does not change across pages.
    int renders;
};

void checkDocument(const QString &directory, const Sample &sample)
{
    const QString path = fixture(directory, QString::fromLatin1(sample.name));
    if (!QFileInfo::exists(path)) {
        check(false, QStringLiteral("%1 is missing; run tools/make_fixtures.py").arg(path));
        return;
    }

    PdfiumBackend pdfium;
    const Document::Status status = pdfium.load(path, QString());
    check(status == Document::Status::Ready,
          QStringLiteral("%1 opens (%2)").arg(sample.name).arg(
              status == Document::Status::Ready ? QStringLiteral("ready")
                                                : pdfium.lastError()));
    if (status != Document::Status::Ready)
        return;

    const int pages = pdfium.pageCount();
    check(pages > 0, QStringLiteral("%1 has pages to count (%2)").arg(sample.name).arg(pages));

    // Every page sized, and every page's text asked for: the size walk is the row
    // sec.5.2 says the F5 probe never took (it looked at page one, and the layout
    // uses all of them), and asking every page is what makes a per-page cache bug
    // show up on the page nobody looks at first.
    int withText = 0;
    for (int page = 0; page < pages; ++page) {
        const QSizeF size = pdfium.pageSize(page);
        check(size.width() > 0 && size.height() > 0,
              QStringLiteral("%1 page %2 has a size (%3x%4)")
                  .arg(sample.name)
                  .arg(page + 1)
                  .arg(size.width())
                  .arg(size.height()));
        if (!pdfium.text(page).isEmpty())
            ++withText;
    }
    check(sample.hasText ? withText > 0 : withText == 0,
          QStringLiteral("%1: %2 of %3 pages have a text layer, and the fixture is %4")
              .arg(sample.name)
              .arg(withText)
              .arg(pages)
              .arg(sample.hasText ? QStringLiteral("supposed to") : QStringLiteral("not")));

    // ---- the rendered pages ----
    const int toRender = qMin(sample.renders, pages);
    const int step = qMax(1, pages / toRender);
    int rendered = 0;
    int empties = 0;
    for (int page = 0; page < pages && rendered < toRender; page += step) {
        const QSizeF points = pdfium.pageSize(page);
        const QSize pixels(qRound(points.width() * 96.0 / 72.0),
                           qRound(points.height() * 96.0 / 72.0));
        // The view's options: annotations on, subpixel text off (PageRenderer
        // never asks for it; image export and print explicitly do not).
        RenderOptions options;
        const QImage image = pdfium.render(page, pixels, options);
        check(!image.isNull(),
              QStringLiteral("%1 page %2 renders").arg(sample.name).arg(page + 1));
        check(image.size() == pixels,
              QStringLiteral("%1 page %2: the image is %3x%4, asked for %5x%6")
                  .arg(sample.name)
                  .arg(page + 1)
                  .arg(image.width())
                  .arg(image.height())
                  .arg(pixels.width())
                  .arg(pixels.height()));
        if (image.isNull())
            continue;
        // Some ink, and not nothing but ink: a page that comes back flat is the
        // failure this file exists to catch, and a page that comes back as one
        // solid block is the same failure the other way round -- that is sec.3's
        // C8, where the probe counted the paper itself as ink because PDFium does
        // not fill the buffer it is given. The measured fractions across these
        // fixtures run from 0.2% (a line of text) to 38% (the panel page), so the
        // claim pinned here is "there is ink and there is paper", and the fraction
        // is printed rather than judged.
        const int ink = inkPixels(image);
        const int total = image.width() * image.height();
        if (ink == 0)
            ++empties;
        check(ink > 0 && ink < total,
              QStringLiteral("%1 page %2: %3 of %4 pixels are ink, and the rest is paper")
                  .arg(sample.name)
                  .arg(page + 1)
                  .arg(ink)
                  .arg(total));
        ++rendered;
    }
    check(empties == 0,
          QStringLiteral("%1: none of the %2 pages rendered came back empty").arg(sample.name)
              .arg(rendered));
    note(QStringLiteral("  %1: %2 pages, %3 rendered, %4 pages with text")
             .arg(sample.name)
             .arg(pages)
             .arg(rendered)
             .arg(withText));
}

// ---- rotation ----
//
// rotated.pdf is stored 300x400 and marked /Rotate 90, and FPDF_GetPageSizeByIndexF
// already answers 400x300. So pageSize must not rotate a second time; and a page
// turned 90 degrees by the caller -- on top of the page's own rotation -- is a
// 300x400 image, because the renderer is told to turn it and not handed a
// rectangle that was already the wrong shape.
void checkRotation(const QString &directory)
{
    const QString path = fixture(directory, QStringLiteral("rotated.pdf"));
    PdfiumBackend pdfium;
    check(pdfium.load(path, QString()) == Document::Status::Ready,
          "rotated.pdf opens through PDFium");
    if (pdfium.pageCount() != 1) {
        check(false, "rotated.pdf has one page");
        return;
    }

    const QSizeF size = pdfium.pageSize(0);
    check(close(size.width(), 400) && close(size.height(), 300),
          QStringLiteral("rotated.pdf is 400x300 (%1x%2), the way up it is read")
              .arg(size.width())
              .arg(size.height()));

    RenderOptions upright;
    const QImage uprightImage = pdfium.render(0, QSize(400, 300), upright);
    check(!uprightImage.isNull() && uprightImage.size() == QSize(400, 300),
          "and it renders upright at the size asked for");

    RenderOptions turned = upright;
    turned.rotationDegrees = 90;
    const QImage turnedImage = pdfium.render(0, QSize(300, 400), turned);
    check(turnedImage.size() == QSize(300, 400),
          QStringLiteral("a 90-degree turn gives a 300x400 image (%1x%2)")
              .arg(turnedImage.width())
              .arg(turnedImage.height()));

    // A rotation argument the engine ignored would still give the right size, so
    // the same page at 180 degrees -- the same image size as the upright one, so
    // nothing is skipped for a shape mismatch -- has to be a different picture.
    RenderOptions upsideDown = upright;
    upsideDown.rotationDegrees = 180;
    const QImage upsideDownImage = pdfium.render(0, QSize(400, 300), upsideDown);
    const Difference difference = comparePixels(uprightImage, upsideDownImage);
    check(difference.sameSize && difference.maxChannel > 0,
          QStringLiteral("180 degrees is a different picture from 0 degrees: %1")
              .arg(describeDifference(difference)));
}

// ---- a band of a page ----
//
// The view at 800% asks for the part of the page on screen: `clip` is in the
// scaled page's grid, the image is the band's size. A backend that passes the
// band's size to the library gets a squashed page instead, and nothing on screen
// says which happened until the picture is wrong. So aim a band at real ink and
// require the band to be the same page's own picture cropped to exactly that
// rectangle -- the one claim a single engine can make about its own geometry.
void checkBand(const QString &directory)
{
    const QString path = fixture(directory, QStringLiteral("text.pdf"));
    PdfiumBackend pdfium;
    check(pdfium.load(path, QString()) == Document::Status::Ready,
          "text.pdf opens through PDFium");
    if (pdfium.pageCount() < 1)
        return;

    const QSize whole(1190, 1684); // A4 at 2x the 96 dpi screen scale
    RenderOptions options;
    const QImage wholeImage = pdfium.render(0, whole, options);
    check(!wholeImage.isNull(), "text.pdf renders at the banding scale");

    const QRect ink = inkBox(wholeImage);
    check(ink.isValid(), "text.pdf's first page has ink to aim a band at");
    if (!ink.isValid())
        return;

    // Around the top-left corner of the ink, and away from the page's own
    // corner: the band is what makes a flipped start_x/start_y visible, and a
    // band whose clip starts at 0 renders the same picture either way.
    const QSize bandSize(900, 200);
    const QRect band(qBound(0, ink.left() - 24, whole.width() - bandSize.width()),
                     qBound(0, ink.top() - 24, whole.height() - bandSize.height()),
                     bandSize.width(), bandSize.height());
    note(QStringLiteral("  band: ink %1,%2 - %3,%4; band %5,%6 %7x%8")
             .arg(ink.left()).arg(ink.top()).arg(ink.right()).arg(ink.bottom())
             .arg(band.x()).arg(band.y()).arg(band.width()).arg(band.height()));
    check(band.x() > 0 && band.y() > 0,
          QStringLiteral("the band starts off the page's corner (%1,%2), so a wrong "
                         "start_x/start_y sign has to move the picture")
              .arg(band.x()).arg(band.y()));

    RenderOptions clipped = options;
    clipped.scaledSize = whole;
    clipped.clip = band;
    const QImage bandImage = pdfium.render(0, band.size(), clipped);
    check(bandImage.size() == band.size(),
          QStringLiteral("the band comes back at the band's size (%1x%2)")
              .arg(bandImage.width())
              .arg(bandImage.height()));

    // Everything below is white-on-white if the band happens to miss the ink, so
    // say first that it does not.
    QImage paper(band.size(), QImage::Format_RGB32);
    paper.fill(Qt::white);
    check(comparePixels(bandImage, paper).maxChannel > 8,
          "the band holds ink, so a wrong origin has something to move");

    const Difference crop = comparePixels(wholeImage.copy(band), bandImage);
    check(crop.sameSize && crop.maxChannel <= 8,
          QStringLiteral("the band is the page's own picture cropped: %1")
              .arg(describeDifference(crop)));

    // The band must not be a page shrunk to 900x200: that would also be the right
    // size and would also be "stable", so compare against a squashed full page.
    const QImage squashed = wholeImage.scaled(band.size());
    check(comparePixels(bandImage, squashed).maxChannel > 8,
          "the band is a crop of the page, not the whole page scaled down");
}

// ---- the file handle (R7) ----

QString copyOf(const QString &source, const QString &directory, const QString &name)
{
    const QString target = directory + QLatin1Char('/') + name;
    QFile::remove(target);
    if (!QFile::copy(source, target))
        return QString();
    return target;
}

void checkDeleteWhileOpen(const QString &directory)
{
    QTemporaryDir temporary;
    check(temporary.isValid(), "a scratch directory for the file-handle checks");
    if (!temporary.isValid())
        return;

    const QString victim = copyOf(fixture(directory, QStringLiteral("text.pdf")),
                                  temporary.path(), QStringLiteral("delete-me.pdf"));
    check(!victim.isEmpty(), "a copy of text.pdf to delete");
    if (victim.isEmpty())
        return;

    PdfiumBackend document;
    check(document.load(victim, QString()) == Document::Status::Ready,
          "the copy opens before it is deleted");
    // Page 0 is what the reader has on screen; page 1 has not been read yet and
    // is what proves the open handle still reaches the bytes afterwards.
    const QImage before = document.render(0, QSize(595, 842), RenderOptions{});
    check(!before.isNull(), "page 1 renders before the delete");

    check(QFile::remove(victim),
          "the open document can be deleted out from under the reader (R7: "
          "FILE_SHARE_DELETE on our own handle)");
    check(!QFileInfo::exists(victim), "and the name is gone");

    check(document.pageCount() == 2, "the deleted document still knows its page count");
    check(!document.text(1).isEmpty(),
          "and page 2's text still comes through the handle that outlived the name");
    check(!document.render(1, QSize(400, 500), RenderOptions{}).isNull(),
          "and page 2 still renders");
    note(QStringLiteral("  delete while open: remove succeeded, %1 pages and page 2's text still "
                        "read through the handle")
             .arg(document.pageCount()));
}

void checkReplaceWhileOpen(const QString &directory)
{
    QTemporaryDir temporary;
    check(temporary.isValid(), "a scratch directory for the replace check");
    if (!temporary.isValid())
        return;

    const QString victim = copyOf(fixture(directory, QStringLiteral("text.pdf")),
                                  temporary.path(), QStringLiteral("replace-me.pdf"));
    const QString donor = copyOf(fixture(directory, QStringLiteral("many-pages.pdf")),
                                 temporary.path(), QStringLiteral("donor.pdf"));
    check(!victim.isEmpty() && !donor.isEmpty(), "a copy to replace and a donor to replace it with");
    if (victim.isEmpty() || donor.isEmpty())
        return;

    PdfiumBackend document;
    check(document.load(victim, QString()) == Document::Status::Ready,
          "the copy opens before it is replaced");
    check(document.pageCount() == 2, "the copy is the two-page fixture");

    // This is the F8 shape: a new file written beside the old one, then moved
    // over it. With an open handle that did not share DELETE, this call fails
    // with ERROR_SHARING_VIOLATION while the reader has the document on screen.
    const BOOL replaced =
        ReplaceFileW(reinterpret_cast<LPCWSTR>(victim.utf16()),
                     reinterpret_cast<LPCWSTR>(donor.utf16()), nullptr,
                     REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
    check(replaced != FALSE,
          QStringLiteral("ReplaceFileW works with the document open (error %1)")
              .arg(GetLastError()));
    if (!replaced)
        return;

    check(document.pageCount() == 2,
          "the already-open document still shows the bytes it opened (2 pages)");
    check(!document.render(0, QSize(595, 842), RenderOptions{}).isNull(),
          "and still renders them");

    PdfiumBackend reopened;
    check(reopened.load(victim, QString()) == Document::Status::Ready,
          "the replacement opens at the same path");
    check(reopened.pageCount() > 2,
          QStringLiteral("and is the donor, not the old file (%1 pages)")
              .arg(reopened.pageCount()));
    note(QStringLiteral("  replace while open: ReplaceFileW succeeded, the old handle still reads "
                        "%1 pages, reopening sees %2")
             .arg(document.pageCount())
             .arg(reopened.pageCount()));
}

// The bar above the page, driven the way the tab drives it: watch the path, then
// do to the file what a save-by-replacement does. This is the half of R7 the
// handle change exists for -- a watcher that goes silent after the first replace
// would leave the reader looking at an old page with nothing saying so.
bool waitForNotice(FileChangeNotice &notice, int timeoutMilliseconds)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (notice.noticeVisible())
            return true;
        QThread::msleep(10);
    }
    return notice.noticeVisible();
}

void checkWatcherAfterChange(const QString &directory)
{
    QTemporaryDir temporary;
    check(temporary.isValid(), "a scratch directory for the watcher checks");
    if (!temporary.isValid())
        return;

    QWidget host;
    host.show();

    const QString deleted = copyOf(fixture(directory, QStringLiteral("text.pdf")),
                                   temporary.path(), QStringLiteral("watched-delete.pdf"));
    check(!deleted.isEmpty(), "a file for the watcher to lose");
    if (deleted.isEmpty())
        return;

    FileChangeNotice deleteNotice(&host, [](QString *) { return false; });
    deleteNotice.watch(deleted);
    check(QFile::remove(deleted), "the watched file is deleted");
    check(waitForNotice(deleteNotice, 4000),
          "the bar says the file changed after it was deleted");

    const QString replaced = copyOf(fixture(directory, QStringLiteral("text.pdf")),
                                    temporary.path(), QStringLiteral("watched-replace.pdf"));
    const QString donor = copyOf(fixture(directory, QStringLiteral("many-pages.pdf")),
                                 temporary.path(), QStringLiteral("watched-donor.pdf"));
    check(!replaced.isEmpty() && !donor.isEmpty(), "a watched file and its replacement");
    if (replaced.isEmpty() || donor.isEmpty())
        return;

    FileChangeNotice replaceNotice(&host, [](QString *) { return false; });
    replaceNotice.watch(replaced);
    const BOOL replacedOk =
        ReplaceFileW(reinterpret_cast<LPCWSTR>(replaced.utf16()),
                     reinterpret_cast<LPCWSTR>(donor.utf16()), nullptr,
                     REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
    check(replacedOk != FALSE,
          QStringLiteral("the watched file is replaced (error %1)").arg(GetLastError()));
    const bool noticed = replacedOk != FALSE && waitForNotice(replaceNotice, 4000);
    check(noticed, "the bar says the file changed after it was replaced");
    if (noticed)
        note(QStringLiteral("  watcher: the bar appeared after the delete and after the replace"));
}

// ---- the sentences a failed load gets ----
//
// What is pinned is the shape the dialog depends on: the right status, and a
// sentence rather than an empty string, because document.h says a backend that
// can say no more must say that -- an empty one turns into an empty dialog.
void checkFailures(const QString &directory)
{
    PdfiumBackend missing;
    const Document::Status missingStatus =
        missing.load(fixture(directory, QStringLiteral("no-such-file.pdf")), QString());
    check(missingStatus == Document::Status::FileError, "a missing file is a file error");
    check(!missing.lastError().isEmpty(), "with a sentence for the dialog");
    check(missing.pageCount() == 0, "and no pages");

    PdfiumBackend damaged;
    const Document::Status damagedStatus =
        damaged.load(fixture(directory, QStringLiteral("damaged.pdf")), QString());
    check(damagedStatus != Document::Status::Ready, "a truncated PDF does not open");
    check(!damaged.lastError().isEmpty(), "and says why");
    check(damaged.pageCount() == 0, "and has no pages to show");

    // A successful load after a failed one has to clear the error, or the
    // properties dialog reads the previous document's complaint.
    const Document::Status recovered =
        damaged.load(fixture(directory, QStringLiteral("text.pdf")), QString());
    check(recovered == Document::Status::Ready, "a good file opens after a bad one");
    check(damaged.lastError().isEmpty(), "and the previous error is gone");
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);

    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");

    const Sample samples[] = {
        {"text.pdf", true, 8},
        {"table.pdf", true, 8},
        {"colored.pdf", true, 8},
        {"graphic.pdf", true, 8},
        {"mixed.pdf", true, 8},
        {"lines.pdf", true, 8},
        {"picture.pdf", false, 8},
        {"scan.pdf", false, 3},
        {"many-pages.pdf", true, 8},
        {"huge.pdf", true, 8},
        // The two files F7-E added for the reads that had nothing to be read
        // from before: one that describes itself, one with links on it.
        {"info.pdf", true, 2},
        {"links.pdf", true, 2},
    };
    for (const Sample &sample : samples)
        checkDocument(directory, sample);

    checkRotation(directory);
    checkBand(directory);
    checkFailures(directory);

    checkDeleteWhileOpen(directory);
    checkReplaceWhileOpen(directory);
    checkWatcherAfterChange(directory);

    if (failures == 0) {
        std::cout << "pdfium backend: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
