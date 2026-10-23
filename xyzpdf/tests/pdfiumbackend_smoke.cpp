// The two document engines, read on the same files and compared page by page.
//
// docs/pdfium-plan.md sec.10 F7-B asks for "同一份文件两条路逐档对数（C6 那种）" and
// sec.5.2 names what the F5 probe left unmeasured: C6 compared only the first page
// of each fixture, while the layout uses every page. So this walks every page of
// every committed fixture -- page count, page size, text -- and renders a sample
// of pages through both backends at the same size. The comparisons are equalities
// with a stated tolerance, not a threshold somebody can raise: the only reason
// any of them is allowed to differ at all is sec.5.1's measurement that the two
// rasterisers disagree by a few counts per channel on text and vectors, and by
// nothing on bitmap pages.
//
// ---- the second half is not a comparison ----
//
// docs/handoff-03.md F7 item 3 / docs/pdfium-plan.md R7 changes how the file is
// opened: our own handle with FILE_SHARE_READ | WRITE | DELETE, so that a save by
// replacement (F8) can move a new file over the one on screen. Qt PDF cannot be
// asked for that, so there is no column to compare it against -- these are
// assertions about the PDFium backend itself, and about the notice above the page
// still firing after the file is deleted or replaced. The control for the handle
// half is in the record: removing FILE_SHARE_DELETE from pdfiumbackend.cpp turns
// the delete and the replace checks red (the watcher's own file is not held open,
// so it stays green -- it is asserting FileChangeNotice, not our handle).
//
// Run by tools/build_all_tests.py like every other test, and only created when a
// PDFium release was found (see the target's guard in CMakeLists.txt).

#include "../src/document/pdfiumbackend.h"
#include "../src/document/qtpdfbackend.h"
#include "../src/services/filechangenotice.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
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

// A rendered page with the paper it is going to be looked at on. Qt PDF hands
// back transparent pixels where the page paints nothing; PdfiumBackend fills
// white, because PDFium does not fill the buffer it is given and uninitialised
// memory is not paper (docs/pdfium-plan.md sec.3, C8). Sec.5.1 compared the two
// the same way, so the difference that remains is the rasteriser's, not the
// background's.
QImage onPaper(const QImage &image)
{
    if (image.isNull())
        return image;
    QImage papered(image.size(), QImage::Format_RGB32);
    papered.fill(Qt::white);
    QPainter painter(&papered);
    painter.drawImage(0, 0, image);
    return papered;
}

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
    const QImage a = onPaper(left).convertToFormat(QImage::Format_RGB32);
    const QImage b = onPaper(right).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < a.height(); ++y) {
        const auto *lineA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *lineB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int red = qAbs(qRed(lineA[x]) - qRed(lineB[x]));
            const int green = qAbs(qGreen(lineA[x]) - qGreen(lineB[x]));
            const int blue = qAbs(qBlue(lineA[x]) - qBlue(lineB[x]));
            const int worst = qMax(red, qMax(green, blue));
            if (worst != 0)
                ++result.differing;
            result.maxChannel = qMax(result.maxChannel, worst);
        }
    }
    return result;
}

// Sec.5.1 measured at most 3/255 on the seven synthetic text and vector fixtures
// and 0 on the two bitmap pages. The tolerance here is 8 rather than 3 so a
// different-but-correct rasteriser build is not called a defect; a wrong page,
// a wrong scale or a wrong rotation is hundreds, not single digits.
constexpr int kRasterTolerance = 8;

QString describeDifference(const Difference &difference)
{
    return QStringLiteral("%1 pixels differ, max channel %2")
        .arg(difference.differing)
        .arg(difference.maxChannel);
}

