// Measures one document: how long its pages take to render, how many pixels
// that is, how many of them fit in the render cache's budget, what the text
// layer of one page looks like, and what a search for a phrase would find.
//
//     xyzpdf_pdfprobe <file.pdf> [samples] [viewport width] [text page] [needle]
//     xyzpdf_pdfprobe --fields <file.pdf>
//     xyzpdf_pdfprobe --write-form-sample <path.pdf>
//     xyzpdf_pdfprobe --write-signed-sample <path.pdf> [--docmdp 0|1|2|3]
//
// Not a test -- it prints numbers rather than passing or failing, which is why
// tools/build_all_tests.py skips anything with "probe" in its name. It exists
// because "the program is slow with my document" is not a question that can be
// answered from a description of the document: whether the time goes on
// rendering or on memory depends on whether the pages are text or scans, how
// big they are, and what the screen is, and the numbers have to come off the
// machine where it happens.
//
// The three subcommands are the same idea aimed at a form on a screen (handoff-05
// H2, and handoff-07 I2 for the signed one), and none of them may write a document:
// --fields opens, reads and closes, and the two writers refuse a path that already
// exists, because they make a sample rather than editing a file.
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

// The byte writer the headless tests already use, borrowed rather than retyped:
// a second xref writer is a second claim about the bytes above it, and the two
// would drift (tests/formsample.h exists for exactly this reason -- its note at
// the top says the sample's shape has to come from the specification, not from
// whichever library happened to write it). It brings windows.h and three of the
// PDFium headers with it.
#include "../tests/formsample.h"

// Two more PDFium headers, for the page's own box and turn: the media box lives
// in fpdf_transformpage.h and the rotation in fpdf_edit.h, and neither of those
// is one formsample.h needs.
#include <fpdf_edit.h>
#include <fpdf_transformpage.h>

// windows.h (via minwindef.h, line 95 of the SDK on this machine) does
// `#define near`, so every local variable called `near` below this include stops
// being a name -- the declaration reads as `const PageSelection = ...` and the
// error lands 470 lines away from the line that caused it. This is AGENTS.md's
// `#undef small` rule measured again, and the answer is the same: undo the macro,
// do not rename somebody else's variable.
#undef near

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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

// Every reading this tool prints goes out as ASCII, and a character that is not
// ASCII goes out as U+XXXX. The reason is the console: it is GBK on this
// machine, so a Chinese field value printed raw arrives as mojibake and the
// harness ends up comparing against the console's encoding instead of against
// the file. It is the same rule gui-probe.ps1 keeps for itself (its script has
// to be ASCII, and a Chinese needle is spelled as code points), applied to the
// other end of the pipe.
QString asciiSafe(const QString &text)
{
    QString out;
    for (const QChar &character : text) {
        if (character.unicode() >= 0x20 && character.unicode() < 0x7F) {
            out += character;
            continue;
        }
        if (!out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
            out += QLatin1Char(' ');
        out += QStringLiteral("U+%1")
                   .arg(int(character.unicode()), 4, 16, QLatin1Char('0'))
                   .toUpper();
        out += QLatin1Char(' ');
    }
    return out.trimmed();
}

// The word rather than the number: FPDF_FORMFIELD_CHECKBOX is 2 in
// fpdf_formfill.h, and a harness that matched on the digit would keep working
// after a library bump that renumbered it, which is the quiet half of a
// measurement. Anything this table does not know is printed as the number it
// was handed. The eight XFA types (8..15) are deliberately absent: fpdf_formfill.h
// only defines them under PDF_ENABLE_XFA, which nothing in this project defines,
// so writing the name here would not compile -- and a form of that kind is
// refused by the reader anyway (sec.112's XFA notice), which makes its number
// arriving un-named the honest reading.
const char *fieldTypeName(int type)
{
    switch (type) {
    case FPDF_FORMFIELD_UNKNOWN:       return "UNKNOWN";
    case FPDF_FORMFIELD_PUSHBUTTON:    return "PUSHBUTTON";
    case FPDF_FORMFIELD_CHECKBOX:      return "CHECKBOX";
    case FPDF_FORMFIELD_RADIOBUTTON:   return "RADIOBUTTON";
    case FPDF_FORMFIELD_COMBOBOX:      return "COMBOBOX";
    case FPDF_FORMFIELD_LISTBOX:       return "LISTBOX";
    case FPDF_FORMFIELD_TEXTFIELD:     return "TEXTFIELD";
    case FPDF_FORMFIELD_SIGNATURE:     return "SIGNATURE";
    default:                           return nullptr;
    }
}

QString fromWide(const FPDF_WCHAR *buffer)
{
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer));
}

