#include "pdfiumbackend.h"

#include "../core/stagingcleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>
#include <QUuid>

// Before fpdfview.h, which includes windows.h itself (line 26 of the release's
// header): once that has run, NOMINMAX is too late and numeric_limits::max() is
// a macro call. The PDFium headers and windows.h have to share one inclusion.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <fpdf_formfill.h>
#include <fpdf_fwlevent.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <QDateTime>
#include <QTimeZone>

#include <limits>

namespace {

// One lock for every FPDF call in this process, and the library's own lifetime
// counted on top of it. Qt PDF has the same shape (pagerenderer.h records what
// it is for); this is the backend's own copy now that the calls are ours.
QRecursiveMutex &pdfiumMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}

int &pdfiumUsers()
{
    static int users = 0;
    return users;
}

void acquirePdfium()
{
    QMutexLocker locker(&pdfiumMutex());
    if (pdfiumUsers()++ == 0)
        FPDF_InitLibrary();
}

void releasePdfium()
{
    QMutexLocker locker(&pdfiumMutex());
    if (--pdfiumUsers() == 0)
        FPDF_DestroyLibrary();
}

// The file PDFium reads through, with the one property that makes R7 worth the
// trouble: the handle does not stop DeleteFileW or ReplaceFileW.
struct PdfFileHandle
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    FPDF_FILEACCESS access{};

    ~PdfFileHandle() { close(); }

    void close()
    {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }
};

// Returns 1 on success, 0 on any failure -- the shape FPDF_FILEACCESS asks for.
// PDFium only ever asks for positions and sizes inside m_FileLen.
int readBlock(void *param, unsigned long position, unsigned char *buffer, unsigned long size)
{
    auto *file = static_cast<PdfFileHandle *>(param);
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(position);
    if (!SetFilePointerEx(file->handle, offset, nullptr, FILE_BEGIN))
        return 0;

    unsigned long done = 0;
    while (done < size) {
        DWORD got = 0;
        if (!ReadFile(file->handle, buffer + done, size - done, &got, nullptr) || got == 0)
            return 0;
        done += got;
    }
    return 1;
}

// A few pages held open, oldest let go. Qt PDF keeps its pages open and that is
// part of why its per-page render figure is lower than a probe that loads and
// closes a page every time (docs/pdfium-plan.md sec.5.2, confound 1). Four is
// what the text caches above this use, and enough for the two pages a drag
// across a page boundary alternates between.
constexpr int kPagesHeld = 4;

// ---- the space a box is reported in ----
//
// PDFium's text and link rectangles are in PDF user space: the origin is the
// page's bottom-left corner and y grows upwards. Measured on this machine, on
// tests/fixtures/text.pdf -- a 595x842 page whose only line sits near the top
// answers FPDFText_GetRect with left 60.10, top 779.18, right 200.40, bottom
// 761.74. document.h reports points from the page's top-left corner, which is
// what every layout calculation and every drawn highlight in this program uses,
// so no box leaves this file without passing through here.
QRectF fromUserSpace(double left, double top, double right, double bottom, double pageHeight)
{
    return QRectF(left, pageHeight - top, right - left, top - bottom).normalized();
}

// The page's line boxes: one rectangle per run of characters that share a line
// and a font setting (fpdf_text.h on FPDFText_CountRects). A two-column page
// therefore gives two boxes per row, which is what a drawn highlight wants.
QVector<QRectF> lineBoxes(FPDF_TEXTPAGE text, double pageHeight)
{
    QVector<QRectF> boxes;
    const int count = FPDFText_CountRects(text, 0, FPDFText_CountChars(text));
    boxes.reserve(qMax(0, count));
    for (int index = 0; index < count; ++index) {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
        if (!FPDFText_GetRect(text, index, &left, &top, &right, &bottom))
            continue;
        boxes.append(fromUserSpace(left, top, right, bottom, pageHeight));
    }
    return boxes;
}

// The line a vertical position belongs to. Zero distance inside the line's own
// band, so a point that is on a line always beats one that is merely near it.
int nearestLine(const QVector<QRectF> &lines, double y)
{
    int nearest = -1;
    double best = std::numeric_limits<double>::max();
    for (int line = 0; line < lines.size(); ++line) {
        const QRectF &box = lines.at(line);
        const double distance =
            y < box.top() ? box.top() - y : (y > box.bottom() ? y - box.bottom() : 0.0);
        if (distance < best) {
            best = distance;
            nearest = line;
        }
    }
    return nearest;
}

QRectF characterBox(FPDF_TEXTPAGE text, int index, double pageHeight)
{
    double left = 0;
    double right = 0;
    double bottom = 0;
    double top = 0;
    // The argument order is the header's: left, right, bottom, top.
    if (!FPDFText_GetCharBox(text, index, &left, &right, &bottom, &top))
        return {};
    return fromUserSpace(left, top, right, bottom, pageHeight);
}

// Whether a character comes before a point, in the page's reading order.
//
// The vertical position alone does not say which line a character is on: a space
// has a box of almost no height sitting on the baseline, so it compares as being
// below the point whenever the point is above the baseline -- inside the same
// line of text. So both the character and the point are put on their nearest
// line, and only when they are the same line does the horizontal position
// decide.
//
// A character with no geometry at all is answered by the last one before it that
// has a box. Measured on tests/fixtures/table.pdf: a line break is two of them
// (U+000D then U+000A), each zero-width and each placed at the end of the line
// it breaks, and a space can come back zero-width too. A break is before the
// point exactly when the end of its line is -- and a comparison that answered for
// the break itself would not be monotone, which is what a bisection needs.
bool characterIsBefore(FPDF_TEXTPAGE text, double pageHeight, const QVector<QRectF> &lines,
                       int index, int textLength, QPointF point)
{
    if (index < 0)
        return true;
    if (index >= textLength)
        return false;

    QRectF box = characterBox(text, index, pageHeight);
    while (box.isNull() && index > 0) {
        --index;
        box = characterBox(text, index, pageHeight);
    }
    if (box.isNull())
        return true;

    const int characterLine = nearestLine(lines, box.center().y());
    const int pointLine = nearestLine(lines, point.y());
    if (characterLine >= 0 && pointLine >= 0 && characterLine != pointLine)
        return characterLine < pointLine;
    return box.center().x() < point.x();
}

// How far outside a glyph the pointer still counts as being over the line, in
// points. About the width of a space at ordinary text sizes, so that a pointer
// in the gap between two words is over the line while one in the margin is not
// -- and a two-point tolerance would make the cursor flicker back to an arrow in
// every word space, which reads as the program being unsure.
constexpr double kTextHitTolerance = 4.0;

// The text of a range of the page, in the same index space as the boxes above.
//
// FPDFText_GetText writes UCS-2 values and a terminator, so the string it hands
// back is one unit shorter than the number it reports. It skips characters with
// no UCS-2 representation, which is why the length of a page's text can come out
// below FPDFText_CountChars -- and why the index a caller gets back is a place in
// this string, which is what document.h promises.
QString textRange(FPDF_TEXTPAGE text, int start, int count)
{
    if (count <= 0)
        return {};
    QVector<char16_t> buffer(count + 1);
    const int written = FPDFText_GetText(text, start, count,
                                         reinterpret_cast<unsigned short *>(buffer.data()));
    if (written <= 0)
        return {};
    return QString::fromUtf16(buffer.constData(), written - 1);
}

// ---- strings PDFium hands back as UTF-16LE ----
//
// FPDFBookmark_GetTitle, FPDF_GetMetaText and FPDF_GetPageLabel all have the same
// shape: ask once with a null buffer for the length in bytes including the
// terminator, then ask again into a buffer that size. The headers report an
// absent value as 0 bytes and an empty one as 2, so <= 2 means "nothing".
template <typename Query>
QString utf16String(Query query)
{
    const unsigned long bytes = query(nullptr, 0);
    if (bytes <= 2)
        return {};

    QVector<char16_t> buffer(int(bytes / 2) + 1);
    const unsigned long written = query(buffer.data(), static_cast<unsigned long>(
                                                             buffer.size() * sizeof(char16_t)));
    if (written <= 2)
        return {};

    const int units = qMin<int>(int(written / 2), int(buffer.size())) - 1;
    QString text = QString::fromUtf16(buffer.constData(), units);
    // A PDF string may carry a byte order mark; the value it encodes does not.
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    return text;
}

QString bookmarkTitle(FPDF_BOOKMARK bookmark)
{
    return utf16String([bookmark](void *buffer, unsigned long length) {
        return FPDFBookmark_GetTitle(bookmark, buffer, length);
    });
}

QString metaText(FPDF_DOCUMENT document, const char *key)
{
    return utf16String([document, key](void *buffer, unsigned long length) {
        return FPDF_GetMetaText(document, key, buffer, length);
    });
}

// The address a URI action points at -- and this one is a byte string, not
// UTF-16. fpdf_doc.h says so while saying it cannot promise what the bytes are:
// "the |buffer| may contain badly encoded data. The caller should validate the
// output. e.g. check to see if it is UTF-8." A URI is ASCII in the specification
// and UTF-8 in the wild, so fromUtf8 is the validation that keeps a stray byte
// from becoming a stray path segment; it answers U+FFFD for anything else.
QString uriOf(FPDF_DOCUMENT document, FPDF_ACTION action)
{
    const unsigned long bytes = FPDFAction_GetURIPath(document, action, nullptr, 0);
    if (bytes <= 1)
        return {};

    QVector<char> buffer(int(bytes) + 1);
    const unsigned long written = FPDFAction_GetURIPath(
        document, action, buffer.data(), static_cast<unsigned long>(buffer.size()));
    if (written <= 1)
        return {};

    return QString::fromUtf8(buffer.constData(),
                             qMin<int>(int(written) - 1, int(buffer.size()) - 1));
}

