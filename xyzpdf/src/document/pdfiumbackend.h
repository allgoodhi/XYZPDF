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
// ---- what it answers, what it draws, and what it does not yet ----
//
// F7-B moves the five calls a page view makes: load, pageCount, pageSize, render
// and text. Everything below them -- textRuns, the character-range selection,
// outline, links, metadata -- is still QtPdfBackend's in this build and returns
// nothing here until F7-E. That is a real limit of the development switch, not a
// claim that those methods are optional: a build configured with
// -DXYZPDF_DOCUMENT_BACKEND=pdfium shows pages and text and has no selection,
// sidebar tree, links or properties until F7-E lands. The default build is Qt.
//
// F7-C adds the form layer's first half: FPDFDOC_InitFormFillEnvironment per
// document and FPDF_FFLDraw after every page render, so a field's appearance
// (the picture its author or PDFium generated) is now part of the page image.
// Nothing is accepted as input and nothing is written: this is the "draw only"
// step of pdfium-plan.md sec.7. Two limits still hold and are not bugs: the
// value in a widget is PDFium's own face -- its font, size and alignment come
// from its rules, not the original design -- and a document whose form is also
// an XFA packet still shows only its field copy (sec.4.3; this build has no XFA
// module). /NeedAppearances is not answered either.
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

#include <memory>

class PdfiumBackend : public Document
{
public:
    PdfiumBackend();
    ~PdfiumBackend() override;

    Status load(const QString &path, const QString &password) override;
    QString lastError() const override;
    QString path() const override;
    int pageCount() const override;
    QSizeF pageSize(int page) const override;
    QImage render(int page, QSize pixels, const RenderOptions &options) override;
    QString text(int page) const override;

    // F7-E. Answers nothing until then; see the header comment.
    QVector<TextRun> textRuns(int page) const override;
    int characterIndexAt(int page, QPointF point) const override;
    PageSelection pageSelection(int page, int from, int to) const override;
    bool hasTextAt(int page, QPointF point) const override;
    Outline outline() const override;
    QVector<Link> links(int page) const override;
    DocMetadata metadata() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
