// The form-input half of the engine backend, compiled only into the application.
//
// H1 3 split pdfiumbackend.cpp: everything that takes input -- the FORM_On*
// routing behind FormFill, the eight FFI_* callbacks that let PDFium ask for a
// repaint or a timer, and stageFieldValues()' FPDF_SaveAsCopy -- lives here. The
// two shell DLLs link pdfiumformfillstub.cpp instead, so a reader that only
// wants to look at a page does not carry the code that types into it. Drawing is
// not here: FPDF_FFLDraw stays in pdfiumbackend.cpp's render(), which both
// halves compile (handoff-05 H1 3).
#include "pdfiumbackend_p.h"

#include "../core/stagingcleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QUuid>

namespace {

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

void installFormFillCallbacks(FormFillInfo &info, FormState *&state)
{
    // Only the application needs a state to collect what PDFium asks for while an
    // event is in flight. Allocating it here rather than in load() is what keeps
    // its timer and page tables out of the two shell DLLs (H1 3).
    state = new FormState;
    info.self = state;
    // The eight fpdf_formfill.h marks as required for version 1. F7-C could leave
    // them all null and still draw, because drawing asks nothing of the embedder;
    // accepting input does, and a null one of those is a call through a null
    // pointer -- which is why the F5 probe never tried it.
    info.base.FFI_Invalidate = &onInvalidate;
    info.base.FFI_SetCursor = &onSetCursor;
    info.base.FFI_SetTimer = &onSetTimer;
    info.base.FFI_KillTimer = &onKillTimer;
    info.base.FFI_GetLocalTime = &onGetLocalTime;
    info.base.FFI_GetPage = &onGetPage;
    info.base.FFI_GetRotation = &onGetRotation;
    info.base.FFI_ExecuteNamedAction = &onExecuteNamedAction;
}

void resetFormState(FormState *state)
{
    if (!state)
        return;
    // The timers first and unconditionally: their slots call back into PDFium, and
    // a queued one firing after FPDFDOC_ExitFormFillEnvironment would be a call into
    // a form handle that is gone -- the shape of the crash F10 recorded for the
    // recognition pool, with a different object left alive. clear() kills them and
    // takes them out of the process-wide owner table.
    //
    // The pages FFI_GetPage loaded belong to neither the LRU nor PDFium, so this is
    // the only place they get closed -- before the document, as everything else here
    // is.
    const QVector<FPDF_PAGE> borrowed = state->loadedForForm;
    state->clear();
    for (FPDF_PAGE page : borrowed)
        FPDF_ClosePage(page);
    // The state stops naming the handle before it is exited, so a callback that
    // somehow arrives between the two cannot use a handle that is gone.
    state->form = nullptr;
}

void destroyFormState(FormState *state)
{
    delete state;
}

void noteFormPage(FormState *state, int index, FPDF_PAGE page)
{
    if (state)
        state->notePage(index, page);
}

void forgetFormPage(FormState *state, FPDF_PAGE page)
{
    if (state)
        state->forgetPage(page);
}

// ===========================================================================
// FormFill -- filling a field, and getting the filled document out to a file
// ===========================================================================

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
    // The move the window system delivers before every button, which this view does not
    // (it routes moves only while a drag is in flight, pageview.cpp). It is not a
    // formality: measured on chromium/8111, a press with no move before it is refused.
    // On a combo box's dropdown button the first such press took the focus and left the
    // list closed, and only the second one opened it; the same press with a move first
    // opened it straight away. Every click that moves between two fields is refused the
    // same way. So the move goes out here, before either branch's press.
    {
        const Impl::Dispatch guard(*impl);
        FORM_OnMouseMove(impl->form, handle, formEventFlags(modifiers), point.x, point.y);
    }
    if (!FormFill::isInteractive(field)) {
        // The hit test answers about the *page*, and a combobox's dropdown is not on
        // it: measured on a four-option combo, a press on an open popup's third row
        // gave HasFormFieldAtPoint -1 while FORM_OnLButtonDown answered true, the row
        // became the field's live text and the popup closed on the release. Refusing
        // the press here would drop the pick -- and with it the release, because the
        // view only sends one to a press it was told about.
        //
        // So while a field of this page holds the keyboard the press goes out anyway
        // and PDFium's own answer is the one returned: true over its dropdown, which
        // makes the view follow with the release that picks; false over paper, which
        // leaves the click to the view, whose next line lets the field go (and whose
        // kill-focus closes an open popup -- measured, without touching the value).
        if (impl->focusedPageLocked() != page)
            return false;
        impl->noteFocusedFieldBeforeTouch();
        const Impl::Dispatch guard(*impl);
        return FORM_OnLButtonDown(impl->form, handle, formEventFlags(modifiers), point.x,
                                  point.y)
            != 0;
    }
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

bool PdfiumBackend::mouseWheeled(int page, const Frame &frame, const QPointF &devicePoint,
                                 int detents, Qt::KeyboardModifiers modifiers)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->form || detents == 0)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    FS_POINTF point{};
    if (!handle || !formUserSpace(handle, frame, devicePoint, &point))
        return false;
    // A wheel that moves a list's selection is an edit, so the value it had before the
    // first one is noted the same way a press notes it.
    impl->noteFocusedFieldBeforeTouch();
    const Impl::Dispatch guard(*impl);
    // One event per detent, of magnitude one. FORM_OnMouseWheel's own comment says the
    // caller normalises the platform's delta, and measured on a twenty-row list this
    // build ignores the magnitude it is handed: dy -1 and dy -120 each moved the
    // selection by exactly one row. Passing a touchpad's accumulated delta would
    // therefore scroll one row, and passing each of its small deltas separately would
    // scroll fifteen -- so the counting is done here, where one call is one row.
    //
    // The sign is the one the header documents ("negative values mean down"), which is
    // Qt's angleDelta().y() unchanged: positive walked o05 -> o04 -> o03, negative
    // o05 -> o06 -> o07.
    const int step = detents > 0 ? 1 : -1;
    bool taken = false;
    for (int remaining = qAbs(detents); remaining > 0; --remaining) {
        if (FORM_OnMouseWheel(impl->form, handle, formEventFlags(modifiers), &point, 0, step))
            taken = true;
    }
    return taken;
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
    const Field kind = fieldOf(FPDFAnnot_GetFormFieldType(impl->form, focused));
    const QRectF rect = fieldRect(focused);
    FPDFPage_CloseAnnot(focused);
    impl->noteFocusedFieldBeforeTouch();

    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return false;

    // Alt+Down opens the list, and it opens it by being a click on the dropdown
    // button rather than a key. Measured on a four-option combo that had focus:
    // FORM_OnKeyDown(FWL_VKEY_Down, FWL_EVENTFLAG_AltKey) answered false and painted
    // nothing new, while a hover-less Down+Up two points inside the field's right edge
    // opened the popup (the band under the field went from 122 to 700 inked pixels),
    // closed it on the next round and opened it again on the one after. Plain Down is
    // left to PDFium: it walks the field's live text and commits nothing, which is
    // what makes the popup worth opening -- a row picked from it is the commit.
    if (qtKey == Qt::Key_Down && modifiers.testFlag(Qt::AltModifier)
        && kind == Field::ComboBox && !rect.isEmpty()) {
        const Impl::Dispatch guard(*impl);
        // The button is the field's right-hand ten points or so: right-2 opened it on
        // all three rounds, right-15 opened nothing.
        const float atX = float(rect.right() - 2);
        const float atY = float(rect.center().y());
        FORM_OnLButtonDown(impl->form, handle, 0, atX, atY);
        FORM_OnLButtonUp(impl->form, handle, 0, atX, atY);
        return true;
    }

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
    return std::move(impl->formState->repaints);
}

FormFill::Pointer PdfiumBackend::takeRequestedPointer()
{
    QMutexLocker locker(&pdfiumMutex());
    const Pointer pointer = impl->formState->pendingPointer;
    impl->formState->pendingPointer = Pointer::Arrow;
    return pointer;
}

int PdfiumBackend::differingFieldCount() const
{
    QMutexLocker locker(&pdfiumMutex());
    QVector<Impl::Comparison> pairs;
    impl->currentAndOriginalValues(pairs);
    int differing = 0;
    for (const Impl::Comparison &one : std::as_const(pairs)) {
        if (one.unknowable || one.original != one.now)
            ++differing;
    }
    return differing;
}

void PdfiumBackend::forgetFieldValueSnapshots()
{
    QMutexLocker locker(&pdfiumMutex());
    impl->formState->valueBeforeTouch.clear();
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