// A PDF date is a string of PDF's own making -- "D:YYYYMMDDHHmmSSOHH'mm'", with
// everything after the year optional (PDF 32000-1, §14.3.3, Table 231) -- and
// PDFium hands it over exactly as the file wrote it, where Qt PDF parsed it first.
//
// A whole date or nothing: "D:2026" is not shown as 1 January, because the file
// did not say January. That is the rule the Qt backend recorded in its own
// dateOf(): a missing date beats an invented one, but a date the file did state
// and this program threw away is a Properties dialog with a blank line for no
// reason.
QDateTime pdfDate(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.startsWith(QLatin1String("D:")))
        text.remove(0, 2);

    QString digits;
    int position = 0;
    while (position < text.size() && text.at(position).isDigit())
        digits.append(text.at(position++));
    if (digits.size() < 8)
        return {};

    const QDate date(digits.mid(0, 4).toInt(), digits.mid(4, 2).toInt(),
                     digits.mid(6, 2).toInt());
    if (!date.isValid())
        return {};
    const int hour = digits.size() >= 10 ? digits.mid(8, 2).toInt() : 0;
    const int minute = digits.size() >= 12 ? digits.mid(10, 2).toInt() : 0;
    const int second = digits.size() >= 14 ? digits.mid(12, 2).toInt() : 0;
    const QTime time(hour, minute, second);
    if (!time.isValid())
        return {};

    if (position >= text.size())
        return QDateTime(date, time); // no zone written: the clock of wherever it was made

    const QChar mark = text.at(position);
    if (mark == QLatin1Char('Z'))
        return QDateTime(date, time, QTimeZone::UTC);
    if (mark != QLatin1Char('+') && mark != QLatin1Char('-'))
        return QDateTime(date, time);

    ++position;
    QString zone;
    while (position < text.size() && text.at(position).isDigit())
        zone.append(text.at(position++));
    // The offset may be written HHmm, HH'mm' or HH alone; in every case the hours
    // are the first two digits and the minutes whatever follows them.
    const int hours = zone.left(2).toInt();
    const int minutes = zone.size() > 2 ? zone.mid(2, 2).toInt() : 0;
    const int offset = (hours * 3600 + minutes * 60) * (mark == QLatin1Char('-') ? -1 : 1);
    return QDateTime(date, time, QTimeZone(offset));
}

// A bookmark tree is data from a file, and fpdf_doc.h puts cycles on the caller:
// "the caller is responsible for handling circular bookmark references, as may
// arise from malformed documents". Both numbers are far above any document
// anybody has read, and a walk that reaches one stops instead of spinning.
constexpr int kMaxBookmarkDepth = 128;
constexpr int kMaxBookmarks = 20000;

// Depth-first, in the order a sidebar draws: each sibling in turn, then its
// children. The level is counted from 1, which is what outlineFromRows assembles
// against -- the same rule the Qt read followed, and for the same reason: a
// level of 0 for every row loses the nesting.
void appendBookmarks(FPDF_DOCUMENT document, FPDF_BOOKMARK parent, int depth,
                     QVector<OutlineRow> &rows, int &remaining)
{
    if (depth > kMaxBookmarkDepth || remaining <= 0)
        return;
    for (FPDF_BOOKMARK item = FPDFBookmark_GetFirstChild(document, parent); item && remaining > 0;
         item = FPDFBookmark_GetNextSibling(document, item)) {
        OutlineRow entry;
        entry.title = bookmarkTitle(item);
        const FPDF_DEST destination = FPDFBookmark_GetDest(document, item);
        entry.page = destination ? FPDFDest_GetDestPageIndex(document, destination) : -1;
        entry.level = depth;
        rows.append(entry);
        --remaining;
        appendBookmarks(document, item, depth + 1, rows, remaining);
    }
}

// ---- the form layer's callbacks ----
//
// FPDF_FORMFILLINFO is a C struct of function pointers with no client-data slot,
// so the standard embedder trick applies: put it first in a struct of our own and
// hang the pointer off the end. `base` is the first member of a
// standard-layout struct, which is what makes the cast below (from the pointer
// PDFium hands the callback back to the enclosing FormFillInfo) more than a
// guess -- and it is a layout promise the release's own header relies on, since
// every one of these callbacks is called through it.
//
// The member is not called `interface`: windows.h is in this translation unit, and
// the rpc header family that comes with it defines `interface` as `struct`, which
// would turn this declaration into a syntax error several lines away from the cause
// (AGENTS.md, the same trap as `small`).
struct FormFillInfo
{
    FPDF_FORMFILLINFO base;
    void *self = nullptr;
};

// FPDFPage_HasFormFieldAtPoint's answer, and FPDF_FORMFIELD_* from the same header.
FormFill::Field fieldOf(int pdfiumType)
{
    switch (pdfiumType) {
    case FPDF_FORMFIELD_PUSHBUTTON:
        return FormFill::Field::PushButton;
    case FPDF_FORMFIELD_CHECKBOX:
        return FormFill::Field::CheckBox;
    case FPDF_FORMFIELD_RADIOBUTTON:
        return FormFill::Field::RadioButton;
    case FPDF_FORMFIELD_COMBOBOX:
        return FormFill::Field::ComboBox;
    case FPDF_FORMFIELD_LISTBOX:
        return FormFill::Field::ListBox;
    case FPDF_FORMFIELD_TEXTFIELD:
        return FormFill::Field::TextField;
    case FPDF_FORMFIELD_SIGNATURE:
        return FormFill::Field::Signature;
    default:
        return FormFill::Field::None;
    }
}


// FFI_SetCursor's int. The release names no constants for it -- its comment says "see
// Flags for Cursor type" and nothing in the package's headers defines them -- so this
// table is only as wide as what has been read on this machine. A scratch build of nine
// fields (a text field, five button and three choice variants) recorded which shape each
// one asked for as it took focus: the text field and the combobox both asked 3, and the
// listbox asked 0. Those two are therefore IBeam and Arrow; anything else PDFium may ask
// for on a build or a field this program has not put a cursor to yet comes back as an
// arrow rather than as a guess, because the hover shape does not depend on this -- it
// comes from the hit test, which is measured.
FormFill::Pointer pointerOf(int pdfiumCursor)
{
    switch (pdfiumCursor) {
    case 0:
        return FormFill::Pointer::Arrow;
    case 3:
        return FormFill::Pointer::IBeam;
    default:
        return FormFill::Pointer::Arrow;
    }
}

// Qt's modifiers to FWL_EVENTFLAG_*, which is the numbering FORM_On* asks for.
int formEventFlags(Qt::KeyboardModifiers modifiers)
{
    int flags = 0;
    if (modifiers.testFlag(Qt::ShiftModifier))
        flags |= FWL_EVENTFLAG_ShiftKey;
    if (modifiers.testFlag(Qt::ControlModifier))
        flags |= FWL_EVENTFLAG_ControlKey;
    if (modifiers.testFlag(Qt::AltModifier))
        flags |= FWL_EVENTFLAG_AltKey;
    return flags;
}

// Qt::Key to the FWL_VKEY_ code of the same key. PDFium reads the Windows
// virtual-key numbering (fpdf_fwlevent.h's FWL_VKEY_* are those values), and the
// two agree for letters and digits -- which is why this is a list of the rest
// rather than a table of everything. 0 for a key PDFium has no name for: the
// caller then lets the key go where it would have gone before the form existed.
int formVirtualKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Backspace:
        return FWL_VKEY_Back;
    case Qt::Key_Tab:
        return FWL_VKEY_Tab;
    case Qt::Key_Clear:
        return FWL_VKEY_Clear;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return FWL_VKEY_Return;
    case Qt::Key_Escape:
        return FWL_VKEY_Escape;
    case Qt::Key_Space:
        return FWL_VKEY_Space;
    case Qt::Key_PageUp:
        return FWL_VKEY_Prior;
    case Qt::Key_PageDown:
        return FWL_VKEY_Next;
    case Qt::Key_End:
        return FWL_VKEY_End;
    case Qt::Key_Home:
        return FWL_VKEY_Home;
    case Qt::Key_Left:
        return FWL_VKEY_Left;
    case Qt::Key_Up:
        return FWL_VKEY_Up;
    case Qt::Key_Right:
        return FWL_VKEY_Right;
    case Qt::Key_Down:
        return FWL_VKEY_Down;
    case Qt::Key_Insert:
        return FWL_VKEY_Insert;
    case Qt::Key_Delete:
        return FWL_VKEY_Delete;
    default:
        break;
    }
    // The Latin letters and the main-row digits are the same numbers in both, and
    // Qt reports an uppercase letter as Key_A plus ShiftModifier, so no case
    // adjustment is needed here -- the character itself arrives through OnChar.
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return FWL_VKEY_A + (qtKey - Qt::Key_A);
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return FWL_VKEY_0 + (qtKey - Qt::Key_0);
    return 0;
}

