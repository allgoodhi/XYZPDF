// PDFium asked directly, with the renderer this program actually uses printed beside it.
//
// ---- why this exists ----
//
// sec.54 looked at a page with a form field on it and found nothing drawn. sec.82 found the reason:
// the renderer refuses the annotation whose /Subtype is /Widget, whatever its appearance looks like
// -- the same stream hung on /Square paints 6000 pixels, hung on /Widget paints none. sec.94 showed
// that one 0 reaches all six places a page gets drawn (page view, sidebar strip, shell thumbnail,
// preview pane, image export, print), so a user printing a filled form gets blank boxes on paper.
//
// Qt PDF *is* PDFium -- the same library, statically linked, with the form-fill environment never
// initialised. PDFium draws widgets by itself: FPDFDOC_InitFormFillEnvironment() plus FPDF_FFLDraw()
// exist for exactly that, and Qt PDF does not call them (sec.82 counted the render flags it exposes,
// and there is no second switch to press). So the long-term answer, already in docs/PLAN.md sec.1, is
// to link PDFium directly.
//
// This probe is the measurement that decision needs before anybody writes F7. It changes no product
// code: it opens the same files through two libraries and counts pixels.
//
// ---- the questions, in the order the output prints them ----
//
//   1. Identity. Which bytes are being asked -- pdfium.dll and Qt6Pdf.dll, each by path, size and
//      sha256, plus the release's own build flags. sec.97's rule: a size is not an identity, and
//      "which one did I run against" is only answerable by hashing.
//   2. Does a widget reach the page image through PDFium, and which of the five call shapes buys it?
//      One block per render geometry, each route measured against its own baseline, all seven samples.
//   3. What the file carries, read by the library that is going to draw it: annotation count, subtype,
//      how many page objects that annotation's appearance decodes into, /V, FPDF_GetFormType. This is
//      sec.82's companion column for table 2: a row that reads 0 has to say whether there was anything
//      there to draw, because "the reader skips it" and "the page is empty" print the same digit.
//   4. Fidelity of the swap: the two libraries drawing the same ordinary page at the same size -- how
//      many pixels differ, and by how much. A backend change that moves antialiasing is a change users
//      see, and it is cheaper to measure now than to explain after somebody notices.
//   5. Cost, in the geometry docs/perf.md was written in (96/72 zoom, 40 pages spread over the
//      document), on the two committed fixtures and on a real book when one is given. Plus what the
//      form layer itself costs: the same page with and without FPDF_FFLDraw.
//   6. The thread model, measured rather than quoted: four threads, four documents, no lock. The
//      header says none of these APIs are thread-safe (fpdfview.h:11-12); what that costs when each
//      thread owns its own document is the reading, next to the working set of four documents open at
//      once -- because "one document per render thread" (docs/PLAN.md sec.3.3's other option)
//      multiplies memory, and F7 has to know by how much.
//   7. Filling, not only drawing: can a value be written with FPDFAnnot_SetStringValue so the next
//      frame shows it, and does FPDFPage_Flatten bake the widget into the page's own content? That is
//      the line between "step one: draw appearances" and "step two: let somebody type into it".
//
// ---- controls ----
//
// A probe that only prints can be misread, so eight checks gate the exit code and each of them can go
// red. They are printed after the tables they read from.
//
//   C1 every cell rendered an image. A refused render otherwise looks exactly like a page that drew
//      nothing, which is the shape of every false negative this project has been caught by (sec.81).
//   C2 the annotation layer is reachable through both libraries: markup-square.pdf (a /Square with a
//      fill) must differ from its own baseline on qt-pdf and on every PDFium route that asks for
//      annotations. If it does not, that route cannot see the layer and its widget 0 means nothing.
//   C3 the flag is real: the same /Square must NOT differ on pdfium-page, where no annotation flag is
//      passed. Otherwise C2 would be a route that draws everything regardless.
//   C4 the counting window lands on real pixels: painted-rect.pdf, whose mark is in the page's own
//      content stream (sec.89), must differ from plain on every route at every geometry.
//   C5 the claim itself: field-appearance.pdf (/Widget carrying /AP /N) must paint nothing on
//      pdfium-page+annot and must paint something on pdfium-page+annot+ffl. The first half reproduces
//      sec.94's 0 through this new channel -- so if the geometry is wrong both halves break at once --
//      and the second half is the whole reason to consider the swap. If PDFium stops drawing, this
//      probe says so instead of printing a table quietly full of new zeros.
//   C6 the two page-size readers agree. The box is computed in points; if Qt and PDFium disagree about
//      the page, "the same rectangle" in the two columns is not the same rectangle (sec.89's lesson:
//      one rectangle has a size and an origin, and both have to be checked).
//   C7 the form module owns something. plain.pdf must report no form and field-appearance.pdf must
//      report one, and the appearance must decode into objects on the file that carries one and into
//      nothing on field-flat.pdf, which does not. Without this, the handle behind every FFL row could
//      be a handle to nothing and table 2 would be measuring a no-op.
//   C8 both libraries must put a *little* ink on plain.pdf's page: it is one line of text on white
//      paper, so the count has to be above zero and below a fifth of the page. Both bounds are
//      load-bearing. The upper one caught this probe's own bug -- PDFium hands back an unfilled
//      buffer, so the paper itself counted as ink and every box in table 2 was meaningless; the
//      lower one is what says a route that reports 0 was actually asked to draw something.
//
// ---- how to run it ----
//
// It is a probe, so tools/build_all_tests.py never runs it, and the target only exists when the build
// was told where PDFium lives (see the block in CMakeLists.txt: the option is OFF by default, and a
// default build never sees the library). From the project root:
//
//   cmake -B build-pdfium-probe -S . -G Ninja -DCMAKE_BUILD_TYPE=Release ^
//         -DCMAKE_PREFIX_PATH=E:/qt/6.11.2/msvc2022_64 ^
//         -DXYZPDF_BUILD_PDFIUM_PROBE=ON ^
//         -DXYZPDF_PDFIUM_DIR=H:/test/XYZPDF-wt/third-party/pdfium-8111
//   cmake --build build-pdfium-probe --target xyzpdf_pdfiumprobe
//
//   set XYZPDF_FORM_SAMPLE_DIR=H:\tmp\pdfium-probe\samples
//   build-local-release\tests\xyzpdf_documenteditor_smoke.exe
//   build-pdfium-probe\tests\xyzpdf_pdfiumprobe.exe H:\tmp\outline\real-book.pdf
//
// The middle line writes the form samples (sec.54's generator); the probe reads them, it does not
// make them. Output is ASCII on purpose: this console is GBK, and one non-ASCII byte turns a reading
// into mojibake, which is how a measurement gets mis-transcribed.

#include "../src/document/document.h"
#include "../src/document/qtpdfbackend.h"

#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_flatten.h>
#include <fpdf_formfill.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
// Two guards, both paid for. `small`: the path that reaches psapi.h also reaches rpcndr.h, which
// defines it as char (AGENTS.md, sec.79). `min` and `max`: NOMINMAX here is too late to help -- the
// Qt includes above already reached windows.h, so `std::min({...})` compiles as a macro call with
// three arguments (C4002 then C2589, and the cascade looks like a broken namespace). Everything in
// this file therefore compares by hand.
#undef small
#endif

