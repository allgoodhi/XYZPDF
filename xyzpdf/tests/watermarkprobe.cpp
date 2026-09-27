// Whether a watermark can be a feature of this program at all, and which of the two ways of
// putting ink on a page survives contact with the renderer.
//
// This is a probe, not a test: tools/build_all_tests.py builds it and does not run it (its
// name carries 'probe'), because every line here is somebody else's answer rather than this
// program's promise. It is run by hand, from the project root so the fixtures are found by
// relative path:
//
//   ./build-local-release/tests/xyzpdf_watermarkprobe.exe
//   ./build-local-release/tests/xyzpdf_watermarkprobe.exe "H:\pdf_test\CH340.PDF"
//   QT_QPA_PLATFORM=windows ./build-local-release/tests/xyzpdf_watermarkprobe.exe \
//       "H:\pdf_test\CH340.PDF" H:\tmp\wm-renders
//
// The second argument is a real document to run the routes over, and the third is a folder to
// keep page-1 pictures in. Three of the six questions below are only answered by one of those
// pictures (see saveRender), so a run that intends to settle them passes the third.
//
// Why it exists before any of the feature: docs/PLAN.md lists 水印 under "QPDF 结构编辑 ——
// 这些不碰页面内容流". That is wrong, and the wrongness is the whole question. A page number,
// a rotation, a page taken out are dictionary edits. A watermark is *drawn marks*, which means
// writing content-stream operators into the page and placing something over it -- which is the
// content stream. So the plan's sentence has to be either fixed or explained, and the honest
// way to do that is to measure what the two candidate routes produce.
//
// The questions, in the order the output prints them:
//
//   1. Text route. Draw the string with PDF operators in a base-14 font (Helvetica) at 25%
//      alpha. Two sub-questions: does Qt PDF paint it, and does the string then appear in the
//      page's *extractable* text -- because if it does, a watermark silently joins everything
//      that reads a page's text: copying, 划线翻译, export. That is a consequence, not a
//      curiosity.
//   2. Chinese. The same route with 水印测试, which base-14 cannot spell at all, and the
//      classic shortcut around that: a Type0 font naming STSong-Light with /UniGB-UCS2-H and
//      *no embedded font file*. If nothing is drawn, 中文水印 needs an embedded font -- which
//      is not a few lines but a font writer -- and the honest feature is "ASCII only" or the
//      other route.
//   3. Image route. Qt paints the string into a bitmap (any script the system has a font for,
//      any angle, for free) and the page gets an image XObject. Two variants measured against
//      each other: without /SMask, which must cover the page's own text, and with it, which
//      must not. "The watermark appeared" cannot tell those apart -- which is exactly why a
//      route that ignores alpha would ship looking fine and covering documents.
//   4. Where it lands on a page that declares /Rotate 90.
//   5. Whether one save can both watermark and carry a password, since both are pending
//      operations on the same document and cannot be two saves.
//   6. Cost, per page, on a 600-page file. Which of the two routes is the expensive one is not
//      readable from the fact that one of them carries a bitmap: the other carries a font object
//      and a graphics state per page, and the bitmap is one object shared by the whole document.
//
// Run it twice, because the platform changes the answers to questions 2 and 3. Qt PDF implements
// PDFium's system-font API with Qt's own font database, so *both* routes' glyphs come from
// whatever the platform resolves: under the offscreen platform that is nothing (family "", every
// glyph an empty box of the same width, and the Latin watermark came out as a row of boxes), and
// the control on it says so by failing. Under QT_QPA_PLATFORM=windows the same lines are what the
// shipped program faces.
//
// The controls are on the instrument, and a broken one makes every line above mean nothing.
// C1/C2 prove the ink sampler can see the page's own text at all; C3 proves "the reader opens
// it" is a check by failing on a file that is not a PDF; C4 proves the file read back is the
// file written (a watermark on page 3 must leave page 1 alone, and a rewrite with no watermark
// must leave it alone too); C5 proves the image bytes that reach the disk decode back to the
// pixels that were painted, so a blank watermark cannot be misread as "the renderer ignores
// /SMask"; C6 proves Qt resolved a font rather than a row of .notdef boxes, which the first
// version of it did not -- it asked only whether any pixel got painted, and a box paints; C7
// proves the two ink thresholds can disagree at all, which is what makes "the marks are light" a
// reachable answer rather than an assumption about /ca.
//
// The answers, as measured on 2026-09-25 with qpdf 12.4 and Qt 6.11.2 on this machine, over the
// 40-page fixture and a 6-page Chinese datasheet (H:\pdf_test\CH340.PDF):
//
//   1. Both routes draw. The text route's string also joins the page's extractable text (13
//      characters became 30), so it would be copied, translated by 划线翻译 and exported along
//      with the document. The image route leaves the text alone (13 stays 13).
//   2. Yes -- a Type0 font naming STSong-Light with /UniGB-UCS2-H and no embedded file drew 水印测试
//      correctly, because the renderer substitutes a system CJK font. Which is also the caveat:
//      the same saved file is drawn with whatever fonts the *next* machine has, and with none it
//      is boxes and garbage (measured under offscreen, where the text route's Chinese came out as
//      "l4SpmK" shapes).
//   3. /SMask is honoured. Unmasked, the bitmap's own transparent-painted black background covers
//      the page and every region reads as 1.0000 ink; masked, the page's own text region reads
//      exactly what it did untouched (0.3564 -> 0.3564) while the page as a whole gains the marks
//      (0.0028 -> 0.0536). /ca on the text route is honoured too: the marks it adds are 0.0067 of
//      the page and 0.0000 of it is dark.
//   4. Neither route knows about /Rotate: the marks are placed in the page's own unrotated space
//      and rotate with it, so on a page displayed rotated they read sideways to whoever is
//      looking. Surviving the rotation is measured (18486 pixels of the render differ); reading
//      upright is not, and needs the placement matrix to fold in page.getRotate().
//   5. One save does both: 40 pages watermarked and R6-encrypted in 56 ms, 0 warnings, refused
//      without the password and drawn with the marks when it is given.
//   6. The image route is the *cheaper* one per page: 0.24 KB/page against the text route's 0.42
//      over a 600-page file, because the bitmap is one shared object and the page only gets a
//      `cm` and a `Do`, while the text route writes a font and a graphics state per page. Both
//      took under 70 ms for 600 pages.
//
// Which is to say: docs/PLAN.md's "这些不碰页面内容流" is wrong about 水印 -- both routes are
// content-stream writes -- and the choice between them is not about feasibility. It is about
// script coverage and rotation (where the image route is free and the text route needs a font
// per script) against text pollution and vector crispness (where the text route is clean and the
// image route is a bitmap at whatever resolution it was painted).

#include "../src/document/qtpdfbackend.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

int brokenControls = 0;

