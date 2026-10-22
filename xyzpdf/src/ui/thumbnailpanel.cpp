#include "thumbnailpanel.h"

#include "../document/document.h"
#include "../document/pagemetrics.h"

#include <QAtomicInt>
#include <QIcon>
#include <QImage>
#include <QMetaObject>
#include <QPixmap>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace {

// A page's thumbnail size: a fixed width, the height following the page's own
// aspect. A zero-width page (which should not exist, but documents lie) gets a
// square so the render is not asked for a zero-pixel image.
QSize thumbnailSize(const QSizeF &pagePoints, int width)
{
    if (pagePoints.width() <= 0 || pagePoints.height() <= 0)
        return QSize(width, width);
    const double height = double(width) * pagePoints.height() / pagePoints.width();
    return QSize(width, qMax(1, int(std::lround(height))));
}

// How far either side of the viewport is worth having a picture for, and how
// far one is worth keeping, in viewports.
//
// Render one screen beyond what is shown so that a scroll lands on pictures
// rather than on numbers; keep three, so that scrolling a screen down and back
// up does not re-render what was just let go. The memory this costs is the
// whole panel's upper bound: the rows within three viewports, about 81 KB each.
constexpr int kRenderViewports = 1;
constexpr int kKeepViewports = 3;

} // namespace

ThumbnailPanel::ThumbnailPanel(QWidget *parent)
    : QListView(parent)
{
    model = new QStandardItemModel(this);
    setModel(model);

    setViewMode(QListView::IconMode);
    setFlow(QListView::TopToBottom);
    setWrapping(false); // one column, scrolled vertically -- a strip, not a grid
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    // Every row the same height, decided in start() before any picture exists.
    // Not cosmetic: the band of rows worth rendering is read off the scroll
    // position, and rows that grow when their picture arrives would move the
    // answer while it was being acted on.
    setUniformItemSizes(true);
    // Pixels, not items. QListView scrolls per item by default, which makes the
    // scrollbar's value an index -- and the band below reads that value as a
    // position in the strip. Measured the hard way: with the default, every
    // band came out as "the first two rows", so scrolling to the end of a
    // 600-page document rendered nothing there. It is also the smoother scroll
    // of the two.
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setIconSize(QSize(thumbWidth, int(thumbWidth * 1.4)));
    setSpacing(10);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(this, &QListView::clicked, this, [this](const QModelIndex &index) {
        // The page the row shows, not the row: once the strip is showing the
        // document a save would write, row 3 may be page 7 of the one on screen
        // -- and a row a merge brought in has no page here to go to.
        if (!index.isValid() || index.row() >= rows.size())
            return;
        const int page = rows.at(index.row()).sourcePage;
        if (page >= 0)
            Q_EMIT jumpToPage(page);
    });
    // Scrolling is the usual way the band moves.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { refreshBand(); });
}

void ThumbnailPanel::start(const QVector<QSizeF> &sizes, Document *document)
{
    // Finish any job still reading the previous document before its data goes
    // away, then start afresh under a new cancellation token.
    cancelAndDrain();
    generation = QSharedPointer<QAtomicInt>::create(0);

    pageSizes = sizes;
    source = sizes.isEmpty() ? nullptr : document;

    rows.clear();
    rows.reserve(sizes.size());
    for (int page = 0; page < sizes.size(); ++page)
        rows.append(Row{page, 0, QString::number(page + 1), QString()});
    rebuildModel();
    refreshBand();
}

void ThumbnailPanel::setRows(const QVector<Row> &newRows)
{
    // The job in flight is rendering pictures for rows that are about to mean
    // something else, so it goes first -- and its late results are dropped by
    // the token they carry (see startBatch).
    cancelAndDrain();
    generation = QSharedPointer<QAtomicInt>::create(0);
    rows = newRows;
    rebuildModel();
    refreshBand();
}

void ThumbnailPanel::rebuildModel()
{
    model->clear();
    held.clear();

    // One height for every row, from the first page's shape: the icon size is
    // uniform too, so a page of another shape is drawn inside the same box.
    // The text under it is a page number, and 24 px is room for one line.
    const QSize picture = pageSizes.isEmpty() ? QSize(thumbWidth, int(thumbWidth * 1.4))
                                              : thumbnailSize(pageSizes.first(), thumbWidth);
    rowHeight = picture.height() + 24 + 2 * spacing();

    for (const Row &row : std::as_const(rows)) {
        auto *item = new QStandardItem(row.label);
        item->setEditable(false);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setSizeHint(QSize(thumbWidth + 2 * spacing(), rowHeight - 2 * spacing()));
        if (!row.toolTip.isEmpty())
            item->setToolTip(row.toolTip);
        model->appendRow(item);
    }
    viewport()->update();
}

void ThumbnailPanel::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    refreshBand();
}

void ThumbnailPanel::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    refreshBand();
}

void ThumbnailPanel::bandRows(int viewports, int *first, int *last) const
{
    *first = 0;
    *last = -1;
    const int rows = model ? model->rowCount() : 0;
    if (rows == 0)
        return;

    // The pitch is asked of the view rather than worked out from the size hint.
    // Measured: the hint this class sets is 214 px and the rows actually sit
    // 203 apart, and 11 px of error per row is 34 rows of error by the end of a
    // 600-page book -- enough that scrolling to the bottom rendered a band that
    // stopped short of it. Two rectangles answer exactly, and the items are
    // uniform so two is all it takes.
    const QRect firstRow = rectForIndex(model->index(0, 0));
    const int pitch =
        rows > 1 ? rectForIndex(model->index(1, 0)).top() - firstRow.top() : firstRow.height();
    if (pitch <= 0)
        return; // not laid out yet; the next scroll or resize asks again

    const int top = verticalScrollBar()->value();
    const int height = qMax(1, viewport()->height());
    const int margin = viewports * height;
    *first = qBound(0, (top - margin - firstRow.top()) / pitch, rows - 1);
    *last = qBound(0, (top + height + margin - firstRow.top()) / pitch, rows - 1);
}