namespace {

int brokenControls = 0;

void print(const QString &text)
{
    std::cout << text.toStdString() << '\n';
    std::cout.flush();
}

void control(int number, const QString &what, bool held)
{
    print(QStringLiteral("  C%1 %2  %3")
              .arg(number)
              .arg(held ? QStringLiteral("ok    ") : QStringLiteral("BROKEN"))
              .arg(what));
    if (!held)
        ++brokenControls;
}

QString padded(const QString &text, int width)
{
    return text.size() < width ? text + QString(width - text.size(), QLatin1Char(' ')) : text;
}

// Points to device pixels at the zoom a screen shows: 96 dpi over PDF's 72 -- the convention
// docs/perf.md's numbers were taken at, so the two tables can be read against each other.
constexpr double kScreenZoom = 96.0 / 72.0;

// How many pages the timing rows render, spread over the document rather than taken from its front:
// the first pages of a file are often the least representative thing in it.
constexpr int kRendered = 40;

// The six geometries sec.94 tabulated, copied from the call sites that table names. A backend swap
// has to answer for all six, because sec.94's 0 was a 0 in all six.
struct RenderPath
{
    const char *name;
    int width;
    int height;
    bool lcd;
};

const QVector<RenderPath> kPaths = {
    {"page-view-fit-width", 1240, 1753, true},
    {"thumbnail-strip", 150, 212, false},
    {"shell-thumbnail", 260, 368, false},
    {"preview-pane", 400, 566, false},
    {"image-export-150dpi", 1240, 1754, false},
    {"print-a4-300dpi", 2480, 3508, false},
};

// A sample and the rectangle its claim lives in, in the page's own points with the PDF origin at the
// bottom left. Every pixel reading is counted inside that box and nowhere else: at 150 px wide a
// 200x30-point mark is a handful of pixels, and a page-wide difference would let unrelated
// resampling noise answer the question (sec.94's reason for the same choice).
struct Sample
{
    const char *file;
    const char *what;
    double left;
    double bottom;
    double right;
    double top;
};

const QVector<Sample> kSamples = {
    {"plain.pdf", "no annotation at all -- the baseline each route is measured against",
     60, 600, 260, 630},
    {"field-flat.pdf", "/Widget with /V and no /AP -- the shape sec.54 found and drew nothing",
     60, 600, 260, 630},
    {"field-appearance.pdf", "/Widget carrying its own /AP /N -- sec.57's shape",
     60, 600, 260, 630},
    {"field-directive.pdf", "the same field plus /AcroForm /NeedAppearances -- sec.58's shape",
     60, 600, 260, 630},
    {"field-xfa.pdf", "a field with an XFA packet beside it -- sec.55's shape",
     60, 600, 260, 630},
    {"markup-square.pdf", "/Square with a fill, no /AP -- C2 and C3, the layer must be reachable",
     70, 700, 260, 760},
    {"painted-rect.pdf", "the same rectangle painted by the page's own content stream -- C4",
     70, 770, 300, 792},
};

// A way of drawing one page. Qt PDF has the one shape (it is the second column of every block);
// PDFium has four worth separating, because the argument is precisely about which of these calls get
// made. pdfium-page+ffl with no annotation flag is in the table because PDFium documents that
// FFLDraw draws widgets regardless of the flag -- that claim belongs in a measurement, not in a
// sentence copied out of a header.
struct Route
{
    const char *name;
    bool useQt;
    int pageFlags;
    bool drawForm;
    int formFlags;
};

const QVector<Route> kRoutes = {
    {"qt-pdf", true, 0, false, 0},
    {"pdfium-page", false, 0, false, 0},
    {"pdfium-page+annot", false, FPDF_ANNOT, false, 0},
    {"pdfium-page+annot+ffl", false, FPDF_ANNOT, true, FPDF_ANNOT},
    {"pdfium-page+ffl", false, 0, true, 0},
};

qint64 workingSetKb()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters;
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters)))
        return -1;
    return qint64(counters.WorkingSetSize) / 1024;
#else
    return -1;
#endif
}

QString sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("(unreadable)");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QStringLiteral("(unreadable)");
    return QString::fromLatin1(hash.result().toHex()).left(12);
}

void reportLibrary(const QString &caption, const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        print(QStringLiteral("  %1 (not found)").arg(padded(caption, 14)));
        return;
    }
    print(QStringLiteral("  %1 %2 bytes  sha256=%3  %4")
              .arg(padded(caption, 14))
              .arg(QFileInfo(path).size())
              .arg(sha256Of(path))
              .arg(path));
}

double milliseconds(const std::function<void()> &work)
{
    QElapsedTimer clock;
    clock.start();
    work();
    // nsecsElapsed, not elapsed: elapsed() is already whole milliseconds, and a page of a synthetic
    // document renders in less than one. In the wrong unit the whole table reads as zeros, which look
    // like a fast program rather than like an instrument that is not measuring.
    return double(clock.nsecsElapsed()) / 1.0e6;
}

// --------------------------------------------------------------------- PDFium, held the way an app holds it

// PDFium's form environment is a struct of function pointers the library calls back into. Nothing
// here answers any of them: the question is the drawing, and an unanswered callback is PDFium's
// problem to tolerate -- if one of them turns out to be required for a widget to paint, control C5
// goes red, and that is itself the answer F7 needs. So it is left visible rather than papered over.
struct FormEnvironment
{
    FPDF_FORMFILLINFO info;

    FormEnvironment()
    {
        std::memset(&info, 0, sizeof(info));
        info.version = 2;
    }
};

class PdfDocument {
public:
    PdfDocument() = default;
    PdfDocument(const PdfDocument &) = delete;
    PdfDocument &operator=(const PdfDocument &) = delete;
    ~PdfDocument() { close(); }

    bool open(const QString &path, bool withForm)
    {
        close();
        handle = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
        if (!handle)
            return false;
        if (withForm)
            form = FPDFDOC_InitFormFillEnvironment(handle, &environment.info);
        return true;
    }

    void close()
    {
        if (form)
            FPDFDOC_ExitFormFillEnvironment(form);
        form = nullptr;
        if (handle)
            FPDF_CloseDocument(handle);
        handle = nullptr;
    }

    QSizeF sizeOfPage(int page) const
    {
        FS_SIZEF size = {0.0, 0.0};
        if (!handle || !FPDF_GetPageSizeByIndexF(handle, page, &size))
            return {};
        return QSizeF(size.width, size.height);
    }

    int formType() const { return handle ? FPDF_GetFormType(handle) : FORMTYPE_NONE; }

    FPDF_DOCUMENT handle = nullptr;
    FPDF_FORMHANDLE form = nullptr;

private:
    FormEnvironment environment;
};

// One page drawn into a QImage. A null return means PDFium refused; it must never be counted as a row
// of zeros, which is what control C1 is for.
QImage renderPdfium(PdfDocument &document, int page, QSize pixels, int pageFlags, bool drawForm,
                    int formFlags)
{
    if (!document.handle)
        return {};
    FPDF_PAGE loaded = FPDF_LoadPage(document.handle, page);
    if (!loaded)
        return {};
    if (document.form)
        FORM_OnAfterLoadPage(loaded, document.form);

    FPDF_BITMAP bitmap = FPDFBitmap_Create(pixels.width(), pixels.height(), 0);
    if (!bitmap) {
        FPDF_ClosePage(loaded);
        return {};
    }
    // The buffer comes back **uninitialised**: nothing fills it with the colour of paper, so an
    // unpainted page reads as a black one. That was this probe's own bug, caught by control C8 rather
    // than by reading the tables (a page that is entirely "ink" looks like a page full of content).
    // pdfium_test does the same thing for the same reason; a PDF page has no background colour of its
    // own, so the embedder supplies it.
    FPDFBitmap_FillRect(bitmap, 0, 0, pixels.width(), pixels.height(), 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, loaded, 0, 0, pixels.width(), pixels.height(), 0, pageFlags);
    if (drawForm && document.form)
        FPDF_FFLDraw(document.form, bitmap, loaded, 0, 0, pixels.width(), pixels.height(), 0,
                     formFlags);

    // Format_RGB32 because FPDFBitmap_Create(...,0) is BGRx: the fourth byte is documented as
    // unused, and an RGB32 QImage ignores exactly that byte. Defensive, not load-bearing -- the
    // control run that read this buffer as ARGB32 instead changed no reading at all, because PDFium
    // writes 0xFF there once the bitmap has been filled. The byte that decides what the counting
    // means is the fill above.
    QImage image(pixels, QImage::Format_RGB32);
    image.fill(qRgb(255, 255, 255));
    const unsigned char *buffer = static_cast<const unsigned char *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    if (buffer) {
        for (int y = 0; y < pixels.height(); ++y) {
            std::memcpy(image.scanLine(y), buffer + std::size_t(y) * stride,
                        std::size_t(pixels.width()) * 4);
        }
    }
    FPDFBitmap_Destroy(bitmap);

    if (document.form)
        FORM_OnBeforeClosePage(loaded, document.form);
    FPDF_ClosePage(loaded);
    return image;
}

// --------------------------------------------------------------------- the two counts