void print(const QString &text)
{
    // Deliberately ASCII throughout: this console is GBK, and one non-ASCII byte turns the
    // reading into mojibake -- which is how a measurement gets mis-transcribed.
    std::cout << text.toStdString() << '\n';
    std::cout.flush();
}

void control(const QString &what, bool held)
{
    print(QStringLiteral("  CONTROL %1  %2").arg(held ? "ok   " : "BROKEN").arg(what));
    if (!held)
        ++brokenControls;
}

QString hexCodePoints(const QString &text)
{
    QString out;
    for (const QChar ch : text) {
        if (!out.isEmpty())
            out += QLatin1Char(' ');
        out += QStringLiteral("U+%1").arg(uint(ch.unicode()), 4, 16, QLatin1Char('0'));
    }
    return out;
}

// ---------------------------------------------------------------- the ink instrument

constexpr int kRenderWidth = 700;

struct Ink
{
    bool ok = false;
    QString error;
    int pages = -1;
    // Fractions of the rendered page that are not white: over the whole page, and over the
    // rectangle the *baseline* page's own text occupies. The second is what separates "drew
    // something" from "covered the page".
    double whole = 0.0;
    // The same page against a *dark* threshold (128 rather than 250). Marks drawn at 25% opacity
    // land near 191 on white, which `whole` counts as ink and this does not, so this is the pair
    // that can say "light marks" at all: without it an ignored /ca and an honoured one are the
    // same number, which is how an earlier run of this probe shipped solid black text and still
    // reported a watermark.
    double dark = 0.0;
    double label = 0.0;
    // The strip along the top edge, which the fixtures leave blank. Without this number the
    // sampler has no way to report "the page is white": every threshold on ink-only regions can
    // be met by an instrument that reads everything as ink.
    double paper = 0.0;
    // How much text the reader can pull out of the page: a watermark placed as real text adds
    // to this; one placed as an image does not.
    int chars = 0;
    // The sampled region, in the page's own points, so it can be handed to another file that
    // measures the same page.
    QRectF regionPoints;
    // The page's own size, so a control can say "this region is smaller than the page".
    QSizeF pagePoints;
};

// What the reader actually shows: Qt PDF hands back a page with *transparent* paper, and the
// window paints a colour behind it. Dropping the alpha instead would report every unpainted
// pixel as black -- measured: the first version of this instrument read the fixture's page 1 as
// 100% ink. Same family as pixelColor() outside an image.
QImage ontoPaper(const QImage &source)
{
    QImage canvas(source.size(), QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.drawImage(0, 0, source);
    painter.end();
    return canvas;
}

double inkFraction(const QImage &image, const QRect &region, int threshold = 250)
{
    const QImage rgb = ontoPaper(image);
    const QRect box = region.intersected(rgb.rect());
    if (box.isEmpty())
        return 0.0;
    // Intersected on purpose. pixelColor() outside the image answers an *invalid* colour whose
    // channels all read 0, which is indistinguishable from black -- the trap that made a
    // preview-pane test report "the paper is black" (docs/store-submission.md's gotchas).
    int dark = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) {
            const QRgb pixel = rgb.pixel(x, y);
            if (qRed(pixel) < threshold || qGreen(pixel) < threshold || qBlue(pixel) < threshold)
                ++dark;
        }
    }
    return double(dark) / double(box.width() * box.height());
}

// One page, rendered at a fixed width, plus whatever the reader says about it.
//
// `useRegion` is what makes a before/after pair comparable: the region comes from the untouched
// file's own text lines and is then sampled in the modified file as well. Recomputing it per
// file would let a watermark that became selectable text move the region and quietly change
// what is being measured.
Ink measureInk(const QString &path, const QString &password, int page,
               const QRectF *useRegion = nullptr)
{
    Ink out;
    QtPdfBackend document;
    if (document.load(path, password) != Document::Status::Ready) {
        out.error = document.lastError();
        return out;
    }
    out.ok = true;
    out.pages = document.pageCount();
    out.chars = int(document.text(page).size());
    out.pagePoints = document.pageSize(page);

    const QSizeF points = out.pagePoints;
    if (points.isEmpty()) {
        out.error = QStringLiteral("empty page size");
        return out;
    }
    const int height =
        qMax(1, int(std::lround(points.height() * double(kRenderWidth) / points.width())));
    const QImage image = document.render(page, QSize(kRenderWidth, height),
                                         RenderOptions());
    if (image.isNull()) {
        out.error = QStringLiteral("rendered nothing");
        return out;
    }

    QRectF region;
    if (useRegion) {
        region = *useRegion;
    } else {
        // The union of the page's own text lines -- for our fixtures that is the "XYZPDF page
        // N" label and whatever else is written there: small, and exactly where ink must not be
        // hidden. A page with no text at all (a scan) falls back to the middle half.
        const QVector<TextRun> runs = document.textRuns(page);
        for (const TextRun &run : runs) {
            if (run.box.isEmpty())
                continue;
            region = region.isNull() ? run.box : region.united(run.box);
        }
        if (region.isNull()) {
            region = QRectF(0.25 * points.width(), 0.25 * points.height(), 0.5 * points.width(),
                            0.5 * points.height());
        }
    }
    out.regionPoints = region;
    const double scale = double(image.width()) / points.width();
    const QRect regionPixels(int(region.x() * scale), int(region.y() * scale),
                             int(region.width() * scale), int(region.height() * scale));
    out.whole = inkFraction(image, image.rect());
    out.dark = inkFraction(image, image.rect(), 128);
    out.label = inkFraction(image, regionPixels);
    out.paper = inkFraction(image, QRect(0, 0, image.width(), qMax(1, image.height() / 16)));
    return out;
}

// Pixels that differ between the same page of two files. Used for the rotated page, where the
// text-run boxes of the two files cannot be compared fairly, and where "did the marks survive
// the rotation at all" is the only question being asked.
int differingPixels(const QString &pathA, const QString &pathB, int page)
{
    QtPdfBackend a;
    QtPdfBackend b;
    if (a.load(pathA, QString()) != Document::Status::Ready
        || b.load(pathB, QString()) != Document::Status::Ready)
        return -1;
    const QSizeF points = a.pageSize(page);
    const int height =
        qMax(1, int(std::lround(points.height() * double(kRenderWidth) / points.width())));
    const QImage imageA =
        ontoPaper(a.render(page, QSize(kRenderWidth, height), RenderOptions()));
    const QImage imageB =
        ontoPaper(b.render(page, QSize(kRenderWidth, height), RenderOptions()));
    if (imageA.isNull() || imageB.isNull() || imageA.size() != imageB.size())
        return -1;
    int different = 0;
    for (int y = 0; y < imageA.height(); ++y) {
        for (int x = 0; x < imageA.width(); ++x) {
            if (imageA.pixel(x, y) != imageB.pixel(x, y))
                ++different;
        }
    }
    return different;
}