// --fields: what the file says, one line per form field. The rectangle is in
// the page's own user space (points, origin at the bottom-left, y growing up --
// the reading tests/formsample.h has a paragraph about, since the other reading
// produced a rectangle with a negative height once), which is what makes a
// screen harness able to aim a click at a field: the page's drawn rectangle on
// screen plus this one is the whole of the arithmetic.
int dumpFields(const QString &path)
{
    // Unbuffered: a crash in a probe would otherwise take the readings with it,
    // and "no output at all" is indistinguishable from "it never got there".
    std::cout << std::unitbuf;
    // The library has to be initialised before it is asked anything, and the way
    // that shows up is worth keeping: without this call FPDF_LoadDocument,
    // FPDF_GetPageCount and FPDF_GetFormType all answer with the right numbers
    // (measured on two files: 40 pages / formtype 1, and 2 pages / formtype 0),
    // and FPDF_LoadPage then takes the process down with no message at all.
    // The product does the same thing behind a refcount (pdfiumbackend.cpp's
    // acquirePdfium); this tool has one user, so it is a plain pair of calls.
    FPDF_InitLibrary();
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    if (!document) {
        std::cerr << "could not open " << path.toStdString() << ": last error "
                  << FPDF_GetLastError() << '\n';
        FPDF_DestroyLibrary();
        return 1;
    }
    FPDF_FORMFILLINFO info{};
    info.version = 1;
    // No callbacks: this reader draws nothing and takes no input, and the F5
    // probe measured that a zeroed interface still opens a form
    // (docs/pdfium-plan.md sec.7).
    FPDF_FORMHANDLE form = FPDFDOC_InitFormFillEnvironment(document, &info);

    const int pages = FPDF_GetPageCount(document);
    std::cout << "file: " << path.toStdString() << '\n';
    std::cout << "pages: " << pages << '\n';
    // Which of the two forms this document carries, asked of the library rather
    // than read off a key: sec.112 measured what happens when the catalog's
    // /NeedsRendering is taken for the form's, and the answer was a test and the
    // code under it agreeing with each other about the wrong dictionary. 1 is
    // FORMTYPE_ACRO_FORM; 2 and 3 are the two XFA readings this build cannot
    // load (they are also what makes the reader refuse to fill).
    std::cout << "formtype: " << FPDF_GetFormType(document) << '\n';
    // Printed rather than assumed, because every field reading below goes through
    // that handle: a null one answers with an empty name and an empty value for
    // every field, which is indistinguishable from a document nobody filled in.
    std::cout << "formenv: " << (form ? "opened" : "REFUSED") << '\n';

    int fields = 0;
    for (int page = 0; page < pages; ++page) {
        FPDF_PAGE handle = FPDF_LoadPage(document, page);
        if (!handle)
            continue;
        float mediaLeft = 0;
        float mediaRight = 0;
        float mediaBottom = 0;
        float mediaTop = 0;
        const bool hasMedia
            = FPDFPage_GetMediaBox(handle, &mediaLeft, &mediaBottom, &mediaRight, &mediaTop);
        std::cout << "PAGE " << page << " mediabox="
                  << (hasMedia ? QStringLiteral("%1,%2,%3,%4")
                                   .arg(mediaLeft)
                                   .arg(mediaBottom)
                                   .arg(mediaRight)
                                   .arg(mediaTop)
                                   .toStdString()
                               : std::string("(none)"))
                  << " rotate=" << FPDFPage_GetRotation(handle)
                  << " width=" << FPDF_GetPageWidthF(handle)
                  << " height=" << FPDF_GetPageHeightF(handle)
                  << " annots=" << FPDFPage_GetAnnotCount(handle) << '\n';

        // The page is handed to the form environment the way the reader does,
        // because a field read off a page the environment never saw is a reading
        // about nothing. Only when the environment opened: with a zeroed
        // interface the call is the one that can take the process down, and a
        // null handle says so above instead.
        if (form)
            FORM_OnAfterLoadPage(handle, form);
        const int annots = FPDFPage_GetAnnotCount(handle);
        for (int index = 0; index < annots; ++index) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(handle, index);
            if (!annot)
                continue;
            // 20 in fpdf_annot.h: only a widget is a form field, and the
            // annotation list of a page also carries the markups a reader draws
            // (highlight, square, note) -- counting those as fields would make
            // this tool's rectangle the wrong one to click.
            if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_WIDGET) {
                FPDFPage_CloseAnnot(annot);
                continue;
            }
            FPDF_WCHAR name[256] = {0};
            FPDFAnnot_GetFormFieldName(form, annot, name, sizeof(name));
            FPDF_WCHAR value[512] = {0};
            // The bytes come back including the terminating null, which is why
            // the test that reads one field by name asks for more than two of
            // them before it believes anything was there.
            const unsigned long valueBytes
                = FPDFAnnot_GetFormFieldValue(form, annot, value, sizeof(value));
            const QString text = valueBytes > 2
                ? QString::fromUtf16(reinterpret_cast<const char16_t *>(value),
                                     int(valueBytes / 2) - 1)
                : QString();
            FS_RECTF rect{};
            const bool hasRect = FPDFAnnot_GetRect(annot, &rect);
            const int type = FPDFAnnot_GetFormFieldType(form, annot);
            const char *named = fieldTypeName(type);
            std::cout << "FIELD " << fields << " page=" << page << " name=["
                      << asciiSafe(fromWide(name)).toStdString() << "] type="
                      << (named ? std::string(named) : "TYPE" + std::to_string(type))
                      << " flags=" << FPDFAnnot_GetFormFieldFlags(form, annot) << " rect(l,b,r,t)="
                      // Printed in the order the arithmetic wants, not the order
                      // FS_RECTF stores it in: that struct's members are left,
                      // top, right, bottom, so top is the *higher* y of the two
                      // and a reader of this line would otherwise have to know it.
                      << (hasRect ? QStringLiteral("%1,%2,%3,%4")
                                      .arg(double(rect.left), 0, 'f', 1)
                                      .arg(double(rect.bottom), 0, 'f', 1)
                                      .arg(double(rect.right), 0, 'f', 1)
                                      .arg(double(rect.top), 0, 'f', 1)
                                      .toStdString()
                                  : std::string("(none)"))
                      << " value=[" << asciiSafe(text).toStdString() << "]\n";
            // A choice field's answer is not the whole answer: a multi-select list box's
            // /V is an array, and FPDFAnnot_GetFormFieldValue answers only its first
            // entry (measured in handoff-05 H1: five options, three picked, one value).
            // The per-option reads below are the library's own answer to "which rows
            // are selected", which is what the screen modes need to check a Ctrl+click
            // and what no string read can produce. Printed as separate lines rather
            // than squeezed into FIELD so the existing line's shape does not change.
            if (form) {
                const int options = FPDFAnnot_GetOptionCount(form, annot);
                for (int option = 0; option < options; ++option) {
                    FPDF_WCHAR label[256] = {0};
                    FPDFAnnot_GetOptionLabel(form, annot, option, label, sizeof(label));
                    std::cout << "OPTION field=[" << asciiSafe(fromWide(name)).toStdString()
                              << "] index=" << option << " label=["
                              << asciiSafe(fromWide(label)).toStdString() << "] selected="
                              << (FPDFAnnot_IsOptionSelected(form, annot, option) ? 1 : 0) << '\n';
                }
            }
            ++fields;
            FPDFPage_CloseAnnot(annot);
        }
        if (form)
            FORM_OnBeforeClosePage(handle, form);
        FPDF_ClosePage(handle);
    }
    std::cout << "fields: " << fields << '\n';

    if (form)
        FPDFDOC_ExitFormFillEnvironment(form);
    FPDF_CloseDocument(document);
    FPDF_DestroyLibrary();
    return 0;
}

