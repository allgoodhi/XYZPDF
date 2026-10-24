// What the two halves of the engine backend share (H1 3).
//
// Until H1 3 all of this sat in an anonymous namespace inside pdfiumbackend.cpp.
// The application takes form input and the two shell DLLs only draw fields, and
// the handoff asked the input half to stop being linked into the handlers -- so
// the file was split, and the half both sides still need moved here:
//
//   * PdfiumBackend::Impl, because the Document methods in pdfiumbackend.cpp and
//     the FormFill methods in pdfiumformfill.cpp both walk it.
//   * the form environment's state and the helpers that read a field, because
//     drawing asks for the environment (FPDF_FFLDraw) and the fill half asks the
//     field questions.
//
// What is deliberately *not* here: the eight FFI_* callbacks. They are the input
// half's, and installFormFillCallbacks() below is the seam -- pdfiumformfill.cpp
// installs them, pdfiumformfillstub.cpp (the shell DLLs' copy) leaves the
// interface bare apart from the version load() sets, because drawing asks nothing
// of the embedder (F7-C measured exactly that).
//
// Everything at namespace scope is inline: this header is included by two
// translation units in the application, and by /Zc:inline + /OPT:REF the shell
// DLLs drop the inline functions neither of their two files calls.
#pragma once

#include "document.h"
#include "formfill.h"
#include "pdfiumbackend.h"

#include <QHash>
#include <QMutex>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

// The PDFium headers and windows.h have to share one inclusion, and NOMINMAX has
// to be defined before fpdfview.h includes windows.h -- the same note the
// application's half carries above its own includes.
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

// One lock for every FPDF call in this process, and the library's own lifetime
// counted on top of it. Qt PDF has the same shape (pagerenderer.h records what
// it is for); this is the backend's own copy now that the calls are ours.
inline QRecursiveMutex &pdfiumMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}

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

// A few pages held open, oldest let go. Qt PDF keeps its pages open and that is
// part of why its per-page render figure is lower than a probe that loads and
// closes a page every time (docs/pdfium-plan.md sec.5.2, confound 1). Four is
// what the text caches above this use, and enough for the two pages a drag
// across a page boundary alternates between.
inline constexpr int kPagesHeld = 4;

struct FormFillInfo
{
    FPDF_FORMFILLINFO base;
    void *self = nullptr;
};

// FPDFPage_HasFormFieldAtPoint's answer, and FPDF_FORMFIELD_* from the same header.
inline FormFill::Field fieldOf(int pdfiumType)
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
inline FormFill::Pointer pointerOf(int pdfiumCursor)
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

// PDF 32000-1's field-flag bits, counted from 1 the way the specification counts
// them, so a bit's value is 1 << (bit - 1). Table 226 (choice fields): bit 22 is
// MultiSelect. Written as a name rather than a number because this same table was got
// the wrong way round twice while measuring it -- bit 19 is Edit and bit 20 is Sort,
// not the other way about -- and a bare 2097152 next to a claim about a flag is a claim
// nobody can check by reading it.
inline constexpr int kFieldFlagMultiSelect = 1 << 21;

// A list box the reader can hold more than one answer in. The field type alone does not
// say: FPDFAnnot_GetFormFieldType answers LISTBOX for both shapes, and only /Ff bit 22
// tells them apart -- which is why this is a question asked of the file and not of the
// name. See selectedOptionSet() for what follows from the answer.
inline bool isMultiSelectListBox(FPDF_FORMHANDLE form, FPDF_ANNOTATION annotation)
{
    return annotation
        && fieldOf(FPDFAnnot_GetFormFieldType(form, annotation)) == FormFill::Field::ListBox
        && (FPDFAnnot_GetFormFieldFlags(form, annotation) & kFieldFlagMultiSelect) != 0;
}

// Qt's modifiers to FWL_EVENTFLAG_*, which is the numbering FORM_On* asks for.
inline int formEventFlags(Qt::KeyboardModifiers modifiers)
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
inline int formVirtualKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Backspace:
        return FWL_VKEY_Back;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: // Qt's name for Shift+Tab; the Shift itself travels in the flags
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
inline QString fieldValue(FPDF_FORMHANDLE form, FPDF_ANNOTATION annotation)
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