// One route, as a picture, when a second argument asks for it.
//
// Because three of the six questions have answers that an ink fraction cannot give: whether the
// non-embedded CID font drew 水印测试 or drew four boxes, whether the marks on a page that
// declares /Rotate 90 read upright, and whether a real document's own background buries an
// underlay. Each of those is a question with a picture as its answer, and the number is only
// evidence that *something* was drawn.
bool saveRender(const QString &rendersDir, const QString &path, const QString &name, int page)
{
    if (rendersDir.isEmpty())
        return true;
    // The route names carry slashes ("text/base14"), which in a file name is a folder that was
    // never made: the first run of this asked for eight pictures, got five, and said nothing.
    QString safe = name;
    safe.replace(QLatin1Char('/'), QLatin1Char('-'));
    QImage image;
    QString why;
    QtPdfBackend document;
    if (document.load(path, QString()) != Document::Status::Ready) {
        why = document.lastError();
    } else {
        const QSizeF points = document.pageSize(page);
        if (points.isEmpty()) {
            why = QStringLiteral("empty page size");
        } else {
            const int height = qMax(1, int(std::lround(points.height() * double(kRenderWidth)
                                                       / points.width())));
            image = ontoPaper(document.render(page, QSize(kRenderWidth, height),
                                              RenderOptions()));
            if (image.isNull())
                why = QStringLiteral("rendered nothing");
        }
    }
    if (!why.isEmpty()) {
        print("  (no render kept for " + safe + ": " + why + ")");
        return false;
    }
    if (!image.save(rendersDir + QLatin1Char('/') + safe + QStringLiteral(".png"))) {
        print("  (could not write " + rendersDir + "/" + safe + ".png)");
        return false;
    }
    return true;
}

QString describe(const Ink &ink)
{
    if (!ink.ok)
        return QStringLiteral("READER FAILED: %1").arg(ink.error);
    return QStringLiteral("whole %1  dark %2  label %3  paper %4  chars %5")
        .arg(ink.whole, 0, 'f', 4)
        .arg(ink.dark, 0, 'f', 4)
        .arg(ink.label, 0, 'f', 4)
        .arg(ink.paper, 0, 'f', 4)
        .arg(ink.chars);
}

// ---------------------------------------------------------------- the two routes

enum class Route {
    TextAscii,      // Helvetica, alpha via ExtGState, appended after the page's own content
    TextAsciiUnder, // the same operators, prepended: does a page's own background hide it
    TextCjk,        // Type0 / STSong-Light / UniGB-UCS2-H, no embedded font file
    ImageOpaque,    // a Qt-painted bitmap, no soft mask: must cover
    ImageAlpha,     // the same bitmap with an /SMask: must not cover
};

QString routeName(Route route)
{
    switch (route) {
    case Route::TextAscii: return QStringLiteral("text/base14");
    case Route::TextAsciiUnder: return QStringLiteral("text/base14-under");
    case Route::TextCjk: return QStringLiteral("text/type0-nofont");
    case Route::ImageOpaque: return QStringLiteral("image-no-smask");
    case Route::ImageAlpha: return QStringLiteral("image-plus-smask");
    }
    return QStringLiteral("?");
}

struct Applied
{
    bool ok = false;
    QString error;
    qint64 milliseconds = -1;
    qint64 bytes = 0;
};

QString pdfLiteral(const QString &text)
{
    // A PDF literal string: backslash and parentheses escaped. ASCII only -- route 1 is a
    // question about base-14 fonts, and anything else belongs to route 2 or 3.
    QString out;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('(') || ch == QLatin1Char(')'))
            out += QLatin1Char('\\');
        out += ch;
    }
    return out;
}

QString hexLiteral(const QString &text)
{
    // UTF-16BE in angle brackets, which is how /UniGB-UCS2-H spells its characters.
    QString out = QStringLiteral("<");
    for (const QChar ch : text)
        out += QStringLiteral("%1").arg(uint(ch.unicode()), 4, 16, QLatin1Char('0'));
    out += QStringLiteral(">");
    return out;
}

// The watermark as Qt paints it: any script the system has a font for, at any angle, with the
// glyph coverage in the alpha channel. This is what the image route spends its bytes on.
QImage paintWatermark(int widthPx, int heightPx, const QString &text, int fontSizePx)
{
    QImage image(widthPx, heightPx, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font;
    font.setPixelSize(fontSizePx);
    font.setBold(true);
    painter.setFont(font);
    painter.translate(widthPx / 2.0, heightPx / 2.0);
    painter.rotate(-30.0);
    painter.setPen(QColor(120, 120, 120));
    painter.drawText(QRectF(-widthPx / 2.0, -heightPx / 2.0, double(widthPx), double(heightPx)),
                     Qt::AlignCenter, text);
    painter.end();
    return image;
}

struct Planes
{
    QByteArray rgb;
    QByteArray alpha;
};

Planes splitPlanes(const QImage &argb)
{
    Planes planes;
    const int width = argb.width();
    const int height = argb.height();
    planes.rgb.resize(width * height * 3);
    planes.alpha.resize(width * height);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const int offset = y * width + x;
            planes.rgb[3 * offset] = char(qRed(line[x]));
            planes.rgb[3 * offset + 1] = char(qGreen(line[x]));
            planes.rgb[3 * offset + 2] = char(qBlue(line[x]));
            // Where nothing was painted the colour is 0/0/0 and the alpha is 0, so both planes
            // are flat over most of the page and compress to almost nothing. That is the only
            // reason this route is affordable at all.
            planes.alpha[offset] = char(qAlpha(line[x]));
        }
    }
    return planes;
}

// ------------------------------------------------------------- putting it on a page

struct Options
{
    Route route = Route::ImageAlpha;
    QString text;
    QVector<int> pages;    // zero-based; empty means every page
    bool rotatePage = false;
    QString password;      // to read a protected source
    bool encrypt = false;
    QString userPassword;
};

QPDFObjectHandle imageStream(QPDF &qpdf, const QByteArray &raw, int width, int height,
                             const char *colorSpace)
{
    QPDFObjectHandle stream = qpdf.newStream();
    // getDict(), not replaceKey on the stream itself: a stream object answers "operation for
    // dictionary attempted on object of type stream" and *ignores* each request -- measured,
    // six warnings for the six keys below, and the image came out with no /Subtype at all.
    QPDFObjectHandle dict = stream.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
    dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName(colorSpace));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    // The pixels as they were painted, and *no* filter: every comment on `replaceStreamData` is
    // that the buffer it is given is "the data read from the file, after any decryption filters
    // have been applied", i.e. already encoded by whatever name is passed as /Filter. Handing
    // over raw RGB bytes and labelling them /FlateDecode produced exactly what that reads as on
    // the way back in: "stream inflate: inflate: data: unknown compression method". Leaving the
    // filter out instead makes the writer the one that decides, which is the same decision the
    // product's save path will make, and control C5 below prints what it chose.
    stream.replaceStreamData(std::string(raw.constData(), size_t(raw.size())),
                             QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    return qpdf.makeIndirectObject(stream);
}

