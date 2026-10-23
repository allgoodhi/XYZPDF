// A form-bearing PDF, written as bytes, for the tests that have to click on a field.
//
// ---- why not qpdf ----
//
// The committed fixtures carry no /AcroForm at all -- none of the 18 files in
// tests/fixtures has a field, which is why sec.54 built a generator in the first place
// -- and the qpdf-based generator lives inside tests/documenteditor_smoke.cpp, a target
// that links qpdf. tests/pdfiumbackend_smoke.cpp does not link it (CMakeLists: LIBRARIES
// xyzpdf_pdfium Qt6::Widgets), so a test of the form layer there would have to build a
// file without it. Writing the objects out is the shorter of the two roads, and it is the
// one that keeps the sample's shape coming from the specification rather than from
// whichever library happened to write it (AGENTS.md, sec.112's rule about a sample that
// was built off the code under test).
//
// ---- the /Ff values, read off PDFium ----
//
// A scratch build of nine fields -- /Btn at /Ff 0, 2, 4, 32768 and 65536, /Ch at 0, 1 and
// 131072, plus one /Tx -- answered FPDFPage_HasFormFieldAtPoint as CHECKBOX for the first
// four buttons, RADIO for 32768 (1<<15), PUSHBUTTON for 65536 (1<<16), LISTBOX for /Ch 0
// and 1, COMBOBOX for 131072 (1<<17) and TEXT for the /Tx. Those are the field-flag bits
// counted from bit 1 as PDF 32000-1 counts them: 2 is Required and 4 is NoExport, both of
// which belong to every field type rather than to buttons -- which is exactly the mistake
// this note exists to stop being made twice.
//
// ---- what is on the page ----
//
// One text field, one checkbox, one radio group with two kids, one combobox, one listbox,
// one push button and one signature field, laid out top-down so each rectangle is a line
// of the page and a click can name the one it landed in. The checkbox carries /AP for both
// of its states, because a checkbox without one cannot be un-checked: the toggle *is* the
// appearance. `rotate` puts /Rotate on the page, which is the reading the form layer's
// coordinate space is decided by (the note at the top of src/document/formfill.h has the
// measurement).

#ifndef XYZPDF_TEST_FORMSAMPLE_H
#define XYZPDF_TEST_FORMSAMPLE_H

#include "../src/document/formfill.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef small

#include <fpdf_annot.h>
#include <fpdf_formfill.h>
#include <fpdfview.h>