// Both renderers hand back an image with an alpha channel, and on this machine the paper of a
// mostly-blank page arrives as alpha 0 with the colour bytes left at 0 -- which reads as black if
// the alpha is dropped, and that is exactly what the first pass of this probe did: every box came
// out "entirely inked" with worst=255, including the baseline. So both libraries' images are
// composited over white before anything is counted, and the pixel this note describes is printed
// once per route at the top of table 2 rather than left as a claim.
QImage overWhite(const QImage &source)
{
    if (source.isNull())
        return {};
    if (!source.hasAlphaChannel())
        return source.convertToFormat(QImage::Format_RGB32);
    const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
    QImage flat(argb.size(), QImage::Format_RGB32);
    flat.fill(qRgb(255, 255, 255));
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(flat.scanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            const int alpha = qAlpha(src[x]);
            if (alpha == 255) {
                dst[x] = qRgb(qRed(src[x]), qGreen(src[x]), qBlue(src[x]));
                continue;
            }
            // alpha 0 leaves the colour bytes as they were, so blend: white underneath.
            const int weight = 255 - alpha;
            const int red = (qRed(src[x]) * alpha + 255 * weight) / 255;
            const int green = (qGreen(src[x]) * alpha + 255 * weight) / 255;
            const int blue = (qBlue(src[x]) * alpha + 255 * weight) / 255;
            dst[x] = qRgb(red, green, blue);
        }
    }
    return flat;
}

// Pixels inside `box` that differ between two pictures of the same page. -1 when the pictures cannot
// be compared: a broken instrument must not read as a zero.
int differingWithin(const QImage &oneRaw, const QImage &otherRaw, const QRect &box)
{
    if (oneRaw.isNull() || otherRaw.isNull() || oneRaw.size() != otherRaw.size())
        return -1;
    const QImage one = overWhite(oneRaw);
    const QImage other = overWhite(otherRaw);
    const QImage a = one.convertToFormat(QImage::Format_RGB32);
    const QImage b = other.convertToFormat(QImage::Format_RGB32);
    int count = 0;
    for (int y = box.top(); y <= box.bottom() && y < a.height(); ++y) {
        const QRgb *rowA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *rowB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = box.left(); x <= box.right() && x < a.width(); ++x) {
            if (qRed(rowA[x]) != qRed(rowB[x]) || qGreen(rowA[x]) != qGreen(rowB[x])
                || qBlue(rowA[x]) != qBlue(rowB[x]))
                ++count;
        }
    }
    return count;
}

// How many pixels in the box are not white, and how far the furthest one is. The threshold is written
// down because the mark being counted is 0.70/0.70/0.90 -- 76/255 from white in blue -- so an
// inequality against 255 counts nothing and a loose one counts paper grain.
void inkWithin(const QImage &image, const QRect &box, int *nonWhite, int *worst)
{
    *nonWhite = 0;
    *worst = 0;
    if (image.isNull())
        return;
    const QImage rgb = overWhite(image);
    for (int y = box.top(); y <= box.bottom() && y < rgb.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = box.left(); x <= box.right() && x < rgb.width(); ++x) {
            int closest = qRed(row[x]);
            if (qGreen(row[x]) < closest)
                closest = qGreen(row[x]);
            if (qBlue(row[x]) < closest)
                closest = qBlue(row[x]);
            const int delta = 255 - closest;
            if (delta > *worst)
                *worst = delta;
            if (delta > 8)
                ++*nonWhite;
        }
    }
}

// The box in device pixels, from the page size the renderer being asked reported. Control C6 is what
// makes "the same rectangle in both columns" more than a hope.
QRect pixelBox(const Sample &sample, QSize pixels, double pageWidth, double pageHeight)
{
    if (pageWidth <= 0.0 || pageHeight <= 0.0)
        return {};
    const double sx = double(pixels.width()) / pageWidth;
    const double sy = double(pixels.height()) / pageHeight;
    return QRect(int(std::floor(sample.left * sx)),
                 int(std::floor((pageHeight - sample.top) * sy)),
                 int(std::ceil((sample.right - sample.left) * sx)),
                 int(std::ceil((sample.top - sample.bottom) * sy)));
}

const char *subtypeName(int subtype)
{
    switch (subtype) {
    case FPDF_ANNOT_TEXT:
        return "Text";
    case FPDF_ANNOT_LINK:
        return "Link";
    case FPDF_ANNOT_SQUARE:
        return "Square";
    case FPDF_ANNOT_HIGHLIGHT:
        return "Highlight";
    case FPDF_ANNOT_WIDGET:
        return "Widget";
    case FPDF_ANNOT_XFAWIDGET:
        return "XfaWidget";
    case FPDF_ANNOT_POPUP:
        return "Popup";
    default:
        return "other/unknown";
    }
}

const char *formTypeName(int type)
{
    switch (type) {
    case FORMTYPE_NONE:
        return "none";
    case FORMTYPE_ACRO_FORM:
        return "acroform";
    case FORMTYPE_XFA_FULL:
        return "xfa-full";
    case FORMTYPE_XFA_FOREGROUND:
        return "xfa-foreground";
    default:
        return "other";
    }
}

// --------------------------------------------------------------------- one document per file per shape

// Documents are opened once each and kept to the end. Opening one per table cell would make the
// working-set reading in section 6 mean nothing and would spend more time loading than drawing;
// keeping two shapes per file (with and without a form environment) is what the routes need, and it
// is also the honest cost of the two shapes -- a real backend would pick one per document.
class Holders {
public:
    QtPdfBackend *qt(const QString &path)
    {
        const QString key = QFileInfo(path).fileName();
        if (QtPdfBackend *found = m_qt.value(key, nullptr))
            return found;
        auto *backend = new QtPdfBackend;
        if (backend->load(path, QString()) != Document::Status::Ready) {
            print(QStringLiteral("  NOTE %1 does not open through Qt PDF: %2")
                      .arg(key, backend->lastError()));
        }
        m_qt.insert(key, backend);
        return backend;
    }

    PdfDocument *pdfium(const QString &path, bool withForm)
    {
        const QString key = QFileInfo(path).fileName() + QLatin1Char('|')
            + (withForm ? QLatin1Char('f') : QLatin1Char('p'));
        if (key.contains(QLatin1Char('\0')))
            return nullptr;
        if (PdfDocument *found = m_pdfium.value(key, nullptr))
            return found;
        auto *document = new PdfDocument;
        if (!document->open(path, withForm)) {
            print(QStringLiteral("  NOTE %1 does not open through PDFium (lastError=%2)")
                      .arg(QFileInfo(path).fileName())
                      .arg(FPDF_GetLastError()));
            delete document;
            m_pdfium.insert(key, nullptr);
            return nullptr;
        }
        m_pdfium.insert(key, document);
        return document;
    }

    QImage render(const Route &route, const QString &path, QSize pixels, bool lcd)
    {
        if (route.useQt) {
            RenderOptions options;
            options.optimizedForLcd = lcd;
            return qt(path)->render(0, pixels, options);
        }
        PdfDocument *document = pdfium(path, route.drawForm);
        return document ? renderPdfium(*document, 0, pixels, route.pageFlags, route.drawForm,
                                       route.formFlags)
                        : QImage();
    }

    QSizeF pageSize(const Route &route, const QString &path)
    {
        if (route.useQt)
            return qt(path)->pageSize(0);
        PdfDocument *document = pdfium(path, route.drawForm);
        return document ? document->sizeOfPage(0) : QSizeF();
    }

    ~Holders()
    {
        for (QtPdfBackend *backend : std::as_const(m_qt))
            delete backend;
        for (PdfDocument *document : std::as_const(m_pdfium))
            delete document;
    }

private:
    QHash<QString, QtPdfBackend *> m_qt;
    QHash<QString, PdfDocument *> m_pdfium;
};

// --------------------------------------------------------------------- 1. identity

void reportIdentity()
{
    print(QStringLiteral("\n== 1. which bytes are being asked =="));
#ifdef XYZPDF_PDFIUM_VERSION
    print(QStringLiteral("  release: PDFium %1 from bblanchon/pdfium-binaries").arg(
        QString::fromLatin1(XYZPDF_PDFIUM_VERSION)));
    print(QStringLiteral("  args.gn of that release: pdf_enable_v8=false, pdf_enable_xfa=false, "
                         "is_component_build=false, target_cpu=x64"));
#endif
#ifdef Q_OS_WIN
    const auto loadedFrom = [](const char *name) {
        QString path;
        if (HMODULE module = GetModuleHandleA(name)) {
            char buffer[MAX_PATH] = {0};
            if (GetModuleFileNameA(module, buffer, MAX_PATH))
                path = QString::fromLocal8Bit(buffer);
        }
        return path;
    };
    // By hash, not by name: the same file name can come from three different places on this machine
    // (sec.96/97 measured exactly that with dxcompiler.dll), and the answer to "which PDFium did the
    // numbers come out of" has to survive that.
    reportLibrary(QStringLiteral("pdfium.dll"), loadedFrom("pdfium.dll"));
    reportLibrary(QStringLiteral("Qt6Pdf.dll"), loadedFrom("Qt6Pdf.dll"));
    print(QStringLiteral("  (Qt PDF carries its own copy of PDFium statically: the two lines above "
                         "are two libraries in the process, which is the cost of keeping both)"));
#else
    print(QStringLiteral("  (module paths are only read on Windows)"));
#endif
}

