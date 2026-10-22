// A column of page thumbnails, for jumping around a document by sight.
//
// The second tab of the reading sidebar, beside the bookmarks (see OutlinePanel).
// Where the bookmarks are the document's own table of contents, the thumbnails
// are always there -- every page has a picture of itself -- so this is the
// navigation that works on a document with no bookmarks at all.
//
// ---- why the pictures come from a background job ----
//
// A thumbnail is a render, and a render is a PDFium call behind the same global
// lock the view uses. Rendering a five-hundred-page document's worth of them is
// minutes of it, so they are made off the UI thread and posted back to be
// dropped into the list. The job is cancellable and its future is waited for
// before the document it is reading is replaced, for the same reason the search
// and the export are (see DocumentTab::drainRenders): a render still running
// inside a document that has been reloaded is a crash, and it is the one
// failure mode that only shows up when somebody opens a second file quickly.
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
// ---- why only the strip that can be seen is rendered, and kept ----
//
// The same argument made twice over. A walk over the whole document is work
// nobody asked for *and* memory nobody capped: every picture stayed in the
// model for as long as the document was open, 120x170 of ARGB each, about
// 81 KB -- 49 MB on a 600-page book, outside the render cache's budget
// entirely, which is the mistake docs/review-02.md found in PageView::images
// one layer down (docs/review-03.md §2.2).
//
// So the rows near the viewport are rendered and the rest are let go, which
// bounds both at once: the pictures held are the ones a scroll of a screen or
// two can reach. Rows carry their size from the start, before any picture
// arrives, because a strip whose rows grow as the pictures land is a strip
// whose "which rows are visible" answer changes while it is being acted on.
#pragma once

#include <QFuture>
#include <QListView>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QSizeF>
#include <QVector>

#include <functional>

class Document;
class QAtomicInt;
class QStandardItemModel;

class ThumbnailPanel : public QListView
{
    Q_OBJECT

public:
    // One row of the strip: which page of the open document it shows, which way
    // up, and what is written under it.
    //
    // Rows rather than "one per page" because the strip can show the document a
    // save would write rather than the one on disk (docs/review-03.md §1.3):
    // pages taken out are absent, pages moved are where they will be, pages
    // turned are turned. A row whose page is not in the open document -- one a
    // merge brings in from another file -- has no page to render, and says so
    // in its label instead.
    struct Row
    {
        // Zero-based page of the open document to draw, or -1 for none.
        int sourcePage = -1;
        // Degrees clockwise on top of the page's own /Rotate: 0, 90, 180, 270.
        int rotation = 0;
        QString label;
        QString toolTip;
    };

    explicit ThumbnailPanel(QWidget *parent = nullptr);

    // Rebuild the list for a freshly opened document and take on the job of
    // rendering its pages. `sizes` is every page's size in points, in page
    // order. Any job still running from a previous document is cancelled and
    // waited for first, so the old document outlives the render that was
    // reading it.
    //
    // The rendering itself does not begin here unless the panel is already on
    // screen, and then only for the rows near the viewport -- see the notes
    // above. Passing a null document (or no pages) is how a tab says "there is
    // nothing to show", and it clears the job as well as the list.
    void start(const QVector<QSizeF> &sizes, Document *document);

    // Replaces what the rows show, keeping the document start() was given. The
    // pictures already held belong to the old rows and are let go; the band is
    // rendered again for the new ones. Clicking a row jumps to its sourcePage,
    // and a row without one does nothing.
    void setRows(const QVector<Row> &newRows);
    int rowCount() const { return int(rows.size()); }
    const Row &rowAt(int row) const { return rows.at(row); }

    // Whether there is a document none of whose pages has been drawn yet. True
    // between an open and the first time the panel is shown; the tests ask it,
    // because "nothing has been rendered" and "nothing is left to render" are
    // the two halves of the deferral and only one of them is visible in the
    // pictures.
    bool waiting() const { return source != nullptr && held.isEmpty(); }

    // How many pictures are being held. The bounding is the point, so this is
    // the number a test can put a limit on.
    int heldCount() const { return int(held.size()); }

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
    // The three ways the band of rows worth rendering can move: the panel comes
    // on screen, it changes size, or the reader scrolls it (wired to the
    // scrollbar in the constructor).
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyThumbnail(int row, const QImage &image);
    // The model, rebuilt from `rows`. Every row the same height, decided before
    // any picture exists -- see the constructor.
    void rebuildModel();
    // Renders what is near the viewport and lets go of what is not. Safe to
    // call at any time and from anywhere: it does nothing while the panel is
    // hidden, and while a batch is in flight it only notes that the answer has
    // moved, so that scrolling during a render does not start a second one.
    void refreshBand();
    void startBatch(const QVector<int> &pages);
    // Rows [first, last] of the band, by the row height every row was given in
    // start(). Computed arithmetically rather than through indexAt(), which
    // answers -1 for a point past the last row and would need the empty cases
    // handling twice.
    void bandRows(int viewports, int *first, int *last) const;

    QStandardItemModel *model = nullptr;
    QSharedPointer<QAtomicInt> generation; // cancelled when a new job starts
    QFuture<void> pending;
    bool batchRunning = false;
    // Set while a batch is running and the band has moved underneath it; the
    // next refresh happens when that batch reports in.
    bool bandMovedDuringBatch = false;

    // The document being shown and its pages' sizes, kept for as long as it is
    // open because the band is recomputed on every scroll. The document is
    // borrowed: the tab cancels and drains before it reloads, and replaces this
    // through start() on the way past, so the pointer never outlives what it
    // points at.
    QVector<QSizeF> pageSizes;
    Document *source = nullptr;
    // What each row shows. start() fills it with one row per page; setRows()
    // replaces it.
    QVector<Row> rows;
    // The rows that have a picture in them right now.
    QSet<int> held;
    int rowHeight = 0;

    int thumbWidth = 120;
};