// --write-form-sample: the sample the screen modes aim at, built as bytes. Its
// shape is chosen for what a screen can see, which is why it is not one of the
// samples the headless generator writes beside the tests:
//
//   * four text fields on one page, so a Tab has somewhere to go and "the letters
//     landed in the other field" is a reading rather than an inference -- and so
//     that each of the four screen modes, and each route formime measures, has a
//     field of its own: a value read back out of a field another route already
//     wrote into says nothing about the route;
//   * a checkbox whose checked appearance really paints, because the generated
//     one carries two EMPTY form XObjects -- toggling that changes no pixel at
//     all, and "the ink moved" would be a row that reads 0 for a reason nobody
//     would notice (sec.82's rule about a 0 that has nothing beside it);
//   * a label in the page's own content stream beside each box, so a picture of
//     the page says which field is which without the harness asserting it;
//   * the whole form on one page and inside the first view of it, because a field
//     the harness has to scroll to is a field whose rectangle it has to recompute
//     after the scroll -- and a stale rectangle clicks somewhere else quietly.
//
// handoff-06 H5 added the three choice fields the H2 sample did not carry: a
// closed combo box, an editable combo box, and a multi-select list box. Their
// placement is the measurement rather than a layout choice:
//
//   * the combo sits at the bottom of the page with nothing under it, because the
//     popup PDFium paints hangs BELOW the field and the mode reads its ink there --
//     a text field under the popup would put its own glyphs into that region and a
//     "the list is open" count would then be a count of something else;
//   * the list box is tall enough for four rows at the fit-the-whole-page zoom the
//     form modes settle at, so each row is a band of ink the mode can click, and
//     the rows are short words rather than sentences so no row wraps;
//   * the editable combo sits in the right-hand column at the height of beta, in
//     space nothing else uses, because it is clicked and typed into and its own
//     rectangle must not overlap the combo's popup region.
//
// The /Ff values are the ones tests/formsample.h measured off the library
// (PDF 32000-1 table 229 counting bits from 1): Combo 1<<17, Edit 1<<18,
// MultiSelect 1<<21. A multi-select list's /V is an array (table 226); the
// sample starts with no /V at all, which is the honest empty state, and the
// screen mode's first read asserts that none of the four options is selected.
//
// The object numbers are planned, not counted, and they follow the layout
// tests/formsample.h writes, because that is where widgetBody()'s fixed
// "/P 4 0 R" comes from: the page has to be object 4. Every slot the xref
// covers has to hold something, so the unused ones hold null.
int writeFormSample(const QString &path)
{
    if (QFile::exists(path)) {
        std::cerr << "refusing to write over " << path.toStdString()
                  << " -- this tool makes samples, it does not write documents\n";
        return 1;
    }

    // Top-down on the page (user space grows up, so the first field has the
    // largest y): 70 pt apart, which at the fit-width zoom a 595 pt page opens
    // at leaves every one of them inside the first screenful.
    const QRectF alpha{60, 700, 200, 30};
    const QRectF beta{60, 630, 200, 30};
    const QRectF gamma{60, 560, 200, 30};
    const QRectF delta{60, 490, 200, 30};
    const QRectF agree{60, 430, 16, 16};
    // Four rows at the whole-page zoom; the mode finds them as ink bands inside this
    // rectangle rather than assuming PDFium's row height.
    const QRectF list{60, 300, 200, 88};
    // Nothing below it: the popup is painted under this field and read there.
    const QRectF choice{60, 210, 200, 28};
    // Right-hand column, clear of the popup's x range and of every label.
    const QRectF typeable{330, 620, 180, 28};

    QVector<QByteArray> objects(21);
    objects[1] = "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>";
    objects[2] = "<< /Type /Pages /Kids [4 0 R] /Count 1 >>";
    // /AcroForm exactly as tests/formsample.h writes it -- no /NeedAppearances:
    // the headless sample does not carry it either, and a viewer told to rebuild
    // every appearance is a viewer that may rebuild the checkbox's painted /Yes,
    // which is the pixel difference formcheck reads.
    objects[3] = QByteArray("<< /Type /AcroForm /Fields [5 0 R 6 0 R 7 0 R 8 0 R 9 0 R "
                            "18 0 R 19 0 R 20 0 R]"
                            " /DA (/Helv 12 Tf 0 g ) /DR << /Font << /Helv 11 0 R >> >> >>");
    objects[4] = QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Rotate 0"
                            " /Resources << /Font << /Helv 11 0 R >> >> /Contents 10 0 R"
                            " /Annots [5 0 R 6 0 R 7 0 R 8 0 R 9 0 R 18 0 R 19 0 R 20 0 R] >>");
    // /V empty rather than absent: a field with no /V at all is the shape the
    // reader's own save has to fill in, and the harness's first reading of the
    // file then says nothing about what typing did. The order of /Fields is also
    // the order a Tab walks, so the four text fields come before the checkbox.
    objects[5] = FormSampleFile::widgetBody(QStringLiteral("alpha"), "/Tx", alpha,
                                            " /V () /DA (/Helv 12 Tf 0 g )");
    objects[6] = FormSampleFile::widgetBody(QStringLiteral("beta"), "/Tx", beta,
                                            " /V () /DA (/Helv 12 Tf 0 g )");
    objects[7] = FormSampleFile::widgetBody(QStringLiteral("gamma"), "/Tx", gamma,
                                            " /V () /DA (/Helv 12 Tf 0 g )");
    objects[8] = FormSampleFile::widgetBody(QStringLiteral("delta"), "/Tx", delta,
                                            " /V () /DA (/Helv 12 Tf 0 g )");
    objects[9] = FormSampleFile::widgetBody(
        QStringLiteral("agree"), "/Btn", agree,
        " /V /Off /AS /Off /MK << /CA 8 >> /AP << /N << /Off 16 0 R /Yes 17 0 R >> >>");
    // The three choice shapes H5 puts on screen. /V () is the empty answer for both
    // combos: an empty string, which is what a reader's first keypress replaces. The
    // list box carries no /V at all -- an empty selection is the absence of a value,
    // and the array table (226) has no entry that means "nothing selected".
    objects[18] = FormSampleFile::widgetBody(
        QStringLiteral("list"), "/Ch", list,
        " /Ff 2097152 /Opt [(one) (two) (three) (four)] /DA (/Helv 12 Tf 0 g )");
    objects[19] = FormSampleFile::widgetBody(
        QStringLiteral("choice"), "/Ch", choice,
        " /Ff 131072 /V () /Opt [(red) (green) (blue) (amber)] /DA (/Helv 12 Tf 0 g )");
    objects[20] = FormSampleFile::widgetBody(
        QStringLiteral("typeable"), "/Ch", typeable,
        " /Ff 393216 /V () /Opt [(red) (green) (blue) (amber)] /DA (/Helv 12 Tf 0 g )");
    // The labels, to the right of the four boxes and beside the checkbox, in the
    // page's own content stream so they are page ink and not a field's face: a
    // label inside a field would be typed over by the first keystroke. The three
    // new ones sit to the right of their boxes -- outside every rectangle the modes
    // measure, including the combo's popup region, which is the field's own width.
    const QByteArray labels
        = QByteArray("BT /Helv 11 Tf 1 0 0 1 270 710 Tm (alpha) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 270 640 Tm (beta) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 270 570 Tm (gamma) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 270 500 Tm (delta) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 90 434 Tm (agree) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 270 380 Tm (list) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 270 218 Tm (choice) Tj ET\n"
                     "BT /Helv 11 Tf 1 0 0 1 520 630 Tm (typeable) Tj ET\n");
    objects[10] = QByteArray("<< /Length ") + QByteArray::number(labels.size())
        + QByteArray(" >>\nstream\n") + labels + QByteArray("endstream");
    objects[11] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    for (int number : {12, 13, 14, 15})
        objects[number] = "null";
    // The two states of the checkbox. /Off paints nothing; /Yes fills its whole
    // 16x16 box, which is the pixel difference the screen mode reads.
    objects[16] = QByteArray("<< /Type /XObject /Subtype /Form /BBox [0 0 16 16] /Resources << >>"
                             " /Length 0 >>\nstream\n\nendstream");
    const QByteArray checked = "0 g 0 0 16 16 re f";
    objects[17] = QByteArray("<< /Type /XObject /Subtype /Form /BBox [0 0 16 16] /Resources << >>"
                             " /Length ")
        + QByteArray::number(checked.size()) + QByteArray(" >>\nstream\n") + checked
        + QByteArray("\nendstream");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "could not write " << path.toStdString() << '\n';
        return 1;
    }
    const QByteArray bytes = FormSampleFile::assemblePdf(objects, 1);
    file.write(bytes);
    file.close();

    // A writer that reports success is not evidence that the file opens, so the
    // bytes just written are read straight back with the library the reader
    // itself uses, and the fields it answers with are printed here. Initialised
    // for the same reason --fields is: without it the open appears to work and
    // the page load does not.
    FPDF_InitLibrary();
    FPDF_DOCUMENT check = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    if (!check) {
        std::cerr << "wrote " << bytes.size() << " bytes to " << path.toStdString()
                  << " but it does not open: last error " << FPDF_GetLastError() << '\n';
        FPDF_DestroyLibrary();
        return 1;
    }
    std::cout << "wrote " << bytes.size() << " bytes to " << path.toStdString() << '\n';
    std::cout << "it opens: pages=" << FPDF_GetPageCount(check)
              << " formtype=" << FPDF_GetFormType(check) << '\n';
    FPDF_CloseDocument(check);
    FPDF_DestroyLibrary();
    // One line per box rather than a sentence that has to be edited when a field
    // is added: the eight rectangles above are the claim, and this is the same
    // claim read back, so a field placed off the page or under another one shows
    // up here before a screen round wastes itself on it.
    const QVector<QPair<const char *, QRectF>> boxes{
        {"alpha", alpha}, {"beta", beta}, {"gamma", gamma}, {"delta", delta}, {"agree", agree},
        {"list", list}, {"choice", choice}, {"typeable", typeable}};
    for (const auto &box : boxes) {
        std::cout << "box " << box.first << '=' << box.second.x() << ',' << box.second.y() << ' '
                  << box.second.width() << 'x' << box.second.height() << '\n';
    }
    std::cout << "page=595x842 rotate=0\n";
    return 0;
}