void ThumbnailPanel::refreshBand()
{
    if (!source || pageSizes.isEmpty() || !isVisible())
        return;
    if (batchRunning) {
        // One batch at a time: a scroll while pages are being rendered is
        // answered when that batch reports in, which keeps a drag down the
        // strip from starting a job per pixel.
        bandMovedDuringBatch = true;
        return;
    }

    int keepFirst = 0;
    int keepLast = -1;
    bandRows(kKeepViewports, &keepFirst, &keepLast);
    for (auto page = held.begin(); page != held.end();) {
        if (*page >= keepFirst && *page <= keepLast) {
            ++page;
            continue;
        }
        if (auto *item = model->item(*page))
            item->setIcon(QIcon());
        page = held.erase(page);
    }

    int renderFirst = 0;
    int renderLast = -1;
    bandRows(kRenderViewports, &renderFirst, &renderLast);
    QVector<int> wanted;
    for (int row = renderFirst; row <= renderLast; ++row) {
        if (held.contains(row) || row >= rows.size() || rows.at(row).sourcePage < 0)
            continue; // already drawn, or nothing here to draw
        wanted.append(row);
    }
    if (!wanted.isEmpty())
        startBatch(wanted);
}

void ThumbnailPanel::startBatch(const QVector<int> &wantedRows)
{
    const QSharedPointer<QAtomicInt> cancel = generation;
    const int width = thumbWidth;
    const QVector<QSizeF> sizes = pageSizes;
    Document *document = source;
    // Which page and which way up, for each row asked for -- copied, because the
    // rows may be replaced on the UI thread while this runs.
    QVector<QPair<int, Row>> work;
    work.reserve(wantedRows.size());
    for (const int row : wantedRows)
        work.append({row, rows.at(row)});

    batchRunning = true;
    bandMovedDuringBatch = false;
    pending = QtConcurrent::run([this, document, sizes, work, width, cancel] {
        for (const auto &job : work) {
            if (cancel->loadRelaxed())
                break; // the document is going away, or a newer job took over
            const int row = job.first;
            const int page = job.second.sourcePage;
            if (page < 0 || page >= sizes.size())
                continue;
            RenderOptions options;
            options.annotations = true;
            options.optimizedForLcd = false;
            options.rotationDegrees = job.second.rotation;
            // The turned page's shape, so that a page turned a quarter is drawn
            // landscape rather than squeezed into its old portrait box.
            const QSizeF shape = PageMetrics::rotated(sizes.at(page), job.second.rotation);
            const QImage image = document->render(page, thumbnailSize(shape, width), options);
            if (image.isNull() || cancel->loadRelaxed())
                continue;
            // Hand the finished picture back to the UI thread. The context is
            // `this`, so if the panel is gone by the time the event is delivered
            // Qt drops it rather than calling into freed memory; the token is
            // asked again there, because rows replaced after this was posted
            // would otherwise receive a picture of a page they no longer show.
            QMetaObject::invokeMethod(
                this,
                [this, row, image, cancel] {
                    if (!cancel->loadRelaxed())
                        applyThumbnail(row, image);
                },
                Qt::QueuedConnection);
        }
        // And the same for "that is the batch", posted from here rather than
        // read off the future: a future is not finished until after this lambda
        // returns, and the next band has to be decided on the UI thread anyway.
        QMetaObject::invokeMethod(
            this,
            [this, cancel] {
                // A batch that was cancelled has already been accounted for by
                // whoever cancelled it -- and a newer batch may be running now,
                // which this must not mark as finished.
                if (cancel->loadRelaxed())
                    return;
                batchRunning = false;
                // The band may have moved while this ran. If it did not, there
                // is nothing to do: everything asked for has arrived.
                if (bandMovedDuringBatch)
                    refreshBand();
            },
            Qt::QueuedConnection);
    });
}

void ThumbnailPanel::applyThumbnail(int row, const QImage &image)
{
    if (row < 0 || row >= model->rowCount())
        return; // a late result for a document that has since been replaced
    if (auto *item = model->item(row)) {
        item->setIcon(QIcon(QPixmap::fromImage(image)));
        held.insert(row);
    }
}

bool ThumbnailPanel::rendering() const
{
    return pending.isRunning();
}

void ThumbnailPanel::cancel()
{
    if (generation)
        generation->storeRelaxed(1);
}

void ThumbnailPanel::cancelAndDrain()
{
    cancel();
    if (pending.isStarted())
        pending.waitForFinished();
    // Whoever is draining is about to replace the document, and the queued
    // "that is the batch" would arrive after it had.
    batchRunning = false;
    bandMovedDuringBatch = false;
}

void ThumbnailPanel::setCurrentPage(int page)
{
    // `page` is a page of the document on screen, and the row showing it is
    // found by what each row shows: once a delete or a move is pending they are
    // no longer the same number, and a page being taken out has no row at all
    // -- in which case nothing is highlighted rather than the wrong thing.
    for (int row = 0; row < rows.size(); ++row) {
        if (rows.at(row).sourcePage != page)
            continue;
        const QModelIndex index = model->index(row, 0);
        setCurrentIndex(index);
        scrollTo(index, QAbstractItemView::PositionAtCenter);
        return;
    }
    clearSelection();
}