// What a multi-select list box holds, as one string a comparison can be made on: every
// option FPDFAnnot_IsOptionSelected answers true for, in the order the file lists them,
// joined by a separator no label can contain.
//
// fieldValue() cannot answer this. Measured on chromium/8111 with a four-option field
// whose /V starts as {b}: a plain click on row a and a Ctrl+click on row b leave the
// field holding {a,b}, and then FPDFAnnot_GetFormFieldValue answers "a" -- the first
// selected value alone -- while FPDFAnnot_GetStringValue("/V") answers nothing at all,
// because /V is an array now and that read only answers strings. Comparing "a" against a
// snapshot of "b" would call a real change a difference and a real *second* selection no
// change at all.
//
// The markers are also the one read that is not live: at load and while the field is
// being clicked they still show the file's own set ({b}), and they only move once the
// field lets go of the keyboard, at which point they show {a,b}. So this is the truth
// about a committed field, and the caller treats a focused one as a question it cannot
// answer (Impl::currentAndOriginalValues).
inline QString selectedOptionSet(FPDF_FORMHANDLE form, FPDF_ANNOTATION annotation)
{
    if (!annotation)
        return {};
    QStringList picked;
    const int options = FPDFAnnot_GetOptionCount(form, annotation);
    for (int index = 0; index < options; ++index) {
        if (!FPDFAnnot_IsOptionSelected(form, annotation, index))
            continue;
        QVector<FPDF_WCHAR> buffer(256);
        const unsigned long bytes = FPDFAnnot_GetOptionLabel(
            form, annotation, index, buffer.data(),
            static_cast<unsigned long>(buffer.size() * sizeof(FPDF_WCHAR)));
        if (bytes <= 2)
            continue;
        const int units = qMin<int>(int(bytes / 2), int(buffer.size())) - 1;
        picked.append(QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()),
                                        units));
    }
    return picked.join(QLatin1Char('\x1f'));
}

// A field's own name, which is what a value is remembered against. A widget and
// its parent field share the name, and it does not move when the page it is on
// moves -- which matters because a pending 删除页面 has already shifted page
// numbers by the time a reader looks at a field on a later page.
inline QString fieldName(FPDF_FORMHANDLE form, FPDF_ANNOTATION annotation)
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
inline QRectF fieldRect(FPDF_ANNOTATION annotation)
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
inline QHash<int, FormState *> &formTimerOwners()
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