// --------------------------------------------------------------------- 3. what the files carry

struct Carried
{
    QString file;
    int annotations = 0;
    QString subtype = QStringLiteral("-");
    int appearanceObjects = -1;
    QString value;
    int formType = FORMTYPE_NONE;
    bool handleOpened = false;
    bool formHandleCreated = false;
};

QVector<Carried> gCarried;

void reportCarried(const QString &sampleDir, Holders &holders)
{
    print(QStringLiteral("\n== 3. what each sample carries, read by PDFium itself =="));
    print(QStringLiteral("  %1 %2 %3 %4 %5 %6 %7")
              .arg(padded(QStringLiteral("sample"), 22), padded(QStringLiteral("annots"), 8),
                   padded(QStringLiteral("subtype"), 12), padded(QStringLiteral("objs"), 6),
                   padded(QStringLiteral("form"), 14), padded(QStringLiteral("page pt"), 14),
                   QStringLiteral("/V")));

    for (const Sample &sample : kSamples) {
        const QString path = QDir(sampleDir).filePath(QLatin1String(sample.file));
        Carried carried;
        carried.file = QString::fromLatin1(sample.file);
        PdfDocument *document = holders.pdfium(path, true);
        carried.handleOpened = document && document->handle;
        if (!carried.handleOpened) {
            gCarried.append(carried);
            continue;
        }
        carried.formHandleCreated = document->form != nullptr;
        carried.formType = document->formType();
        const QSizeF size = document->sizeOfPage(0);

        if (FPDF_PAGE page = FPDF_LoadPage(document->handle, 0)) {
            carried.annotations = FPDFPage_GetAnnotCount(page);
            for (int index = 0; index < carried.annotations; ++index) {
                FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, index);
                if (!annot)
                    continue;
                const QString name = QString::fromLatin1(
                    subtypeName(FPDFAnnot_GetSubtype(annot)));
                if (name != QStringLiteral("Popup"))
                    carried.subtype = name;
                const int objects = FPDFAnnot_GetObjectCount(annot);
                if (objects > 0 && carried.appearanceObjects < 0)
                    carried.appearanceObjects = objects;
                unsigned short utf16[256] = {0};
                const unsigned long bytes
                    = FPDFAnnot_GetStringValue(annot, "/V", utf16, sizeof(utf16));
                if (bytes > 1 && carried.value.isEmpty()) {
                    carried.value = QString::fromUtf16(
                        reinterpret_cast<const char16_t *>(utf16), int(bytes / 2) - 1);
                }
                FPDFPage_CloseAnnot(annot);
            }
            if (carried.appearanceObjects < 0)
                carried.appearanceObjects = 0;
            FPDF_ClosePage(page);
        }

        print(QStringLiteral("  %1 %2 %3 %4 %5 %6 %7")
                  .arg(padded(carried.file, 22), padded(QString::number(carried.annotations), 8),
                       padded(carried.subtype, 12),
                       padded(QString::number(carried.appearanceObjects), 6),
                       padded(QString::fromLatin1(formTypeName(carried.formType)), 14),
                       padded(QStringLiteral("%1x%2").arg(size.width(), 0, 'f', 1)
                                  .arg(size.height(), 0, 'f', 1),
                              14),
                       carried.value.left(32)));
        gCarried.append(carried);
    }
    print(QStringLiteral("  (objs = page objects inside the annotation's normal appearance; 0 with "
                         "annots>0 is a mark the file carries but that has nothing drawn in it)"));

    // The XFA boundary, asked of this build directly: the release is compiled with
    // pdf_enable_xfa=false, so the answer F3's sentence and F7's scope both need is not "does PDFium
    // do XFA" in the abstract but what THIS binary reports and what it draws. sec.55 measured the
    // packet at the file level; this is the reader side of the same question.
    print(QStringLiteral("  FPDF_LoadXFA on the same handles (release has pdf_enable_xfa=false):"));
    for (const Carried &carried : gCarried) {
        if (carried.formType == FORMTYPE_NONE)
            continue;
        const QString path = QDir(sampleDir).filePath(carried.file);
        PdfDocument document;
        if (!document.open(path, true)) {
            print(QStringLiteral("    %1 (did not open)").arg(padded(carried.file, 22)));
            continue;
        }
        const FPDF_BOOL loaded = FPDF_LoadXFA(document.handle);
        print(QStringLiteral("    %1 form before=%2 LoadXFA=%3 form after=%4")
                  .arg(padded(carried.file, 22))
                  .arg(QString::fromLatin1(formTypeName(carried.formType)))
                  .arg(loaded ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(QString::fromLatin1(formTypeName(document.formType()))));
    }
}

// --------------------------------------------------------------------- 2. the pixel table

struct Cell
{
    QString sample;
    QString route;
    QString geometry;
    int differ = -1;
    int ink = 0;
    int baselineInk = 0;
    int worst = 0;
    int pageInk = 0;
    int baselinePageInk = 0;
    QRect box;
    bool rendered = false;
    bool baselineRendered = false;
    QSizeF pageUsed;
};

QVector<Cell> gCells;