// The value of a field, as the form layer sees it.
//
// Not FPDFAnnot_GetStringValue(annot, "/V"), which answers 2 bytes -- the empty
// string -- on the same file that this reads back as "typed on page 1". Measured
// on field-flat.pdf, and it is the asymmetry docs/pdfium-plan.md sec.4.2 wrote as
// unexplained: the form-aware read resolves the field's value the way the widget
// that draws it does, the raw dictionary read of the widget annotation does not.
QString fieldValue(FPDF_FORMHANDLE form, FPDF_ANNOTATION annotation)
{
    if (!annotation)
        return {};
    QVector<FPDF_WCHAR> buffer(256);
    const unsigned long bytes
        = FPDFAnnot_GetFormFieldValue(form, annotation, buffer.data(),
                                      static_cast<unsigned long>(buffer.size() * sizeof(FPDF_WCHAR)));
    if (bytes <= 2)
        return {};
    const int units = qMin<int>(int(bytes / 2), int(buffer.size())) - 1;
    // FPDF_WCHAR is unsigned short, which is what QString::fromUtf16 wants.
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()), units);
}

// A field's own name, which is what a value is remembered against. A widget and
// its parent field share the name, and it does not move when the page it is on
// moves -- which matters because a pending 删除页面 has already shifted page
// numbers by the time a reader looks at a field on a later page.
QString fieldName(FPDF_FORMHANDLE form, FPDF_ANNOTATION annotation)
{
    if (!annotation)
        return {};
    QVector<FPDF_WCHAR> buffer(256);
    const unsigned long bytes = FPDFAnnot_GetFormFieldName(
        form, annotation, buffer.data(), static_cast<unsigned long>(buffer.size() * sizeof(FPDF_WCHAR)));
    if (bytes <= 2)
        return {};
    const int units = qMin<int>(int(bytes / 2), int(buffer.size())) - 1;
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()), units);
}

// The rectangle a widget annotation occupies, in the page's own user space.
QRectF fieldRect(FPDF_ANNOTATION annotation)
{
    FS_RECTF rect{};
    if (!annotation || !FPDFAnnot_GetRect(annotation, &rect))
        return {};
    // FS_RECTF is left/top/right/bottom in the same bottom-left-origin space
    // everything here uses, so `top` is the larger y and the height is top - bottom.
    return QRectF(rect.left, rect.bottom, rect.right - rect.left, rect.top - rect.bottom);
}

// Everything the PDFium form callbacks are allowed to touch, for one document.
//
// It is a struct of its own rather than a few more fields on PdfiumBackend::Impl
// because of how the callbacks find their way home: FPDF_FORMFILLINFO has no
// client-data slot, so the only route from a callback to the object it belongs to
// is the FormFillInfo this file puts around it -- and a free function cannot name
// `PdfiumBackend::Impl`, which is a private nested type. Pointing at something the
// callbacks *can* name keeps that access rule intact and makes the boundary
// explicit instead: anything the library may reach for lives here and nowhere else.
//
// One of these per document, not one per process. A window with two tabs has two
// backends and two form environments, and each callback arrives through the
// FORMFILLINFO of whichever document caused it, so a process-wide singleton would
// attribute one tab's keystroke to the other's page.
struct FormState;

// The timer id is process-wide, because the callback PDFium hands back takes only
// an id and the table it looks this up in has to be reachable from a bare function
// pointer. It is the one piece of the form layer that is not per-document, and it
// holds nothing but (id -> which document's timer to start).
QHash<int, FormState *> &formTimerOwners()
{
    static QHash<int, FormState *> owners;
    return owners;
}

struct FormState
{
    // The form handle, for the one callback that has to tell PDFium about a page it
    // loaded on the document's behalf (FFI_GetPage). load() sets it, close() clears
    // it before the handle is exited.
    FPDF_FORMHANDLE form = nullptr;

    // What PDFium asked for while the last FORM_* call was in flight. Both are
    // collected rather than acted on, because the caller that made that call is the
    // one that knows which pages are on screen. Drained after every routed event by
    // takeRepaints() and takeRequestedPointer().
    QVector<FormFill::Repaint> repaints;
    FormFill::Pointer pendingPointer = FormFill::Pointer::Arrow;

    // The pages this document is holding, both ways round. holdPage() maintains it
    // beside its own LRU; the callbacks need it because FFI_Invalidate is handed a
    // page *handle* and the view needs a page *index*, and FFI_GetPage runs the
    // other direction.
    QHash<int, FPDF_PAGE> pageByIndex;
    QHash<FPDF_PAGE, int> indexByPage;

    // A page FFI_GetPage had to load for PDFium rather than find in the LRU -- for
    // instance when a Tab walks off the end of the held set. The LRU cannot take
    // these: eviction would close a handle PDFium is using mid-gesture, and
    // formDispatchDepth already stops that from happening for the LRU's own pages.
    QVector<FPDF_PAGE> loadedForForm;

    // PDFium's caret blink runs through FFI_SetTimer, whose callback is a bare
    // function pointer taking only the timer id -- and whose return value, kill
    // semantics and "nonzero on success" wording are Win32 SetTimer's, because that is
    // what the interface was copied from. So the answer is SetTimer itself rather than
    // a Qt timer: these same sources are compiled into the two shell DLLs, which are
    // linked /DELAYLOAD:Qt6Core.dll, and a QTimer pulls in a data import that cannot be
    // delay-loaded (LNK1194 on staticMetaObject). A thread-message timer needs a message
    // loop on the thread that started it, which the reader's UI thread has and the
    // preview pane never asks for in the first place: no input, no focus, no caret.
    struct Timer
    {
        TimerCallback callback = nullptr;
    };
    QHash<int, Timer> timers;
    QVector<UINT_PTR> timerIds;

    // Greater than zero while a FORM_* call is on the stack, which is what makes
    // holdPage() let the LRU grow instead of evicting a handle PDFium is using.
    int dispatchDepth = 0;

    // The value each field held before the first event this program routed to it,
    // keyed by page index and field name. This is what differingFieldCount()
    // compares against, and why that question costs nothing on a document whose
    // fields the reader never touched.
    QHash<QString, QString> valueBeforeTouch;

    int indexOfPage(FPDF_PAGE page) const
    {
        const auto found = indexByPage.constFind(page);
        return found == indexByPage.constEnd() ? -1 : found.value();
    }

    void notePage(int index, FPDF_PAGE page)
    {
        pageByIndex.insert(index, page);
        indexByPage.insert(page, index);
    }

    void forgetPage(FPDF_PAGE page)
    {
        indexByPage.remove(page);
        for (auto it = pageByIndex.begin(); it != pageByIndex.end(); ++it) {
            if (it.value() == page) {
                pageByIndex.erase(it);
                break;
            }
        }
    }

    // Everything, on the way out. The caller has already let PDFium go.
    void clear()
    {
        for (const UINT_PTR id : std::as_const(timerIds)) {
            KillTimer(nullptr, id);
            formTimerOwners().remove(int(id));
        }
        timerIds.clear();
        timers.clear();
        repaints.clear();
        pendingPointer = FormFill::Pointer::Arrow;
        pageByIndex.clear();
        indexByPage.clear();
        loadedForForm.clear();
        valueBeforeTouch.clear();
        dispatchDepth = 0;
    }

};

// ---- the form callbacks ----
//
// Every one of these runs synchronously on the thread that made the FORM_* call,
// which already holds the process-wide PDFium mutex -- so none of them may take it
// again and none may block. FFI_SetTimer's callback is the exception: it arrives
// later, from the event loop, and the code that runs it takes the lock there.

void onInvalidate(FPDF_FORMFILLINFO *self, FPDF_PAGE page, double left, double top, double right,
                  double bottom)
{
    FormState *state = static_cast<FormState *>(reinterpret_cast<FormFillInfo *>(self)->self);
    // The header gives the rectangle in PDF user space and says the implementation
    // "should call FPDF_RenderPageBitmap() for repainting the specified page area".
    // In this program that means telling the view, because the view is the one that
    // knows which band of which page is on screen and what asking for it costs.
    // Measured on field-flat.pdf: focusing the field reported
    // (59,599)-(261,631), the field's own /Rect grown by a point on each side.
    const int index = state->indexOfPage(page);
    if (index < 0)
        return; // a page nothing is holding is a page nothing can show
    // The four numbers come in screen order -- `top` the smaller y, `bottom` the larger
    // -- while the space they name has its origin at the page's bottom-left corner. Measured
    // on field-flat.pdf: a field whose /Rect is [60 600 260 630] reported (59,599,261,631).
    // So the rectangle is normalised by min/max rather than by trusting the names, and a
    // Repaint's userSpace means the same thing a click's coordinates do.
    const QRectF area(qMin(left, right), qMin(top, bottom), qAbs(right - left),
                      qAbs(bottom - top));
    // One entry per page, grown to cover what has been asked for. A single click in
    // a text field reports seven of these (measured), and a keystroke more than one
    // again; keeping them apart would mean seven renders of the same band.
    for (FormFill::Repaint &existing : state->repaints) {
        if (existing.page == index) {
            existing.userSpace = existing.userSpace.united(area);
            return;
        }
    }
    state->repaints.append(FormFill::Repaint{index, area});
}

void onSetCursor(FPDF_FORMFILLINFO *self, int cursorType)
{
    FormState *state = static_cast<FormState *>(reinterpret_cast<FormFillInfo *>(self)->self);
    state->pendingPointer = pointerOf(cursorType);
}