// The input half's seams. The application's definitions allocate the form state,
// assign the eight callbacks fpdf_formfill.h marks as required for version 1, and
// keep the page table and timer table alive; the shell DLLs' definitions do
// nothing at all, because they only draw and drawing asks nothing of the
// embedder. load() has already set the version.
//
// These are also what keeps FormState's containers -- and the Qt template code
// behind them -- out of the two handler DLLs: no FormState object is ever created
// there, so nothing in it is instantiated. H1 3 measured that.
void installFormFillCallbacks(FormFillInfo &info, FormState *&state);
// Two steps because the FPDF_FORMFILLINFO has to stay valid across the exit of the
// environment: resetFormState() stops the timers, closes the borrowed pages and
// forgets the handle; destroyFormState() deletes it once PDFium is done.
void resetFormState(FormState *state);
void destroyFormState(FormState *state);
void noteFormPage(FormState *state, int index, FPDF_PAGE page);
void forgetFormPage(FormState *state, FPDF_PAGE page);

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
            if (impl.formState)
                ++impl.formState->dispatchDepth;
        }
        ~Dispatch()
        {
            if (impl.formState)
                --impl.formState->dispatchDepth;
        }

        Impl &impl;
    };

    PdfFileHandle file;
    FPDF_DOCUMENT document = nullptr;
    // The form-fill environment and its interface. Both live as long as the
    // document: fpdf_formfill.h says the FPDF_FORMFILLINFO must stay alive until
    // the handle is closed, and the handle must be exited before the document.
    // Since G1 the eight callbacks the header marks as required for version 1 are
    // filled in by installFormFillCallbacks() -- in the application that is the
    // definition in pdfiumformfill.cpp, in the two shell DLLs the bare one in
    // pdfiumformfillstub.cpp; the optional ones
    // and everything the header reserves for XFA stay null, because this build has
    // no XFA module (pdfium-plan.md sec.4.3) and no V8, so FFI_GetCurrentPage --
    // which the header says is "required when V8 support is present, otherwise
    // unused" -- is not answered either.
    FormFillInfo formInfo{};
    FPDF_FORMHANDLE form = nullptr;
    QString path;
    QString error;
    bool encrypted = false;

    // What the file says about its digital signatures, read once in load() while the
    // document is open (PDF 32000-1 sec.12.8). Both are zero for a file with none,
    // and docMdpPermission() is zero for a file whose signatures are ordinary ones
    // rather than a certification signature with /TransformMethod /DocMDP.
    int signatureCount = 0;
    int docMdpPermission = 0;
    // The fields a signature's /Lock seals, by fully qualified name (PDF 32000-1
    // sec.12.8.4, table 233). PDFium neither exposes nor enforces /Lock, so these names
    // are read by DocumentEditor's qpdf pass and handed over before the first gesture
    // (setLockedFieldNames). Empty means nothing is sealed.
    QSet<QString> lockedFieldNames;

    // ---- what the form layer asked for while the last event was in flight ----
    //
    // The callbacks may only touch this, and they reach it through the FormFillInfo
    // wrapped around the FORMFILLINFO they were registered with.
    //
    // Null in the two shell DLLs, which install no callbacks and so have nothing to
    // collect. H1 3 made it a pointer for that reason: a FormState by value dragged
    // its timer table and page maps into every handler, and none of that code is
    // reachable when nothing ever calls back. Only the application's
    // installFormFillCallbacks() allocates one.
    FormState *formState = nullptr;

    QHash<int, HeldPage> pages;
    quint64 useCounter = 0;

    // Caller holds the mutex.
    FPDF_PAGE holdPage(int index, bool textNeeded)
    {
        if (!document || index < 0 || index >= FPDF_GetPageCount(document))
            return nullptr;

        auto held = pages.find(index);
        if (held == pages.end()) {
            if (pages.size() >= kPagesHeld
                && (!formState || formState->dispatchDepth == 0)) {
                auto oldest = pages.begin();
                for (auto it = pages.begin(); it != pages.end(); ++it) {
                    if (it->lastUsed < oldest->lastUsed)
                        oldest = it;
                }
                if (oldest->page)
                    forgetFormPage(formState, oldest->page);
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
            noteFormPage(formState, index, fresh.page);
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
    // The string a field's change is judged on: its value, except for a multi-select
    // list box, whose value no single read answers (selectedOptionSet() has the
    // measurement). The snapshot and the later read both come through here, so the two
    // sides of the comparison are always the same shape -- a snapshot taken as a set and
    // read back as a first value would call every such field changed.
    QString comparableValue(FPDF_ANNOTATION annotation) const
    {
        return isMultiSelectListBox(form, annotation) ? selectedOptionSet(form, annotation)
                                                      : fieldValue(form, annotation);
    }

    void noteFieldBeforeTouch(int page, FPDF_ANNOTATION annotation)
    {
        if (!formState || !annotation)
            return;
        const QString name = fieldName(form, annotation);
        if (name.isEmpty())
            return;
        const QString key = QStringLiteral("%1|%2").arg(page).arg(name);
        if (formState->valueBeforeTouch.contains(key))
            return;
        formState->valueBeforeTouch.insert(key, comparableValue(annotation));
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
    // answer it. The value a field holds now, and the one it held before this program's
    // first event aimed at it. The focused widget is asked for its live text, because a
    // value the reader is still typing has not reached the field yet (FormFill's
    // header has the measurement).
    struct Comparison
    {
        QString original;
        QString now;
        // True when the two strings above cannot answer the question, so the caller
        // has to take "different" as the answer. Only one shape sets it; the comment
        // where it is set says which and why.
        bool unknowable = false;
    };

    void currentAndOriginalValues(QVector<Comparison> &out)
    {
        if (!formState)
            return;
        out.reserve(formState->valueBeforeTouch.size());
        for (auto entry = formState->valueBeforeTouch.constBegin();
             entry != formState->valueBeforeTouch.constEnd(); ++entry) {
            const int page = entry.key().section(QLatin1Char('|'), 0, 0).toInt();
            const QString name = entry.key().section(QLatin1Char('|'), 1);
            Comparison one;
            one.original = entry.value();
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
                    one.now = QString::fromUtf16(
                        reinterpret_cast<const char16_t *>(buffer.constData()), units);
                }
                // A multi-select list box that still has the keyboard is not a
                // comparison this program can make, and the answer has to be
                // "different" rather than "the same". Measured, two clicks: /V={b}, a
                // plain click on row a, then Ctrl+click on row b -- the field holds
                // {a,b} while its live text is "b", the label picked last, which is
                // exactly what the snapshot says. The truth is FPDFAnnot_IsOptionSelected
                // and it is not live: while the field has the keyboard the markers still
                // read {b}, and they only move to {a,b} once it lets go.
                //
                // So the count over-reports while such a field is focused: a reader who
                // multi-selects and then puts the same set back sees the star until the
                // field lets go. The other way round is the one that loses data --
                // DocumentTab::saveEdits() does nothing at all when this count is zero,
                // so an under-report makes Ctrl+S silently throw the second selection
                // away and the close prompt not ask. Once focus leaves, the markers are
                // the truth and comparableValue() reads them, so the over-report ends
                // there instead of lasting for the rest of the document's life.
                one.unknowable = isMultiSelectListBox(form, focused);
                FPDFPage_CloseAnnot(focused);
            } else {
                if (focused)
                    FPDFPage_CloseAnnot(focused);
                FPDF_PAGE pageHandle = holdPageRef(page);
                FPDF_ANNOTATION annotation
                    = pageHandle ? pageField(pageHandle, name) : nullptr;
                one.now = comparableValue(annotation);
                if (annotation)
                    FPDFPage_CloseAnnot(annotation);
            }
            out.append(one);
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
        // The state is emptied first: its timer slots call back into PDFium, and a
        // queued one firing after FPDFDOC_ExitFormFillEnvironment would be a call
        // into a form handle that is gone -- the shape of the crash F10 recorded for
        // the recognition pool, with a different object left alive. resetFormState()
        // also closes the pages FFI_GetPage loaded for PDFium; they belong to
        // neither the LRU nor PDFium, so this is the only place they get closed.
        //
        // The state itself outlives the handle on purpose: FPDF_FORMFILLINFO has to
        // stay valid until the environment is exited, and the exit is allowed to call
        // back into it. It is deleted just after.
        if (formState)
            resetFormState(formState);
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
        if (form) {
            // resetFormState() stopped naming this handle, so a callback arriving
            // during the exit finds no handle to use.
            FPDFDOC_ExitFormFillEnvironment(form);
            form = nullptr;
        }
        if (formState) {
            destroyFormState(formState);
            formState = nullptr;
        }
        if (document) {
            FPDF_CloseDocument(document);
            document = nullptr;
        }
        file.close();
        // Both answers were read off that document handle; with no document there is
        // nothing to have counted -- and the lock names belong to that document too.
        signatureCount = 0;
        docMdpPermission = 0;
        lockedFieldNames.clear();
    }

    // Whether this field is one a signature's /Lock seals. The name is the one
    // FPDFAnnot_GetFormFieldName answers, which is the field tree's fully qualified
    // name -- the same spelling the qpdf walk collected.
    bool fieldLocked(FPDF_ANNOTATION annotation) const
    {
        const QString name = fieldName(form, annotation);
        return !name.isEmpty() && lockedFieldNames.contains(name);
    }

    // The focused field, if it is sealed. Caller holds the mutex; the annotation is
    // closed before the answer comes back.
    bool focusedFieldLocked() const
    {
        int page = -1;
        FPDF_ANNOTATION focused = focusedAnnot(&page);
        if (!focused)
            return false;
        const bool locked = fieldLocked(focused);
        FPDFPage_CloseAnnot(focused);
        return locked;
    }
};