void reportPixels(const QString &sampleDir, Holders &holders)
{
    print(QStringLiteral("\n== 2. does a widget reach the page image, per route and per geometry =="));
    print(QStringLiteral("  columns: differ = pixels inside the box that differ from plain.pdf drawn "
                         "by the same route;"));
    print(QStringLiteral("  ink / base = non-white pixels in the box, this file and the route's own "
                         "baseline;"));
    print(QStringLiteral("  all = non-white pixels over the whole page, baseline and file, which is "
                         "what says a mark was drawn somewhere rather than nowhere;"));
    print(QStringLiteral("  both count over a copy composited onto white, because both renderers "
                         "leave the paper of a blank region at alpha 0."));
    const QStringList header
        = {padded(QStringLiteral("sample"), 22), padded(QStringLiteral("differ"), 8),
           padded(QStringLiteral("ink"), 7), padded(QStringLiteral("base"), 7),
           padded(QStringLiteral("worst"), 9), padded(QStringLiteral("all=base/page"), 14),
           QStringLiteral("box")};
    print(QStringLiteral("      ") + header.join(QStringLiteral(" ")));

    const QString plainPath = QDir(sampleDir).filePath(QStringLiteral("plain.pdf"));

    for (const RenderPath &geometry : kPaths) {
        const QSize pixels(geometry.width, geometry.height);
        print(QStringLiteral("\n  -- %1 at %2x%3 (lcd=%4)")
                  .arg(QString::fromLatin1(geometry.name))
                  .arg(pixels.width())
                  .arg(pixels.height())
                  .arg(geometry.lcd ? 1 : 0));

        for (const Route &route : kRoutes) {
            const QImage baseline = holders.render(route, plainPath, pixels, geometry.lcd);
            const QSizeF basePage = holders.pageSize(route, plainPath);
            // The claim about alpha, said as a reading rather than as a comment: the pixel in the
            // middle of the field box on plain.pdf, as the library returned it and as the counting
            // sees it. Printing it once per route is what keeps "the paper is blank" from becoming
            // something the next person has to take on faith (and it is what caught the first pass of
            // this probe counting every pixel of every box as ink).
            if (QString::fromLatin1(geometry.name)
                == QString::fromLatin1(kPaths.first().name)) {
                const QRect probe = pixelBox(kSamples[2], pixels, basePage.width(),
                                             basePage.height());
                const QPoint centre = probe.center();
                const QImage raw = baseline.convertToFormat(QImage::Format_ARGB32);
                const QRgb rawPixel = raw.isNull() ? qRgba(0, 0, 0, 0) : raw.pixel(centre);
                const QRgb flatPixel = overWhite(baseline).isNull()
                    ? qRgb(0, 0, 0)
                    : overWhite(baseline).pixelColor(centre).rgba();
                print(QStringLiteral("    route %1  page=%2x%3 pt  format=%4  box centre raw="
                                     "%5,%6,%7,a%8  over white=%9,%10,%11")
                          .arg(padded(QString::fromLatin1(route.name), 24))
                          .arg(basePage.width(), 0, 'f', 1)
                          .arg(basePage.height(), 0, 'f', 1)
                          .arg(baseline.isNull()
                                   ? QStringLiteral("(none)")
                                   : QStringLiteral("fmt%1 alpha=%2")
                                         .arg(int(baseline.format()))
                                         .arg(baseline.hasAlphaChannel()
                                                  ? QStringLiteral("yes")
                                                  : QStringLiteral("no")))
                          .arg(qRed(rawPixel))
                          .arg(qGreen(rawPixel))
                          .arg(qBlue(rawPixel))
                          .arg(qAlpha(rawPixel))
                          .arg(qRed(flatPixel))
                          .arg(qGreen(flatPixel))
                          .arg(qBlue(flatPixel)));
            } else {
                print(QStringLiteral("    route %1  page=%2x%3 pt")
                          .arg(padded(QString::fromLatin1(route.name), 24))
                          .arg(basePage.width(), 0, 'f', 1)
                          .arg(basePage.height(), 0, 'f', 1));
            }

            for (const Sample &sample : kSamples) {
                const QString path = QDir(sampleDir).filePath(QLatin1String(sample.file));
                const QImage drawn = holders.render(route, path, pixels, geometry.lcd);
                const QRect box = pixelBox(sample, pixels, basePage.width(), basePage.height());
                Cell cell;
                cell.sample = QString::fromLatin1(sample.file);
                cell.route = QString::fromLatin1(route.name);
                cell.geometry = QString::fromLatin1(geometry.name);
                cell.box = box;
                cell.pageUsed = basePage;
                cell.baselineRendered = !baseline.isNull();
                cell.rendered = !drawn.isNull();
                cell.differ = differingWithin(baseline, drawn, box);
                int worst = 0;
                inkWithin(drawn, box, &cell.ink, &worst);
                cell.worst = worst;
                // Where the ink is, page-wide: a mark drawn at the wrong coordinates would read as
                // "the box is empty" forever, and the count for the whole page is what tells the two
                // apart (sec.54's first reading was that shape -- a difference that turned out to be
                // the tab strip rather than the page).
                const QRect whole(QPoint(0, 0), pixels);
                int unusedWorst = 0;
                inkWithin(drawn, whole, &cell.pageInk, &unusedWorst);
                inkWithin(baseline, whole, &cell.baselinePageInk, &unusedWorst);
                int baselineWorst = 0;
                inkWithin(baseline, box, &cell.baselineInk, &baselineWorst);
                gCells.append(cell);

                const QStringList columns
                    = {padded(cell.sample, 22), padded(QString::number(cell.differ), 8),
                       padded(QString::number(cell.ink), 7),
                       padded(QString::number(cell.baselineInk), 7),
                       padded(QStringLiteral("worst%1").arg(cell.worst), 9),
                       padded(QStringLiteral("all=%1/%2").arg(cell.baselinePageInk)
                                                   .arg(cell.pageInk), 14),
                       QStringLiteral("box=%1,%2 %3x%4")
                           .arg(cell.box.x())
                           .arg(cell.box.y())
                           .arg(cell.box.width())
                           .arg(cell.box.height())};
                print(QStringLiteral("      ") + columns.join(QStringLiteral(" ")));
            }
        }
    }
}

// --------------------------------------------------------------------- controls over tables 2 and 3

int cellDiffer(const QString &sample, const QString &route, const QString &geometry)
{
    for (const Cell &cell : gCells) {
        if (cell.sample == sample && cell.route == route && cell.geometry == geometry)
            return cell.differ;
    }
    return -2;
}

const Cell *cellAt(const QString &sample, const QString &route, const QString &geometry)
{
    for (const Cell &cell : gCells) {
        if (cell.sample == sample && cell.route == route && cell.geometry == geometry)
            return &cell;
    }
    return nullptr;
}

const Carried *carriedOf(const QString &file)
{
    for (const Carried &carried : gCarried) {
        if (carried.file == file)
            return &carried;
    }
    return nullptr;
}

void runPixelControls(const QString &sampleDir, Holders &holders)
{
    print(QStringLiteral("\n== controls over the table above =="));

    // C1: nothing in the table may be a refusal dressed up as a zero.
    int nulls = 0;
    for (const Cell &cell : gCells) {
        if (!cell.rendered || !cell.baselineRendered)
            ++nulls;
    }
    control(1, QStringLiteral("every cell rendered an image (%1 of %2 cells did not)")
                  .arg(nulls)
                  .arg(gCells.size()),
            nulls == 0);

    // C2 and C3 ride on the same file with one variable changed: the annotation flag.
    QString c2broken;
    bool c2 = true;
    for (const RenderPath &geometry : kPaths) {
        const QString name = QString::fromLatin1(geometry.name);
        for (const QString &route : {QStringLiteral("qt-pdf"),
                                     QStringLiteral("pdfium-page+annot"),
                                     QStringLiteral("pdfium-page+annot+ffl")}) {
            const int value = cellDiffer(QStringLiteral("markup-square.pdf"), route, name);
            if (value <= 0) {
                c2 = false;
                c2broken += QStringLiteral(" [%1/%2=%3]").arg(route, name).arg(value);
            }
        }
    }
    control(2, QStringLiteral("the annotation layer is reachable in both libraries: markup-square "
                              "differs from its baseline wherever annotations were asked for%1")
                  .arg(c2broken),
            c2);

    bool c3 = true;
    QString c3broken;
    for (const RenderPath &geometry : kPaths) {
        const QString name = QString::fromLatin1(geometry.name);
        const int value = cellDiffer(QStringLiteral("markup-square.pdf"),
                                     QStringLiteral("pdfium-page"), name);
        if (value != 0) {
            c3 = false;
            c3broken += QStringLiteral(" [%1=%2]").arg(name).arg(value);
        }
    }
    control(3, QStringLiteral("the annotation flag is the thing making the difference: the same "
                              "square still draws with flags=0%1 -- so C2 was a route that shows "
                              "everything anyway")
                  .arg(c3broken),
            c3);

    // C4: the box coordinates land on real ink somewhere -- the page's own content stream.
    bool c4 = true;
    QString c4broken;
    for (const RenderPath &geometry : kPaths) {
        const QString name = QString::fromLatin1(geometry.name);
        for (const Route &route : kRoutes) {
            const int value = cellDiffer(QStringLiteral("painted-rect.pdf"),
                                         QString::fromLatin1(route.name), name);
            if (value <= 0) {
                c4 = false;
                c4broken += QStringLiteral(" [%1/%2=%3]").arg(QString::fromLatin1(route.name), name)
                                .arg(value);
            }
        }
    }
    control(4, QStringLiteral("painted-rect.pdf differs from plain on every route at every geometry "
                              "-- the window is on the page%1")
                  .arg(c4broken),
            c4);

    // C5: the claim, and its negative, in one file with one call between them.
    bool widgetHidden = true;
    bool widgetDrawn = true;
    QString c5broken;
    for (const RenderPath &geometry : kPaths) {
        const QString name = QString::fromLatin1(geometry.name);
        const int withoutFfl
            = cellDiffer(QStringLiteral("field-appearance.pdf"),
                         QStringLiteral("pdfium-page+annot"), name);
        const int withFfl
            = cellDiffer(QStringLiteral("field-appearance.pdf"),
                         QStringLiteral("pdfium-page+annot+ffl"), name);
        if (withoutFfl != 0) {
            widgetHidden = false;
            c5broken += QStringLiteral(" [annot=%1/%2]").arg(name).arg(withoutFfl);
        }
        if (withFfl <= 0) {
            widgetDrawn = false;
            c5broken += QStringLiteral(" [annot+ffl=%1/%2]").arg(name).arg(withFfl);
        }
    }
    control(5, QStringLiteral("field-appearance.pdf paints nothing without FFLDraw (%1) and "
                              "something with it (%2)%3")
                  .arg(widgetHidden ? QStringLiteral("ok") : QStringLiteral("BROKEN"))
                  .arg(widgetDrawn ? QStringLiteral("ok") : QStringLiteral("BROKEN"))
                  .arg(c5broken),
            widgetHidden && widgetDrawn);

    // C6: the two page-size readers agree on the sample page, or the box is two different rectangles.
    bool sizesAgree = true;
    QString c6broken;
    for (const Sample &sample : kSamples) {
        const QString path = QDir(sampleDir).filePath(QLatin1String(sample.file));
        const QSizeF qtSize = holders.pageSize(kRoutes.first(), path);
        const QSizeF pdfiumSize = holders.pageSize(kRoutes[1], path);
        if (qtSize.isEmpty() || pdfiumSize.isEmpty()
            || std::abs(qtSize.width() - pdfiumSize.width()) > 0.5
            || std::abs(qtSize.height() - pdfiumSize.height()) > 0.5) {
            sizesAgree = false;
            c6broken += QStringLiteral(" [%1 qt=%2x%3 pdfium=%4x%5]")
                            .arg(QString::fromLatin1(sample.file))
                            .arg(qtSize.width())
                            .arg(qtSize.height())
                            .arg(pdfiumSize.width())
                            .arg(pdfiumSize.height());
        }
    }
    control(6, QStringLiteral("Qt and PDFium report the same page size for every sample%1")
                  .arg(c6broken),
            sizesAgree);

    // C7: the form module owns something, and the appearance the pixels are attributed to is really
    // in the file (sec.82's rule: a 0 needs its companion row).
    const Carried *plain = carriedOf(QStringLiteral("plain.pdf"));
    const Carried *appearance = carriedOf(QStringLiteral("field-appearance.pdf"));
    const Carried *flat = carriedOf(QStringLiteral("field-flat.pdf"));
    const bool formHeld = plain && appearance && flat && plain->handleOpened && flat->handleOpened
        && appearance->handleOpened && plain->formType == FORMTYPE_NONE
        && appearance->formType != FORMTYPE_NONE && appearance->formHandleCreated
        && appearance->appearanceObjects > 0 && flat->appearanceObjects == 0;
    control(7, QStringLiteral("plain.pdf reports no form, field-appearance.pdf reports one with a "
                              "form handle and an appearance that decodes into %1 objects, and "
                              "field-flat.pdf's appearance decodes into %2 -- so the FFL rows are "
                              "measuring a form module that owns something, and the widget's 0 has "
                              "its companion row")
                  .arg(appearance ? appearance->appearanceObjects : -99)
                  .arg(flat ? flat->appearanceObjects : -99)
                  .arg(formHeld ? QStringLiteral(" [ok]") : QStringLiteral(" [BROKEN]")),
            formHeld);

    // C8: the page's own ink has to be visible to both libraries, on a file that is nothing but a
    // line of text. This is the control that keeps a whole class of instrument failure -- the one
    // this probe caught twice in its own first passes, where reading PDFium's BGRx buffer as ARGB32
    // composited every pixel it had painted back to white and the tables went quietly full of zeros.
    const QString firstGeometry = QString::fromLatin1(kPaths.first().name);
    const Cell *qtPlain = cellAt(QStringLiteral("plain.pdf"), QStringLiteral("qt-pdf"),
                                 firstGeometry);
    const Cell *pdfiumPlain = cellAt(QStringLiteral("plain.pdf"), QStringLiteral("pdfium-page"),
                                     firstGeometry);
    const int pagePixels = kPaths.first().width * kPaths.first().height;
    // Both bounds matter, and they are the two ways this instrument has already failed: a page whose
    // ink count is 0 means the counting threw the drawing away (reading PDFium's BGRx as ARGB32), and
    // a page that is almost all ink means the paper itself was never painted (the unfilled bitmap
    // reads black). What the file really is: one line of text on white paper.
    const bool inkSane = qtPlain && pdfiumPlain && qtPlain->pageInk > 0
        && pdfiumPlain->pageInk > 0
        && qtPlain->pageInk < pagePixels / 5 && pdfiumPlain->pageInk < pagePixels / 5;
    control(8, QStringLiteral("both libraries put a little ink on plain.pdf's page (qt=%1 pdfium=%2 "
                              "of %3 pixels, expecting more than 0 and less than a fifth) -- the "
                              "counting can see the page at all, so a 0 in a box is about the box")
                  .arg(qtPlain ? qtPlain->pageInk : -1)
                  .arg(pdfiumPlain ? pdfiumPlain->pageInk : -1)
                  .arg(pagePixels),
            inkSane);
}

