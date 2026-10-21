#include "thumbnailpanel.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QIcon>
#include <QImage>
#include <QMetaObject>
#include <QPixmap>
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
    setUniformItemSizes(false);
    setIconSize(QSize(thumbWidth, int(thumbWidth * 1.4)));
    setSpacing(10);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(this, &QListView::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid())
            Q_EMIT jumpToPage(index.row());
    });
}

void ThumbnailPanel::start(const QVector<QSizeF> &sizes, Document *document)
{
    // Finish any job still reading the previous document before its data goes
    // away, then start a fresh one under a new cancellation token.
    cancelAndDrain();
    generation = QSharedPointer<QAtomicInt>::create(0);

    model->clear();
    for (int page = 0; page < sizes.size(); ++page) {
        auto *item = new QStandardItem(QString::number(page + 1));
        item->setEditable(false);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        model->appendRow(item);
    }
    viewport()->update();

    waitingSizes.clear();
    waitingDocument = nullptr;
    if (!document || sizes.isEmpty())
        return;

    // Remembered rather than begun: the walk costs the reader the global PDFium
    // lock, and the sidebar starts hidden. See the note in the header for the
    // measurement that says how much.
    waitingSizes = sizes;
    waitingDocument = document;
    beginIfVisible();
}

void ThumbnailPanel::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    beginIfVisible();
}

void ThumbnailPanel::beginIfVisible()
{
    if (!waitingDocument || !isVisible())
        return;

    // Taken out of the waiting slot before the job starts, so that a second
    // showEvent -- the sidebar hidden and shown again while the walk runs --
    // does not start the same walk twice.
    const QVector<QSizeF> sizes = waitingSizes;
    Document *document = waitingDocument;
    waitingSizes.clear();
    waitingDocument = nullptr;

    const QSharedPointer<QAtomicInt> cancel = generation;
    const int width = thumbWidth;
    pending = QtConcurrent::run([this, document, sizes, width, cancel] {
        for (int page = 0; page < sizes.size(); ++page) {
            if (cancel->loadRelaxed())
                return; // the document is going away, or a newer job took over
            RenderOptions options;
            options.annotations = true;
            options.optimizedForLcd = false;
            const QImage image = document->render(page, thumbnailSize(sizes.at(page), width),
                                                  options);
            if (image.isNull() || cancel->loadRelaxed())
                continue;
            // Hand the finished picture back to the UI thread. The context is
            // `this`, so if the panel is gone by the time the event is delivered
            // Qt drops it rather than calling into freed memory.
            QMetaObject::invokeMethod(
                this, [this, page, image] { applyThumbnail(page, image); }, Qt::QueuedConnection);
        }
    });
}

void ThumbnailPanel::applyThumbnail(int page, const QImage &image)
{
    if (page < 0 || page >= model->rowCount())
        return; // a late result for a document that has since been replaced
    if (auto *item = model->item(page))
        item->setIcon(QIcon(QPixmap::fromImage(image)));
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
}

void ThumbnailPanel::setCurrentPage(int page)
{
    if (page < 0 || page >= model->rowCount())
        return;
    const QModelIndex index = model->index(page, 0);
    setCurrentIndex(index);
    scrollTo(index, QAbstractItemView::PositionAtCenter);
}