// Where a Win32 thread-message timer lands. The id PDFium was handed is the key, and
// the registry is checked again on every fire: a document can be closed between two
// blinks, and then its FormState is gone -- which is the shape of the crash F10
// recorded for the recognition pool, with a different object left alive.
void CALLBACK formTimerThunk(HWND, UINT, UINT_PTR eventId, DWORD)
{
    const auto owner = formTimerOwners().constFind(int(eventId));
    if (owner == formTimerOwners().constEnd() || !owner.value())
        return;
    const auto entry = owner.value()->timers.constFind(int(eventId));
    if (entry == owner.value()->timers.constEnd() || !entry->callback)
        return;
    // The callback is a PDFium call, so it takes the process lock like every other
    // entry point that reaches the library.
    const QMutexLocker locker(&pdfiumMutex());
    entry->callback(int(eventId));
}

void onKillTimer(FPDF_FORMFILLINFO *self, int timerId)
{
    FormState *state = static_cast<FormState *>(reinterpret_cast<FormFillInfo *>(self)->self);
    const auto found = state->timers.constFind(timerId);
    if (found == state->timers.constEnd())
        return;
    KillTimer(nullptr, UINT_PTR(timerId));
    state->timerIds.removeOne(UINT_PTR(timerId));
    state->timers.erase(found);
    formTimerOwners().remove(timerId);
}

int onSetTimer(FPDF_FORMFILLINFO *self, int elapsed, TimerCallback callback)
{
    FormState *state = static_cast<FormState *>(reinterpret_cast<FormFillInfo *>(self)->self);
    if (!callback || elapsed <= 0)
        return 0;
    // hWnd NULL, a TimerProc, and no id of our own: the return value is the identifier
    // to hand back, exactly the contract FFI_SetTimer's header describes. A zero answer
    // would tell PDFium it has no timer, and the caret in a text field would never
    // blink -- measured to be asked for with `elapsed` 500 on the first focus.
    const UINT_PTR id = SetTimer(nullptr, 0, UINT(elapsed), &formTimerThunk);
    if (id == 0)
        return 0;
    FormState::Timer timer;
    timer.callback = callback;
    state->timers.insert(int(id), timer);
    state->timerIds.append(id);
    formTimerOwners().insert(int(id), state);
    return int(id);
}

FPDF_SYSTEMTIME onGetLocalTime(FPDF_FORMFILLINFO *self)
{
    (void)self;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    FPDF_SYSTEMTIME out{};
    out.wYear = now.wYear;
    out.wMonth = now.wMonth;
    out.wDayOfWeek = now.wDayOfWeek;
    out.wDay = now.wDay;
    out.wHour = now.wHour;
    out.wMinute = now.wMinute;
    out.wSecond = now.wSecond;
    out.wMilliseconds = now.wMilliseconds;
    return out;
}

FPDF_PAGE onGetPage(FPDF_FORMFILLINFO *self, FPDF_DOCUMENT document, int index)
{
    FormState *state = static_cast<FormState *>(reinterpret_cast<FormFillInfo *>(self)->self);
    const auto held = state->pageByIndex.constFind(index);
    if (held != state->pageByIndex.constEnd() && held.value())
        return held.value();
    // Not on screen. PDFium asks this when a field's action names another page --
    // a Tab at the end of one page asking for the next -- and the handle it gets
    // back is one it does not own, so this one is remembered and closed at
    // teardown rather than handed to the view's LRU, which would evict it while
    // PDFium is still using it.
    FPDF_PAGE page = FPDF_LoadPage(document, index);
    if (!page)
        return nullptr;
    if (state->form)
        FORM_OnAfterLoadPage(page, state->form);
    state->loadedForForm.append(page);
    state->notePage(index, page);
    return page;
}

int onGetRotation(FPDF_FORMFILLINFO *self, FPDF_PAGE page)
{
    (void)self;
    (void)page;
    // Zero, and that is the consistent answer rather than a default: the click
    // coordinates this class hands FORM_On* come out of FPDF_DeviceToPage in the
    // page's *unrotated* user space -- measured, and formfill.h's own field-rect
    // reads are in that space too -- so a rotation reported here would be applied a
    // second time inside PDFium. Measured also: no FORM_* call in the sequence
    // focus, type, commit ever reached this callback on this build.
    return 0;
}

void onExecuteNamedAction(FPDF_FORMFILLINFO *self, FPDF_BYTESTRING action)
{
    (void)self;
    (void)action;
    // Nothing. A named action is a document command -- Print, SaveAs, SubmitForm,
    // a jump -- and handoff-04 G1 5 puts the reader's own buttons outside what this
    // version responds to. PDFium's field navigation ("Next Field", "Prev Field")
    // does not come through here: Tab is handled inside FORM_OnKeyDown.
}

} // namespace

struct PdfiumBackend::Impl
{
    struct HeldPage
    {
        FPDF_PAGE page = nullptr;
        FPDF_TEXTPAGE text = nullptr;
        quint64 lastUsed = 0;
    };

    // Keeps holdPage() from evicting while a FORM_* call is on the stack: PDFium is
    // using the handles it was given, and FORM_OnBeforeClosePage on one of them
    // mid-call is the bug F10 describes for a different object, with a page handle.
    struct Dispatch
    {
        explicit Dispatch(Impl &owner)
            : impl(owner)
        {
            ++impl.formState.dispatchDepth;
        }
        ~Dispatch() { --impl.formState.dispatchDepth; }

        Impl &impl;
    };

    PdfFileHandle file;
    FPDF_DOCUMENT document = nullptr;
    // The form-fill environment and its interface. Both live as long as the
    // document: fpdf_formfill.h says the FPDF_FORMFILLINFO must stay alive until
    // the handle is closed, and the handle must be exited before the document.
    // Since G1 the eight callbacks the header marks as required for version 1 are
    // filled in (see the block under "the form callbacks" below); the optional ones
    // and everything the header reserves for XFA stay null, because this build has
    // no XFA module (pdfium-plan.md sec.4.3) and no V8, so FFI_GetCurrentPage --
    // which the header says is "required when V8 support is present, otherwise
    // unused" -- is not answered either.
    FormFillInfo formInfo{};
    FPDF_FORMHANDLE form = nullptr;
    QString path;
    QString error;
    bool encrypted = false;

    // ---- what the form layer asked for while the last event was in flight ----
    //
    // The callbacks may only touch this, and they reach it through the FormFillInfo
    // wrapped around the FORMFILLINFO they were registered with.
    FormState formState;

    QHash<int, HeldPage> pages;
    quint64 useCounter = 0;

    // Caller holds the mutex.
    FPDF_PAGE holdPage(int index, bool textNeeded)
    {
        if (!document || index < 0 || index >= FPDF_GetPageCount(document))
            return nullptr;

        auto held = pages.find(index);
        if (held == pages.end()) {
            if (pages.size() >= kPagesHeld && formState.dispatchDepth == 0) {
                auto oldest = pages.begin();
                for (auto it = pages.begin(); it != pages.end(); ++it) {
                    if (it->lastUsed < oldest->lastUsed)
                        oldest = it;
                }
                if (oldest->page)
                    formState.forgetPage(oldest->page);
                if (oldest->page && form)
                    FORM_OnBeforeClosePage(oldest->page, form);
                if (oldest->text)
                    FPDFText_ClosePage(oldest->text);
                if (oldest->page)
                    FPDF_ClosePage(oldest->page);
                pages.erase(oldest);
            }
            HeldPage fresh;
            fresh.page = FPDF_LoadPage(document, index);
            if (!fresh.page)
                return nullptr;
            // fpdf_formfill.h pairs these with FORM_OnBeforeClosePage: the form
            // layer is told which page it is about to draw on.
            if (form)
                FORM_OnAfterLoadPage(fresh.page, form);
            formState.notePage(index, fresh.page);
            held = pages.insert(index, fresh);
        }

        held->lastUsed = ++useCounter;
        if (textNeeded && !held->text)
            held->text = FPDFText_LoadPage(held->page);
        return held->page;
    }

    // The text page holdPage() loaded for a page. Caller holds the mutex and has
    // already called holdPage(index, true); a page it dropped, and a page it never
    // had, both answer null.
    FPDF_TEXTPAGE heldText(int index) const
    {
        const auto held = pages.constFind(index);
        return held == pages.constEnd() ? nullptr : held->text;
    }

    // The annotation the form layer has focused, and which page it is on through
    // `pageIndex`. The caller must close it with FPDFPage_CloseAnnot(); the header
    // says so for FORM_GetFocusedAnnot's answer, and an answer that is not a focus
    // comes back null with the index untouched.
    FPDF_ANNOTATION focusedAnnot(int *pageIndex) const
    {
        if (!form)
            return nullptr;
        int index = -1;
        FPDF_ANNOTATION annotation = nullptr;
        if (!FORM_GetFocusedAnnot(form, &index, &annotation) || !annotation)
            return nullptr;
        if (pageIndex)
            *pageIndex = index;
        return annotation;
    }

    // Which page has the focused field, or -1. The handle it returns through
    // FORM_GetFocusedAnnot is closed here; the caller only wants the number.
    int focusedPageLocked() const
    {
        int page = -1;
        FPDF_ANNOTATION focused = focusedAnnot(&page);
        if (!focused)
            return -1;
        FPDFPage_CloseAnnot(focused);
        return page;
    }