// --------------------------------------------------------------------- 4. fidelity of the swap

void reportFidelity(const QString &fixtureDir, const QString &realBook)
{
    print(QStringLiteral("\n== 4. do the two libraries draw the same ordinary page =="));
    print(QStringLiteral("    whole page at %1 zoom (%2 px wide on a 595 pt page); annotations asked "
                         "for on both sides")
              .arg(kScreenZoom, 0, 'f', 4)
              .arg(int(595 * kScreenZoom)));

    QStringList files = {QStringLiteral("text.pdf"), QStringLiteral("table.pdf"),
                         QStringLiteral("colored.pdf"), QStringLiteral("graphic.pdf"),
                         QStringLiteral("mixed.pdf"), QStringLiteral("picture.pdf"),
                         QStringLiteral("many-pages.pdf"), QStringLiteral("scan.pdf")};
    if (!realBook.isEmpty())
        files << QStringLiteral("real-book");

    for (const QString &entry : files) {
        const QString path = entry == QStringLiteral("real-book")
                               ? realBook
                               : QDir(fixtureDir).filePath(entry);
        if (!QFileInfo::exists(path))
            continue;
        const QString name = QFileInfo(path).fileName();

        QtPdfBackend backend;
        if (backend.load(path, QString()) != Document::Status::Ready) {
            print(QStringLiteral("    %1 does not open through Qt PDF").arg(padded(name, 22)));
            continue;
        }
        const QSizeF qtSize = backend.pageSize(0);
        const QSize pixels(int(qtSize.width() * kScreenZoom + 0.5),
                           int(qtSize.height() * kScreenZoom + 0.5));
        RenderOptions options;
        options.optimizedForLcd = true;
        const QImage viaQt = backend.render(0, pixels, options);

        PdfDocument document;
        if (!document.open(path, false) || viaQt.isNull()) {
            print(QStringLiteral("    %1 skipped: one of the two renderers produced nothing")
                      .arg(padded(name, 22)));
            continue;
        }
        const QSizeF pdfiumSize = document.sizeOfPage(0);
        const QImage viaPdfium = renderPdfium(document, 0, pixels, 0, false, 0);
        const QImage withAnnotations = renderPdfium(document, 0, pixels, FPDF_ANNOT, false, 0);

        // The largest single-channel distance anywhere on the page, so "they agree" is arguable with
        // a number rather than with a count: 1/255 on one antialiased edge is a different claim from
        // a glyph that moved.
        auto worstDelta = [](const QImage &one, const QImage &other) {
            if (one.isNull() || other.isNull() || one.size() != other.size())
                return -1;
            const QImage a = overWhite(one);
            const QImage b = overWhite(other);
            int worst = 0;
            for (int y = 0; y < a.height(); ++y) {
                const QRgb *rowA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
                const QRgb *rowB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
                for (int x = 0; x < a.width(); ++x) {
                    const int deltaRed = qRed(rowA[x]) > qRed(rowB[x])
                                           ? qRed(rowA[x]) - qRed(rowB[x])
                                           : qRed(rowB[x]) - qRed(rowA[x]);
                    const int deltaGreen = qGreen(rowA[x]) > qGreen(rowB[x])
                                             ? qGreen(rowA[x]) - qGreen(rowB[x])
                                             : qGreen(rowB[x]) - qGreen(rowA[x]);
                    const int deltaBlue = qBlue(rowA[x]) > qBlue(rowB[x])
                                            ? qBlue(rowA[x]) - qBlue(rowB[x])
                                            : qBlue(rowB[x]) - qBlue(rowA[x]);
                    if (deltaRed > worst)
                        worst = deltaRed;
                    if (deltaGreen > worst)
                        worst = deltaGreen;
                    if (deltaBlue > worst)
                        worst = deltaBlue;
                }
            }
            return worst;
        };

        const QRect whole(QPoint(0, 0), pixels);
        // The ink each side put on the page, beside the difference between them: differ=0 with one
        // side at zero ink would mean two empty pages agreeing, which is not the reading anybody
        // wants to take as "the two libraries draw the same page" (sec.82's rule again, one line
        // further along: an agreement has to say that there was something to agree about).
        int qtInk = 0;
        int qtWorst = 0;
        inkWithin(viaQt, whole, &qtInk, &qtWorst);
        int pdfiumInk = 0;
        int pdfiumWorst = 0;
        inkWithin(viaPdfium, whole, &pdfiumInk, &pdfiumWorst);
        print(QStringLiteral("    %1 page pt qt=%2x%3 pdfium=%4x%5 | ink qt=%6 pdfium=%7 | "
                             "qt-vs-pdfium differ=%8 of %9 worst=%10 | annot-vs-page differ=%11")
                  .arg(padded(name, 22))
                  .arg(qtSize.width(), 0, 'f', 1)
                  .arg(qtSize.height(), 0, 'f', 1)
                  .arg(pdfiumSize.width(), 0, 'f', 1)
                  .arg(pdfiumSize.height(), 0, 'f', 1)
                  .arg(qtInk)
                  .arg(pdfiumInk)
                  .arg(differingWithin(viaQt, viaPdfium, whole))
                  .arg(pixels.width() * pixels.height())
                  .arg(worstDelta(viaQt, viaPdfium))
                  .arg(differingWithin(viaPdfium, withAnnotations, whole)));
        if (std::abs(qtSize.width() - pdfiumSize.width()) > 0.5
            || std::abs(qtSize.height() - pdfiumSize.height()) > 0.5) {
            control(6, QStringLiteral("a fidelity fixture disagrees about the page size: %1")
                          .arg(name),
                    false);
        }
    }
}