namespace FormSampleFile {

// ---- the raw form sample ----
//
// The /Ff values are the ones PDFium reads, and they were read off the library rather
// than typed from memory: a scratch build with nine fields (text, five button variants at
// /Ff 0, 2, 4, 32768, 65536 and three choice variants at 0, 1, 131072) answered CHECKBOX
// for the first four buttons, RADIO for 32768 (1<<15) and PUSHBUTTON for 65536 (1<<16),
// and LISTBOX for /Ff 0 and 1 with COMBOBOX for 131072 (1<<17). The values that read as
// "button" at 2 and 4 are the *all-field* flags -- Required and NoExport -- which is the
// mistake this note exists to stop being made twice.
//
// Written as bytes rather than with qpdf, because this target does not link qpdf
// (CMakeLists: LIBRARIES xyzpdf_pdfium Qt6::Widgets) and because the shapes here are
// exactly the ones PDFium reads: /FT names and /Ff bits from PDF 32000-1's field-flag
// tables -- button bit 2 (radio) and bit 3 (pushbutton) (table 222), choice bit 1
// (combo) (table 229). The sample carries one of every kind the reader can fill, the
// two kinds this version only shows, and no /AP appearances at all: what PDFium draws
// for a field is its own face, which is the limit pdfium-plan.md sec.7 says to keep
// saying out loud.
//
// A page it can be read on is a page with one text field and one of everything else,
// laid out top-down so each rectangle is a line of the page and a click can name the
// one it landed in. `rotate` puts /Rotate on the page, which is the reading the form
// layer's coordinate space is decided by (see the note in formfill.h).
struct Geometry
{
    QString path;
    // Each field's rectangle in the page's own user space, whose origin is the page's
    // bottom-left corner and whose y grows upwards. So x is the left edge, y the
    // *lower* edge, and the /Rect a PDF writes is [x, y, x+w, y+h] -- and the point a
    // click has to land on is the centre of that, which is what QRectF::center()
    // already is. Documented because the other reading (y as the top edge, which is
    // what the rest of this program's page boxes use) produced a rectangle with a
    // negative height here and a hit test that answered "no field" for six of eight.
    QRectF textField {60, 600, 200, 30};
    QRectF checkBox {60, 540, 16, 16};
    QRectF radioOne {60, 500, 16, 16};
    QRectF radioTwo {90, 500, 16, 16};
    QRectF comboBox {60, 460, 140, 20};
    QRectF listBox {60, 380, 140, 60};
    QRectF pushButton {60, 340, 100, 20};
    QRectF signature {300, 600, 100, 30};
};

// One object body per number, then the xref the reader needs. `objects` is 1-based:
// slot 0 is the free entry every cross-reference table starts with, so writing it as
// an object would put the catalog one number off -- which is exactly the bug this
// function's numbering had before. The offsets are counted in bytes off the finished
// buffer, which is the only reason this is a function rather than a string literal: a
// hand-typed xref is a claim about the bytes above it.
QByteArray assemblePdf(const QVector<QByteArray> &objects, int root)
{
    QByteArray bytes("%PDF-1.7\n%xyzpdf-sample\n");
    QVector<qint64> offsets; // offsets.at(n) is where object n begins
    offsets.append(0);
    for (int number = 1; number < objects.size(); ++number) {
        offsets.append(bytes.size());
        bytes += QByteArray::number(number) + " 0 obj\n" + objects.at(number)
            + "\nendobj\n";
    }
    const qint64 xrefStart = bytes.size();
    bytes += "xref\n0 " + QByteArray::number(objects.size()) + "\n";
    bytes += "0000000000 65535 f \n";
    for (int number = 1; number < offsets.size(); ++number) {
        const QByteArray padded = QByteArray::number(offsets.at(number)).rightJustified(10, '0');
        bytes += padded + " 00000 n \n";
    }
    bytes += "trailer\n<< /Size " + QByteArray::number(objects.size())
        + " /Root " + QByteArray::number(root) + " 0 R >>\nstartxref\n"
        + QByteArray::number(xrefStart) + "\n%%EOF\n";
    return bytes;
}

// The rectangle a PDF writes: [lower-left x, lower-left y, upper-right x, upper-right y].
QByteArray rectOf(const QRectF &rect)
{
    return QByteArray("[") + QByteArray::number(qint64(rect.left())) + " "
        + QByteArray::number(qint64(rect.top())) + " "
        + QByteArray::number(qint64(rect.right())) + " "
        + QByteArray::number(qint64(rect.bottom())) + "]";
}

// The point a click on that field has to be answered for, in user space.
QPointF userCentre(const QRectF &rect)
{
    return rect.center();
}

// The same point in the pixel grid of a page drawn at one device pixel per point with no
// reader rotation -- which is what the flat frame in this test is. Device y grows
// downwards from the top of the drawn page, so the flip is the page's own height.
QPointF flatDevice(const QRectF &rect)
{
    const QPointF at = userCentre(rect);
    return QPointF(at.x(), 842.0 - at.y());
}

// A widget annotation. `extra` carries what each kind needs beyond the shared keys:
// /V and /AS for the buttons, /Opt for the choices, /Parent for a radio's kid.
QByteArray widgetBody(const QString &name, const char *fieldType, const QRectF &rect,
                      const QByteArray &extra)
{
    QByteArray body("<< /Type /Annot /Subtype /Widget");
    // A radio group's kid carries no /FT of its own: the field type is inherited from
    // the parent, and PDFium reads it from there.
    if (fieldType && *fieldType)
        body += " /FT " + QByteArray(fieldType);
    body += " /T (" + name.toUtf8() + ") /Rect " + rectOf(rect) + " /P 4 0 R /F 4";
    // /DA is what a text-ish field is drawn with. Left out of the buttons, which take
    // /MK; a choice field without it is a field PDFium would have no font for, and a
    // test that read 0 pixels would be about the sample rather than about the engine.
    body += extra;
    body += " >>";
    return body;
}

// The sample's object numbers are planned rather than counted, because a PDF refers to
// its objects by number and a writer that appends has to guess at the forward
// references. Slot 0 is unused; every body below is put where its number says.
bool write(const QString &path, int pageRotation, bool withXfaPacket, QString *error)
{
    Geometry sample;
    QVector<QByteArray> objects(18);
    objects[1] = "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>";
    objects[2] = "<< /Type /Pages /Kids [4 0 R] /Count 1 >>";
    objects[3] = QByteArray("<< /Type /AcroForm /Fields [5 0 R 6 0 R 7 0 R 10 0 R 11 0 R "
                             "12 0 R 13 0 R] /DA (/Helv 9 Tf 0 g ) /DR << /Font << /Helv "
                             "9 0 R >> >>");
    if (withXfaPacket)
        // The XFA copy of the form: /AcroForm /XFA holding (subtype, stream) pairs
        // (PDF 32000-1 sec.12.7.6.3, the shape sec.55 built and sec.114 reads on
        // screen). An embedded-file stream is enough for the type to be reported.
        objects[3] += " /XFA [(/datasets) 14 0 R]";
    objects[3] += " >>";
    objects[4] = QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Rotate ")
        + QByteArray::number(pageRotation)
        + " /Resources << /Font << /Helv 9 0 R >> >> /Annots [5 0 R 6 0 R 8 0 R 10 0 R "
          "11 0 R 12 0 R 13 0 R 15 0 R] >>";
    objects[5] = widgetBody(QStringLiteral("name"), "/Tx", sample.textField,
                            " /V (typed on page 1) /DA (/Helv 10 Tf 0 g )");
    // A checkbox without an /AP for each of its states is a checkbox a reader cannot
    // un-check: the toggle is the appearance, and PDFium has nothing to switch to. Real
    // forms carry /AP /N << /Off .. /Yes .. >>, so the sample does too -- two empty form
    // XObjects, one per state (objects 16 and 17), which is the smallest shape that is
    // not a claim about pixels.
    objects[6] = widgetBody(QStringLiteral("agree"), "/Btn", sample.checkBox,
                            " /V /Off /AS /Off /MK << /CA 8 >> /AP << /N << /Off 16 0 R "
                            "/Yes 17 0 R >> >>");
    // The radio group is a field with kids: the group carries /T, /FT and the current
    // value, and each kid is the widget on the page with its export value in /MK /CA --
    // which is the only place a radio says which button it is (table 223).
    objects[7] = "<< /FT /Btn /Ff 32768 /T (group) /V /One /Kids [8 0 R 15 0 R] >>";
    objects[8] = widgetBody(QString(), "", sample.radioOne,
                            " /Parent 7 0 R /AS /Off /MK << /CA (One) >>");
    objects[9] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    objects[10] = widgetBody(QStringLiteral("pick"), "/Ch", sample.comboBox,
                             " /Ff 131072 /V (b) /Opt [(a) (b) (c)] /DA (/Helv 10 Tf 0 g )");
    objects[11] = widgetBody(QStringLiteral("list"), "/Ch", sample.listBox,
                             " /Ff 0 /V (b) /Opt [(a) (b) (c)] /DA (/Helv 10 Tf 0 g )");
    objects[12] = widgetBody(QStringLiteral("go"), "/Btn", sample.pushButton, " /Ff 65536");
    objects[13] = widgetBody(QStringLiteral("sig"), "/Sig", sample.signature, "");
    if (withXfaPacket) {
        const QByteArray packet("<x:xfaTemplate/>");
        // A stream object: the dictionary, then `stream`, the bytes, `endstream`. The
        // /Length is the byte count of what is between the two keywords.
        objects[14] = QByteArray("<< /Type /EmbeddedFile /Length ")
            + QByteArray::number(packet.size()) + " >>\n"
            + QByteArray("stream\n") + packet + QByteArray("\nendstream");
    } else {
        objects[14] = "null";
    }
    objects[15] = widgetBody(QString(), "", sample.radioTwo,
                             " /Parent 7 0 R /AS /On /MK << /CA (Two) >>");
    // The two appearance states of the checkbox. Empty content, /Length 0: they exist so
    // that the field has a state to be drawn in, which is all the toggle needs.
    for (int number : {16, 17}) {
        objects[number] = "<< /Type /XObject /Subtype /Form /BBox [0 0 16 16] /Resources << "
                          ">> /Length 0 >>\nstream\n\nendstream";
    }

    QByteArray bytes;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("could not write %1").arg(path);
        return false;
    }
    file.write(assemblePdf(objects, 1));
    file.close();
    return true;
}