    // Remember what this field held, once, before the first event that could change
    // it. Called *before* dispatching, which is the whole point: after the click the
    // checkbox is already toggled, and the value that has to be compared against is
    // the one the file says.
    void noteFieldBeforeTouch(int page, FPDF_ANNOTATION annotation)
    {
        if (!annotation)
            return;
        const QString name = fieldName(form, annotation);
        if (name.isEmpty())
            return;
        const QString key = QStringLiteral("%1|%2").arg(page).arg(name);
        if (formState.valueBeforeTouch.contains(key))
            return;
        formState.valueBeforeTouch.insert(key, fieldValue(form, annotation));
    }

    // The same question for the field that has focus, which is what a keystroke is
    // about -- and what a Tab has already moved on from by the time this is asked.
    void noteFocusedFieldBeforeTouch()
    {
        int page = -1;
        FPDF_ANNOTATION annotation = focusedAnnot(&page);
        if (!annotation)
            return;
        noteFieldBeforeTouch(page, annotation);
        FPDFPage_CloseAnnot(annotation);
    }

    // How many fields hold something other than what the file says, and the pairs that
// answer it. The value a field holds now, and the one it held before this program's first
    // event aimed at it. The focused widget is asked for its live text, because a
    // value the reader is still typing has not reached the field yet (FormFill's
    // header has the measurement). Answers are UTF-16 → QString pairs the caller
    // compares.
    void currentAndOriginalValues(QVector<QPair<QString, QString>> &out)
    {
        out.reserve(formState.valueBeforeTouch.size());
        for (auto entry = formState.valueBeforeTouch.constBegin();
             entry != formState.valueBeforeTouch.constEnd(); ++entry) {
            const int page = entry.key().section(QLatin1Char('|'), 0, 0).toInt();
            const QString name = entry.key().section(QLatin1Char('|'), 1);
            QString now;
            int focusedPageIndex = -1;
            FPDF_ANNOTATION focused = focusedAnnot(&focusedPageIndex);
            if (focused && focusedPageIndex == page && fieldName(form, focused) == name) {
                QVector<FPDF_WCHAR> buffer(1024);
                const unsigned long bytes = FORM_GetFocusedText(
                    form, focused ? holdPageRef(focusedPageIndex) : nullptr, buffer.data(),
                    static_cast<unsigned long>(buffer.size() * sizeof(FPDF_WCHAR)));
                if (bytes > 2) {
                    const int units
                        = qMin<int>(int(bytes / 2), int(buffer.size())) - 1;
                    now = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()),
                                             units);
                }
                FPDFPage_CloseAnnot(focused);
            } else {
                if (focused)
                    FPDFPage_CloseAnnot(focused);
                FPDF_PAGE pageHandle = holdPageRef(page);
                FPDF_ANNOTATION annotation
                    = pageHandle ? pageField(pageHandle, name) : nullptr;
                now = fieldValue(form, annotation);
                if (annotation)
                    FPDFPage_CloseAnnot(annotation);
            }
            out.append({entry.value(), now});
        }
    }

    // holdPage() through a page index, for the reads that only need the handle.
    FPDF_PAGE holdPageRef(int index)
    {
        return index < 0 ? nullptr : holdPage(index, false);
    }

    // The field of a page with this exact name. A page is walked by annotation index,
    // which is bounded by what is on it, and the walk stops at the first match.
    FPDF_ANNOTATION pageField(FPDF_PAGE page, const QString &name)
    {
        if (!page || name.isEmpty())
            return nullptr;
        const int count = FPDFPage_GetAnnotCount(page);
        for (int index = 0; index < count; ++index) {
            FPDF_ANNOTATION annotation = FPDFPage_GetAnnot(page, index);
            if (!annotation)
                continue;
            if (fieldName(form, annotation) == name)
                return annotation; // the caller closes it
            FPDFPage_CloseAnnot(annotation);
        }
        return nullptr;
    }

    // Caller holds the mutex.
    void close()
    {
        // The timers first and unconditionally: their slots call back into PDFium,
        // and a queued one firing after FPDFDOC_ExitFormFillEnvironment would be a
        // call into a form handle that is gone -- the shape of the crash F10 recorded
        // for the recognition pool, with a different object left alive.
        for (const int id : formState.timers.keys())
            formTimerOwners().remove(id);
        const QVector<FPDF_PAGE> borrowed = formState.loadedForForm;
        formState.clear();
        for (auto &held : pages) {
            if (held.page && form)
                FORM_OnBeforeClosePage(held.page, form);
            if (held.text)
                FPDFText_ClosePage(held.text);
            if (held.page)
                FPDF_ClosePage(held.page);
        }
        pages.clear();
        useCounter = 0;
        // The pages FFI_GetPage loaded for PDFium belong to neither the LRU nor
        // PDFium, so this is the only place they get closed. Before the document, as
        // everything else here is.
        for (FPDF_PAGE page : borrowed)
            FPDF_ClosePage(page);
        if (form) {
            // The state stops naming the handle before it is exited, so a callback
            // that somehow arrives between the two cannot use a handle that is gone.
            formState.form = nullptr;
            FPDFDOC_ExitFormFillEnvironment(form);
            form = nullptr;
        }
        if (document) {
            FPDF_CloseDocument(document);
            document = nullptr;
        }
        file.close();
    }
};
PdfiumBackend::PdfiumBackend()
    : impl(std::make_unique<Impl>())
{
    acquirePdfium();
}

PdfiumBackend::~PdfiumBackend()
{
    {
        QMutexLocker locker(&pdfiumMutex());
        impl->close();
    }
    releasePdfium();
}

Document::Status PdfiumBackend::load(const QString &path, const QString &password)
{
    QMutexLocker locker(&pdfiumMutex());
    impl->close();
    impl->path.clear();
    impl->error.clear();
    impl->encrypted = false;

    // The shares are the point of this function: READ because we read, WRITE
    // because a writer may be regenerating the file under us (the bar above the
    // page exists for that), and DELETE because a save by replace has to be able
    // to move a new file over this one while it is on screen.
    const HANDLE handle =
        CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto tr = [](const char *text) {
            return QCoreApplication::translate("PdfiumBackend", text);
        };
        const DWORD code = GetLastError();
        impl->error =
            (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                ? tr("找不到文件：%1").arg(path)
                : tr("打开 %1 时出错：%2").arg(path, tr("文件无法解析"));
        return Status::FileError;
    }

    LARGE_INTEGER length;
    // FPDF_FILEACCESS carries the length in an unsigned long, which is 32 bits
    // on Windows even in a 64-bit build. A PDF larger than that cannot be handed
    // to this API at all, and refusing here is better than a truncated file.
    if (!GetFileSizeEx(handle, &length)
        || static_cast<unsigned long long>(length.QuadPart)
               > std::numeric_limits<unsigned long>::max()) {
        CloseHandle(handle);
        const auto tr = [](const char *text) {
            return QCoreApplication::translate("PdfiumBackend", text);
        };
        impl->error = tr("打开 %1 时出错：%2").arg(path, tr("文件无法解析"));
        return Status::FileError;
    }

    impl->file.handle = handle;
    impl->file.access.m_FileLen = static_cast<unsigned long>(length.QuadPart);
    impl->file.access.m_GetBlock = &readBlock;
    impl->file.access.m_Param = &impl->file;

    impl->document = FPDF_LoadCustomDocument(&impl->file.access, password.toUtf8().constData());
    if (!impl->document) {
        const unsigned long code = FPDF_GetLastError();
        // The handle goes with the failed load: FPDF_LoadCustomDocument did not
        // take ownership of it (the application keeps the file resources valid
        // while the document is open, and there is no document).
        impl->file.close();

        const auto tr = [](const char *text) {
            return QCoreApplication::translate("PdfiumBackend", text);
        };
        switch (code) {
        case FPDF_ERR_PASSWORD:
            impl->error = tr("此文档已加密，需要密码。");
            return Status::NeedsPassword;
        case FPDF_ERR_FORMAT:
        case FPDF_ERR_PAGE:
            impl->error = tr("这个文件不是有效的 PDF，或者已经损坏。");
            return Status::InvalidFormat;
        case FPDF_ERR_SECURITY:
            impl->error = tr("这个 PDF 使用了不支持的加密方式，无法打开。");
            return Status::InvalidFormat;
        case FPDF_ERR_FILE:
            impl->error = tr("打开 %1 时出错：%2").arg(path, tr("文件无法解析"));
            return Status::FileError;
        default:
            impl->error = tr("打开 %1 时出错：%2")
                              .arg(path, QFileInfo::exists(path) ? tr("文件无法解析")
                                                                : tr("文件不存在"));
            return Status::FileError;
        }
    }

    impl->path = path;
    // The form layer is initialized for every document, not only ones with
    // fields: asking whether there are any costs a parse, and on a document
    // without fields this handle draws nothing and takes no input. Version 1 is
    // the stable interface, and this build of PDFium has no XFA module
    // (pdfium-plan.md sec.4.3).
    //
    // The eight callbacks are the ones fpdf_formfill.h marks as required for
    // version 1. F7-C could leave them all null and still draw, because drawing
    // asks nothing of the embedder; accepting input does, and a null one of those
    // is a call through a null pointer -- which is why the F5 probe never tried it.
    impl->formInfo.base.version = 1;
    impl->formInfo.self = &impl->formState;
    impl->formInfo.base.FFI_Invalidate = &onInvalidate;
    impl->formInfo.base.FFI_SetCursor = &onSetCursor;
    impl->formInfo.base.FFI_SetTimer = &onSetTimer;
    impl->formInfo.base.FFI_KillTimer = &onKillTimer;
    impl->formInfo.base.FFI_GetLocalTime = &onGetLocalTime;
    impl->formInfo.base.FFI_GetPage = &onGetPage;
    impl->formInfo.base.FFI_GetRotation = &onGetRotation;
    impl->formInfo.base.FFI_ExecuteNamedAction = &onExecuteNamedAction;
    impl->form = FPDFDOC_InitFormFillEnvironment(impl->document, &impl->formInfo.base);
    impl->formState.form = impl->form;
    // Pages this backend was holding from a previous file are gone by now (close()
    // ran at the top of load()), so the handle table starts with nothing in it and
    // FFI_GetPage will load rather than find.
    // A non-empty password that opened the document means it was encrypted. There
    // is no cheaper way to ask a PDF that did not need one.
    impl->encrypted = !password.isEmpty();
    return Status::Ready;
}