// --------------------------------------------------------------------- 5. cost

void reportCost(const QString &fixtureDir, const QString &realBook, const QString &sampleDir)
{
    print(QStringLiteral("\n== 5. what it costs, in the geometry docs/perf.md was written in =="));
    QStringList documents = {QStringLiteral("huge.pdf"), QStringLiteral("scan.pdf")};
    if (!realBook.isEmpty())
        documents << realBook;

    for (const QString &entry : documents) {
        const QString path = entry.contains(QLatin1Char(':')) || entry.startsWith(QLatin1Char('/'))
                               ? entry
                               : QDir(fixtureDir).filePath(entry);
        if (!QFileInfo::exists(path))
            continue;
        const QString name = QFileInfo(path).fileName();

        QtPdfBackend backend;
        int pages = 0;
        const double qtOpen = milliseconds([&] {
            backend.load(path, QString());
            pages = backend.pageCount();
        });

        PdfDocument document;
        const double pdfiumOpen = milliseconds([&] {
            document.open(path, false);
        });
        const int pdfiumPages = document.handle ? FPDF_GetPageCount(document.handle) : 0;

        QVector<QSizeF> qtSizes;
        const double qtSizesMs = milliseconds([&] {
            for (int page = 0; page < pages; ++page)
                qtSizes.append(backend.pageSize(page));
        });
        int pdfiumSizeCount = 0;
        const double pdfiumSizesMs = milliseconds([&] {
            for (int page = 0; page < pdfiumPages; ++page) {
                if (!document.sizeOfPage(page).isEmpty())
                    ++pdfiumSizeCount;
            }
        });

        QVector<int> picked;
        for (int index = 0; index < kRendered && pages > 0; ++index)
            picked.append(int(qint64(index) * pages / qMax(1, kRendered)));

        QSize firstPixels;
        const double qtRender = milliseconds([&] {
            RenderOptions options;
            options.optimizedForLcd = true;
            for (int page : picked) {
                const QSizeF size = qtSizes.value(page);
                const QSize pixels(int(size.width() * kScreenZoom + 0.5),
                                   int(size.height() * kScreenZoom + 0.5));
                if (page == picked.first())
                    firstPixels = pixels;
                backend.render(page, pixels, options);
            }
        });
        const double pdfiumRender = milliseconds([&] {
            for (int page : picked) {
                const QSizeF size = document.sizeOfPage(page);
                renderPdfium(document, page,
                             QSize(int(size.width() * kScreenZoom + 0.5),
                                   int(size.height() * kScreenZoom + 0.5)),
                             0, false, 0);
            }
        });

        int qtCharacters = 0;
        const double qtText = milliseconds([&] {
            for (int page : picked)
                qtCharacters += backend.text(page).size();
        });
        int pdfiumCharacters = 0;
        const double pdfiumText = milliseconds([&] {
            for (int page : picked) {
                FPDF_PAGE loaded = FPDF_LoadPage(document.handle, page);
                if (!loaded)
                    continue;
                FPDF_TEXTPAGE text = FPDFText_LoadPage(loaded);
                if (text) {
                    pdfiumCharacters += FPDFText_CountChars(text);
                    FPDFText_ClosePage(text);
                }
                FPDF_ClosePage(loaded);
            }
        });

        print(QStringLiteral("    %1  pages qt=%2 pdfium=%3  page px=%4")
                  .arg(padded(name, 22))
                  .arg(pages)
                  .arg(pdfiumPages)
                  .arg(firstPixels.isValid()
                           ? QStringLiteral("%1x%2").arg(firstPixels.width())
                                                 .arg(firstPixels.height())
                           : QStringLiteral("-")));
        print(QStringLiteral("      open        qt=%1 ms   pdfium=%2 ms")
                  .arg(qtOpen, 0, 'f', 1)
                  .arg(pdfiumOpen, 0, 'f', 1));
        print(QStringLiteral("      all sizes   qt=%1 ms   pdfium=%2 ms (%3 of %4 read)")
                  .arg(qtSizesMs, 0, 'f', 1)
                  .arg(pdfiumSizesMs, 0, 'f', 1)
                  .arg(pdfiumSizeCount)
                  .arg(pdfiumPages));
        print(QStringLiteral("      %1 pages img qt=%2 ms (%3 ms/page)  pdfium=%4 ms (%5 ms/page)")
                  .arg(picked.size())
                  .arg(qtRender, 0, 'f', 1)
                  .arg(picked.isEmpty() ? 0.0 : qtRender / picked.size(), 0, 'f', 2)
                  .arg(pdfiumRender, 0, 'f', 1)
                  .arg(picked.isEmpty() ? 0.0 : pdfiumRender / picked.size(), 0, 'f', 2));
        print(QStringLiteral("      %1 pages txt qt=%2 ms (%3 chars)  pdfium=%4 ms (%5 chars)")
                  .arg(picked.size())
                  .arg(qtText, 0, 'f', 2)
                  .arg(qtCharacters)
                  .arg(pdfiumText, 0, 'f', 2)
                  .arg(pdfiumCharacters));
    }

    // The form layer's own cost, at the size that prints, on the sample that has something to draw.
    const QString appearance = QDir(sampleDir).filePath(QStringLiteral("field-appearance.pdf"));
    const QSize pixels(kPaths.last().width, kPaths.last().height);
    PdfDocument plain;
    PdfDocument withForm;
    if (plain.open(appearance, false) && withForm.open(appearance, true)) {
        const double pageOnly = milliseconds([&] {
            renderPdfium(plain, 0, pixels, FPDF_ANNOT, false, 0);
        });
        const double firstFrame = milliseconds([&] {
            renderPdfium(withForm, 0, pixels, FPDF_ANNOT, true, FPDF_ANNOT);
        });
        const double secondFrame = milliseconds([&] {
            renderPdfium(withForm, 0, pixels, FPDF_ANNOT, true, FPDF_ANNOT);
        });
        print(QStringLiteral("    form layer at %1x%2 on field-appearance.pdf: page+annot=%3 ms, "
                             "first frame with FFL=%4 ms, second=%5 ms")
                  .arg(pixels.width())
                  .arg(pixels.height())
                  .arg(pageOnly, 0, 'f', 1)
                  .arg(firstFrame, 0, 'f', 1)
                  .arg(secondFrame, 0, 'f', 1));
    } else {
        print(QStringLiteral("    (the form layer's cost could not be timed: the sample did not "
                             "open through PDFium)"));
    }
}

// --------------------------------------------------------------------- 6. threads

