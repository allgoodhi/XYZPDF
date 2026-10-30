// The v2 document backend: PDFium called directly, without Qt's wrapper.
//
// ---- why ----
//
// Qt PDF is PDFium behind a Qt-shaped API, and it is the API that made the form
// fields invisible: the form-fill environment is never initialised, so a filled
// widget paints nothing (docs/pdfium-plan.md sec.4 and sec.82). It also stops at
// the five things a viewer needs and has no room for the ones an editor will.
// This class is the same library with its own flags, which is the first step of
// the swap docs/pdfium-plan.md lays out.
//
// ---- what it answers, and what it draws ----
//
// All of Document. F7-B moved the five calls a page view makes (load, pageCount,
// pageSize, render, text), F7-C added the form layer and F7-E the rest: textRuns,
// the character-range selection, outline, links and metadata. Since F7-E this is
// the only backend in the build -- Qt PDF and the development switch that chose
// between them are both gone -- so a package carries pdfium.dll and nothing else
// that can read a PDF.
//
// F7-C adds the form layer's first half: FPDFDOC_InitFormFillEnvironment per
// document and FPDF_FFLDraw after every page render, so a field's appearance
// (the picture its author or PDFium generated) is now part of the page image.
// G1 adds the second half: the FORM_On* events, so a reader can fill a field and
// see it, and FPDF_SaveAsCopy, so the filled document can be written out when
// they ask (FormFill::stageFieldValues). Two limits still hold and are not bugs:
// the value in a widget is PDFium's own face -- its font, size and alignment come
// from its rules, not the original design -- and a document whose form is also
// an XFA packet still shows only its field copy, unfilled (sec.4.3; this build
// has no XFA module). /NeedAppearances is not answered either. Scripts are not
// either: this build has no V8, so a field's calculation, validation and
// formatting actions never run.
//
// The bookmark read gives a title and a page and no place inside the page, which
// is what the sidebar asks for; where the file said to look is carried by the
// editor's own read of /Dest (src/edit/documenteditor.cpp), not by this tree.
//
// ---- one lock, one document, one worker thread ----
//
// fpdfview.h's own first lines: none of the PDFium APIs are thread-safe, and an
// embedder has to serialise them itself. This class follows Qt PDF's model: one
// global recursive mutex for every FPDF call in the process, one document object,
// and rendering moved to a worker (render/PageRenderer) rather than parallelised.
// Two consequences carry over unchanged from pagerenderer.h:
//
//   * One call runs at a time inside PDFium, so there is no multi-core speedup.
//
//   * Any question asked from the UI thread waits for a render in progress. A
//     300 ms scanned page is a 300 ms freeze for pageSize(), text(), or anything
//     else reaching the document from that thread. The lock is not a promise of
//     responsiveness.
//
// ---- the file handle is ours (docs/pdfium-plan.md R7) ----
//
// FPDF_LoadDocument() opens the path itself, and the handle it keeps cannot be
// used by anyone else's delete or replace -- docs/review-03.md sec.3.4 measured
// that, and the preview pane has read a copy because of it since sec.66. This
// backend opens the file with FILE_SHARE_READ | FILE_SHARE_WRITE |
// FILE_SHARE_DELETE and hands PDFium a FPDF_FILEACCESS instead, so a document on
// screen does not stop a writer from replacing its file. That is also what F8's
// ReplaceFileW save needs.
#pragma once

#include "document.h"
#include "formfill.h"

#include <memory>

class PdfiumBackend : public Document, public FormFill
{
public:
    PdfiumBackend();
    ~PdfiumBackend() override;

    Status load(const QString &path, const QString &password) override;
    Status loadData(const QByteArray &bytes, const QString &password) override;
    void close() override;
    QString lastError() const override;
    QString path() const override;
    int pageCount() const override;
    QSizeF pageSize(int page) const override;
    QImage render(int page, QSize pixels, const RenderOptions &options) override;
    QString text(int page) const override;

    QVector<TextRun> textRuns(int page) const override;
    int characterIndexAt(int page, QPointF point) const override;
    PageSelection pageSelection(int page, int from, int to) const override;
    bool hasTextAt(int page, QPointF point) const override;
    Outline outline() const override;
    QVector<Link> links(int page) const override;
    DocMetadata metadata() const override;
    int signatureCount() const override;
    int docMdpPermission() const override;

    bool fillable() const override;
    void setLockedFieldNames(const QStringList &names) override;
    Field fieldAt(int page, const Frame &frame, const QPointF &devicePoint) const override;
    bool mouseMoved(int page, const Frame &frame, const QPointF &devicePoint) override;
    bool mousePressed(int page, const Frame &frame, const QPointF &devicePoint,
                      Qt::KeyboardModifiers modifiers) override;
    bool mouseReleased(int page, const Frame &frame, const QPointF &devicePoint,
                       Qt::KeyboardModifiers modifiers) override;
    bool mouseDoubleClicked(int page, const Frame &frame, const QPointF &devicePoint) override;
    bool mouseWheeled(int page, const Frame &frame, const QPointF &devicePoint, int detents,
                      Qt::KeyboardModifiers modifiers) override;
    bool keyPressed(int qtKey, Qt::KeyboardModifiers modifiers) override;
    bool characterTyped(QChar character, Qt::KeyboardModifiers modifiers) override;
    void commitText(const QString &text) override;
    bool hasFocusedField() const override;
    int focusedPage() const override;
    Field focusedFieldType() const override;
    bool focusedFieldRect(int *page, QRectF *userSpace) const override;
    void releaseFocus() override;
    bool canUndoFocused() const override;
    bool canRedoFocused() const override;
    void undoFocused() override;
    void redoFocused() override;
    QVector<Repaint> takeRepaints() override;
    Pointer takeRequestedPointer() override;
    int differingFieldCount() const override;
    void forgetFieldValueSnapshots() override;
    bool stageFieldValues(const QString &directory, QString *staged, QString *error) override;

private:
    // The Word export's reader of page objects (src/document/pdfiumcontent.cpp). A
    // friend rather than a member so that only the targets that export link it: the
    // two shell DLLs and most tests never see that file.
    friend struct PdfiumContentReader;

    // The second half of load() and loadData(), once PDFium has answered.
    Status adoptLoaded(const QString &path, const QString &password);

    struct Impl;
    std::unique_ptr<Impl> impl;
};