// The ink on a page, so a band can be aimed at something. A band of blank paper
// is not a test -- it is white pixels compared with white pixels whatever the
// origin was. The F7-B control run that flipped start_y's sign stayed green on
// the original fixed band (y=500 of a page whose text stops above it), which is
// how this helper came to exist.
QRect inkBox(const QImage &page)
{
    const QImage papered = onPaper(page).convertToFormat(QImage::Format_RGB32);
    int left = papered.width();
    int top = papered.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < papered.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(papered.constScanLine(y));
        for (int x = 0; x < papered.width(); ++x) {
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

// ---- the page-by-page comparison ----

struct Sample
{
    const char *name;
    // Bitmap-only pages: same input pixels, so the two rasterisers have no
    // antialiasing to disagree about and the comparison is allowed no slack at
    // all. This is sec.5.1's pair of zeroes, turned into a rule.
    bool bitmapOnly;
    // Rendered pages, spread across the document. All of them for a small
    // fixture; a sample for huge.pdf, where a full pair of renders per page is
    // minutes of work for a number that does not change across pages.
    int renders;
};

void compareDocument(const QString &directory, const Sample &sample)
{
    const QString path = fixture(directory, QString::fromLatin1(sample.name));
    if (!QFileInfo::exists(path)) {
        check(false, QStringLiteral("%1 is missing; run tools/make_fixtures.py").arg(path));
        return;
    }

    QtPdfBackend qt;
    PdfiumBackend pdfium;
    const Document::Status qtStatus = qt.load(path, QString());
    const Document::Status pdfiumStatus = pdfium.load(path, QString());
    check(qtStatus == Document::Status::Ready,
          QStringLiteral("%1 opens through Qt PDF").arg(sample.name));
    check(pdfiumStatus == Document::Status::Ready,
          QStringLiteral("%1 opens through PDFium (%2)").arg(sample.name).arg(pdfiumStatus ==
                  Document::Status::Ready ? QStringLiteral("ready")
                                          : pdfium.lastError()));
    if (qtStatus != Document::Status::Ready || pdfiumStatus != Document::Status::Ready)
        return;

    const int pages = qt.pageCount();
    check(pdfium.pageCount() == pages,
          QStringLiteral("%1: both engines count %2 pages (PDFium says %3)")
              .arg(sample.name)
              .arg(pages)
              .arg(pdfium.pageCount()));
    if (pdfium.pageCount() != pages)
        return;

    // ---- every page's size and text ----
    //
    // The size walk is the row sec.5.2 says C6 never took: it looked at page one
    // and the layout uses all of them. The text walk is exact; the two backends
    // are reading the same text page out of the same PDFium, so anything but an
    // identical string is a real difference and not a tolerance question.
    QSizeF smallestDifference;
    int textMismatches = 0;
    for (int page = 0; page < pages; ++page) {
        const QSizeF qtSize = qt.pageSize(page);
        const QSizeF pdfiumSize = pdfium.pageSize(page);
        if (!close(qtSize.width(), pdfiumSize.width())
            || !close(qtSize.height(), pdfiumSize.height())) {
            check(false, QStringLiteral("%1 page %2: Qt says %3x%4, PDFium says %5x%6")
                             .arg(sample.name)
                             .arg(page + 1)
                             .arg(qtSize.width())
                             .arg(qtSize.height())
                             .arg(pdfiumSize.width())
                             .arg(pdfiumSize.height()));
        }

        const QString qtText = qt.text(page);
        const QString pdfiumText = pdfium.text(page);
        if (qtText != pdfiumText) {
            ++textMismatches;
            if (textMismatches <= 3) {
                check(false, QStringLiteral("%1 page %2: text differs (Qt %3 chars, PDFium %4)")
                                 .arg(sample.name)
                                 .arg(page + 1)
                                 .arg(qtText.size())
                                 .arg(pdfiumText.size()));
            }
        }
    }
    check(textMismatches == 0,
          QStringLiteral("%1: all %2 pages have the same text in both engines (%3 differ)")
              .arg(sample.name)
              .arg(pages)
              .arg(textMismatches));

    // ---- the rendered pages ----
    const int toRender = qMin(sample.renders, pages);
    const int step = qMax(1, pages / toRender);
    int rendered = 0;
    Difference worst;
    for (int page = 0; page < pages && rendered < toRender; page += step) {
        const QSizeF points = pdfium.pageSize(page);
        const QSize pixels(qRound(points.width() * 96.0 / 72.0),
                           qRound(points.height() * 96.0 / 72.0));
        // The view's options: annotations on, subpixel text off (PageRenderer
        // never asks for it; image export and print explicitly do not). Asking
        // for LCD text here measured a max channel difference of 84/85 rather
        // than sec.5.1's 3 -- a comparison of a route the product does not take.
        RenderOptions options;

        const QImage qtImage = qt.render(page, pixels, options);
        const QImage pdfiumImage = pdfium.render(page, pixels, options);
        check(!qtImage.isNull(),
              QStringLiteral("%1 page %2 renders through Qt PDF").arg(sample.name).arg(page + 1));
        check(!pdfiumImage.isNull(), QStringLiteral("%1 page %2 renders through PDFium")
                                         .arg(sample.name)
                                         .arg(page + 1));
        if (qtImage.isNull() || pdfiumImage.isNull())
            continue;

        check(qtImage.size() == pixels,
              QStringLiteral("%1 page %2: Qt image is %3x%4, asked for %5x%6")
                  .arg(sample.name).arg(page + 1)
                  .arg(qtImage.width()).arg(qtImage.height())
                  .arg(pixels.width()).arg(pixels.height()));
        check(pdfiumImage.size() == pixels,
              QStringLiteral("%1 page %2: PDFium image is %3x%4, asked for %5x%6")
                  .arg(sample.name).arg(page + 1)
                  .arg(pdfiumImage.width()).arg(pdfiumImage.height())
                  .arg(pixels.width()).arg(pixels.height()));

        const Difference difference = comparePixels(qtImage, pdfiumImage);
        check(difference.sameSize,
              QStringLiteral("%1 page %2: both images are the same size")
                  .arg(sample.name).arg(page + 1));
        const int allowed = sample.bitmapOnly ? 0 : kRasterTolerance;
        check(difference.maxChannel <= allowed,
              QStringLiteral("%1 page %2: %3 (allowed %4)")
                  .arg(sample.name).arg(page + 1)
                  .arg(describeDifference(difference))
                  .arg(allowed));
        worst.differing = qMax(worst.differing, difference.differing);
        worst.maxChannel = qMax(worst.maxChannel, difference.maxChannel);
        ++rendered;
    }
    note(QStringLiteral("  %1: %2 pages, %3 rendered; worst %4")
             .arg(sample.name)
             .arg(pages)
             .arg(rendered)
             .arg(describeDifference(worst)));
}

// ---- rotation ----
//
// rotated.pdf is stored 300x400 and marked /Rotate 90, and FPDF_GetPageSizeByIndexF
// already answers 400x300 (measured before the backend was written). So pageSize
// must not rotate a second time, and a render at rotation 0 must look like Qt's.
// Rotation 90 is the caller's rotation on top of the page's own, and both engines
// turn it clockwise into a 300x400 image.
void compareRotation(const QString &directory)
{
    const QString path = fixture(directory, QStringLiteral("rotated.pdf"));
    QtPdfBackend qt;
    PdfiumBackend pdfium;
    check(qt.load(path, QString()) == Document::Status::Ready, "rotated.pdf opens through Qt PDF");
    check(pdfium.load(path, QString()) == Document::Status::Ready,
          "rotated.pdf opens through PDFium");
    if (qt.pageCount() != 1 || pdfium.pageCount() != 1) {
        check(false, "rotated.pdf has one page in both engines");
        return;
    }

    const QSizeF qtSize = qt.pageSize(0);
    const QSizeF pdfiumSize = pdfium.pageSize(0);
    check(close(qtSize.width(), 400) && close(qtSize.height(), 300),
          QStringLiteral("rotated.pdf is 400x300 through Qt PDF (%1x%2)")
              .arg(qtSize.width()).arg(qtSize.height()));
    check(close(pdfiumSize.width(), 400) && close(pdfiumSize.height(), 300),
          QStringLiteral("rotated.pdf is 400x300 through PDFium (%1x%2)")
              .arg(pdfiumSize.width()).arg(pdfiumSize.height()));

    RenderOptions upright;
    const QImage qtUpright = qt.render(0, QSize(400, 300), upright);
    const QImage pdfiumUpright = pdfium.render(0, QSize(400, 300), upright);
    check(!qtUpright.isNull() && !pdfiumUpright.isNull(),
          "rotated.pdf renders upright through both engines");
    const Difference uprightDifference = comparePixels(qtUpright, pdfiumUpright);
    check(uprightDifference.sameSize && uprightDifference.maxChannel <= kRasterTolerance,
          QStringLiteral("rotated.pdf at rotation 0: %1")
              .arg(describeDifference(uprightDifference)));

    RenderOptions turned = upright;
    turned.rotationDegrees = 90;
    const QImage qtTurned = qt.render(0, QSize(300, 400), turned);
    const QImage pdfiumTurned = pdfium.render(0, QSize(300, 400), turned);
    check(qtTurned.size() == QSize(300, 400),
          "Qt PDF turns rotated.pdf into a 300x400 image at 90 degrees");
    check(pdfiumTurned.size() == QSize(300, 400),
          "PDFium turns rotated.pdf into a 300x400 image at 90 degrees");
    const Difference turnedDifference = comparePixels(qtTurned, pdfiumTurned);
    check(turnedDifference.sameSize && turnedDifference.maxChannel <= kRasterTolerance,
          QStringLiteral("rotated.pdf at 90 degrees: %1")
              .arg(describeDifference(turnedDifference)));

    // A rotation argument both engines ignored would compare equal too, so the
    // same page at 180 degrees -- the same image size, so the comparison is not
    // skipped for a shape mismatch -- has to be a different picture.
    RenderOptions upsideDown = upright;
    upsideDown.rotationDegrees = 180;
    const QImage qtUpsideDown = qt.render(0, QSize(400, 300), upsideDown);
    const Difference upsideDownDifference = comparePixels(qtUpright, qtUpsideDown);
    check(upsideDownDifference.sameSize && upsideDownDifference.maxChannel > 0,
          "180 degrees is a different picture from 0 degrees");
}

// ---- a band of a page ----
//
// The view at 800% asks for the part of the page on screen: `clip` is in the
// scaled page's grid, the image is the band's size. A backend that passes the
// band's size to the library gets a squashed page instead, and nothing on screen
// says which happened until the picture is wrong. So aim a band at real ink,
// require PDFium's band to be its own full render cropped to exactly that
// rectangle, and require it to be what Qt PDF's whole-page render has there.
//
// Qt's own band is size-checked only: at 2x it lands one pixel up-left of its
// own crop and rasterises the glyphs at that phase (max 140). That is a Qt
// quirk, and the blank band this test used to aim at hid it -- see the note at
// the comparison.
void compareBand(const QString &directory)
{
    const QString path = fixture(directory, QStringLiteral("text.pdf"));
    QtPdfBackend qt;
    PdfiumBackend pdfium;
    check(qt.load(path, QString()) == Document::Status::Ready, "text.pdf opens through Qt PDF");
    check(pdfium.load(path, QString()) == Document::Status::Ready,
          "text.pdf opens through PDFium");
    if (qt.pageCount() < 1 || pdfium.pageCount() < 1)
        return;

    const QSize whole(1190, 1684); // A4 at 2x the 96 dpi screen scale
    // The product's defaults: annotations on, no LCD text. The LCD flag changes
    // text rendering by tens of counts between the engines (measured while this
    // test was being written: max 84-85), which is a different question from
    // whether a band lands where the view will put it.
    RenderOptions options;
    const QImage qtWhole = qt.render(0, whole, options);
    const QImage pdfiumWhole = pdfium.render(0, whole, options);

    const QRect ink = inkBox(qtWhole);
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
    const QImage qtBand = qt.render(0, band.size(), clipped);
    const QImage pdfiumBand = pdfium.render(0, band.size(), clipped);

    // Everything below is white-on-white if the band happens to miss the ink,
    // so say first that it does not.
    QImage paper(band.size(), QImage::Format_RGB32);
    paper.fill(Qt::white);
    check(comparePixels(pdfiumBand, paper).maxChannel > kRasterTolerance,
          "the band holds ink, so a wrong origin has something to move");

    check(qtBand.size() == band.size(),
          QStringLiteral("Qt PDF returns the band at the band's size (%1x%2)")
              .arg(qtBand.width()).arg(qtBand.height()));
    check(pdfiumBand.size() == band.size(),
          QStringLiteral("PDFium returns the band at the band's size (%1x%2)")
              .arg(pdfiumBand.width()).arg(pdfiumBand.height()));

    // Qt's band is checked for its size only. Measured while this test was
    // written: at 2x Qt's band path puts the ink one pixel up-left of its own
    // full render's crop and rasterises the glyphs at that other phase (max
    // 140), so "Qt band == Qt crop" is false on the stock backend -- the old,
    // blank band hid it. Encoding that would only pin a Qt quirk; what has to
    // hold is that PDFium's band is the page's picture, and the page itself is
    // compared against Qt above.
    const Difference pdfiumCrop = comparePixels(pdfiumWhole.copy(band), pdfiumBand);
    check(pdfiumCrop.sameSize && pdfiumCrop.maxChannel <= kRasterTolerance,
          QStringLiteral("PDFium's band is its own full render cropped: %1")
              .arg(describeDifference(pdfiumCrop)));
    const Difference across = comparePixels(qtWhole.copy(band), pdfiumBand);
    check(across.sameSize && across.maxChannel <= kRasterTolerance,
          QStringLiteral("PDFium's band is where Qt PDF's page has that rectangle: %1")
              .arg(describeDifference(across)));

    // The band must not be a page shrunk to 900x200: that would also be 900x200
    // and would also be "stable", so compare against a squashed full page.
    const QImage squashed = pdfiumWhole.scaled(band.size());
    check(comparePixels(pdfiumBand, squashed).maxChannel > kRasterTolerance,
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
// Not a comparison: Qt PDF and PDFium word these differently and both are
// allowed to. What is pinned is the shape the dialog depends on -- the right
// status, and a sentence rather than an empty string (document.h says a backend
// that can say no more must say that).
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
        {"text.pdf", false, 8},
        {"table.pdf", false, 8},
        {"colored.pdf", false, 8},
        {"graphic.pdf", false, 8},
        {"mixed.pdf", false, 8},
        {"picture.pdf", true, 8},
        {"scan.pdf", true, 3},
        {"many-pages.pdf", false, 8},
        {"huge.pdf", false, 8},
    };
    for (const Sample &sample : samples)
        compareDocument(directory, sample);

    compareRotation(directory);
    compareBand(directory);
    checkFailures(directory);

    checkDeleteWhileOpen(directory);
    checkReplaceWhileOpen(directory);
    checkWatcherAfterChange(directory);

    if (failures == 0) {
        std::cout << "pdfium backend: all comparisons passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
