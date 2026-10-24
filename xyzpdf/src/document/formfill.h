// The interactive half of the form layer: what a reader does to a field.
//
// ---- why this is a second interface and not a fifth method on Document ----
//
// docs/PLAN.md §7.3 splits reading from editing, and document.h keeps to it: no
// write path may be reachable through the read-only type. Filling a field is not
// a write -- nothing reaches a file until the reader presses 保存 -- but it is
// not a question about a page either, and a Document that answered it would be a
// Document whose every caller has to know whether the engine underneath has a
// form module at all. So the capability is its own type, and the one place it is
// attached to a document is the tab that owns the backend
// (DocumentTab::formSurface(), a dynamic_cast over the engine seam rather than a
// method on it): an engine with no form layer simply is not one of these.
//
// ---- what a caller has to hand over, and why the frame is measured ----
//
// PDFium wants the point in PDF user space: origin at the page's bottom-left
// corner, in points, and -- measured, and the part that is easy to get wrong --
// without the page's own /Rotate folded in and without the reader's rotation
// folded in either. docs/pdfium-plan.md sec.7 lists the calls; this is what the
// release's own headers say, read off chromium/8111 on this machine:
//
//   * tests/fixtures/rotated.pdf is stored 300x400 and marked /Rotate 90, so
//     FPDF_GetPageWidthF answers 400x300. Feeding FPDF_DeviceToPage the four
//     corners of a 400x300 render answers x in [0,299] and y in [0,399] -- the
//     stored 300x400 box, not the displayed one. The same call with the reader's
//     rotate=1 answers inside the same 300x400 span. So the space is the page's
//     own, and rotation is something the conversion absorbs.
//   * tests/fixtures/text.pdf (595x842, no /Rotate) answers (0,842) for the
//     device top-left and (0,1) for the device bottom-left, which is the same
//     finding with the y-axis visible: bottom-left origin, y up.
//   * FPDFAnnot_GetFormFieldAtPoint and FPDFPage_HasFormFieldAtPoint hit the
//     field of field-flat.pdf at its raw /Rect centre (160,615) and answer
//     "no field" at the top-left-origin reading of the same spot (160,227).
//
// That is why the caller passes a Frame -- the page's pixel size and the reader's
// quarter turns, exactly as they were passed to the render that put those pixels
// on screen -- and a point in that pixel grid, and why the conversion is
// FPDF_DeviceToPage rather than arithmetic here. The alternative was to redo
// PDFium's matrix by hand, and a hand-made one is a second answer to "where did
// the reader click" -- the reason pagePointFor() and the band math already agree.
//
// ---- the input methods, in the order a keystroke travels ----
//
// A press goes to FORM_OnFocus through FORM_OnLButtonDown/Up; a key to
// FORM_OnKeyDown with a Windows virtual-key code and FORM_OnChar with one UTF-16
// code unit at a time; an input-method commit to FORM_ReplaceSelection, because
// sending a composed string as single OnChar calls is what breaks it (handoff-04
// G1 2). Tab and Shift+Tab are FORM_OnKeyDown's own business -- it walks the
// field order and crosses pages, which is why FFI_GetPage is a callback and not
// something this class could answer from the page it was handed.
//
// ---- where the typed text lives ----
//
// Measured: with the caret in a field, FORM_GetFocusedText answers the typed text
// and FPDFAnnot_GetFormFieldValue still answers the file's. The commit happens
// when the widget loses its focus -- after FORM_ForceToKillFocus the field's own
// value reads back with the typed text, and only then does FPDF_SaveAsCopy put it
// in the bytes (a save taken while the field still had focus wrote a file that did
// not contain what the reader had typed). So differingFieldCount() asks the
// focused widget for its live text and everyone else for their value, and
// stageFieldValues() commits before it writes.
#pragma once