void reportThreads(const QString &fixtureDir)
{
    print(QStringLiteral("\n== 6. four documents, four threads, no lock =="));
    print(QStringLiteral("    fpdfview.h:11-12 says none of these APIs are thread-safe; what that "
                         "costs when each thread owns its own document is what this measures"));
    const QString path = QDir(fixtureDir).filePath(QStringLiteral("scan.pdf"));
    if (!QFileInfo::exists(path))
        return;
    const QSize pixels(int(595 * kScreenZoom + 0.5), int(842 * kScreenZoom + 0.5));

    PdfDocument single;
    if (!single.open(path, false))
        return;
    const int pages = FPDF_GetPageCount(single.handle);
    int serialImages = 0;
    const double serial = milliseconds([&] {
        for (int round = 0; round < 4; ++round) {
            if (!renderPdfium(single, round % qMax(1, pages), pixels, 0, false, 0).isNull())
                ++serialImages;
        }
    });

    const qint64 beforeMemory = workingSetKb();
    QVector<PdfDocument *> four;
    const double openFour = milliseconds([&] {
        for (int index = 0; index < 4; ++index) {
            auto *document = new PdfDocument;
            if (document->open(path, false))
                four.append(document);
            else
                delete document;
        }
    });
    const qint64 afterMemory = workingSetKb();

    std::atomic<int> succeeded{0};
    std::atomic<int> failed{0};
    QElapsedTimer clock;
    clock.start();
    std::vector<std::thread> workers;
    for (PdfDocument *document : four) {
        workers.emplace_back([document, pixels, &succeeded, &failed] {
            const QImage image = renderPdfium(*document, 0, pixels, 0, false, 0);
            if (image.isNull())
                ++failed;
            else
                ++succeeded;
        });
    }
    for (std::thread &worker : workers)
        worker.join();
    const double wall = double(clock.nsecsElapsed()) / 1.0e6;

    print(QStringLiteral("    serial, one document, 4 pages: %1 ms (%2 images)")
              .arg(serial, 0, 'f', 1)
              .arg(serialImages));
    print(QStringLiteral("    %1 documents open at once: %2 ms, working set %3 KB -> %4 KB (%5 KB)")
              .arg(four.size())
              .arg(openFour, 0, 'f', 1)
              .arg(beforeMemory)
              .arg(afterMemory)
              .arg(afterMemory - beforeMemory));
    print(QStringLiteral("    4 threads, own document, no lock: %1 ms wall, ok=%2 failed=%3")
              .arg(wall, 0, 'f', 1)
              .arg(succeeded.load())
              .arg(failed.load()));
    print(QStringLiteral("    (a single document shared between threads is deliberately not tried: "
                         "PDFium's answer there is a race, and a probe that crashes takes its "
                         "readings with it. pages=%1)")
              .arg(pages));
    for (PdfDocument *document : four)
        delete document;
}

// --------------------------------------------------------------------- 7. filling

void reportFilling(const QString &sampleDir)
{
    print(QStringLiteral("\n== 7. can a value be written, and does the next frame show it? =="));
    const QString path = QDir(sampleDir).filePath(QStringLiteral("field-appearance.pdf"));
    const QSize pixels(kPaths.first().width, kPaths.first().height);

    PdfDocument document;
    if (!document.open(path, true)) {
        print(QStringLiteral("    (could not open %1 with a form environment)").arg(path));
        return;
    }
    FPDF_PAGE page = FPDF_LoadPage(document.handle, 0);
    if (!page)
        return;
    FORM_OnAfterLoadPage(page, document.form);

    const int annotations = FPDFPage_GetAnnotCount(page);
    FPDF_ANNOTATION annot = annotations > 0 ? FPDFPage_GetAnnot(page, 0) : nullptr;

    const Sample &widget = kSamples[2];
    const QRect box = pixelBox(widget, pixels, document.sizeOfPage(0).width(),
                              document.sizeOfPage(0).height());

    const QImage before = renderPdfium(document, 0, pixels, FPDF_ANNOT, true, FPDF_ANNOT);

    QString writtenBack = QStringLiteral("(no annotation to write to)");
    int pixelsAfterWriting = -1;
    if (annot) {
        const QString replacement = QStringLiteral("typed-by-probe");
        unsigned short utf16[64] = {0};
        for (int index = 0; index < replacement.size() && index < 62; ++index)
            utf16[index] = replacement.at(index).unicode();
        const FPDF_BOOL accepted
            = FPDFAnnot_SetStringValue(annot, "/V", reinterpret_cast<FPDF_WIDESTRING>(utf16));
        unsigned short readBack[128] = {0};
        const unsigned long bytes
            = FPDFAnnot_GetStringValue(annot, "/V", readBack, sizeof(readBack));
        const QString value = bytes > 1
            ? QString::fromUtf16(reinterpret_cast<const char16_t *>(readBack), int(bytes / 2) - 1)
            : QStringLiteral("(none)");
        writtenBack = QStringLiteral("set=%1  /V now [%2]")
                          .arg(accepted ? QStringLiteral("accepted") : QStringLiteral("refused"),
                               value);
        const QImage after = renderPdfium(document, 0, pixels, FPDF_ANNOT, true, FPDF_ANNOT);
        pixelsAfterWriting = differingWithin(before, after, box);
    }
    print(QStringLiteral("    annots=%1 %2  pixels that changed in the box: %3")
              .arg(annotations)
              .arg(writtenBack)
              .arg(pixelsAfterWriting));

    // Flatten is the other way to get the drawing onto the page: it turns the annotation into page
    // content, which is what a print job or an export wants if the form layer stays a layer.
    const int flatten = FPDFPage_Flatten(page, FLAT_PRINT);
    const QImage afterFlatten = renderPdfium(document, 0, pixels, FPDF_ANNOT, false, 0);
    print(QStringLiteral("    FPDFPage_Flatten(FLAT_PRINT) -> %1 (%2=success, 0=fail, 2=nothing to "
                         "do); page afterwards differs from the frame before by %3 pixels")
              .arg(flatten)
              .arg(FLATTEN_SUCCESS)
              .arg(differingWithin(before, afterFlatten, QRect(QPoint(0, 0), pixels))));

    if (annot)
        FPDFPage_CloseAnnot(annot);
    FORM_OnBeforeClosePage(page, document.form);
    FPDF_ClosePage(page);
}

// --------------------------------------------------------------------- the run, and the way out

// Everything that owns a PDFium handle lives inside runProbe's scope, and main destroys the library
// only after that scope has closed. The first pass printed its last line and then died with
// 0xC0000005: the documents and their form handles were still alive when FPDF_DestroyLibrary() ran,
// and their destructors (FPDFDOC_ExitFormFillEnvironment, FPDF_CloseDocument) then reached into a
// library that was already gone. The readings were all on stdout by then, which is how it got
// misread as a clean run with a strange exit code -- an exit code the gate would have caught and a
// log full of plausible numbers would not.
int runProbe(const QString &sampleDir, const QString &fixtureDir, const QString &realBook)
{
    // Before anything else: without the generated samples most of this prints an empty table, which
    // reads like a measurement that found nothing rather than like an instrument with nothing to
    // measure. sec.54's guard on the fixture directory comes with it -- those files are generated for
    // a window and for probes, and a test that globs tests/fixtures would pick them up.
    bool samplesThere = !sampleDir.isEmpty()
        && !sampleDir.contains(QStringLiteral("fixtures"), Qt::CaseInsensitive);
    if (samplesThere) {
        for (const Sample &sample : kSamples) {
            if (!QFileInfo::exists(QDir(sampleDir).filePath(QLatin1String(sample.file))))
                samplesThere = false;
        }
    }
    if (!samplesThere) {
        print(QStringLiteral(
            "Bail: XYZPDF_FORM_SAMPLE_DIR must name a directory holding the generated form samples "
            "(and must not name tests/fixtures). Build the suite, then run\n"
            "  set XYZPDF_FORM_SAMPLE_DIR=<a scratch directory>\n"
            "  build-local-release\\tests\\xyzpdf_documenteditor_smoke.exe\n"
            "to write them."));
        return 1;
    }
    print(QStringLiteral("samples: %1").arg(sampleDir));
    print(QStringLiteral("fixtures: %1").arg(QDir(fixtureDir).exists()
                                                ? fixtureDir
                                                : QStringLiteral("(missing -- run from the project "
                                                                 "root)")));

    reportIdentity();
    Holders holders;
    reportCarried(sampleDir, holders);
    reportPixels(sampleDir, holders);
    runPixelControls(sampleDir, holders);
    reportFidelity(fixtureDir, realBook);
    reportCost(fixtureDir, realBook, sampleDir);
    reportThreads(fixtureDir);
    reportFilling(sampleDir);

    print(QStringLiteral("\n%1 control(s) broken").arg(brokenControls));
    return brokenControls > 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    FPDF_InitLibrary();

    const QString sampleDir = QString::fromLocal8Bit(qgetenv("XYZPDF_FORM_SAMPLE_DIR"));
    const QString realBook = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const int outcome = runProbe(sampleDir, QStringLiteral("tests/fixtures"), realBook);

    FPDF_DestroyLibrary();
    return outcome;
}