// The rectangles the sample was built with, for the clicks that have to name them.
Geometry geometry()
{
    return Geometry();
}

// The form layer's own reads, run straight against the library -- a second reader of
// the file, for the assertions about what a save put in it. The backend keeps its
// FPDF_DOCUMENT to itself on purpose, so a test that wants to know what a file says has
// to ask the library the way the backend does, in the open this process already holds.
QString fieldValue(const QString &path, const QString &name, QString *why)
{
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    if (!document) {
        *why = QStringLiteral("FPDF_LoadDocument failed, last error %1").arg(FPDF_GetLastError());
        return {};
    }
    FPDF_FORMFILLINFO info{};
    info.version = 1;
    // No callbacks: this reader draws nothing and takes no input, and the F5 probe
    // measured that a zeroed interface still opens a form (pdfium-plan sec.7).
    FPDF_FORMHANDLE form = FPDFDOC_InitFormFillEnvironment(document, &info);
    QString found;
    const int pages = FPDF_GetPageCount(document);
    for (int page = 0; page < pages && found.isEmpty(); ++page) {
        FPDF_PAGE handle = FPDF_LoadPage(document, page);
        if (!handle)
            continue;
        FORM_OnAfterLoadPage(handle, form);
        const int annots = FPDFPage_GetAnnotCount(handle);
        for (int index = 0; index < annots && found.isEmpty(); ++index) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(handle, index);
            if (!annot)
                continue;
            FPDF_WCHAR names[256] = {0};
            FPDFAnnot_GetFormFieldName(form, annot, names, sizeof(names));
            if (QString::fromUtf16(reinterpret_cast<const char16_t *>(names)) != name) {
                continue;
            }
            FPDF_WCHAR values[512] = {0};
            const unsigned long bytes
                = FPDFAnnot_GetFormFieldValue(form, annot, values, sizeof(values));
            if (bytes > 2)
                found = QString::fromUtf16(reinterpret_cast<const char16_t *>(values),
                                           int(bytes / 2) - 1);
            else
                found = QStringLiteral("(empty)");
        }
        FORM_OnBeforeClosePage(handle, form);
        FPDF_ClosePage(handle);
    }
    FPDFDOC_ExitFormFillEnvironment(form);
    FPDF_CloseDocument(document);
    if (found.isEmpty())
        *why = QStringLiteral("no field named %1 in %2").arg(name, path);
    return found;
}

// The device geometry the sample's page is drawn at: the page's own size in points, so
// one device pixel is one point and a click's arithmetic can be read off the rectangle.

// Where a point in the page's own user space lands in the pixel grid a render of size
// `pixels` with `quarterTurns` of reader rotation produced -- FPDF_PageToDevice, the same
// conversion the form layer's reverse question makes. A test that clicks a field on a
// turned page asks the library where it is rather than computing it, which is the point:
// the two conversions have to be inverses, not two guesses that agree by chance.
QPointF deviceCentre(const QString &path, QPointF userSpace, QSize pixels, int quarterTurns)
{
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    if (!document)
        return QPointF();
    FPDF_PAGE page = FPDF_LoadPage(document, 0);
    int x = 0;
    int y = 0;
    const bool ok = page
        && FPDF_PageToDevice(page, 0, 0, pixels.width(), pixels.height(), quarterTurns,
                             userSpace.x(), userSpace.y(), &x, &y);
    if (page)
        FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    return ok ? QPointF(x, y) : QPointF();
}

} // namespace FormSampleFile

#endif // XYZPDF_TEST_FORMSAMPLE_H