QString PdfiumBackend::lastError() const
{
    QMutexLocker locker(&pdfiumMutex());
    return impl->error;
}

QString PdfiumBackend::path() const
{
    QMutexLocker locker(&pdfiumMutex());
    return impl->path;
}

void PdfiumBackend::close()
{
    QMutexLocker locker(&pdfiumMutex());
    // Pages, form environment, document and file handle, in that order -- the same
    // teardown load() starts with. impl->path is kept on purpose: path() still
    // answers with the file the tab is about, which is what the in-place save
    // reopens and what the title reads while the page is briefly empty.
    impl->close();
    impl->error.clear();
    impl->encrypted = false;
}

int PdfiumBackend::pageCount() const
{
    QMutexLocker locker(&pdfiumMutex());
    return impl->document ? FPDF_GetPageCount(impl->document) : 0;
}

QSizeF PdfiumBackend::pageSize(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return {};
    FS_SIZEF size{};
    if (!FPDF_GetPageSizeByIndexF(impl->document, page, &size))
        return {};
    // Measured, not assumed (the plan's C6 only compared the first page of each
    // fixture): tests/fixtures/rotated.pdf is stored 300x400 and marked /Rotate
    // 90, and this call answers 400x300 -- the same displayed size Qt PDF
    // reports. Applying the page's rotation here as well would turn a page
    // marked 90 into one marked 180.
    return QSizeF(size.width, size.height);
}

QImage PdfiumBackend::render(int page, QSize pixels, const RenderOptions &options)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document || page < 0 || pixels.isEmpty())
        return {};

    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return {};

    QImage image(pixels, QImage::Format_RGB32);
    if (image.isNull())
        return {};

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(image.width(), image.height(), FPDFBitmap_BGRx,
                                             image.bits(), image.bytesPerLine());
    if (!bitmap)
        return {};
    // PDFium does not fill the buffer it is given; a page that paints nothing
    // would come back as uninitialised memory, which is how the F5 probe first
    // counted the paper itself as ink (docs/pdfium-plan.md sec.3, C8).
    FPDFBitmap_FillRect(bitmap, 0, 0, image.width(), image.height(), 0xFFFFFFFF);

    int rotation = options.rotationDegrees % 360;
    if (rotation < 0)
        rotation += 360;
    const int quarterTurns = rotation == 90 ? 1 : rotation == 180 ? 2 : rotation == 270 ? 3 : 0;

    int startX = 0;
    int startY = 0;
    int sizeX = pixels.width();
    int sizeY = pixels.height();
    if (!options.clip.isNull()) {
        // The page is displayed at scaledSize and the band is a rectangle in
        // that grid; start_x/y name where the page's own origin lands in the
        // bitmap, so the band's top-left is subtracted rather than copied.
        const QSize whole = options.scaledSize.isEmpty() ? pixels : options.scaledSize;
        startX = -options.clip.x();
        startY = -options.clip.y();
        sizeX = whole.width();
        sizeY = whole.height();
    }

    int flags = 0;
    if (options.annotations)
        flags |= FPDF_ANNOT;
    if (options.optimizedForLcd)
        flags |= FPDF_LCD_TEXT;
    // Measured inert at the print size on every fixture this program is tested
    // against (document.h's note on `printing` says which four and by how much --
    // zero). Set because this is the flag the library names for that job, and
    // pinned as inert in documenteditor_smoke so a build that starts honouring it
    // in some new way reddens a row instead of changing paper quietly.
    if (options.printing)
        flags |= FPDF_PRINTING;

    FPDF_RenderPageBitmap(bitmap, handle, startX, startY, sizeX, sizeY, quarterTurns, flags);
    // Field appearances are a second pass, with the same geometry: the form
    // layer owns where a widget's box lands after scaling, clipping and
    // rotation, so it has to be handed exactly what the page render was. This
    // is the whole of F7-C -- fields are drawn, not editable; FPDF_FFLDraw's
    // own comment says widgets are drawn whatever the annotation flag says.
    if (impl->form) {
        FPDF_FFLDraw(impl->form, bitmap, handle, startX, startY, sizeX, sizeY, quarterTurns,
                     flags);
    }
    FPDFBitmap_Destroy(bitmap);
    return image;
}

QString PdfiumBackend::text(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return {};
    if (!impl->holdPage(page, true))
        return {};

    const FPDF_TEXTPAGE text = impl->heldText(page);
    if (!text)
        return {};
    return textRange(text, 0, FPDFText_CountChars(text));
}

QVector<TextRun> PdfiumBackend::textRuns(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    QVector<TextRun> runs;
    if (!impl->document)
        return runs;
    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return runs;

    // One box per line segment, in points from the page's top-left corner -- the
    // same space pageSize() reports, so a caller can put one on a rendered image
    // by scaling.
    const double height = FPDF_GetPageHeightF(handle);
    const int count = FPDFText_CountChars(text);
    for (int index = 0; index < FPDFText_CountRects(text, 0, count); ++index) {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
        if (!FPDFText_GetRect(text, index, &left, &top, &right, &bottom))
            continue;
        runs.append(TextRun{fromUserSpace(left, top, right, bottom, height)});
    }
    return runs;
}

int PdfiumBackend::characterIndexAt(int page, QPointF point) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return -1;
    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return -1;

    const QString pageText = textRange(text, 0, FPDFText_CountChars(text));
    if (pageText.isEmpty())
        return -1;

    // ---- why this is a search rather than a query ----
    //
    // PDFium has a point query -- FPDFText_GetCharIndexAtPos -- and measured on
    // tests/fixtures/text.pdf it answers -1 for five of the nine gaps between the
    // characters of the page's only line, and -1 for every point above the line,
    // below it, past its end or out in the margin (with zero tolerance it answers
    // -1 for all nine). That is no use for a click: a reader clicking a line
    // expects the cursor somewhere on that line, and half a line is not ink.
    //
    // So the index is found by bisection over the character boxes, which are
    // exact and always available -- twelve FPDFText_GetCharBox calls on a page of
    // three thousand characters, and a character box is cheap here in a way it was
    // not through Qt PDF's selection API.
    const double height = FPDF_GetPageHeightF(handle);
    const QVector<QRectF> lines = lineBoxes(text, height);

    // Above the text is its beginning and below it is its end, whatever the
    // horizontal position: the contract in document.h, and where a drag that
    // starts in the margin lands.
    if (!lines.isEmpty()) {
        double top = lines.first().top();
        double bottom = lines.first().bottom();
        for (const QRectF &box : lines) {
            top = qMin(top, box.top());
            bottom = qMax(bottom, box.bottom());
        }
        if (point.y() < top)
            return 0;
        if (point.y() > bottom)
            return pageText.length();
    }

    int low = 0;
    int high = pageText.length();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (characterIsBefore(text, height, lines, middle, pageText.length(), point))
            low = middle + 1;
        else
            high = middle;
    }
    // One past the last character is a legitimate answer, and the useful one: it
    // is where a selection dragged off the end of the text stops, and where a
    // click below the last line puts the cursor. Only a page with no text at all
    // has no answer.
    return low;
}

PageSelection PdfiumBackend::pageSelection(int page, int from, int to) const
{
    QMutexLocker locker(&pdfiumMutex());
    PageSelection result;
    if (!impl->document)
        return result;

    // A backwards or empty range is not a range. Normalising it here would turn
    // a caller's mistake into one character selected at the end it did not mean,
    // which is worse than nothing: nothing is a selection that does not appear,
    // and a wrong character is a selection that appears to be right. The one
    // caller sorts its ends before it gets here, and sorts them because dragging
    // backwards is a real gesture.
    const int start = qMax(0, from);
    const int length = to - start;
    if (length <= 0)
        return result;

    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return result;

    result.text = textRange(text, start, length);
    if (result.text.isEmpty())
        return result;

    const double height = FPDF_GetPageHeightF(handle);
    // FPDFText_CountRects for this range, not the page's: a selection that stops
    // halfway down a line must not be drawn with the rest of it. The rectangle
    // list is rebuilt by every CountRects call, so the GetRect loop below runs
    // against the one it just asked for.
    const int rects = FPDFText_CountRects(text, start, length);
    result.boxes.reserve(qMax(0, rects));
    for (int index = 0; index < rects; ++index) {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
        if (!FPDFText_GetRect(text, index, &left, &top, &right, &bottom))
            continue;
        result.boxes.append(fromUserSpace(left, top, right, bottom, height));
    }
    return result;
}

