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
//
// ---- why nothing is rendered until the panel is on screen ----
//
// "Off the UI thread" is not "free". Every one of those renders holds the same
// global PDFium lock the page the reader is waiting for needs, so a job that
// walks a whole document is a job that makes the document slow to read -- and
// the sidebar starts hidden, so until this slice it was doing that for a panel
// nobody had asked to see.
//
// Measured on this machine with tools/pdfprobe.cpp (docs/review-03.md §2.1):
// shrinking the output to a 120 px thumbnail does *not* make a scanned page
// cheap -- 17 ms against 30 ms for the whole page, because the cost is in
// decoding the image, not in sampling it. A 500-page scan is therefore about
// 8.5 seconds of lock held away from the reader, at the moment they open the
// file. A text page is 0 ms, so this is invisible on exactly the documents that
// are already fast and expensive on exactly the ones that are already slow.
//
// So start() now only remembers what to render, and the walk begins the first
// time the panel is actually shown. A reader who never opens the sidebar never
// pays for it.
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

    // Rebuild the list for a freshly opened document and take on the job of
    // rendering its pages. `sizes` is every page's size in points, in page
    // order. Any job still running from a previous document is cancelled and
    // waited for first, so the old document outlives the render that was
    // reading it.
    //
    // The rendering itself does not begin here unless the panel is already on
    // screen -- see the note above. Passing a null document (or no pages) is
    // how a tab says "there is nothing to show", and it clears the job as well
    // as the list.
    void start(const QVector<QSizeF> &sizes, Document *document);

    // Whether there is a document whose pages have not been walked yet. True
    // between an open and the first time the panel is shown; the tests ask it,
    // because "nothing has been rendered" and "nothing is left to render" are
    // the two halves of the deferral and only one of them is visible in the
    // pictures.
    bool waiting() const { return waitingDocument != nullptr; }

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

protected:
    // Where the deferred walk begins. Qt sends this when the sidebar is shown
    // *and* this is the tab in front of it, which is exactly the moment the
    // pictures start being worth their cost.
    void showEvent(QShowEvent *event) override;

private:
    void applyThumbnail(int page, const QImage &image);
    // Starts the walk if there is one waiting and the panel can be seen.
    // Called from start() and from showEvent(), so that it does not matter
    // which of the two happens first.
    void beginIfVisible();

    QStandardItemModel *model = nullptr;
    QSharedPointer<QAtomicInt> generation; // cancelled when a new job starts
    QFuture<void> pending;
    // The job start() was given, held until the panel is shown. The document is
    // borrowed, like the one the running job holds: the tab cancels and drains
    // before it reloads, and replaces this through start() on the way past, so
    // the pointer never outlives what it points at.
    QVector<QSizeF> waitingSizes;
    Document *waitingDocument = nullptr;
    int thumbWidth = 120;
};
