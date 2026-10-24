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
#include "formsample.h"
#include "../src/services/filechangenotice.h"

#include <QApplication>
#include <QCryptographicHash>
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

#include <fpdf_annot.h>
#include <fpdf_formfill.h>
#include <fpdf_signature.h>
#include <fpdfview.h>

#include <cmath>
#include <iostream>

namespace {

using namespace FormSampleFile;

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
    // Flushed: a run that hangs must leave the last row it reached behind, or the log
    // says nothing about where to look (sec.99's rule about a print that never came out).
    std::cout << what.toStdString() << '\n' << std::flush;
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

FormFill::Frame flatFrame()
{
    FormFill::Frame frame;
    frame.pixels = QSize(595, 842);
    frame.quarterTurns = 0;
    return frame;
}

void checkFormFilling(const QString &scratch)
{
    const Geometry geometry = FormSampleFile::geometry();
    const QString samplePath = scratch + QStringLiteral("/form-sample.pdf");
    const QString rotatedPath = scratch + QStringLiteral("/form-rotated.pdf");
    const QString xfaPath = scratch + QStringLiteral("/form-xfa.pdf");
    QString why;
    check(write(samplePath, 0, false, &why),
          QStringLiteral("setup: the form sample is written: %1").arg(why));
    check(write(rotatedPath, 90, false, &why),
          QStringLiteral("setup: the rotated form sample is written: %1").arg(why));
    check(write(xfaPath, 0, true, &why),
          QStringLiteral("setup: the XFA form sample is written: %1").arg(why));

    const QByteArray originalBytes = [&samplePath] {
        QFile file(samplePath);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    }();
    check(!originalBytes.isEmpty(), "and the sample can be read back for the byte compare");

    PdfiumBackend document;
    const Document::Status opened = document.load(samplePath, QString());
    {
        // Which of the two answers -- the status the backend gives and the error code
        // the library itself holds -- is what turns "the sample does not open" into a
        // fact about the bytes rather than a guess about the parser.
        FPDF_DOCUMENT raw = FPDF_LoadDocument(samplePath.toUtf8().constData(), nullptr);
        QFile head(samplePath);
        head.open(QIODevice::ReadOnly);
        const QByteArray first = head.read(16);
        head.close();
        note(QStringLiteral("formdiag: load status=%1 raw=%2 lastError=%3 bytes=%4 "
                            "formType=%5 head=%6")
                 .arg(int(opened))
                 .arg(raw ? 1 : 0)
                 .arg(FPDF_GetLastError())
                 .arg(QFileInfo(samplePath).size())
                 .arg(raw ? FPDF_GetFormType(raw) : -1)
                 .arg(QString::fromUtf8(first.toPercentEncoding())));
        if (raw)
            FPDF_CloseDocument(raw);
    }
    check(opened == Document::Status::Ready,
          QStringLiteral("form: the sample opens (%1)").arg(document.lastError()));
    check(document.fillable(), "form: an AcroForm document is fillable");

    // ---- what is under the pointer, before any input ----
    //
    // These rows are the companion every later zero needs (sec.82's rule): a field that
    // answers None somewhere else on the page is only evidence if it answers its kind
    // where the file put it.
    struct Expected
    {
        const char *name;
        QRectF rect;
        FormFill::Field field;
    };
    const Expected expected[] = {
        {"text", geometry.textField, FormFill::Field::TextField},
        {"checkbox", geometry.checkBox, FormFill::Field::CheckBox},
        {"radio one", geometry.radioOne, FormFill::Field::RadioButton},
        {"radio two", geometry.radioTwo, FormFill::Field::RadioButton},
        {"combobox", geometry.comboBox, FormFill::Field::ComboBox},
        {"listbox", geometry.listBox, FormFill::Field::ListBox},
    };
    for (const Expected &one : expected) {
        const FormFill::Field found = document.fieldAt(0, flatFrame(), flatDevice(one.rect));
        check(found == one.field,
              QStringLiteral("form: the %1 is hit as its own kind (got %2)")
                  .arg(one.name)
                  .arg(int(found)));
        note(QStringLiteral("formhit %1: field=%2 interactive=%3")
                 .arg(one.name)
                 .arg(int(found))
                 .arg(FormFill::isInteractive(found) ? 1 : 0));
    }
    check(document.fieldAt(0, flatFrame(), QPointF(400, 700)) == FormFill::Field::None,
          "form: blank paper is not a field");

    // ---- the two kinds this version shows and does not answer ----
    //
    // Named by what they are not rather than by what PDFium calls them. The push button
    // reads back as PUSHBUTTON (1); the signature widget answers 8, a value this build
    // gives no name to without the XFA enum. Both mean the same thing to a reader, which
    // is that the click does nothing -- and that is the contract handoff-04 G1 5 wrote,
    // so it is the contract pinned here while the numbers go to the log.
    const Expected shown[] = {
        {"push button", geometry.pushButton, FormFill::Field::None},
        {"signature", geometry.signature, FormFill::Field::None},
    };
    for (const Expected &one : shown) {
        const FormFill::Field found = document.fieldAt(0, flatFrame(), flatDevice(one.rect));
        note(QStringLiteral("formshown %1: field=%2 interactive=%3")
                 .arg(one.name)
                 .arg(int(found))
                 .arg(FormFill::isInteractive(found) ? 1 : 0));
        check(!FormFill::isInteractive(found),
              QStringLiteral("form: the %1 is not answered by a click").arg(one.name));
        check(!document.mousePressed(0, flatFrame(), flatDevice(one.rect), {}),
              QStringLiteral("form: the %1 takes no press").arg(one.name));
    }
    check(!document.hasFocusedField(), "and neither of them took the keyboard either");

    // ---- click, type, undo ----
    const FormFill::Frame frame = flatFrame();
    check(document.mousePressed(0, frame, flatDevice(geometry.textField), {}),
          "form: a press on the text field goes to the form");
    check(document.mouseReleased(0, frame, flatDevice(geometry.textField), {}),
          "and so does its release");
    check(document.hasFocusedField(), "form: the text field has the keyboard now");
    check(document.focusedPage() == 0, "and the form layer knows which page it is on");
    check(document.focusedFieldType() == FormFill::Field::TextField,
          "and what kind of field it is");
    const FormFill::Pointer shape = document.takeRequestedPointer();
    note(QStringLiteral("formcursor after focusing a text field: %1").arg(int(shape)));
    check(shape == FormFill::Pointer::IBeam,
          "form: PDFium asked for the I-beam while the field took focus");

    const QVector<FormFill::Repaint> focusRepaints = document.takeRepaints();
    note(QStringLiteral("formrepaint on focus: %1 entries, first page=%2 rect=(%3,%4)-(%5,%6)")
             .arg(focusRepaints.size())
             .arg(focusRepaints.isEmpty() ? -1 : focusRepaints.first().page)
             .arg(focusRepaints.isEmpty() ? 0 : focusRepaints.first().userSpace.left())
             .arg(focusRepaints.isEmpty() ? 0 : focusRepaints.first().userSpace.top())
             .arg(focusRepaints.isEmpty() ? 0 : focusRepaints.first().userSpace.right())
             .arg(focusRepaints.isEmpty() ? 0 : focusRepaints.first().userSpace.bottom()));
    check(!focusRepaints.isEmpty(), "form: taking focus asks for a repaint");
    check(focusRepaints.first().page == 0, "and says which page it is about");
    // The rectangle has to cover the field's own. sec.82's rule about a row that reads 0
    // works in the other direction too: a repaint over the wrong part of the page would
    // leave the field showing what it showed before the click, and a test that only
    // counted entries would never see that. Measured on this sample the answer is the
    // field's /Rect grown by about two points on each side (the focus ring), so what is
    // pinned is containment rather than equality -- equality would fail on the growth
    // that is PDFium's own business.
    check(focusRepaints.first().userSpace.contains(geometry.textField),
          "and the rectangle covers the field it is about");

    check(document.differingFieldCount() == 0,
          "form: a click and a caret are not a change to the document");
    for (const char letter : {'H', 'E', 'L', 'L', 'O'}) {
        check(document.characterTyped(QChar(letter), {}),
              QStringLiteral("form: the character %1 is taken").arg(QChar(letter)));
        document.takeRepaints();
    }
    check(document.differingFieldCount() == 1,
          "form: five keystrokes in one field are one changed field");
    check(document.canUndoFocused(), "form: the field has something to undo");
    // Measured: the field's own undo is per keystroke, not per editing session. One undo
    // after five characters takes one character back, so the field still differs from the
    // file. Worth pinning, because the reader's 撤销 while a field has the keyboard means
    // "one more character" and the same menu item with no focus means "the last page
    // operation" -- the two histories are not the same size and the menu says nothing
    // about which one it walked.
    document.undoFocused();
    document.takeRepaints();
    check(document.differingFieldCount() == 1,
          "form: one undo takes one keystroke back, not the whole edit");
    for (int step = 0; step < 4; ++step) {
        document.undoFocused();
        document.takeRepaints();
    }
    check(document.differingFieldCount() == 0,
          "form: undoing every keystroke puts the file's own value back");
    check(document.canRedoFocused(), "and the redo is still there to walk back");
    document.redoFocused();
    document.takeRepaints();
    check(document.differingFieldCount() == 1, "and redoing puts the change back");
    // Back to the file's value, so the groups below start from a count of zero and can
    // assert absolute numbers instead of deltas off whatever the last group left behind.
    document.undoFocused();
    document.takeRepaints();
    check(document.differingFieldCount() == 0, "and undoing the redo is back to zero");

    // ---- Escape and the click on paper are the view's, not the field's ----
    document.releaseFocus();
    document.takeRepaints();
    check(!document.hasFocusedField(), "form: letting go of the focus takes the keyboard back");
    check(!document.mousePressed(0, frame, QPointF(400, 700), {}),
          "and a press on paper is not the form's to take");

    // ---- Tab walks the field order ----
    check(document.mousePressed(0, frame, flatDevice(geometry.textField), {}),
          "form: refocus the text field for the tab");
    document.mouseReleased(0, frame, flatDevice(geometry.textField), {});
    document.takeRepaints();
    const bool tabTaken = document.keyPressed(Qt::Key_Tab, {});
    const FormFill::Field afterTab = document.focusedFieldType();
    note(QStringLiteral("formtab: taken=%1 field now=%2 page now=%3")
             .arg(tabTaken)
             .arg(int(afterTab))
             .arg(document.focusedPage()));
    check(tabTaken, "form: Tab goes to the field, not to the page");
    check(afterTab != FormFill::Field::TextField, "and the focus moved on from the text field");
    const bool shiftTabTaken = document.keyPressed(Qt::Key_Tab, Qt::ShiftModifier);
    note(QStringLiteral("formshifttab: taken=%1 field now=%2")
             .arg(shiftTabTaken)
             .arg(int(document.focusedFieldType())));
    document.releaseFocus();
    document.takeRepaints();

    // ---- the checkbox and the radio: a click is the whole edit ----
    //
    // The value is read back off a staged file rather than off the in-memory field,
    // because reading a field while it still holds focus answers the file's value and not
    // the one the widget is holding (formfill.h has that measurement). Staging commits
    // first, so what the file says is what the reader sees.
    const QString checkboxStage = scratch + QStringLiteral("/checkbox-stage.pdf");
    auto checkboxValue = [&document, &checkboxStage, &why](const QString &label) {
        QString staged;
        if (!document.stageFieldValues(QFileInfo(checkboxStage).absolutePath(), &staged, &why))
            return QString();
        QFile::remove(checkboxStage);
        if (!QFile::rename(staged, checkboxStage))
            return QString();
        const QString value = fieldValue(checkboxStage, QStringLiteral("agree"), &why);
        note(QStringLiteral("formcheck %1: agree=[%2] differing=%3")
                 .arg(label, value)
                 .arg(document.differingFieldCount()));
        return value;
    };
    const QString unchecked = checkboxValue(QStringLiteral("before any click"));
    check(document.mousePressed(0, frame, flatDevice(geometry.checkBox), {}),
          "form: a press on the checkbox is the form's");
    document.mouseReleased(0, frame, flatDevice(geometry.checkBox), {});
    document.takeRepaints();
    check(document.differingFieldCount() == 1, "form: checking the box changes one field");
    document.releaseFocus();
    document.takeRepaints();
    const QString checked = checkboxValue(QStringLiteral("after one click"));
    check(checked != unchecked,
          QStringLiteral("form: the box's value in the file really moved (%1 -> %2)")
              .arg(unchecked, checked));
    check(document.mousePressed(0, frame, flatDevice(geometry.checkBox), {}),
          "and clicking it again is still the box's press");
    document.mouseReleased(0, frame, flatDevice(geometry.checkBox), {});
    document.takeRepaints();
    document.releaseFocus();
    document.takeRepaints();
    const QString uncheckedAgain = checkboxValue(QStringLiteral("after two clicks"));
    check(uncheckedAgain == unchecked,
          QStringLiteral("form: the second click puts the file's own value back (%1 vs %2)")
              .arg(uncheckedAgain, unchecked));
    check(document.differingFieldCount() == 0,
          "and with it the count of changed fields goes back to zero");

    check(document.mousePressed(0, frame, flatDevice(geometry.radioTwo), {}),
          "form: the second radio takes the press");
    document.mouseReleased(0, frame, flatDevice(geometry.radioTwo), {});
    document.takeRepaints();
    check(document.differingFieldCount() >= 1,
          "form: choosing the other radio changes the group");
    document.releaseFocus();
    document.takeRepaints();

    // ---- the input-method commit is one edit, not a run of keystrokes ----
    check(document.mousePressed(0, frame, flatDevice(geometry.textField), {}),
          "form: refocus the text field for the commit");
    document.mouseReleased(0, frame, flatDevice(geometry.textField), {});
    document.takeRepaints();
    document.commitText(QStringLiteral("名字"));
    document.takeRepaints();
    check(document.differingFieldCount() >= 1, "form: a committed string changes a field");
    document.releaseFocus();
    document.takeRepaints();
    const QString committed = fieldValue(samplePath, QStringLiteral("name"), &why);
    note(QStringLiteral("formime: the file on disk still says name=[%1] (%2)")
             .arg(committed, why));
    check(committed == QStringLiteral("typed on page 1"),
          "form: filling a field does not write the reader's file -- that is 保存's job");

    // ---- the save: PDFium's own copy, read back by PDFium ----
    check(document.mousePressed(0, frame, flatDevice(geometry.textField), {}),
          "form: refocus the text field to type the value that gets saved");
    document.mouseReleased(0, frame, flatDevice(geometry.textField), {});
    document.takeRepaints();
    document.commitText(QStringLiteral("SAVED-BY-PDFIUM"));
    document.takeRepaints();
    QString staged;
    QString stageError;
    check(document.stageFieldValues(scratch, &staged, &stageError),
          QStringLiteral("form: the field values stage out to a file of their own (%1)")
              .arg(stageError));
    check(staged != samplePath, "and the stage is never the reader's own file");
    const QString stagedValue = fieldValue(staged, QStringLiteral("name"), &why);
    note(QStringLiteral("formsave: staged file %1 holds name=[%2] (%3)")
             .arg(QFileInfo(staged).fileName(), stagedValue, why));
    check(stagedValue.contains(QStringLiteral("SAVED-BY-PDFIUM")),
          "form: the staged file carries what the reader typed");
    const QByteArray after = [&samplePath] {
        QFile file(samplePath);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    }();
    check(after == originalBytes,
          QStringLiteral("form: the reader's file is byte-identical (was %1 bytes, now %2)")
              .arg(originalBytes.size())
              .arg(after.size()));

    // ---- the page the reader turned ----
    //
    // The coordinate space this whole feature is written in was measured for a page
    // with no /Rotate. A page marked /Rotate 90 is where a second answer would show up:
    // the click coordinates PDFium wants are its own user space (formfill.h), and
    // FPDF_DeviceToPage is the conversion that knows the difference -- so the same
    // field, given the geometry its displayed pixels were drawn with, has to take the
    // focus. If it does not, the rotation is being folded in twice.
    PdfiumBackend turned;
    check(turned.load(rotatedPath, QString()) == Document::Status::Ready,
          "form: the rotated sample opens");
    const QSizeF displayed = turned.pageSize(0);
    note(QStringLiteral("formrotate: pageSize(0) on a page stored 595x842 marked /Rotate 90 = %1x%2")
             .arg(displayed.width())
             .arg(displayed.height()));
    // The field's own centre in user space is (160, 615); where that is on screen is
    // FPDF_PageToDevice's answer for the geometry the page was drawn with. Asking the
    // library and then clicking there is the check that the two conversions are
    // inverses of each other rather than two guesses that happen to agree.
    struct Case
    {
        int quarterTurns;
        QSize pixels;
    };
    const Case cases[] = {{0, displayed.toSize()},
                          {1, QSize(int(displayed.height()), int(displayed.width()))}};
    for (const Case &one : cases) {
        FormFill::Frame turnedFrame;
        turnedFrame.pixels = one.pixels;
        turnedFrame.quarterTurns = one.quarterTurns;
        const QPointF clicked = deviceCentre(rotatedPath, geometry.textField.center(),
                                             one.pixels, one.quarterTurns);
        note(QStringLiteral("formrotate reader-turns=%1 frame=%2x%3 device(%4,%5) field=%6")
                 .arg(one.quarterTurns)
                 .arg(one.pixels.width())
                 .arg(one.pixels.height())
                 .arg(clicked.x())
                 .arg(clicked.y())
                 .arg(int(turned.fieldAt(0, turnedFrame, clicked))));
        check(!clicked.isNull(),
              QStringLiteral("form: the turned page can be asked where the field is (turns %1)")
                  .arg(one.quarterTurns));
        check(turned.fieldAt(0, turnedFrame, clicked) == FormFill::Field::TextField,
              QStringLiteral("form: on a turned page the field is hit where the pixels say it is "
                             "(reader turns %1)")
                  .arg(one.quarterTurns));
        check(turned.mousePressed(0, turnedFrame, clicked, {}),
              "form: and a press there goes to the form");
        turned.mouseReleased(0, turnedFrame, clicked, {});
        turned.takeRepaints();
        check(turned.focusedPage() == 0
                  && turned.focusedFieldType() == FormFill::Field::TextField,
              "form: the field on the turned page took the keyboard");
        turned.releaseFocus();
        turned.takeRepaints();
    }

    // ---- the form this build cannot read is not a form it takes input for ----
    PdfiumBackend xfa;
    check(xfa.load(xfaPath, QString()) == Document::Status::Ready,
          "form: the XFA sample opens");
    note(QStringLiteral("formxfa: fillable=%1 fieldAt(text field)=%2")
             .arg(xfa.fillable())
             .arg(int(xfa.fieldAt(0, frame, flatDevice(geometry.textField)))));
    check(!xfa.fillable(),
          "form: a document whose form is an XFA packet takes no input here");

    // ---- what the reader's own changes are, and what forgets them ----
    document.forgetFieldValueSnapshots();
    document.takeRepaints();
    check(document.differingFieldCount() == 0,
          "form: after a load the file is what the values are compared against");
}

// ---- the two choice fields, and the wheel ----
//
// handoff-05 H1 1 and H1 2. Every number these assertions rest on was read off
// chromium/8111 with a scratch probe before it was written down here, and the reading is
// named beside the assertion it carries: which of the engine's own answers a routing
// decision is taken from, what it paints, and what it refuses.
void checkChoiceFields(const QString &scratch)
{
    const Geometry geometry = FormSampleFile::geometry();
    const QString path = scratch + QStringLiteral("/choice-sample.pdf");
    QString why;
    check(write(path, 0, false, &why),
          QStringLiteral("setup: the choice sample is written: %1").arg(why));
    const FormFill::Frame frame = flatFrame();
    const QSize pageSize(595, 842);

    // The value one field holds in the file a save wrote, which is the only place a test
    // can read one: a live document answers nothing about a field's value except through
    // the count of the fields that changed.
    auto savedValue = [&scratch](PdfiumBackend &document, const QString &name) {
        QString staged;
        QString error;
        if (!document.stageFieldValues(scratch, &staged, &error)) {
            note(QStringLiteral("choicesave: %1 wrote nothing out (%2)").arg(name, error));
            return QString();
        }
        QString why;
        const QString value = fieldValue(staged, name, &why);
        note(QStringLiteral("choicesave %1 = [%2] %3").arg(name, value, why));
        QFile::remove(staged);
        return value;
    };

    // ---- the two shapes H1 4 added to the sample ----
    //
    // The companion every later zero needs: a press that is refused somewhere else on the
    // page is only evidence if the same press is accepted where the file put the field.
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              QStringLiteral("choice: the sample opens (%1)").arg(document.lastError()));
        struct Expected
        {
            const char *name;
            QRectF rect;
            FormFill::Field field;
        };
        const Expected expected[] = {
            {"editable combo box", geometry.editableComboBox, FormFill::Field::ComboBox},
            {"multi-select list box", geometry.multiListBox, FormFill::Field::ListBox},
        };
        for (const Expected &one : expected) {
            const FormFill::Field found
                = document.fieldAt(0, frame, flatDevice(one.rect));
            note(QStringLiteral("choicehit %1: field=%2").arg(one.name).arg(int(found)));
            check(found == one.field,
                  QStringLiteral("choice: the %1 is hit as its own kind (got %2)")
                      .arg(one.name)
                      .arg(int(found)));
        }
    }

    // ---- the dropdown is the engine's own, and a row of it is a click the page cannot see
    //
    // The question H1 1 asks first: does PDFium draw the list, or does a Qt popup have to?
    // It draws it. The evidence is in two parts -- the pixels under the field, which are
    // blank paper before the press and the list's first row after it, and the rectangle
    // the engine asks to have drawn again, which reaches below the field's own box
    // because that is where it put the list. (Measured on a four-option combo: the band
    // under the field went from 122 to 700 inked pixels and the invalidate grew from
    // (47,675)-(201,721) to (47,652)-(201,721) for a field whose /Rect is y 700..720.)
    const QRectF combo = geometry.comboBox;
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the dropdown");

        // The strip of paper under the combo box, in the device grid of a render at one
        // pixel per point with no reader turn: from two points under the field's lower
        // edge to two points above the list box's upper one, which is the only blank
        // paper there is. Device y grows downwards, so the flip is the page's height.
        const QRect underCombo(QPoint(int(combo.left()) + 2, int(842 - combo.top()) + 2),
                               QPoint(int(combo.right()) - 2,
                                      int(842 - geometry.listBox.bottom()) - 2));
        const auto inkUnder = [&document, &underCombo, pageSize] {
            const QImage page = document.render(0, pageSize, RenderOptions{});
            return inkBox(page.copy(underCombo));
        };
        const QRect before = inkUnder();
        note(QStringLiteral("choicedrop: strip %1,%2 - %3,%4 ink before=[%5]")
                 .arg(underCombo.x()).arg(underCombo.y()).arg(underCombo.right())
                 .arg(underCombo.bottom()).arg(before.isValid() ? "some" : "none"));
        check(!before.isValid(),
              "choice: the paper under a combo box that has not been opened is blank, so "
              "there is something for the dropdown to be");

        const QPointF button = flatDevicePoint(comboBoxButton(combo));
        check(document.mousePressed(0, frame, button, {}),
              "choice: a press on the dropdown button goes to the form");
        document.mouseReleased(0, frame, button, {});
        check(document.focusedFieldType() == FormFill::Field::ComboBox,
              "choice: and the combo box has the keyboard");
        const QVector<FormFill::Repaint> opened = document.takeRepaints();
        double lowest = 1e9;
        for (const FormFill::Repaint &one : opened)
            lowest = qMin(lowest, one.userSpace.top());
        note(QStringLiteral("choicedrop: %1 repaint(s), lowest user y asked for %2 "
                            "(the field's own lower edge is %3)")
                 .arg(opened.size()).arg(lowest).arg(combo.top()));
        check(!opened.isEmpty() && lowest < combo.top() - 20,
              "choice: the engine asks for a repaint under the field, which is where it "
              "drew the list itself");
        check(inkUnder().isValid(), "choice: and the list is in the pixels");

        // A row of that list is not on the page as far as the hit test is concerned, and
        // the press has to be taken anyway. Measured: at the very point where
        // FPDFPage_HasFormFieldAtPoint answered -1, FORM_OnLButtonDown answered true, the
        // row became the field's live text and the release committed it. A gate that
        // trusted the hit test dropped the pick -- and with it the release, which a view
        // only sends after a press it was told about.
        const QPointF rowZero = flatDevicePoint(QPointF(combo.center().x(),
                                                        popupRowY(combo, 0)));
        check(document.fieldAt(0, frame, rowZero) == FormFill::Field::None,
              "choice: the page's own hit test does not see the dropdown's rows");
        check(document.mousePressed(0, frame, rowZero, {}),
              "choice: a press on a dropdown row still goes to the form");
        document.mouseReleased(0, frame, rowZero, {});
        check(savedValue(document, QStringLiteral("pick")) == QStringLiteral("a"),
              "choice: the row the reader let go on is the answer the file carries");
    }

    // The same press with the list closed is the control for the one above: it is a point
    // on blank paper, the hit test says so, and the engine says no -- measured, a press on
    // paper answers FORM_OnLButtonDown 0 whichever kind of field has the keyboard, which
    // is what leaves the click to the view (and the view lets the field go).
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the closed-dropdown control");
        const QPointF centre = flatDevice(combo);
        check(document.mousePressed(0, frame, centre, {}),
              "choice: a press in the middle of the box takes its focus");
        document.mouseReleased(0, frame, centre, {});
        document.takeRepaints();
        const QPointF rowZero
            = flatDevicePoint(QPointF(combo.center().x(), popupRowY(combo, 0)));
        check(!document.mousePressed(0, frame, rowZero, {}),
              "choice: with no list open, the same point is paper and the press is not "
              "the form's");
        check(savedValue(document, QStringLiteral("pick")) == QStringLiteral("b"),
              "choice: and the value is still the one the file was written with");
    }

    // ---- Alt+Down opens the same list, by being a click rather than a key ----
    //
    // Measured: FORM_OnKeyDown(FWL_VKEY_Down, FWL_EVENTFLAG_AltKey) answered false and
    // painted nothing new, while a hover-less Down+Up two points inside the field's right
    // edge opened the list, closed it on the next round and opened it again on the one
    // after. Plain Down is left to the engine, which walks the field's live text and
    // commits nothing -- measured, and the reason the keyboard route synthesises a click
    // is that the list it opens is the one whose rows a pointer can then pick.
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for Alt+Down");
        const QPointF centre = flatDevice(combo);
        check(document.mousePressed(0, frame, centre, {}), "choice: the box takes focus");
        document.mouseReleased(0, frame, centre, {});
        document.takeRepaints();
        // The key that is not a shortcut for the button first, while the list is still
        // closed: a plain Down walks the choice without opening anything, so it must not
        // be answered by this program pretending to be a mouse.
        check(document.keyPressed(Qt::Key_Down, {}),
              "choice: a plain Down still goes to the engine as a key");
        document.takeRepaints();
        check(document.keyPressed(Qt::Key_Down, Qt::AltModifier),
              "choice: Alt+Down is the form's while a combo box has the keyboard");
        const QVector<FormFill::Repaint> opened = document.takeRepaints();
        double lowest = 1e9;
        for (const FormFill::Repaint &one : opened)
            lowest = qMin(lowest, one.userSpace.top());
        note(QStringLiteral("choicealt: %1 repaint(s), lowest user y %2 (field edge %3)")
                 .arg(opened.size()).arg(lowest).arg(combo.top()));
        check(!opened.isEmpty() && lowest < combo.top() - 20,
              "choice: Alt+Down asked for the band under the field, which is the list");
    }

    // ---- a click that moves between two fields ----
    //
    // The same missing move, from its other side: measured, a press on a field that is
    // not the one holding the keyboard is refused when no FORM_OnMouseMove came first
    // (down=0, the keyboard stays where it was), and the same press after a move is
    // accepted. The view dispatches moves only during a drag, so before the press sent
    // its own move the reader's second click -- on anything -- did nothing but take the
    // dropdown's first click with it.
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for a click that moves between fields");
        check(document.mousePressed(0, frame, flatDevice(combo), {}),
              "choice: a press in the combo box takes the keyboard");
        document.mouseReleased(0, frame, flatDevice(combo), {});
        check(document.focusedFieldType() == FormFill::Field::ComboBox,
              "choice: and it has it");
        check(document.mousePressed(0, frame, flatDevice(geometry.textField), {}),
              "choice: a press on a field that is not the focused one goes to the form");
        document.mouseReleased(0, frame, flatDevice(geometry.textField), {});
        check(document.focusedFieldType() == FormFill::Field::TextField,
              "choice: and the keyboard moves to it");
    }

    // ---- an editable combo box is also a text field ----
    //
    // /Ff carries the Edit bit (19, so 262144) beside the Combo bit, and the engine takes
    // characters for it: measured, FORM_OnChar answered 1 and each character appended to
    // the live text, which is what a reader typing an answer that is in none of the
    // options needs. The same call on a combo box without the bit answered 0.
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the editable box");
        const QPointF centre = flatDevice(geometry.editableComboBox);
        check(document.mousePressed(0, frame, centre, {}),
              "choice: an editable combo box takes focus in its middle, not its button");
        document.mouseReleased(0, frame, centre, {});
        check(document.characterTyped(QLatin1Char('x'), {}),
              "choice: and it takes a character");
        check(document.characterTyped(QLatin1Char('y'), {}), "choice: and another");
        check(savedValue(document, QStringLiteral("typeable")).contains(
                  QStringLiteral("xy")),
              "choice: what was typed into an editable combo box is in the file");
        // The box beside it, which offers the same list without the Edit bit, is the
        // control: it takes the focus and refuses the character.
        PdfiumBackend plain;
        check(plain.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the plain combo box");
        const QPointF other = flatDevice(combo);
        check(plain.mousePressed(0, frame, other, {}), "choice: a plain combo box takes focus");
        plain.mouseReleased(0, frame, other, {});
        check(!plain.characterTyped(QLatin1Char('x'), {}),
              "choice: a combo box without the Edit bit takes no character");
    }

    // ---- a list box that holds more than one answer ----
    //
    // Ctrl+click adds to the selection (measured: FWL_EVENTFLAG_ControlKey left the rows
    // already picked alone, Shift picked a range) and /V becomes an array, which is the
    // shape that breaks every read this program had. All three were read off
    // chromium/8111 on a four-option field whose /V starts as {two}: a plain click on the
    // first row and a Ctrl+click on the second leave it holding {one,two} while its live
    // text reads "two" -- the label picked last, which is exactly what the file said --
    // FPDFAnnot_GetFormFieldValue answers "one", the first selected value alone, and
    // FPDFAnnot_GetStringValue("/V") answers nothing at all. The truth is
    // FPDFAnnot_IsOptionSelected, and it does not move until the field lets go of the
    // keyboard.
    //
    // Under-reporting here is the one that loses data: DocumentTab::saveEdits() does
    // nothing at all when the count of changed fields is zero, so Ctrl+S would quietly
    // throw the second selection away and the close prompt would not ask. The count
    // therefore answers "changed" whenever it cannot compare -- which is while such a
    // field has the keyboard -- and once focus leaves the markers are readable and the
    // comparison is exact again. The three blocks below are the hole, the exactness, and
    // the star going back out.
    const QRectF many = geometry.multiListBox;
    const auto manyRow = [&many](int row) {
        return flatDevicePoint(QPointF(many.center().x(), listRowY(many, row)));
    };
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the multi-select list");
        check(document.mousePressed(0, frame, manyRow(0), {}),
              "choice: a press on a list box row goes to the form");
        document.mouseReleased(0, frame, manyRow(0), {});
        check(document.differingFieldCount() == 1, "choice: picking a row is a change");
        check(document.mousePressed(0, frame, manyRow(1), Qt::ControlModifier),
              "choice: and so is a Ctrl+press on another one");
        document.mouseReleased(0, frame, manyRow(1), Qt::ControlModifier);
        note(QStringLiteral("choicemulti: after a plain click on the first row and a "
                            "Ctrl+click on the one the file already had, the count is %1")
                 .arg(document.differingFieldCount()));
        check(document.differingFieldCount() == 1,
              "choice: a multi-select list box whose live text happens to read like the "
              "file still counts as changed");
        // What a save wrote is read here the only way this target can -- the same
        // GetFormFieldValue the backend uses, which for an array answers the first of
        // what was picked. The array itself (two elements, both of them) is asserted in
        // documenttab_smoke, which links qpdf and can read /V as an array.
        const QString first = savedValue(document, QStringLiteral("many"));
        check(first == QStringLiteral("one") || first == QStringLiteral("two"),
              QStringLiteral("choice: the save wrote a multi-select value at all, of "
                             "which the ordinary read answers [%1]")
                  .arg(first));
    }
    {
        // The one shape that tells the two reads apart: a Ctrl+click that *adds* to the
        // file's own set leaves the first selected value reading exactly what the
        // snapshot says, so a count taken from that read answers "nothing changed".
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the file's own set plus one");
        check(document.mousePressed(0, frame, manyRow(2), Qt::ControlModifier),
              "choice: a Ctrl+press on a third row goes to the form");
        document.mouseReleased(0, frame, manyRow(2), Qt::ControlModifier);
        check(document.differingFieldCount() == 1,
              "choice: adding to a multi-select list box is a change while it has the "
              "keyboard");
        document.releaseFocus();
        check(document.differingFieldCount() == 1,
              "choice: and still one once the field lets go, where the markers can be "
              "read -- the first selected value alone would have said nothing changed");
    }
    {
        // An over-report that never ends is a star that never goes out. Putting the
        // file's own answer back has to read as no change the moment the markers can be
        // read again.
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for a click that changes nothing");
        check(document.mousePressed(0, frame, manyRow(1), {}),
              "choice: a press on the row the file already had goes to the form");
        document.mouseReleased(0, frame, manyRow(1), {});
        check(document.differingFieldCount() == 1,
              "choice: while the field has the keyboard the count cannot compare, so it "
              "answers changed");
        document.releaseFocus();
        check(document.differingFieldCount() == 0,
              "choice: and picking the file's own answer again is no change once the "
              "markers can be read");
    }

    // ---- the wheel belongs to a list box that has the keyboard ----
    //
    // FORM_OnMouseWheel answers 1 only when the point is inside the widget that has focus
    // and that widget is a list: measured 1 for a twenty-row list and for a three-row one
    // with every row already visible, and 0 over blank paper, over a list that had just
    // let go of the keyboard, and over a combo box -- with its own dropdown open, where
    // the point is inside the dropdown. That answer is the whole of the routing rule, so
    // the view asks and scrolls the page only on a no.
    //
    // One call is one row however large the number handed to it is (measured: a dy of -1
    // and a dy of -120 each moved the selection by exactly one row), which is why the
    // interface counts detents and the backend sends one event per detent. The sign is
    // the header's own -- "negative values mean down" -- and Qt's angleDelta().y()
    // unchanged: positive walked o05 -> o04 -> o03 and negative o05 -> o06 -> o07.
    {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the wheel");
        const QRectF list = geometry.listBox; // three options, /V (b) = the second
        const QPointF onList
            = flatDevicePoint(QPointF(list.center().x(), listRowY(list, 1)));
        check(!document.mouseWheeled(0, frame, onList, -1, {}),
              "choice: the wheel over a list box nobody has focused is the page's");
        check(document.mousePressed(0, frame, onList, {}),
              "choice: a press on the list box takes its focus");
        document.mouseReleased(0, frame, onList, {});
        document.takeRepaints();
        check(document.mouseWheeled(0, frame, onList, -2, {}),
              "choice: a focused list box takes the wheel");
        // Asked before the save below, which commits the field and so takes the keyboard
        // away from it: a refusal read after that would be the same refusal as the first
        // line of this block, and would say nothing about the routing rule.
        check(!document.mouseWheeled(0, frame, QPointF(400, 700), -1, {}),
              "choice: the wheel over blank paper is the page's even while a field has "
              "the keyboard");
        check(savedValue(document, QStringLiteral("list")) == QStringLiteral("c"),
              "choice: two detents down walked the selection two rows, to the last one");
    }
    {
        // The other direction, on a document of its own so the walk starts where the file
        // put it.
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready,
              "choice: the sample opens for the wheel upwards");
        const QRectF list = geometry.listBox;
        const QPointF onList
            = flatDevicePoint(QPointF(list.center().x(), listRowY(list, 1)));
        check(document.mousePressed(0, frame, onList, {}), "choice: the list box takes focus");
        document.mouseReleased(0, frame, onList, {});
        check(document.mouseWheeled(0, frame, onList, 1, {}),
              "choice: and takes the wheel the other way too");
        check(savedValue(document, QStringLiteral("list")) == QStringLiteral("a"),
              "choice: one detent up walked the selection to the first row");
        document.releaseFocus();
        check(!document.mouseWheeled(0, frame, onList, -1, {}),
              "choice: a list box that has let go of the keyboard gives the wheel back");
    }
}

