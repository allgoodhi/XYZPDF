// A column of page thumbnails, for jumping around a document by sight.
//
// The second tab of the reading sidebar, beside the bookmarks (see OutlinePanel).
// Where the bookmarks are the document's own table of contents, the thumbnails
// are always there -- every page has a picture of itself -- so this is the
// navigation that works on a document with no bookmarks at all.
//
// ---- why the pictures come from one background job ----
//
// A thumbnail is a render, and a render is a PDFium call behind the same global
// lock the view uses. Rendering a five-hundred-page document's worth of them is
// minutes of it, so they are made off the UI thread -- one job that walks the
// pages, renders each, and posts the picture back to be dropped into the list.
// The job is cancellable and its future is waited for before the document it is
// reading is replaced, for the same reason the search and the export are (see
// DocumentTab::drainRenders): a render still running inside a document that has
// been reloaded is a crash, and it is the one failure mode that only shows up
// when somebody opens a second file quickly.
#pragma once

#include <QFuture>
#include <QListView>
#include <QSharedPointer>

#include <functional>

class Document;
class QAtomicInt;
class QStandardItemModel;

class ThumbnailPanel : public QListView
{
    Q_OBJECT

public:
    explicit ThumbnailPanel(QWidget *parent = nullptr);

    // Rebuild the list for a freshly opened document and start rendering its
    // pages. `sizes` is every page's size in points, in page order. Any job still
    // running from a previous document is cancelled and waited for first, so the
    // old document outlives the render that was reading it.
    void start(const QVector<QSizeF> &sizes, Document *document);

    // Whether the background job is still running, and the means to stop it --
    // used by DocumentTab to wait for it before a reload or teardown.
    bool rendering() const;
    void cancel();
    void cancelAndDrain();

    // Follow the page the reader has scrolled to: highlight it and bring it into
    // view without stealing focus from the page.
    void setCurrentPage(int page);

Q_SIGNALS:
    void jumpToPage(int page);

private:
    void applyThumbnail(int page, const QImage &image);

    QStandardItemModel *model = nullptr;
    QSharedPointer<QAtomicInt> generation; // cancelled when a new job starts
    QFuture<void> pending;
    int thumbWidth = 120;
};