QPDFObjectHandle buildCjkFont(QPDF &qpdf)
{
    QPDFObjectHandle descendant = QPDFObjectHandle::newDictionary();
    descendant.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    descendant.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType0"));
    descendant.replaceKey("/BaseFont", QPDFObjectHandle::newName("/STSong-Light"));
    QPDFObjectHandle cidSystemInfo = QPDFObjectHandle::newDictionary();
    cidSystemInfo.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    cidSystemInfo.replaceKey("/Ordering", QPDFObjectHandle::newString("GB1"));
    cidSystemInfo.replaceKey("/Supplement", QPDFObjectHandle::newInteger(2));
    descendant.replaceKey("/CIDSystemInfo", cidSystemInfo);
    QPDFObjectHandle matrix = QPDFObjectHandle::newArray();
    matrix.insertItem(0, QPDFObjectHandle::newReal(0.001));
    matrix.insertItem(1, QPDFObjectHandle::newInteger(0));
    matrix.insertItem(2, QPDFObjectHandle::newInteger(0));
    matrix.insertItem(3, QPDFObjectHandle::newReal(0.001));
    matrix.insertItem(4, QPDFObjectHandle::newInteger(0));
    matrix.insertItem(5, QPDFObjectHandle::newInteger(0));
    descendant.replaceKey("/FontMatrix", matrix);
    descendant.replaceKey("/DW", QPDFObjectHandle::newInteger(1000));
    descendant.replaceKey("/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));

    QPDFObjectHandle fonts = QPDFObjectHandle::newArray();
    fonts.insertItem(0, qpdf.makeIndirectObject(descendant));

    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/STSong-Light"));
    font.replaceKey("/Encoding", QPDFObjectHandle::newName("/UniGB-UCS2-H"));
    font.replaceKey("/DescendantFonts", fonts);
    return qpdf.makeIndirectObject(font);
}

// A page's /Resources, ready to write into.
//
// Inheritance-aware and shallow-copied when shared: /Resources may live on a parent node of the
// page tree, and adding a key there would put this page's watermark resources on every page that
// inherits it -- including the pages this call was not asked about.
QPDFObjectHandle resourcesForWrite(QPDFPageObjectHelper &page)
{
    QPDFObjectHandle dict = page.getObjectHandle();
    QPDFObjectHandle resources = page.getAttribute("/Resources", /* copy_if_shared = */ true);
    if (!resources.isDictionary()) {
        dict.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
        resources = dict.getKey("/Resources");
    }
    return resources;
}

// One sub-dictionary of a page's /Resources, ready to write into: /Font, /ExtGState, /XObject.
//
// Two reasons this is a function and not a replaceKey on /Resources:
//
//   * A resource name is looked up inside the *subtype* dictionary -- /Resources /Font /Fwm, not
//     /Resources /Fwm. Putting the entry at the top level is not where a reader looks, and a
//     lenient reader that draws something anyway draws it with its own defaults: measured on the
//     first version of this probe, the text route's Helvetica appeared (so Q1's "it drew" was
//     true) but completely black, because the /ca 0.25 ExtGState was never found either. The
//     picture looked right and the claim about alpha had never been tested.
//   * The sub-dictionary is usually an indirect object shared by every page that inherits the
//     same resources, so writing into it writes into the other pages' too. Copied when indirect,
//     which is the precondition for being shared; `unsafeShallowCopy` is the right one because
//     only top-level keys get replaced, never anything below them.
QPDFObjectHandle resourceDictForWrite(QPDF &qpdf, QPDFObjectHandle &resources,
                                      const char *key)
{
    QPDFObjectHandle sub = resources.getKey(key);
    if (!sub.isDictionary()) {
        resources.replaceKey(key, QPDFObjectHandle::newDictionary());
        return resources.getKey(key);
    }
    if (sub.isIndirect()) {
        sub = qpdf.makeIndirectObject(sub.unsafeShallowCopy());
        resources.replaceKey(key, sub);
    }
    return sub;
}

Applied applyWatermark(const QString &source, const QString &destination, const Options &options)
{
    Applied out;
    QElapsedTimer clock;
    clock.start();
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData(),
                         options.password.isEmpty() ? nullptr
                                                    : options.password.toUtf8().constData());
        QPDFPageDocumentHelper helper(qpdf);
        const std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
        QVector<int> targets = options.pages;
        if (targets.isEmpty()) {
            for (int i = 0; i < int(pages.size()); ++i)
                targets.append(i);
        }

        // The bitmap is built once for the whole document and referenced by every page: the
        // placement operator scales it, so pages of different sizes share one image. Making one
        // per page would be the pessimistic reading of question 6 and is not what a save would
        // do. Both image routes use the same pixels, so the /SMask is the only difference
        // between them.
        QPDFObjectHandle image;
        if (options.route == Route::ImageAlpha || options.route == Route::ImageOpaque) {
            const int imageWidth = 1200;
            const int imageHeight = 1200;
            const Planes planes =
                splitPlanes(paintWatermark(imageWidth, imageHeight, options.text, imageWidth / 8));
            image = imageStream(qpdf, planes.rgb, imageWidth, imageHeight, "/DeviceRGB");
            if (options.route == Route::ImageAlpha) {
                const QPDFObjectHandle smask =
                    imageStream(qpdf, planes.alpha, imageWidth, imageHeight, "/DeviceGray");
                // getDict() again, and this one is the whole difference between the two image
                // routes: replacing a key on the *stream* handle warns "operation for dictionary
                // attempted on object of type stream" and drops it, so the masked variant shipped
                // the opaque one and both measured the same 1.0000 of ink over the page.
                image.getDict().replaceKey("/SMask", smask);
            }
        }

        for (const int index : targets) {
            if (index < 0 || index >= int(pages.size()))
                continue;
            QPDFPageObjectHelper page = pages.at(size_t(index));
            const QPDFObjectHandle cropBox = page.getCropBox();
            double llx = 0.0, lly = 0.0, urx = 612.0, ury = 792.0;
            if (cropBox.isArray()) {
                llx = cropBox.getArrayItem(0).getNumericValue();
                lly = cropBox.getArrayItem(1).getNumericValue();
                urx = cropBox.getArrayItem(2).getNumericValue();
                ury = cropBox.getArrayItem(3).getNumericValue();
            }
            const double width = urx - llx;
            const double height = ury - lly;
            QPDFObjectHandle resources = resourcesForWrite(page);
            int suffix = 1;
            std::string content;

            switch (options.route) {
            case Route::TextAscii:
            case Route::TextAsciiUnder: {
                QPDFObjectHandle fonts = resourceDictForWrite(qpdf, resources, "/Font");
                const std::string fontName = fonts.getUniqueResourceName("Fwm", suffix);
                QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
                font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
                font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
                font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
                fonts.replaceKey("/" + fontName, qpdf.makeIndirectObject(font));

                QPDFObjectHandle states =
                    resourceDictForWrite(qpdf, resources, "/ExtGState");
                const std::string gsName = states.getUniqueResourceName("GSwm", suffix);
                QPDFObjectHandle gs = QPDFObjectHandle::newDictionary();
                gs.replaceKey("/Type", QPDFObjectHandle::newName("/ExtGState"));
                gs.replaceKey("/CA", QPDFObjectHandle::newReal(0.25));
                gs.replaceKey("/ca", QPDFObjectHandle::newReal(0.25));
                states.replaceKey("/" + gsName, qpdf.makeIndirectObject(gs));

                char ops[512];
                std::snprintf(ops, sizeof(ops),
                              "q /%s gs 1 0 0 1 %.2f %.2f cm 0.8660 0.5000 -0.5000 0.8660 0 0 cm"
                              " BT /%s %.2f Tf (%s) Tj ET Q\n",
                              gsName.c_str(), llx + 0.06 * width, lly + 0.40 * height,
                              fontName.c_str(), width * 0.055,
                              pdfLiteral(options.text).toUtf8().constData());
                content = ops;
                break;
            }
            case Route::TextCjk: {
                QPDFObjectHandle fonts = resourceDictForWrite(qpdf, resources, "/Font");
                const std::string fontName = fonts.getUniqueResourceName("Fwm", suffix);
                fonts.replaceKey("/" + fontName, buildCjkFont(qpdf));
                const QByteArray hex = hexLiteral(options.text).toUtf8();
                char ops[512];
                std::snprintf(ops, sizeof(ops),
                              "q 1 0 0 1 %.2f %.2f cm 0.8660 0.5000 -0.5000 0.8660 0 0 cm"
                              " BT /%s %.2f Tf %s Tj ET Q\n",
                              llx + 0.06 * width, lly + 0.40 * height, fontName.c_str(),
                              width * 0.055, hex.constData());
                content = ops;
                break;
            }
            case Route::ImageOpaque:
            case Route::ImageAlpha: {
                QPDFObjectHandle xobjects = resourceDictForWrite(qpdf, resources, "/XObject");
                const std::string imageName = xobjects.getUniqueResourceName("Imwm", suffix);
                xobjects.replaceKey("/" + imageName, image);
                char ops[256];
                std::snprintf(ops, sizeof(ops), "q %.4f 0 0 %.4f %.4f %.4f cm /%s Do Q\n", width,
                              height, llx, lly, imageName.c_str());
                content = ops;
                break;
            }
            }

            page.addPageContents(qpdf.newStream(content),
                                 /* first = */ options.route == Route::TextAsciiUnder);
        }

        if (options.rotatePage) {
            std::vector<QPDFPageObjectHelper> all = helper.getAllPages();
            if (!all.empty())
                all.at(0).rotatePage(90, /* relative = */ true);
        }

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        if (options.encrypt) {
            writer.setR6EncryptionParameters(options.userPassword.toUtf8().constData(),
                                            "probe-owner", /* allow_accessibility = */ true,
                                            /* allow_extract = */ true,
                                            /* allow_assemble = */ false,
                                            /* allow_annotate_and_form = */ true,
                                            /* allow_form_filling = */ true,
                                            /* allow_modify_other = */ true, qpdf_r3p_full,
                                            /* encrypt_metadata_aes = */ true);
        }
        writer.write();
        out.ok = true;
        out.bytes = QFileInfo(destination).size();
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what()).replace('\n', ' ');
        QFile::remove(destination);
    }
    out.milliseconds = clock.elapsed();
    return out;
}

