// What the two shell DLLs compile instead of pdfiumformfill.cpp (H1 3).
//
// A preview pane and a thumbnail provider draw a page and never take input, so
// the handler DLLs must not carry the form-input half: no FORM_On* routing, no
// FFI_* callbacks, no FPDF_SaveAsCopy. PDFium's form environment is still
// initialized and FPDF_FFLDraw still runs (both in pdfiumbackend.cpp), which is
// what makes a field visible in the pane -- the two *_smoke targets assert that
// and can go red.
//
// Every answer here is the one a document that takes no input would give. The
// functions themselves cannot be removed: PdfiumBackend derives from FormFill in
// every build, so the vtable needs all of them at link time.
#include "pdfiumbackend_p.h"

void installFormFillCallbacks(FormFillInfo &info, FormState *&state)
{
    (void)info;
    (void)state;
    // No state and no callbacks. Nothing asks the embedder anything because nothing
    // is being drawn into a widget's input state, only painted.
}

void resetFormState(FormState *state)
{
    (void)state;
}

void destroyFormState(FormState *state)
{
    (void)state;
}

void noteFormPage(FormState *state, int index, FPDF_PAGE page)
{
    (void)state;
    (void)index;
    (void)page;
}

void forgetFormPage(FormState *state, FPDF_PAGE page)
{
    (void)state;
    (void)page;
}

bool PdfiumBackend::fillable() const
{
    return false;
}

void PdfiumBackend::setLockedFieldNames(const QStringList &names)
{
    // Nothing here takes input, so nothing needs the names a signature seals.
    (void)names;
}

FormFill::Field PdfiumBackend::fieldAt(int page, const Frame &frame,
                                       const QPointF &devicePoint) const
{
    (void)page;
    (void)frame;
    (void)devicePoint;
    return Field::None;
}

bool PdfiumBackend::mouseMoved(int page, const Frame &frame, const QPointF &devicePoint)
{
    (void)page;
    (void)frame;
    (void)devicePoint;
    return false;
}

bool PdfiumBackend::mousePressed(int page, const Frame &frame, const QPointF &devicePoint,
                                 Qt::KeyboardModifiers modifiers)
{
    (void)page;
    (void)frame;
    (void)devicePoint;
    (void)modifiers;
    return false;
}

bool PdfiumBackend::mouseReleased(int page, const Frame &frame, const QPointF &devicePoint,
                                  Qt::KeyboardModifiers modifiers)
{
    (void)page;
    (void)frame;
    (void)devicePoint;
    (void)modifiers;
    return false;
}

bool PdfiumBackend::mouseDoubleClicked(int page, const Frame &frame,
                                       const QPointF &devicePoint)
{
    (void)page;
    (void)frame;
    (void)devicePoint;
    return false;
}

bool PdfiumBackend::mouseWheeled(int page, const Frame &frame, const QPointF &devicePoint,
                                 int detents, Qt::KeyboardModifiers modifiers)
{
    (void)page;
    (void)frame;
    (void)devicePoint;
    (void)detents;
    (void)modifiers;
    return false;
}

bool PdfiumBackend::keyPressed(int qtKey, Qt::KeyboardModifiers modifiers)
{
    (void)qtKey;
    (void)modifiers;
    return false;
}

bool PdfiumBackend::characterTyped(QChar character, Qt::KeyboardModifiers modifiers)
{
    (void)character;
    (void)modifiers;
    return false;
}

void PdfiumBackend::commitText(const QString &text)
{
    (void)text;
}

bool PdfiumBackend::hasFocusedField() const
{
    return false;
}

int PdfiumBackend::focusedPage() const
{
    return -1;
}

FormFill::Field PdfiumBackend::focusedFieldType() const
{
    return Field::None;
}

bool PdfiumBackend::focusedFieldRect(int *page, QRectF *userSpace) const
{
    (void)page;
    (void)userSpace;
    return false;
}

void PdfiumBackend::releaseFocus()
{
}

bool PdfiumBackend::canUndoFocused() const
{
    return false;
}

bool PdfiumBackend::canRedoFocused() const
{
    return false;
}

void PdfiumBackend::undoFocused()
{
}

void PdfiumBackend::redoFocused()
{
}

QVector<FormFill::Repaint> PdfiumBackend::takeRepaints()
{
    return {};
}

FormFill::Pointer PdfiumBackend::takeRequestedPointer()
{
    return Pointer::Arrow;
}

int PdfiumBackend::differingFieldCount() const
{
    return 0;
}

void PdfiumBackend::forgetFieldValueSnapshots()
{
}

bool PdfiumBackend::stageFieldValues(const QString &directory, QString *staged, QString *error)
{
    (void)directory;
    (void)staged;
    (void)error;
    return false;
}