bool PdfiumBackend::hasTextAt(int page, QPointF point) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return false;

    // ---- the line box, not the characters ----
    //
    // Every character's box would say this exactly. The line box says it well
    // enough and is already in hand: a pointer is over a line when it is beside
    // that line's ink, which is what "the pointer shows an I-beam over text"
    // means -- in the gap between two words, yes; in the margin, no.
    //
    // Only the nearest line, so that a point beside the last line of a page is
    // not over the first line of whatever comes next.
    const double height = FPDF_GetPageHeightF(handle);
    const QVector<QRectF> lines = lineBoxes(text, height);
    const int line = nearestLine(lines, point.y());
    if (line < 0)
        return false;

    const QRectF box = lines.at(line);
    return box.adjusted(-kTextHitTolerance, -kTextHitTolerance, kTextHitTolerance,
                        kTextHitTolerance)
        .contains(point);
}

Outline PdfiumBackend::outline() const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return {};

    QVector<OutlineRow> rows;
    int remaining = kMaxBookmarks;
    appendBookmarks(impl->document, nullptr, 1, rows, remaining);
    return outlineFromRows(rows);
}

QVector<Link> PdfiumBackend::links(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    QVector<Link> found;
    if (!impl->document)
        return found;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return found;

    const double height = FPDF_GetPageHeightF(handle);
    int position = 0;
    FPDF_LINK link = nullptr;
    while (FPDFLink_Enumerate(handle, &position, &link)) {
        Link entry;
        FS_RECTF rect{};
        if (FPDFLink_GetAnnotRect(link, &rect))
            entry.box = fromUserSpace(rect.left, rect.top, rect.right, rect.bottom, height);

        // A URI first, because those are what a link is: an address outside the
        // document, or a place inside it. FPDFAction_GetURIPath answers nothing
        // when the action is not a URI action, and an annotation may carry a
        // destination and no action at all -- which is the internal case, and the
        // common one.
        const FPDF_ACTION action = FPDFLink_GetAction(link);
        const QString uri = action ? uriOf(impl->document, action) : QString();
        if (!uri.isEmpty()) {
            entry.url = QUrl(uri);
        } else {
            const FPDF_DEST destination = FPDFLink_GetDest(impl->document, link);
            if (destination) {
                entry.targetPage = FPDFDest_GetDestPageIndex(impl->document, destination);
                FPDF_BOOL hasX = 0;
                FPDF_BOOL hasY = 0;
                FPDF_BOOL hasZoom = 0;
                FS_FLOAT x = 0;
                FS_FLOAT y = 0;
                FS_FLOAT zoom = 0;
                // Only a /XYZ destination names a place; /Fit and friends name the
                // page and let the reader choose. document.h asks for an invalid
                // point in that case, which is what the default gives.
                if (FPDFDest_GetLocationInPage(destination, &hasX, &hasY, &hasZoom, &x, &y, &zoom)
                    && hasX && hasY)
                    entry.target = QPointF(x, height - y);
            }
        }
        found.append(entry);
    }
    return found;
}

DocMetadata PdfiumBackend::metadata() const
{
    QMutexLocker locker(&pdfiumMutex());
    DocMetadata result;
    if (!impl->document)
        return result;

    // The keys are the dictionary's own names, with no slash: measured against
    // tests/fixtures/info.pdf, which carries all eight, and "/Title" answers 2
    // bytes -- the empty string -- on the same file that "Title" reads back as
    // "XYZPDF Fixture Title". docs/pdfium-plan.md sec.6.2 wrote the call with the
    // slash, and fpdf_doc.h lists the tags without it.
    result.title = metaText(impl->document, "Title");
    result.author = metaText(impl->document, "Author");
    result.subject = metaText(impl->document, "Subject");
    result.keywords = metaText(impl->document, "Keywords");
    result.producer = metaText(impl->document, "Producer");
    result.creator = metaText(impl->document, "Creator");
    result.creationDate = pdfDate(metaText(impl->document, "CreationDate"));
    result.modificationDate = pdfDate(metaText(impl->document, "ModDate"));
    result.encrypted = impl->encrypted;
    return result;
}

// ===========================================================================
// FormFill -- filling a field, and getting the filled document out to a file
// ===========================================================================

namespace {

// A point in the page's pixel grid, turned into the space PDFium's form layer asks
// for. The frame is the one the *displayed* pixels were made with, which is what
// FPDF_DeviceToPage's "you must make sure start_x, start_y, size_x, size_y and
// rotate have exactly same values as you used in the FPDF_RenderPage() function
// call" means; start is 0,0 because the grid is the whole page, not the band that
// was drawn into a buffer.
bool formUserSpace(FPDF_PAGE page, const FormFill::Frame &frame, const QPointF &devicePoint,
                   FS_POINTF *out)
{
    if (!page || frame.pixels.isEmpty())
        return false;
    double x = 0;
    double y = 0;
    if (!FPDF_DeviceToPage(page, 0, 0, frame.pixels.width(), frame.pixels.height(),
                           frame.quarterTurns, qRound(devicePoint.x()), qRound(devicePoint.y()), &x,
                           &y))
        return false;
    out->x = float(x);
    out->y = float(y);
    return true;
}

// The file FPDF_SaveAsCopy writes into. Its WriteBlock gets a chunk at a time, and
// the only failure mode worth reporting is a write that did not happen.
struct FormFileWriter
{
    FPDF_FILEWRITE base;
    QFile *file = nullptr;
    bool failed = false;

    FormFileWriter()
    {
        base.version = 1;
        base.WriteBlock = &writeBlock;
    }

    static int writeBlock(FPDF_FILEWRITE *self, const void *data, unsigned long size)
    {
        // `base` is the first member of a standard-layout struct, which is what makes
        // the round trip from the pointer PDFium hands back to the object that owns it
        // well-defined -- the same trick FormFillInfo uses for the same reason.
        auto *writer = reinterpret_cast<FormFileWriter *>(self);
        if (writer->failed)
            return -1;
        const qint64 written = writer->file->write(static_cast<const char *>(data),
                                                   static_cast<qint64>(size));
        if (written != static_cast<qint64>(size)) {
            writer->failed = true;
            return -1;
        }
        return 0;
    }
};

} // namespace

bool PdfiumBackend::fillable() const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document || !impl->form)
        return false;
    // A form this build cannot read is not a form this build should take input for:
    // FORMTYPE_XFA_FULL and FORMTYPE_XFA_FOREGROUND both mean the packet is the
    // other copy of the form, and this build has no XFA module to load it with
    // (pdfium-plan.md sec.4.3 -- FPDF_LoadXFA answers false for every one of them).
    // The reader is told that in 文档's own sentence at open time; here it means a
    // click on such a field does nothing rather than half-filling one copy of two.
    return FPDF_GetFormType(impl->document) == FORMTYPE_ACRO_FORM;
}

FormFill::Field PdfiumBackend::fieldAt(int page, const Frame &frame,
                                       const QPointF &devicePoint) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return Field::None;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    FS_POINTF point{};
    if (!handle || !formUserSpace(handle, frame, devicePoint, &point))
        return Field::None;
    // The hit test PDFium does itself, in the same space its widgets are drawn in:
    // measured on field-flat.pdf, this answers 6 (a text field) at the field's raw
    // /Rect centre and -1 everywhere else. FPDFAnnot_GetFormFieldAtPoint is the same
    // question with an answer that has to be closed, and a cursor does not need it.
    return fieldOf(FPDFPage_HasFormFieldAtPoint(impl->form, handle, point.x, point.y));
}

bool PdfiumBackend::mouseMoved(int page, const Frame &frame, const QPointF &devicePoint)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    FS_POINTF point{};
    if (!handle || !formUserSpace(handle, frame, devicePoint, &point))
        return false;
    const Impl::Dispatch guard(*impl);
    // Measured on this build: moving over a field requests no cursor at all, so the
    // return value is not how a pointer learns what to be -- fieldAt() is. The call
    // still goes out, because PDFium tracks the pointer for the press that follows
    // it and a widget that never saw the move can miss the release.
    return FORM_OnMouseMove(impl->form, handle, 0, point.x, point.y) != 0;
}

bool PdfiumBackend::mousePressed(int page, const Frame &frame, const QPointF &devicePoint,
                                 Qt::KeyboardModifiers modifiers)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    FS_POINTF point{};
    if (!handle || !formUserSpace(handle, frame, devicePoint, &point))
        return false;
    const Field field = fieldOf(FPDFPage_HasFormFieldAtPoint(impl->form, handle, point.x,
                                                             point.y));
    if (!FormFill::isInteractive(field))
        return false;
    // Before the press, not after: the click that checks a box is the last moment
    // its old value is still the value the file has.
    FPDF_ANNOTATION annotation = FPDFAnnot_GetFormFieldAtPoint(impl->form, handle, &point);
    impl->noteFieldBeforeTouch(page, annotation);
    if (annotation)
        FPDFPage_CloseAnnot(annotation);

    const Impl::Dispatch guard(*impl);
    // FORM_OnLButtonDown's own answer is not what the caller is told. Measured on a page
    // marked /Rotate 90: the hit test named the field, the press was dispatched and the
    // field took the keyboard -- and FORM_OnLButtonDown still answered false. A view that
    // trusted that answer would have started a text selection in the same click. The
    // question the view is asking is "is this click the form's", and the hit test above
    // is the answer to it; the dispatch happens either way.
    FORM_OnLButtonDown(impl->form, handle, formEventFlags(modifiers), point.x, point.y);
    return true;
}