// --write-signed-sample: the signed document handoff-07 I2 puts on a screen.
//
// The bytes come from tests/formsample.h's writeSigned() -- the same generator the headless
// sec.122 assertions are pinned against -- rather than from a second writer written here. The
// reason is the specific claim the two screen modes make: a field-only save leaves the bytes a
// signature covers where they are, and that claim is only testable if the ranges the file was
// built with are the ones the harness reads back. A second xref-and-hole writer would be a
// second opinion about the offsets, and the two would drift the way assemblePdf's note at the
// top of that header says they would.
//
// One text field, one /Sig field whose /V names a signature dictionary, one signature, no
// /Lock. The lock is left out on purpose: what these modes judge is the incremental save and
// the DocMDP door, and a field sealed by /Lock and a field refused by P=1 are the same nothing
// on screen -- so a locked sample would make the P=1 verdict about a mechanism this run is not
// asking about. (sec.122 measured that PDFium does not enforce /Lock at all; DocumentTab hands
// the names to the form layer itself.)
//
// `--docmdp 1|2|3` certifies it: /Perms /DocMDP in the catalog pointing at the signature
// dictionary, whose /Reference carries the same /P, which is how PDF 32000-1 sec.12.8.2.2
// states a certification signature. With no flag the file is signed but NOT certified -- field
// fillable, page changes allowed -- and that is the shape -Mode signedsave's control rotates
// on, so the same generator with a different flag is the only difference between a run that
// must hold its bytes and one that must not.
//
// Nothing here is a real PKCS#7 blob: /Contents is a hex placeholder, so the "signature" is a
// shape, not a claim about cryptography. That is all either reader needs -- the count comes from
// the /V (sec.122's rule about what a signature is), and this version of the program says out
// loud that it does not verify the crypto.
int writeSignedSample(const QString &path, int docMdp)
{
    if (QFile::exists(path)) {
        std::cerr << "refusing to write over " << path.toStdString()
                  << " -- this tool makes samples, it does not write documents\n";
        return 1;
    }
    QString error;
    if (!FormSampleFile::writeSigned(path, docMdp, 1, false, &error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }

    // The same read-back discipline as --write-form-sample: a writer that reports success is
    // not evidence that the file opens, and here the numbers printed below are the ones the
    // screen harness compares against after a save, so they have to come off the bytes on disk
    // rather than off the strings that built them. FPDF_InitLibrary() first, for the reason
    // --fields spells out: without it the document appears to load and the page load takes the
    // process down without a word.
    FPDF_InitLibrary();
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    if (!document) {
        std::cerr << "wrote a sample to " << path.toStdString()
                  << " but it does not open: last error " << FPDF_GetLastError() << '\n';
        FPDF_DestroyLibrary();
        return 1;
    }
    const qint64 written = QFileInfo(path).size();
    std::cout << "wrote " << written << " bytes to " << path.toStdString() << '\n';
    std::cout << "it opens: pages=" << FPDF_GetPageCount(document)
              << " formtype=" << FPDF_GetFormType(document)
              << " signatures=" << FPDF_GetSignatureCount(document) << '\n';
    // Asked of the library rather than printed from the generator: FPDF_GetSignatureCount counts
    // signature FIELDS, so this line says "the field carries a /V" only if the count is one and
    // the range below is four numbers -- a placeholder field with no /V would answer one here and
    // an empty range, and sec.122 is exactly the measurement that told those two apart.
    FPDF_CloseDocument(document);
    const QVector<int> range = FormSampleFile::signatureByteRange(path, 0);
    if (range.size() != 4) {
        std::cerr << "PDFium answers " << range.size()
                  << " numbers for signature 0's /ByteRange, not the four the shape needs\n";
        FPDF_DestroyLibrary();
        return 1;
    }
    std::cout << "byterange: " << range.at(0) << ' ' << range.at(1) << ' ' << range.at(2) << ' '
              << range.at(3) << '\n';
    // The arithmetic the harness will redo in PowerShell, printed so a disagreement about the
    // spans is caught here rather than inside a screen run: the covered bytes are the two
    // ranges concatenated, and they have to total the file minus the /Contents hole.
    const qint64 covered = qint64(range.at(1)) + qint64(range.at(3));
    std::cout << "covered: " << covered << " of " << written << " bytes (the hole is "
              << written - covered << ")\n";

    FPDF_DOCUMENT again = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    const int permission
        = again ? FPDFSignatureObj_GetDocMDPPermission(FPDF_GetSignatureObject(again, 0)) : 0;
    if (again)
        FPDF_CloseDocument(again);
    // The permission the file carries, not the one that was asked for. A --docmdp that reached
    // the dictionary as something PDFium cannot read would otherwise show up as the screen
    // round behaving oddly, with no line here to point at.
    std::cout << "docmdp: " << permission << " (asked for " << docMdp << ")\n";
    FPDF_DestroyLibrary();
    if (permission != docMdp) {
        std::cerr << "asked for /P " << docMdp << " and the library reads " << permission
                  << " out of the file this wrote\n";
        return 1;
    }
    // The two rectangles the modes aim at, in the page's own user space -- printed from the
    // geometry header used to build them, and cross-checked against --fields by the harness.
    const FormSampleFile::SignedGeometry sample;
    std::cout << "box name=" << sample.textField.x() << ',' << sample.textField.y() << ' '
              << sample.textField.width() << 'x' << sample.textField.height() << '\n';
    std::cout << "box sig1=" << sample.signature.x() << ',' << sample.signature.y() << ' '
              << sample.signature.width() << 'x' << sample.signature.height() << '\n';
    std::cout << "page=595x842 rotate=0\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2) {
        std::cerr << "usage: pdfprobe <file.pdf> [samples] [viewport width]\n"
                     "       pdfprobe --fields <file.pdf>\n"
                     "       pdfprobe --write-form-sample <path.pdf>\n"
                     "       pdfprobe --write-signed-sample <path.pdf> [--docmdp 0|1|2|3]\n";
        return 2;
    }

    // The subcommands answer one question each and leave before the timing pass
    // below, which renders pages nobody asked about.
    const QString verb = arguments.at(1);
    if (verb.startsWith(QLatin1String("--"))) {
        if (arguments.size() < 3) {
            std::cerr << verb.toStdString() << " needs a path\n";
            return 2;
        }
        if (verb == QLatin1String("--fields"))
            return dumpFields(arguments.at(2));
        if (verb == QLatin1String("--write-form-sample"))
            return writeFormSample(arguments.at(2));
        if (verb == QLatin1String("--write-signed-sample")) {
            // The flag is read strictly: an unparseable /P must not arrive as the zero that means
            // "no certification signature", because a screen round aimed at the DocMDP door would
            // then be measuring an unsigned file and every one of its verdicts would be about
            // something else.
            int docMdp = 0;
            if (arguments.size() > 3) {
                if (arguments.at(3) != QLatin1String("--docmdp") || arguments.size() < 5) {
                    std::cerr << "--write-signed-sample takes --docmdp 0|1|2|3 after the path\n";
                    return 2;
                }
                bool ok = false;
                docMdp = arguments.at(4).toInt(&ok);
                if (!ok) {
                    std::cerr << "--docmdp wants a number, not [" << arguments.at(4).toStdString()
                              << "]\n";
                    return 2;
                }
            }
            return writeSignedSample(arguments.at(2), docMdp);
        }
        std::cerr << "unknown subcommand " << verb.toStdString() << '\n';
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