// ---- a signed document: what the file says, and what a save does to it ----
//
// The criterion handoff-06 H4 states is bytes, not validity: an incremental update
// appends a revision, so the saved file's first N bytes (N = the source's length) are
// the source's own, and the two spans its /ByteRange names hash the same before and
// after. The sample's ranges are computed in the generator from the assembled bytes
// (formsample.h); this function first proves they name real bytes -- the first span
// ends at a '<', the second starts just past a '>' -- so a reader that agreed with a
// range pointing at nothing could not pass by accident.
void checkSignatures(const QString &scratch)
{
    const SignedGeometry fields;
    const FormFill::Frame frame = flatFrame();
    const auto readAll = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    };
    const auto digest = [](const QByteArray &bytes) {
        return QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    };

    const QString signedPath = scratch + QStringLiteral("/signed.pdf");
    const QString twoPath = scratch + QStringLiteral("/signed-twice.pdf");
    const QString p1Path = scratch + QStringLiteral("/certified-p1.pdf");
    const QString p2Path = scratch + QStringLiteral("/certified-p2.pdf");
    const QString lockedPath = scratch + QStringLiteral("/signed-locked.pdf");
    QString why;
    check(writeSigned(signedPath, 0, 1, false, &why),
          QStringLiteral("signed: setup: a signed sample is written: %1").arg(why));
    check(writeSigned(twoPath, 0, 2, false, &why),
          QStringLiteral("signed: setup: a twice-signed sample is written: %1").arg(why));
    check(writeSigned(p1Path, 1, 1, false, &why),
          QStringLiteral("signed: setup: a P=1 sample is written: %1").arg(why));
    check(writeSigned(p2Path, 2, 1, false, &why),
          QStringLiteral("signed: setup: a P=2 sample is written: %1").arg(why));
    check(writeSigned(lockedPath, 0, 1, true, &why),
          QStringLiteral("signed: setup: a locked-field sample is written: %1").arg(why));

    const QByteArray original = readAll(signedPath);
    check(!original.isEmpty(), "signed: and the signed sample can be read back");
    // The two cycle sizes of the signed document, kept for the unsigned comparison after
    // this block: the whole point of the branch is that an append costs a whole revision
    // every time while a rewrite costs only what changed.
    qint64 signedFirstCycle = 0;
    qint64 signedSecondCycle = 0;

    // ---- the file's own claim, read through PDFium ----
    {
        PdfiumBackend document;
        check(document.load(signedPath, QString()) == Document::Status::Ready,
              QStringLiteral("signed: the signed sample opens (%1)").arg(document.lastError()));
        note(QStringLiteral("signed: count=%1 docMdp=%2")
                 .arg(document.signatureCount())
                 .arg(document.docMdpPermission()));
        check(document.signatureCount() == 1, "signed: one signature is counted");
        check(document.docMdpPermission() == 0,
              "signed: and a signature with no /Perms certifies nothing");

        const QVector<int> range = signatureByteRange(signedPath, 0);
        QStringList rangeNumbers;
        for (int one : range)
            rangeNumbers << QString::number(one);
        note(QStringLiteral("signed: ByteRange=[%1]").arg(rangeNumbers.join(QStringLiteral(", "))));
        check(range.size() == 4, "signed: PDFium reads a four-number ByteRange");
        if (range.size() == 4) {
            check(range.at(0) == 0, "signed: the first span starts at byte zero");
            check(range.at(1) > 0 && range.at(1) < original.size()
                      && original.at(range.at(1)) == '<',
                  "signed: and ends at the '<' that opens /Contents");
            check(range.at(2) > range.at(1) && range.at(2) <= original.size()
                      && original.at(range.at(2) - 1) == '>',
                  "signed: the second span starts just past the '>'");
            check(range.at(2) + range.at(3) == original.size(),
                  QStringLiteral("signed: and runs to the end of the file (%1 + %2 vs %3)")
                      .arg(range.at(2))
                      .arg(range.at(3))
                      .arg(original.size()));
            check(!signatureCoveredBytes(original, range).isEmpty(),
                  "signed: so the covered bytes are real bytes of this file");
        }

        // ---- the save that only fills a field ----
        check(document.mousePressed(0, frame, flatDevice(fields.textField), {}),
              "signed: the text field takes a press");
        document.mouseReleased(0, frame, flatDevice(fields.textField), {});
        document.commitText(QStringLiteral("SIGNED-EDIT"));
        document.takeRepaints();
        document.releaseFocus();
        QString staged;
        QString stageError;
        check(document.stageFieldValues(scratch, &staged, &stageError),
              QStringLiteral("signed: the field values stage out (%1)").arg(stageError));
        const QByteArray stagedBytes = readAll(staged);
        note(QStringLiteral("signed: source %1 bytes, staged %2 bytes, prefix=%3")
                 .arg(original.size())
                 .arg(stagedBytes.size())
                 .arg(stagedBytes.left(original.size()) == original ? 1 : 0));
        check(stagedBytes.size() > original.size(),
              "signed: the staged file is the source plus a revision, not a rewrite");
        check(stagedBytes.left(original.size()) == original,
              "signed: the source's own bytes are the staged file's prefix, unchanged");
        if (range.size() == 4) {
            const QByteArray before = signatureCoveredBytes(original, range);
            const QByteArray after = signatureCoveredBytes(stagedBytes, range);
            note(QStringLiteral("signed: covered sha256 before=%1 after=%2")
                     .arg(digest(before), digest(after)));
            check(!before.isEmpty() && digest(before) == digest(after),
                  "signed: both spans of the ByteRange hash the same before and after");
        }
        check(signatureByteRange(staged, 0).size() == 4,
              "signed: and the staged file still carries a readable signature");
        const QString stagedValue = fieldValue(staged, QStringLiteral("name"), &why);
        check(stagedValue.contains(QStringLiteral("SIGNED-EDIT")),
              QStringLiteral("signed: with the reader's answer inside it (%1)").arg(stagedValue));

        // Two 保存 cycles, which is what the branch is really for. Reading the first
        // save's own file and saving once more is the shape a reader's evening takes;
        // staging twice from one open document was measured too and says nothing about
        // it (3059 bytes then 3059: PDFium writes a revision against the file the
        // document was loaded from, not against what it staged a moment ago).
        QString round2Path;
        QString round2Error;
        {
            PdfiumBackend again;
            check(again.load(staged, QString()) == Document::Status::Ready,
                  QStringLiteral("signed: the file the first save wrote reopens (%1)")
                      .arg(again.lastError()));
            check(again.signatureCount() == 1,
                  "signed: and still counts the signature it was opened with");
            again.mousePressed(0, frame, flatDevice(fields.textField), {});
            again.mouseReleased(0, frame, flatDevice(fields.textField), {});
            again.commitText(QStringLiteral("SIGNED-EDIT ROUND-2"));
            again.takeRepaints();
            again.releaseFocus();
            check(again.stageFieldValues(scratch, &round2Path, &round2Error),
                  QStringLiteral("signed: a second 保存 on the saved file stages (%1)")
                      .arg(round2Error));
        }
        const QByteArray round2Bytes = readAll(round2Path);
        signedFirstCycle = stagedBytes.size();
        signedSecondCycle = round2Bytes.size();
        note(QStringLiteral("signed: two save cycles -- source %1, first %2, second %3 bytes")
                 .arg(original.size())
                 .arg(stagedBytes.size())
                 .arg(round2Bytes.size()));
        check(round2Bytes.size() > stagedBytes.size(),
              "signed: a second cycle costs another revision on top of the first");
        check(round2Bytes.left(original.size()) == original,
              "signed: and the source's bytes are still the prefix of that");
        if (range.size() == 4) {
            check(digest(signatureCoveredBytes(round2Bytes, range))
                      == digest(signatureCoveredBytes(original, range)),
                  "signed: with both spans of the ByteRange still hashing the same");
        }
        QFile::remove(staged);
        QFile::remove(round2Path);
    }

    // ---- the same two cycles on a form with no signature dictionary in it ----
    //
    // The whole-form sample carries a /Sig widget with no /V. FPDF_GetSignatureCount
    // counts that field (measured: 1, with no /ByteRange numbers and no /Contents bytes
    // behind it), while PDF 32000-1 sec.12.8.1 calls the dictionary the signature, so an
    // empty field is nothing. The backend's count keeps the specification's answer, and
    // that is the number 文档 says out loud and the number that picks which save flag
    // runs -- the two rows below are the reading and the consequence.
    {
        const QString unsignedPath = scratch + QStringLiteral("/unsigned-again.pdf");
        check(write(unsignedPath, 0, false, &why),
              QStringLiteral("signed: setup: an unsigned form sample is written: %1").arg(why));
        PdfiumBackend wholeForm;
        check(wholeForm.load(unsignedPath, QString()) == Document::Status::Ready,
              "signed: the unsigned form sample opens");
        int rawCount = 0;
        int rawRangeNumbers = 0;
        unsigned long rawContents = 0;
        if (FPDF_DOCUMENT raw = FPDF_LoadDocument(unsignedPath.toUtf8().constData(), nullptr)) {
            rawCount = FPDF_GetSignatureCount(raw);
            FPDF_SIGNATURE signature = FPDF_GetSignatureObject(raw, 0);
            int buffer[8] = {0};
            rawRangeNumbers = signature ? int(FPDFSignatureObj_GetByteRange(signature, buffer, 8))
                                        : 0;
            rawContents = signature ? FPDFSignatureObj_GetContents(signature, nullptr, 0) : 0;
            FPDF_CloseDocument(raw);
        }
        note(QStringLiteral("signed unsigned: the library counts %1 signature field(s) in the "
                            "whole-form sample; its first answers /ByteRange %2 numbers and "
                            "/Contents %3 bytes, and the reader counts %4")
                 .arg(rawCount)
                 .arg(rawRangeNumbers)
                 .arg(rawContents)
                 .arg(wholeForm.signatureCount()));
        check(rawCount == 1 && rawRangeNumbers == 0 && rawContents == 0,
              "signed: the empty /Sig field is counted by the library and holds no signature");
        check(wholeForm.signatureCount() == 0,
              "signed: so the reader counts no signature in a blank signature field");
        QFile::remove(unsignedPath);

        const QString fieldOnlyPath = scratch + QStringLiteral("/field-only.pdf");
        {
            // One text field and nothing else: no /Sig widget, so the writer's count is
            // the zero this branch is chosen for. The shape is the specification's -- a
            // /Tx widget registered in /AcroForm /Fields, the same three numbers every
            // other sample here uses -- written with the generator's own helpers rather
            // than with the whole-form one, because that one always carries a signature
            // field.
            QVector<QByteArray> objects(7);
            objects[1] = "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>";
            objects[2] = "<< /Type /Pages /Kids [4 0 R] /Count 1 >>";
            objects[3] = "<< /Type /AcroForm /Fields [5 0 R] /DA (/Helv 9 Tf 0 g ) /DR << "
                         "/Font << /Helv 6 0 R >> >> >>";
            objects[4] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Rotate 0 "
                         "/Resources << /Font << /Helv 6 0 R >> >> /Annots [5 0 R] >>";
            objects[5] = widgetBody(QStringLiteral("name"), "/Tx", fields.textField,
                                    " /V (plain) /DA (/Helv 10 Tf 0 g )");
            objects[6] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
            QFile sample(fieldOnlyPath);
            check(sample.open(QIODevice::WriteOnly | QIODevice::Truncate),
                  QStringLiteral("signed: setup: the field-only sample is written (%1)").arg(why));
            sample.write(assemblePdf(objects, 1));
            sample.close();
        }
        const QByteArray plainBefore = readAll(fieldOnlyPath);
        QString firstStage;
        {
            PdfiumBackend document;
            check(document.load(fieldOnlyPath, QString()) == Document::Status::Ready,
                  "signed: the field-only document opens");
            check(document.signatureCount() == 0,
                  "signed: and it counts no signature");
            check(document.fillable(), "signed: while still being a form to fill");
            document.mousePressed(0, frame, flatDevice(fields.textField), {});
            document.mouseReleased(0, frame, flatDevice(fields.textField), {});
            document.commitText(QStringLiteral("PLAIN-EDIT"));
            document.takeRepaints();
            document.releaseFocus();
            QString error;
            check(document.stageFieldValues(scratch, &firstStage, &error),
                  QStringLiteral("signed: the field-only document stages (%1)").arg(error));
        }
        const QByteArray firstBytes = readAll(firstStage);
        QString secondStage;
        {
            PdfiumBackend again;
            check(again.load(firstStage, QString()) == Document::Status::Ready,
                  "signed: the field-only document's own save reopens");
            again.mousePressed(0, frame, flatDevice(fields.textField), {});
            again.mouseReleased(0, frame, flatDevice(fields.textField), {});
            again.commitText(QStringLiteral("PLAIN-EDIT ROUND-2"));
            again.takeRepaints();
            again.releaseFocus();
            QString error;
            check(again.stageFieldValues(scratch, &secondStage, &error),
                  QStringLiteral("signed: and a second 保存 stages (%1)").arg(error));
        }
        const QByteArray secondBytes = readAll(secondStage);
        note(QStringLiteral("signed field-only: two cycles -- source %1, first %2, second %3 "
                            "bytes (the signed sample above went %4 to %5)")
                 .arg(plainBefore.size())
                 .arg(firstBytes.size())
                 .arg(secondBytes.size())
                 .arg(signedFirstCycle)
                 .arg(signedSecondCycle));
        check(firstBytes.left(plainBefore.size()) != plainBefore,
              "signed: a save of a document with no signature rewrites instead of appending");
        // The claim the other half of the ternary buys: the second cycle of an unsigned
        // document costs the words that changed, while the signed one above cost another
        // whole revision.
        check(secondBytes.size() - firstBytes.size() < signedSecondCycle - signedFirstCycle,
              QStringLiteral("signed: and a second unsigned cycle grows less than an append "
                             "cycle does (got %1 after %2, the append went %3 after %4)")
                  .arg(secondBytes.size())
                  .arg(firstBytes.size())
                  .arg(signedSecondCycle)
                  .arg(signedFirstCycle));
        QFile::remove(firstStage);
        QFile::remove(secondStage);
    }

    // ---- two signatures, and the second range as well ----
    {
        PdfiumBackend document;
        check(document.load(twoPath, QString()) == Document::Status::Ready,
              "signed: the twice-signed sample opens");
        check(document.signatureCount() == 2,
              QStringLiteral("signed: both signatures are counted (%1)")
                  .arg(document.signatureCount()));
        const QByteArray bytes = readAll(twoPath);
        for (int index = 0; index < 2; ++index) {
            const QVector<int> range = signatureByteRange(twoPath, index);
            check(range.size() == 4
                      && range.at(0) == 0 && bytes.at(range.at(1)) == '<'
                      && bytes.at(range.at(2) - 1) == '>'
                      && range.at(2) + range.at(3) == bytes.size(),
                  QStringLiteral("signed: signature %1 names its own Contents hole").arg(index));
        }
    }

    // ---- DocMDP: P=1 takes no input, P=2 takes form input ----
    {
        PdfiumBackend sealed;
        check(sealed.load(p1Path, QString()) == Document::Status::Ready,
              "signed: the P=1 sample opens");
        note(QStringLiteral("signed: P=1 docMdp=%1 fillable=%2 fieldAt=%3")
                 .arg(sealed.docMdpPermission())
                 .arg(sealed.fillable())
                 .arg(int(sealed.fieldAt(0, frame, flatDevice(fields.textField)))));
        check(sealed.docMdpPermission() == 1, "signed: the certification signature says P=1");
        check(!sealed.fillable(), "signed: P=1 makes the form read-only");
        check(sealed.fieldAt(0, frame, flatDevice(fields.textField)) == FormFill::Field::None,
              "signed: and a click on the field answers no field at all");
        check(!sealed.mousePressed(0, frame, flatDevice(fields.textField), {}),
              "signed: so no press reaches the form");
        check(!sealed.hasFocusedField(), "signed: and nothing took the keyboard");

        PdfiumBackend fillable;
        check(fillable.load(p2Path, QString()) == Document::Status::Ready,
              "signed: the P=2 sample opens");
        check(fillable.docMdpPermission() == 2, "signed: the certification signature says P=2");
        check(fillable.fillable(), "signed: P=2 leaves the form fillable");
        check(fillable.mousePressed(0, frame, flatDevice(fields.textField), {}),
              "signed: and the text field takes a press");
        fillable.mouseReleased(0, frame, flatDevice(fields.textField), {});
        check(fillable.hasFocusedField(), "signed: with the keyboard following it");
        fillable.commitText(QStringLiteral("P2-EDIT"));
        fillable.takeRepaints();
        fillable.releaseFocus();
        QString staged;
        QString stageError;
        check(fillable.stageFieldValues(scratch, &staged, &stageError),
              QStringLiteral("signed: a P=2 field edit still stages (%1)").arg(stageError));
        const QByteArray p2Bytes = readAll(p2Path);
        const QByteArray p2Staged = readAll(staged);
        check(p2Staged.left(p2Bytes.size()) == p2Bytes,
              "signed: and it is an incremental update of the certified file");
        QFile::remove(staged);

        // P=3 says filling and annotations and nothing else. This version writes no
        // annotations, so what a reader gets is the same door P=2 opens: the field takes
        // the keyboard. Measured rather than inferred from the branch, because "P=3 is
        // like P=2 here" is a claim about this program, not about the specification.
        const QString p3Path = scratch + QStringLiteral("/certified-p3.pdf");
        check(writeSigned(p3Path, 3, 1, false, &why),
              QStringLiteral("signed: setup: a P=3 sample is written: %1").arg(why));
        PdfiumBackend annotating;
        check(annotating.load(p3Path, QString()) == Document::Status::Ready,
              "signed: the P=3 sample opens");
        check(annotating.docMdpPermission() == 3,
              QStringLiteral("signed: the certification signature says P=3 (%1)")
                  .arg(annotating.docMdpPermission()));
        check(annotating.fillable(), "signed: P=3 leaves the form fillable");
        check(annotating.mousePressed(0, frame, flatDevice(fields.textField), {}),
              "signed: and the text field takes a press like the P=2 file does");
        annotating.mouseReleased(0, frame, flatDevice(fields.textField), {});
        check(annotating.hasFocusedField(), "signed: with the keyboard following it");
    }

    // ---- the field a signature's /Lock seals ----
    //
    // Measured before any guard of ours was written: PDFium itself does not honour a
    // /Lock. On chromium/8111 the sealed field answered fieldAt=6, took the press and
    // took the keyboard, exactly like an unsealed one -- so the guard is ours. The
    // application reads the names from DocumentEditor's qpdf pass (table 233) and hands
    // them over before the first gesture; this block comes through the same door with
    // the one name the sample's /Lock lists. The reading itself is measured in
    // documenteditor_smoke.
    {
        PdfiumBackend locked;
        check(locked.load(lockedPath, QString()) == Document::Status::Ready,
              "signed: the locked-field sample opens");
        locked.setLockedFieldNames({QStringLiteral("locked")});

        const FormFill::Field sealedHit
            = locked.fieldAt(0, frame, flatDevice(fields.lockedField));
        const bool sealedPressed
            = locked.mousePressed(0, frame, flatDevice(fields.lockedField), {});
        const bool sealedFocused = locked.hasFocusedField();
        const bool unsealedPressed
            = locked.mousePressed(0, frame, flatDevice(fields.textField), {});
        note(QStringLiteral("signed lock: sealed fieldAt=%1 press=%2 focused=%3, "
                            "unsealed press=%4")
                 .arg(int(sealedHit))
                 .arg(sealedPressed ? 1 : 0)
                 .arg(sealedFocused ? 1 : 0)
                 .arg(unsealedPressed ? 1 : 0));
        check(sealedHit == FormFill::Field::None,
              "signed lock: a sealed field is not a field the pointer can point at");
        check(!sealedPressed, "signed lock: and a click on it reaches no form");
        check(!sealedFocused, "signed lock: so nothing there took the keyboard");

        // The companion row: the same document's unsealed field still fills, so the
        // refusal above is the /Lock and not the sample or the whole form.
        check(locked.fieldAt(0, frame, flatDevice(fields.textField))
                  == FormFill::Field::TextField,
              "signed lock: the field beside it is still a text field");
        check(unsealedPressed, "signed lock: and still takes a press");
        locked.mouseReleased(0, frame, flatDevice(fields.textField), {});
        check(locked.hasFocusedField(), "signed lock: with the keyboard following it");
        locked.releaseFocus();
    }
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

    // The form layer needs files of its own, in a directory that is not the fixtures:
    // a sample written into tests/fixtures would be walked by every test that globs it
    // (the same reason the generated form samples of documenteditor_smoke refuse a
    // directory naming `fixtures`).
    QTemporaryDir formScratch;
    check(formScratch.isValid(), "a scratch directory for the form samples");
    if (formScratch.isValid()) {
        checkFormFilling(formScratch.path());
        checkChoiceFields(formScratch.path());
        checkSignatures(formScratch.path());
    }

    if (failures == 0) {
        std::cout << "pdfium backend: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