bool PdfiumBackend::mouseReleased(int page, const Frame &frame, const QPointF &devicePoint,
                                  Qt::KeyboardModifiers modifiers)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    FS_POINTF point{};
    if (!handle || !formUserSpace(handle, frame, devicePoint, &point))
        return false;
    const Impl::Dispatch guard(*impl);
    FORM_OnLButtonUp(impl->form, handle, formEventFlags(modifiers), point.x, point.y);
    // A release belongs to a press that already said the form had the gesture, so it is
    // taken whenever a field is the one holding the keyboard; the caller's job is to end
    // the drag, and the boolean is the same question the press answered.
    return impl->focusedPageLocked() >= 0;
}

bool PdfiumBackend::mouseDoubleClicked(int page, const Frame &frame, const QPointF &devicePoint)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    FS_POINTF point{};
    if (!handle || !formUserSpace(handle, frame, devicePoint, &point))
        return false;
    const Field field = fieldOf(FPDFPage_HasFormFieldAtPoint(impl->form, handle, point.x,
                                                             point.y));
    if (!FormFill::isInteractive(field))
        return false;
    const Impl::Dispatch guard(*impl);
    // A double-click in a text field selects a word, which is the ordinary way to
    // replace one answer with another. Same reasoning as the press: the hit test says
    // whose gesture this is, and PDFium's boolean is not that question.
    FORM_OnLButtonDoubleClick(impl->form, handle, 0, point.x, point.y);
    return true;
}

bool PdfiumBackend::keyPressed(int qtKey, Qt::KeyboardModifiers modifiers)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return false;
    int page = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&page);
    if (!focused)
        return false;
    FPDFPage_CloseAnnot(focused);
    impl->noteFocusedFieldBeforeTouch();

    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return false;
    const int virtualKey = formVirtualKey(qtKey);
    if (virtualKey == 0)
        return false;
    const Impl::Dispatch guard(*impl);
    return FORM_OnKeyDown(impl->form, handle, virtualKey, formEventFlags(modifiers)) != 0;
}

bool PdfiumBackend::characterTyped(QChar character, Qt::KeyboardModifiers modifiers)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form || character.isNull())
        return false;
    int page = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&page);
    if (!focused)
        return false;
    FPDFPage_CloseAnnot(focused);
    impl->noteFocusedFieldBeforeTouch();

    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return false;
    const Impl::Dispatch guard(*impl);
    return FORM_OnChar(impl->form, handle, character.unicode(), formEventFlags(modifiers)) != 0;
}

void PdfiumBackend::commitText(const QString &text)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form || text.isEmpty())
        return;
    int page = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&page);
    if (!focused)
        return;
    FPDFPage_CloseAnnot(focused);
    impl->noteFocusedFieldBeforeTouch();
    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return;
    const Impl::Dispatch guard(*impl);
    // One call for the whole committed string. FORM_OnChar per character would put
    // the composition into the field one code unit at a time, which is what breaks
    // it -- the candidate string a reader chose is one edit, and the widget's undo
    // stack should hold it as one (handoff-04 G1 2).
    //
    // QString::utf16() is the null-terminated UTF-16 the header asks for; FPDF_WCHAR
    // is unsigned short, which is what a char16_t is on every platform this builds
    // for but not the same type to the compiler.
    FORM_ReplaceSelection(impl->form, handle,
                          reinterpret_cast<FPDF_WIDESTRING>(text.utf16()));
}

bool PdfiumBackend::hasFocusedField() const
{
    QMutexLocker locker(&pdfiumMutex());
    int page = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&page);
    if (!focused)
        return false;
    FPDFPage_CloseAnnot(focused);
    return true;
}

int PdfiumBackend::focusedPage() const
{
    QMutexLocker locker(&pdfiumMutex());
    int page = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&page);
    if (!focused)
        return -1;
    FPDFPage_CloseAnnot(focused);
    return page;
}

FormFill::Field PdfiumBackend::focusedFieldType() const
{
    QMutexLocker locker(&pdfiumMutex());
    int page = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&page);
    if (!focused)
        return Field::None;
    const Field field = fieldOf(FPDFAnnot_GetFormFieldType(impl->form, focused));
    FPDFPage_CloseAnnot(focused);
    return field;
}

bool PdfiumBackend::focusedFieldRect(int *page, QRectF *userSpace) const
{
    QMutexLocker locker(&pdfiumMutex());
    int index = -1;
    FPDF_ANNOTATION focused = impl->focusedAnnot(&index);
    if (!focused)
        return false;
    const QRectF rect = fieldRect(focused);
    FPDFPage_CloseAnnot(focused);
    if (rect.isEmpty())
        return false;
    if (page)
        *page = index;
    if (userSpace)
        *userSpace = rect;
    return true;
}

void PdfiumBackend::releaseFocus()
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form)
        return;
    const Impl::Dispatch guard(*impl);
    // And it is the commit: measured on field-flat.pdf, a save taken while a text
    // field still had focus wrote a file that did not contain what the reader had
    // typed, and the same save after this call did. stageFieldValues() depends on it.
    FORM_ForceToKillFocus(impl->form);
}

bool PdfiumBackend::canUndoFocused() const
{
    QMutexLocker locker(&pdfiumMutex());
    const int page = impl->focusedPageLocked();
    if (page < 0)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    return handle && FORM_CanUndo(impl->form, handle);
}

bool PdfiumBackend::canRedoFocused() const
{
    QMutexLocker locker(&pdfiumMutex());
    const int page = impl->focusedPageLocked();
    if (page < 0)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    return handle && FORM_CanRedo(impl->form, handle);
}

void PdfiumBackend::undoFocused()
{
    QMutexLocker locker(&pdfiumMutex());
    const int page = impl->focusedPageLocked();
    if (page < 0)
        return;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return;
    const Impl::Dispatch guard(*impl);
    FORM_Undo(impl->form, handle);
}

void PdfiumBackend::redoFocused()
{
    QMutexLocker locker(&pdfiumMutex());
    const int page = impl->focusedPageLocked();
    if (page < 0)
        return;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return;
    const Impl::Dispatch guard(*impl);
    FORM_Redo(impl->form, handle);
}

QVector<FormFill::Repaint> PdfiumBackend::takeRepaints()
{
    QMutexLocker locker(&pdfiumMutex());
    return std::move(impl->formState.repaints);
}

FormFill::Pointer PdfiumBackend::takeRequestedPointer()
{
    QMutexLocker locker(&pdfiumMutex());
    const Pointer pointer = impl->formState.pendingPointer;
    impl->formState.pendingPointer = Pointer::Arrow;
    return pointer;
}

int PdfiumBackend::differingFieldCount() const
{
    QMutexLocker locker(&pdfiumMutex());
    QVector<QPair<QString, QString>> pairs;
    impl->currentAndOriginalValues(pairs);
    int differing = 0;
    for (const auto &pair : std::as_const(pairs)) {
        if (pair.first != pair.second)
            ++differing;
    }
    return differing;
}

void PdfiumBackend::forgetFieldValueSnapshots()
{
    QMutexLocker locker(&pdfiumMutex());
    impl->formState.valueBeforeTouch.clear();
}

bool PdfiumBackend::stageFieldValues(const QString &directory, QString *staged, QString *error)
{
    const auto tr = [](const char *text) {
        return QCoreApplication::translate("PdfiumBackend", text);
    };
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return false;

    releaseFocus();

    // Beside the file it will replace, hidden, with a name no second save can pick:
    // the same three rules F8 wrote for DocumentEditor::stageInPlace(), because this
    // is the first of its two stages when a reader has both filled fields and page
    // edits, and a staged file that could collide is how §24's lesson was learned.
    QString candidate;
    for (;;) {
        candidate = QDir(directory).filePath(QStringLiteral("xyzpdf-form-%1.pdf")
                                                 .arg(QUuid::createUuid().toString(
                                                          QUuid::WithoutBraces)));
        if (!QFileInfo::exists(candidate))
            break;
    }

    QFile file(candidate);
    // Recorded before the file is opened, so that a kill inside FPDF_SaveAsCopy -- the window
    // this exists for -- leaves a path the next launch can be told about. The second call,
    // once the copy is hidden, is what puts its creation time in the record; see
    // src/core/stagingcleanup.h for the rule that lets that sweep delete anything.
    StagingCleanup::record(candidate);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = tr("打开 %1 时出错：%2").arg(QFileInfo(candidate).fileName(),
                                             tr("文件无法写入"));
        // No file was made, so only the record has to go.
        StagingCleanup::forget(candidate);
        return false;
    }
    FormFileWriter writer;
    writer.file = &file;
    const int saved = FPDF_SaveAsCopy(impl->document, &writer.base, FPDF_NO_INCREMENTAL);
    const bool flushed = file.flush();
    file.close();

    if (!saved || writer.failed || !flushed) {
        StagingCleanup::removeStaged(candidate);
        *error = tr("这份文档的表单没能写出来。原来的文件没有改动。");
        return false;
    }
    SetFileAttributesW(QDir::toNativeSeparators(candidate).toStdWString().c_str(),
                       FILE_ATTRIBUTE_HIDDEN);
    StagingCleanup::record(candidate);
    *staged = candidate;
    return true;
}