#include <QChar>
#include <QPoint>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class FormFill
{
public:
    // What the pointer should look like over a field. This is PDFium's own list,
    // reduced to the shapes it can actually ask for: FFI_SetCursor hands over an
    // int whose values are documented nowhere in this release (measured: a text
    // field asks for 3 while taking focus, which is the IBeam of PDFium's widget
    // layer), so the mapping is read off the calls rather than from a header.
    enum class Pointer {
        Arrow,
        IBeam,
        Hand,
        Cross,
        Wait,
        UpDown,
        SizeAll,
        No
    };

    // What kind of field is under a point. FPDFPage_HasFormFieldAtPoint answers
    // this directly, and it is the only hover question this build answers without
    // a callback: FORM_OnMouseMove was measured to request no cursor at all
    // (moving over a text field and over blank paper both produced no FFI_SetCursor
    // call), so a pointer that waits for that callback would stay an arrow forever.
    enum class Field {
        None,
        PushButton,
        CheckBox,
        RadioButton,
        ComboBox,
        ListBox,
        TextField,
        PasswordField,
        Signature
    };

    // The geometry one page was drawn with: the whole page in device pixels at the
    // reader's zoom, and the reader's clockwise quarter turns. Both are what the
    // render was given, which is what FPDF_DeviceToPage requires.
    struct Frame
    {
        QSize pixels;
        int quarterTurns = 0;
    };

    // A rectangle PDFium says is no longer what it drew. `userSpace` is in the
    // same space a click is answered in, so the view can put it on screen with the
    // frame it already has.
    struct Repaint
    {
        int page = -1;
        QRectF userSpace;
    };

    virtual ~FormFill() = default;
    // Whether this document has fields a reader can fill here.
    //
    // False for a document with no form, for an XFA one (this build has no XFA
    // module, and the packet's own copy of the form is what a real viewer would
    // draw -- docs/pdfium-plan.md sec.4.3), and for one whose only widgets are
    // push buttons and signatures. It decides whether the view routes input here
    // at all, so it has to be cheap: it is answered from FPDF_GetFormType and one
    // pass over the field types, not from a render.
    virtual bool fillable() const = 0;

    // The fields a digital signature's /Lock seals, by their fully qualified names
    // (PDF 32000-1 sec.12.8.4, table 233). PDFium exposes no API for /Lock, and on
    // chromium/8111 it does not enforce one either -- measured, a field named by a
    // signature's /Lock took the keyboard exactly like an unsealed one. So the names
    // are read from the file by DocumentEditor at open (its qpdf pass) and handed here
    // before the reader's first gesture; every one of them is display-only, the same
    // way a push button and a signature field already are. An empty list says no
    // signature seals anything.
    virtual void setLockedFieldNames(const QStringList &names) = 0;

    // Whether a reader fills this kind of field by hand, or only sees it.
    //
    // A push button and a signature are drawn and not touched: the first runs an
    // action this build cannot run (there is no V8, and its action is usually a
    // submit or a calculation), and the second is a digital signature whose value is
    // a cryptographic object -- editing one here would be editing a document that has
    // been signed, which is a different thing to agree to than filling a form.
    // Handoff-04 G1 5 says both show and neither responds. One function, because the
    // view needs it for the pointer and the backend needs it for the press, and two
    // lists of the same kinds is how they start to disagree.
    static bool isInteractive(Field field)
    {
        switch (field) {
        case Field::CheckBox:
        case Field::RadioButton:
        case Field::ComboBox:
        case Field::ListBox:
        case Field::TextField:
            return true;
        case Field::None:
        case Field::PushButton:
        case Field::Signature:
            break;
        }
        return false;
    }

    // The field under a point in `frame`'s pixel grid, or Field::None.
    virtual Field fieldAt(int page, const Frame &frame, const QPointF &devicePoint) const = 0;

    // ---- the events, in the order they arrive ----
    //
    // Each returns true when the form took the event, which is the view's word for
    // "do not scroll, do not select text, do not let this key reach the window".
    // A false means the reader is not in a field and the page behaves the way it
    // did before this class existed.

    virtual bool mouseMoved(int page, const Frame &frame, const QPointF &devicePoint) = 0;
    virtual bool mousePressed(int page, const Frame &frame, const QPointF &devicePoint,
                              Qt::KeyboardModifiers modifiers) = 0;
    virtual bool mouseReleased(int page, const Frame &frame, const QPointF &devicePoint,
                               Qt::KeyboardModifiers modifiers) = 0;
    virtual bool mouseDoubleClicked(int page, const Frame &frame, const QPointF &devicePoint) = 0;
    // The wheel, counted in detents: positive is away from the reader, the sign Qt's
    // angleDelta().y() already has. One detent is one row, and one detent is one call to
    // the engine -- measured on this build, FORM_OnMouseWheel ignores the magnitude it is
    // handed (a dy of -1 and a dy of -120 each moved a list's selection by exactly one
    // row), so a caller that passed the raw delta of a touchpad's flick would scroll a
    // twenty-row list by one row and a caller that passed each small delta separately
    // would scroll it by fifteen.
    //
    // True only when a list box has the keyboard *and* the point is inside it. The same
    // call answered false over blank paper, over a list that had just lost its focus, and
    // over a combo box -- with its own dropdown open, where the point is inside the
    // dropdown -- which is the whole of the routing rule: a focused list scrolls itself
    // and everything else scrolls the page.
    virtual bool mouseWheeled(int page, const Frame &frame, const QPointF &devicePoint,
                              int detents, Qt::KeyboardModifiers modifiers) = 0;
    // A Qt::Key code. The backend turns it into the Windows virtual key PDFium
    // asks for; the view does not get to know that there is a Windows numbering.
    virtual bool keyPressed(int qtKey, Qt::KeyboardModifiers modifiers) = 0;
    virtual bool characterTyped(QChar character, Qt::KeyboardModifiers modifiers) = 0;
    // One committed input-method string, in one call.
    virtual void commitText(const QString &text) = 0;

    // ---- focus ----
    virtual bool hasFocusedField() const = 0;
    virtual int focusedPage() const = 0; // -1 when nothing has focus
    virtual Field focusedFieldType() const = 0;
    // The focused field's own rectangle, in the page's user space, and which page
    // it is on. This is what an input-method candidate window is placed against:
    // measured on this build, nothing in these headers reports the caret's position
    // inside a widget, so the candidate box follows the field rather than the
    // insertion point.
    virtual bool focusedFieldRect(int *page, QRectF *userSpace) const = 0;
    // Esc, a click on blank paper, a tab change: the widget commits and lets go.
    virtual void releaseFocus() = 0;

    // ---- the undo that belongs to the field ----
    //
    // FORM_Undo/Redo walk the focused widget's own edit history, which is not the
    // structural list in src/edit/documenteditor.cpp: a reader who typed a name,
    // deleted it and typed another one expects 撤销 to take back one keystroke
    // while the page has that field in it, and to take back a deleted page when it
    // does not. Which of the two it is comes from these.
    virtual bool canUndoFocused() const = 0;
    virtual bool canRedoFocused() const = 0;
    virtual void undoFocused() = 0;
    virtual void redoFocused() = 0;

    // ---- what the callbacks collected while the last event was in flight ----
    //
    // Both are drained by the caller after every routed event, on the thread the
    // event arrived on: PDFium calls them synchronously from inside the FORM_* call.
    virtual QVector<Repaint> takeRepaints() = 0;
    virtual Pointer takeRequestedPointer() = 0;

    // ---- the question a save asks ----
    //
    // How many fields now hold something other than what the file says. Answered by
    // comparing each field this reader has touched against the value it had before the
    // first event that could have changed it -- not by diffing the whole form, which
    // for a long document would mean loading every page to ask a question about one.
    // A click and an arrow key do not raise the count; a keystroke that ends up back
    // where it started does not either, and the number going back to zero is what takes
    // the star off the title.
    //
    // A count rather than a yes/no because the sentence a reader is asked to agree to
    // before a tab closes names what it is throwing away, and "改了 2 个字段" is a
    // different thing to read than "有改动".
    virtual int differingFieldCount() const = 0;
    // Everything the reader did to a field is dropped, as it is after a load: the
    // file the tab is reading now is the one the values are compared against.
    virtual void forgetFieldValueSnapshots() = 0;

    // Writes the document, field values included, to a hidden file in `directory`
    // and hands back its path. This is the first stage of a 保存 and the only thing
    // in this class that puts bytes anywhere: the reader's own file is never the
    // target, and the replace that puts the staged file in place is DocumentEditor's
    // (docs/handoff-03.md F8's order, unchanged). The focused field is committed
    // first, because a value that is still in the widget does not reach the bytes.
    virtual bool stageFieldValues(const QString &directory, QString *staged, QString *error) = 0;
};