// Rewrite a file through QPDF with nothing changed, so that "the ink did not move by itself" is
// a measured line rather than an assumption about the writer.
bool plainRewrite(const QString &source, const QString &destination)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &) {
        return false;
    }
    return true;
}

// Re-open what the writer produced, with QPDF itself, and report what it says. Not "the library
// agrees with itself": the file on disk is parsed from scratch, and every later reader goes
// through that parse.
int reopenWarnings(const QString &path, const QString &password, int &pagesSeen)
{
    pagesSeen = -1;
    try {
        QPDF qpdf;
        qpdf.setSuppressWarnings(true);
        qpdf.processFile(path.toUtf8().constData(),
                         password.isEmpty() ? nullptr : password.toUtf8().constData());
        QPDFPageDocumentHelper helper(qpdf);
        pagesSeen = int(helper.getAllPages().size());
        return int(qpdf.numWarnings());
    } catch (const std::exception &e) {
        print(QStringLiteral("  (qpdf refused the file: %1)")
                  .arg(QString::fromUtf8(e.what()).replace('\n', ' ')));
        return -1;
    }
}

} // namespace

int main(int argc, char **argv)
{
    // Offscreen unless told otherwise, but overridable, and this is the reason: the image route's
    // glyphs come from whatever font Qt resolves, and the offscreen platform's font database is
    // not the one the shipped program uses. Measured under offscreen, Qt painted a row of empty
    // .notdef boxes for both the Latin and the Chinese watermark -- "something", and not a word of
    // an answer. QT_QPA_PLATFORM=windows asks the question the product will actually face.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    // QGuiApplication rather than QCoreApplication: the image route paints with QPainter, which
    // needs a platform plugin with a font database. Same offscreen platform the widget tests
    // already use.
    QGuiApplication app(argc, argv);
    print(QStringLiteral("== Qt platform %1, default font family \"%2\" ==")
              .arg(QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM")),
                   QFontInfo(QFont()).family()));

    const QString fixture = QStringLiteral("tests/fixtures/many-pages.pdf");
    const QString big = QStringLiteral("tests/fixtures/huge.pdf");
    if (!QFileInfo::exists(fixture)) {
        print("FAIL: run this from the project root (needs tests/fixtures/many-pages.pdf)");
        return 2;
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        print("FAIL: no scratch folder");
        return 2;
    }
    const QString dir = scratch.path();
    const QString ascii = QStringLiteral("WATERMARK-PROBE");
    const QString chinese = QString::fromUtf8("水印测试");
    // An optional second argument names a folder to keep page-1 pictures in. The files this probe
    // writes live in a scratch folder that is gone when it exits, so without this the questions
    // that only a picture can answer (see saveRender) could not be answered at all.
    QString rendersDir;
    if (argc > 2) {
        rendersDir = QString::fromLocal8Bit(argv[2]);
        QDir().mkpath(rendersDir);
        print("== renders kept in " + rendersDir + " ==");
    }

    print("== the strings the routes are asked to spell ==");
    print("  ascii   : " + hexCodePoints(ascii));
    print("  chinese : " + hexCodePoints(chinese)
          + "   (base-14 has no glyph for any of these; question 2 asks whether a non-embedded"
            " CID font is enough to draw them)");

    // ---------------------------------------------------------------- the instrument
    print("");
    print("== controls on the instrument itself ==");
    const Ink baseline = measureInk(fixture, QString(), 0);
    if (!baseline.ok) {
        print("FAIL: cannot measure the fixture: " + baseline.error);
        return 2;
    }
    print("  fixture page 1: " + describe(baseline));
    // Both halves, and this is the pair that caught the first version of this instrument
    // reading a transparent page as black: ink alone is satisfied by "everything is ink", which
    // is also what a broken sampler reports.
    control("the sampler sees the page's own ink and its blank paper as different things",
            baseline.label > 0.002 && baseline.whole > 0.0002 && baseline.paper < 0.005);
    control("the sampled region is the text's own region and not the whole page",
            baseline.ok && baseline.pagePoints.width() > 0.0
                && baseline.regionPoints.width() < 0.7 * baseline.pagePoints.width());
    {
        const QString notPdf = dir + "/not-a-pdf.pdf";
        QFile file(notPdf);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("this is not a pdf at all\n");
            file.close();
        }
        control("\"the reader opens it\" fails on a file that is not a PDF",
                !measureInk(notPdf, QString(), 0).ok);
    }
    {
        // Question 3's instrument, before question 3: the alpha channel is only evidence if the
        // pixels that were painted are the pixels that were stored.
        const Planes planes = splitPlanes(paintWatermark(64, 64, chinese, 24));
        bool paintedAnything = false;
        for (const char byte : planes.alpha) {
            if (byte != 0) {
                paintedAnything = true;
                break;
            }
        }
        // The font has to have the glyph, not merely paint *something*: an empty .notdef box has
        // alpha too, and the first version of this control passed while the picture showed a row of
        // boxes for both scripts. A real font gives a W and an i different widths; a font database
        // that resolves nothing gives both the box's own width. Which is the answer under this
        // platform, and the reason the run below is repeated with QT_QPA_PLATFORM=windows.
        QFont probed;
        probed.setPixelSize(24);
        const QFontMetricsF metrics(probed);
        const double wideWidth = metrics.horizontalAdvance(QStringLiteral("WWWWWWWWWW"));
        const double thinWidth = metrics.horizontalAdvance(QStringLiteral("iiiiiiiiii"));
        print(QStringLiteral("  Qt resolves \"%1\": 10 Ws are %2 px, 10 is %3 px (equal widths mean"
                             " every character is the same box)")
                  .arg(QFontInfo(probed).family())
                  .arg(wideWidth, 0, 'f', 1)
                  .arg(thinWidth, 0, 'f', 1));
        control("Qt paints real glyphs rather than empty boxes, so route 3's lines mean something",
                paintedAnything && wideWidth > 1.3 * thinWidth);
        // The round trip goes through a file on disk, not through the in-memory object: a
        // stream whose data was handed over unfiltered but labelled /FlateDecode cannot be read
        // back until the writer has applied the filter (measured -- asking the handle for its
        // stream data before writing killed the process with 0xc0000409, which is qpdf's own
        // answer, not ours). So the claim being tested is the one that matters anyway: that the
        // bytes which reach the disk decode back to the pixels that were painted.
        const QString roundTrip = dir + "/stream-round-trip.pdf";
        bool wrote = false;
        try {
            QPDF qpdf;
            qpdf.processFile(fixture.toUtf8().constData());
            QPDFPageDocumentHelper helper(qpdf);
            QPDFPageObjectHelper page = helper.getAllPages().at(0);
            QPDFObjectHandle resources = resourcesForWrite(page);
            QPDFObjectHandle xobjects =
                resourceDictForWrite(qpdf, resources, "/XObject");
            int suffix = 1;
            const std::string name = xobjects.getUniqueResourceName("Probe", suffix);
            xobjects.replaceKey("/" + name, imageStream(qpdf, planes.rgb, 64, 64, "/DeviceRGB"));
            QPDFWriter writer(qpdf, roundTrip.toUtf8().constData());
            writer.write();
            wrote = true;
        } catch (const std::exception &e) {
            print("  (could not build the round-trip file: "
                  + QString::fromUtf8(e.what()).replace('\n', ' ') + ")");
        }
        std::shared_ptr<Buffer> decoded;
        QString diskFilter = QStringLiteral("(not read)");
        if (wrote) {
            try {
                QPDF back;
                back.processFile(roundTrip.toUtf8().constData());
                QPDFPageDocumentHelper helper(back);
                QPDFPageObjectHelper page = helper.getAllPages().at(0);
                const QPDFObjectHandle dict = page.getObjectHandle().getKey("/Resources");
                QPDFObjectHandle image = dict.getKey("/XObject").getKey("/Probe");
                if (!image.isStream())
                    image = dict.getKey("/XObject").getKey("/Probe1");
                // What the writer actually put on the disk, printed rather than assumed: the
                // bytes were handed over unfiltered, so the filter is its choice, and the cost
                // question below is only honest about the number that follows.
                const QPDFObjectHandle filter = image.getDict().getKey("/Filter");
                diskFilter = filter.isName() ? QString::fromStdString(filter.getName())
                                             : QStringLiteral("(none)");
                decoded = image.getStreamData(qpdf_dl_generalized);
            } catch (const std::exception &e) {
                print("  (could not read the stream back: "
                      + QString::fromUtf8(e.what()).replace('\n', ' ') + ")");
            }
        }
        control(QStringLiteral("the stream that reaches the disk (filter %1) decodes back to the"
                               " bytes that were painted, so a blank watermark cannot be read as"
                               " \"the renderer ignores /SMask\"")
                    .arg(diskFilter),
                decoded && decoded->getSize() == size_t(planes.rgb.size())
                    && decoded->view() == std::string_view(planes.rgb.constData(),
                                                           size_t(planes.rgb.size())));
    }
    {
        // C4: the file that gets measured is the file that got written, and only the pages that
        // were asked about changed.
        Options onePage;
        onePage.route = Route::ImageAlpha;
        onePage.text = ascii;
        onePage.pages = {2};
        const Applied marked = applyWatermark(fixture, dir + "/only-page-3.pdf", onePage);
        const bool copied = plainRewrite(fixture, dir + "/plain-copy.pdf");
        const Ink copyPage1 = measureInk(dir + "/plain-copy.pdf", QString(), 0,
                                         &baseline.regionPoints);
        const Ink markedPage1 = measureInk(dir + "/only-page-3.pdf", QString(), 0,
                                           &baseline.regionPoints);
        const Ink markedPage3 = measureInk(dir + "/only-page-3.pdf", QString(), 2);
        control("a rewrite with no watermark leaves page 1's ink where it was, so an ink delta"
                " elsewhere is the watermark and not the writer",
                copied && copyPage1.ok && qAbs(copyPage1.whole - baseline.whole) < 0.0005);
        control("page 1 is unchanged when only page 3 was asked for",
                marked.ok && markedPage1.ok && qAbs(markedPage1.whole - baseline.whole) < 0.0005);
        control("and page 3 of that same file did change",
                markedPage3.ok && markedPage3.whole > baseline.whole + 0.0005);
    }

    // ------------------------------------------------------------------- the routes
    print("");
    print("== each route, applied to every page of the 40-page fixture, measured on page 1 ==");
    print("   label = ink inside the region the *untouched* page's own text occupies; a route");
    print("   that buries it would bury the document.");
    saveRender(rendersDir, fixture, "reference-untouched", 0);
    const qint64 sourceBytes = QFileInfo(fixture).size();
    const QVector<Route> routes = {Route::TextAscii, Route::TextAsciiUnder, Route::TextCjk,
                                   Route::ImageOpaque, Route::ImageAlpha};
    Ink textInk;
    Ink textUnderInk;
    Ink cjkInk;
    Ink opaqueInk;
    Ink alphaInk;
    for (const Route route : routes) {
        Options options;
        options.route = route;
        options.text = route == Route::TextCjk ? chinese : ascii;
        const QString path = dir + "/route-" + QString::number(int(route)) + ".pdf";
        const Applied applied = applyWatermark(fixture, path, options);
        if (!applied.ok) {
            print(routeName(route).leftJustified(20) + " the write itself refused: "
                  + applied.error);
            continue;
        }
        int pagesSeen = -1;
        const int warnings = reopenWarnings(path, QString(), pagesSeen);
        const Ink ink = measureInk(path, QString(), 0, &baseline.regionPoints);
        print(QStringLiteral("%1  %2 ms, %3 KB -> %4 KB (%5%6 KB), qpdf pages %7 warnings %8")
                  .arg(routeName(route).leftJustified(20))
                  .arg(applied.milliseconds)
                  .arg(sourceBytes / 1024)
                  .arg(applied.bytes / 1024)
                  .arg(applied.bytes > sourceBytes ? QStringLiteral("+") : QStringLiteral(""))
                  .arg((applied.bytes - sourceBytes) / 1024)
                  .arg(pagesSeen)
                  .arg(warnings));
        print(QStringLiteral("    %1").arg(describe(ink)));
        saveRender(rendersDir, path, QStringLiteral("route-") + routeName(route), 0);
        switch (route) {
        case Route::TextAscii: textInk = ink; break;
        case Route::TextAsciiUnder: textUnderInk = ink; break;
        case Route::TextCjk: cjkInk = ink; break;
        case Route::ImageOpaque: opaqueInk = ink; break;
        case Route::ImageAlpha: alphaInk = ink; break;
        }
    }
    {
        // Q2's counterpart for the other route. The text route's Chinese is a question about a
        // font file; the image route's is about whether Qt's paint made it through at all, and
        // both are only answered by a picture.
        Options options;
        options.route = Route::ImageAlpha;
        options.text = chinese;
        const QString path = dir + "/image-chinese.pdf";
        const Applied applied = applyWatermark(fixture, path, options);
        const Ink ink = applied.ok ? measureInk(path, QString(), 0, &baseline.regionPoints) : Ink();
        print(QStringLiteral("image-plus-smask + chinese  %1 ms  %2   (%3)")
                  .arg(applied.milliseconds)
                  .arg(describe(ink))
                  .arg(hexCodePoints(options.text)));
        saveRender(rendersDir, path, "image-chinese", 0);
    }
    // A control that could only be made after the routes ran, and the one that caught the broken
    // resource dictionary: the two thresholds have to be able to disagree, or "the marks are
    // light" is not a reachable answer. Deltas rather than levels, because the page's own black
    // text is already half antialiased edge -- what has to be measurable is that the *added* ink
    // is light. Ignoring /ca would make the watermark black and fail this instead of shipping a
    // watermark that looked fine.
    const double addedInk = textInk.whole - baseline.whole;
    const double addedDark = textInk.dark - baseline.dark;
    print(QStringLiteral("  the watermark's own ink: %1 of the page, of which %2 is dark"
                         "  (0.25 opacity is ~0.06 dark)")
              .arg(addedInk, 0, 'f', 4)
              .arg(addedDark, 0, 'f', 4));
    control("the two ink thresholds disagree about the watermark, so \"light marks\" is something"
            " this instrument can report",
            addedInk > 0.002 && addedDark < 0.2 * addedInk);

    print("");
    print("== the questions, one line each ==");
    print(QStringLiteral("Q1 text route drew anything: %1 (whole %2 vs baseline %3); it changed"
                         " what the page reads as by %4 characters")
              .arg(textInk.whole > baseline.whole + 0.0005 ? "yes" : "NO")
              .arg(textInk.whole, 0, 'f', 4)
              .arg(baseline.whole, 0, 'f', 4)
              .arg(textInk.chars - baseline.chars));
    print(QStringLiteral("Q1b underlay vs overlay: under %1, over %2 (baseline %3) -- equal means"
                         " this fixture paints no background of its own, so the question stays"
                         " open for real documents")
              .arg(textUnderInk.whole, 0, 'f', 4)
              .arg(textInk.whole, 0, 'f', 4)
              .arg(baseline.whole, 0, 'f', 4));
    print(QStringLiteral("Q1c and the alpha on it: of the text route's whole-page ink %1, the"
                         " dark-page share is %2 (the untouched page is %3 of %4) -- the marks are"
                         " light, so /ca reached the renderer")
              .arg(textInk.whole, 0, 'f', 4)
              .arg(textInk.dark, 0, 'f', 4)
              .arg(baseline.dark, 0, 'f', 4)
              .arg(baseline.whole, 0, 'f', 4));
    print(QStringLiteral("Q2 chinese with no embedded font: ink appeared (%1 vs baseline %2) and"
                         " %3 characters became extractable, but neither of those can tell the"
                         " right glyphs from boxes -- the render is the answer here")
              .arg(cjkInk.whole, 0, 'f', 4)
              .arg(baseline.whole, 0, 'f', 4)
              .arg(cjkInk.chars - baseline.chars));
    print(QStringLiteral("Q3 alpha honoured? opaque label %1 -> %2, masked label %1 -> %3"
                         " (baseline %1). The unmasked one covers the page with the bitmap's own"
                         " black background, so it must bury that region; the masked one must"
                         " leave it exactly where it was while the whole page still gains ink."
                         " That pair is the answer, and \"the watermark appeared\" is not.")
              .arg(baseline.label, 0, 'f', 4)
              .arg(opaqueInk.label, 0, 'f', 4)
              .arg(alphaInk.label, 0, 'f', 4));
    print(QStringLiteral("Q3b and the marks themselves: opaque whole %1, masked whole %2 (both"
                         " page 1 of their own files)")
              .arg(opaqueInk.whole, 0, 'f', 4)
              .arg(alphaInk.whole, 0, 'f', 4));

    // ------------------------------------------------------------------- rotation
    print("");
    print("== Q4: page 1 declares /Rotate 90 ==");
    {
        const QString without = dir + "/rotate-plain.pdf";
        bool rotatedCopy = false;
        try {
            QPDF qpdf;
            qpdf.processFile(fixture.toUtf8().constData());
            QPDFPageDocumentHelper helper(qpdf);
            helper.getAllPages().at(0).rotatePage(90, true);
            QPDFWriter writer(qpdf, without.toUtf8().constData());
            writer.write();
            rotatedCopy = true;
        } catch (const std::exception &e) {
            print("  (could not build the rotated reference: "
                  + QString::fromUtf8(e.what()).replace('\n', ' ') + ")");
        }
        Options with;
        with.route = Route::ImageAlpha;
        with.text = ascii;
        with.rotatePage = true;
        const QString path = dir + "/rotate-watermarked.pdf";
        const Applied applied = applyWatermark(fixture, path, with);
        const Ink a = measureInk(without, QString(), 0);
        const Ink b = measureInk(path, QString(), 0);
        print(QStringLiteral("  rotated, unwatermarked: %1").arg(describe(a)));
        print(QStringLiteral("  rotated, watermarked  : %2  (%3 ms)")
                  .arg(describe(b))
                  .arg(applied.ok ? QString::number(applied.milliseconds) : applied.error));
        print(QStringLiteral("  pixels differing between the two renders: %1"
                             "  (-1 means the renders were not comparable)")
                  .arg(rotatedCopy ? differingPixels(without, path, 0) : -1));
        saveRender(rendersDir, path, "rotated-watermarked", 0);
        print("  what this does NOT say: whether the marks read upright to a human. That needs"
              " eyes on a screenshot; this number only says the marks survive the rotation.");
    }

    // --------------------------------------------------------- watermark + password
    print("");
    print("== Q5: one save that both watersmarks and encrypts ==");
    {
        Options options;
        options.route = Route::ImageAlpha;
        options.text = ascii;
        options.encrypt = true;
        options.userPassword = QStringLiteral("probe-pw");
        const QString path = dir + "/watermarked-encrypted.pdf";
        const Applied applied = applyWatermark(fixture, path, options);
        if (!applied.ok) {
            print("  refused: " + applied.error);
        } else {
            int pagesSeen = -1;
            const int warnings = reopenWarnings(path, QStringLiteral("probe-pw"), pagesSeen);
            const Ink noPassword = measureInk(path, QString(), 0, &baseline.regionPoints);
            const Ink withPassword = measureInk(path, QStringLiteral("probe-pw"), 0,
                                                &baseline.regionPoints);
            print(QStringLiteral("  wrote in %1 ms; qpdf with the password sees %2 pages, %3"
                                 " warnings")
                      .arg(applied.milliseconds)
                      .arg(pagesSeen)
                      .arg(warnings));
            print(QStringLiteral("  reader without the password: %1")
                      .arg(noPassword.ok ? QStringLiteral("OPENED IT, WHICH IS THE BUG")
                                         : QStringLiteral("refused, as it should be")));
            print(QStringLiteral("  reader with the password   : %2 (label %3 vs baseline %4)")
                      .arg(describe(withPassword))
                      .arg(withPassword.label, 0, 'f', 4)
                      .arg(baseline.label, 0, 'f', 4));
        }
    }

    // ------------------------------------------------------------------------ cost
    print("");
    print("== Q6: cost, which of the two is the expensive one and by how much ==");
    if (QFileInfo::exists(big)) {
        const qint64 bigBytes = QFileInfo(big).size();
        for (const Route route : {Route::TextAscii, Route::ImageAlpha}) {
            Options options;
            options.route = route;
            options.text = ascii;
            const QString destination = dir + "/cost-" + QString::number(int(route)) + ".pdf";
            const Applied applied = applyWatermark(big, destination, options);
            if (!applied.ok) {
                print(QStringLiteral("  600 pages / %1 KB  %2  refused: %3")
                          .arg(bigBytes / 1024)
                          .arg(routeName(route), applied.error));
                continue;
            }
            const Ink ink = measureInk(destination, QString(), 0);
            print(QStringLiteral("  600 pages / %1 KB  %2  %3 ms, -> %4 KB (%5 KB/page)  %6")
                      .arg(bigBytes / 1024)
                      .arg(routeName(route).leftJustified(18))
                      .arg(applied.milliseconds)
                      .arg(applied.bytes / 1024)
                      .arg(double(applied.bytes - bigBytes) / 1024.0 / 600.0, 0, 'f', 2)
                      .arg(describe(ink)));
        }
    } else {
        print("  (huge.pdf not there, skipped)");
    }
    if (argc > 1 && !QString::fromLocal8Bit(argv[1]).isEmpty()) {
        const QString real = QString::fromLocal8Bit(argv[1]);
        if (QFileInfo::exists(real)) {
            const qint64 realBytes = QFileInfo(real).size();
            print("");
            print("== the same routes over a real document, where the page paints its own"
                  " background ==");
            print("  " + real);
            // Its own baseline, and its own region handed to every route below. Measuring the
            // routes against the *fixture's* baseline would compare a datasheet's page 1 with a
            // generated page's, and a watermark that turns text into selectable text moves the
            // region the file's own text runs describe -- which is the thing being measured.
            const Ink realBaseline = measureInk(real, QString(), 0);
            if (!realBaseline.ok) {
                print("  (the reader cannot open it, so nothing below has an answer: "
                      + realBaseline.error + ")");
            } else {
                print("  untouched page 1: " + describe(realBaseline));
                saveRender(rendersDir, real, QStringLiteral("real-untouched"), 0);
                // The underlay variant is here and not only over the fixture: a real page paints
                // a background of its own, which is the one thing that can bury it, and the
                // fixture has none (Q1b measured equal there).
                for (const Route route : {Route::TextAscii, Route::TextAsciiUnder,
                                          Route::ImageAlpha}) {
                    Options options;
                    options.route = route;
                    options.text = ascii;
                    const QString destination =
                        dir + "/real-" + QString::number(int(route)) + ".pdf";
                    const Applied applied = applyWatermark(real, destination, options);
                    print(QStringLiteral("  %1 KB  %2  %3 ms, -> %4 KB  %5")
                              .arg(realBytes / 1024)
                              .arg(routeName(route).leftJustified(18))
                              .arg(applied.ok ? QString::number(applied.milliseconds)
                                              : QStringLiteral("refused"))
                              .arg(applied.bytes / 1024)
                              .arg(applied.ok
                                       ? describe(measureInk(destination, QString(), 0,
                                                             &realBaseline.regionPoints))
                                       : applied.error));
                    if (applied.ok) {
                        saveRender(rendersDir, destination,
                                   QStringLiteral("real-") + routeName(route), 0);
                    }
                }
            }
        } else {
            print("  (the path given does not exist: " + real + ")");
        }
    }

    print("");
    if (brokenControls > 0) {
        print(QStringLiteral("BROKEN CONTROLS: %1 -- the lines above measure nothing.").arg(
            brokenControls));
        return 1;
    }
    print("controls held; read the lines above rather than this one.");
    return 0;
}
