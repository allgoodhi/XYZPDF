// The v1 document backend: Qt PDF, which is PDFium behind a Qt-shaped API.
//
// Everything the program does with a page goes through the Document seam, and
// this is one implementation of it -- the only place the reader names QtPdf.
// The concrete type itself is named in documentfactory.cpp, which is what a
// backend swap changes: DocumentTab, the two shell handlers and pdfprobe ask
// the factory and never see this class. v2 adds a PDFium backend of its own
// when the editing work needs the API Qt PDF does not expose.
#pragma once

#include "document.h"

#include <QBitArray>
#include <QHash>
#include <QString>
#include <QVector>

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

private:
    // A page's text and the boxes of its lines, kept for a few pages.
    //
    // Both come out of one getAllText() call, which is free: measured on a page
    // of 756 characters, the text and every line box costs 0 ms. A *character*
    // box is a different story -- getSelectionAtIndex(), which is the only way
    // to ask for one, is about a millisecond each (measured: 20 of them take
    // 19 ms). A table of those for that page therefore cost 753 ms to build,
    // which is why there is no such table: the index at a point is found by
    // bisection, and this is what the bisection is told about the page.
    struct PageText
    {
        QString text;
        // One box per line, in the order textRuns() reports them, in the page's
        // own points.
        QVector<QRectF> lines;
    };

    const PageText &pageTextOf(int page) const;
    // The line nearest a point vertically, or -1 for a page with no lines.
    static int nearestLine(const QVector<QRectF> &lines, double y);
    // The box of one character, remembered for the page it belongs to.
    //
    // The bisection in characterIndexAt() asks for the same character boxes over
    // and over -- every line of a page narrows toward the same handful of indices,
    // and a page of a pin-out diagram has hundreds of lines. Caching turns those
    // repeated ~1 ms queries into one query per character; for the interactive
    // path (a click, a drag) it is invisible, and for an export of a text-heavy
    // page it is the difference between seconds and tens of seconds.
    QRectF characterBoxAt(int page, const PageText &pageContent, int index) const;
    // Whether the character at `index` of `page` is before `point` in reading
    // order -- the predicate the bisection needs to be monotone, which is what
    // the line boxes are for. See the definition for the two ways it used not to
    // be.
    bool characterIsBefore(int page, const PageText &pageContent, int index, QPointF point) const;

    // Rebuilt when the document changes; Qt's models own their own bookkeeping
    // and are cheap to point at a new document.
    void attachModels();

    std::unique_ptr<QPdfDocument> document;
    // The bookmark model is kept because reading it does not move it, and it is
    // expensive to build: outline() only asks it questions. The link model is
    // NOT kept, because reading it means telling it which page to look at --
    // see links(), which says why that cannot live behind a const method.
    std::unique_ptr<QPdfBookmarkModel> bookmarks;

    // The pages that have been asked about, and the order they were read in so
    // that the oldest can be let go. Four pages: a drag across a page boundary
    // alternates between two, and a reader paging through a document touches
    // one at a time.
    mutable QHash<int, PageText> pageTexts;
    mutable QVector<int> pageTextsOrder;

    // The character boxes asked for so far, per page, with which of them have
    // actually been computed (a null box is a real answer for a line break, so
    // an empty rectangle cannot double as "not asked yet"). A few pages, oldest
    // let go, the same way the line boxes are kept.
    struct CharBoxes
    {
        QVector<QRectF> boxes;
        QBitArray computed;
    };
    mutable QHash<int, CharBoxes> charBoxes;
    mutable QVector<int> charBoxesOrder;

    QString loadedPath;
    QString errorText;
    // Whether the open document needed a password, which is the only question
    // about encryption Qt PDF will answer -- it has no API that reports the
    // file's encryption or its permissions, so this is what loaded with one.
    bool neededPassword = false;
};
