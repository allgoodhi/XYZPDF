// The v1 document backend: Qt PDF, which is PDFium behind a Qt-shaped API.
//
// Everything the program does with a page goes through here, and this is the
// only file that includes QtPdf. v2 adds a PDFium backend of its own when the
// editing work needs the API Qt PDF does not expose; swapping it in should be
// a change to one line in DocumentTab.
#pragma once

#include "document.h"

#include <QString>

#include <memory>

class QPdfDocument;
class QPdfBookmarkModel;
class QPdfLinkModel;

class QtPdfBackend final : public Document
{
public:
    QtPdfBackend();
    ~QtPdfBackend() override;

    Status load(const QString &path, const QString &password) override;
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

    // The underlying document, for render/PageRenderer.
    //
    // This one is deliberately not part of the Document interface: it is the
    // Qt PDF handle, and putting it there would make every other backend
    // pretend to have one. render/PageRenderer is Qt PDF specific in v1 and
    // says so.
    //
    // Null before a successful load, and owned here: whoever takes this pointer
    // must not outlive the backend.
    QPdfDocument *pdfDocument() const;

private:
    // Rebuilt when the document changes; Qt's models own their own bookkeeping
    // and are cheap to point at a new document.
    void attachModels();

    std::unique_ptr<QPdfDocument> document;
    // The bookmark model is kept because reading it does not move it, and it is
    // expensive to build: outline() only asks it questions. The link model is
    // NOT kept, because reading it means telling it which page to look at --
    // see links(), which says why that cannot live behind a const method.
    std::unique_ptr<QPdfBookmarkModel> bookmarks;

    QString loadedPath;
    QString errorText;
    // Whether the open document needed a password, which is the only question
    // about encryption Qt PDF will answer -- it has no API that reports the
    // file's encryption or its permissions, so this is what loaded with one.
    bool neededPassword = false;
};
