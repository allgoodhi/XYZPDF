#include "documenttabbar.h"

#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>

namespace {

// A private type so only this program's tab bars react to the drag; a stray text
// drop onto the strip is not mistaken for a tab.
const char *kTabMimeType = "application/x-xyzpdf-tab";

// The one drag in flight, shared between the source and the receiving bar (same
// process, and a modal QDrag::exec means only one at a time). The source reads it
// back after the drag ends to decide whether it handed the tab off, kept it, or
// must open a new window. `sourceIndex` is -1 for a whole-window drag, which is
// how the two are told apart afterwards without the receiver having to know.
struct DragContext
{
    DocumentTabBar *source = nullptr;
    int sourceIndex = -1;
    bool consumed = false;      // some bar accepted the drop
    bool movedElsewhere = false; // accepted by a *different* window's bar
};

DragContext *dragContext = nullptr;

int dropIndexAt(const QDropEvent *event, const QTabBar *bar)
{
    // The drop is delivered to the bar itself, so the event's position is already
    // in the bar's coordinates.
    const int index = bar->tabAt(event->position().toPoint());
    return index < 0 ? bar->count() : index;
}

} // namespace

DocumentTabBar::DocumentTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setAcceptDrops(true);
}

bool DocumentTabBar::isTabDrag(const QMimeData *mime)
{
    return mime && mime->hasFormat(QLatin1String(kTabMimeType));
}

bool DocumentTabBar::readTabs(const QMimeData *mime, QList<TabRef> &docs)
{
    docs.clear();
    if (!isTabDrag(mime))
        return false;
    const QByteArray payload = mime->data(QLatin1String(kTabMimeType));
    if (payload.isEmpty())
        return false;

    // One tab per line. A Windows path can hold neither a tab nor a newline, so
    // those two are safe as the field and record separators -- which is what lets
    // a single tab and a whole window's worth of them travel the same way.
    const QStringList lines =
        QString::fromUtf8(payload).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        const QString path = parts.value(0);
        if (path.isEmpty())
            continue;
        docs.append(TabRef(path, parts.value(1).toInt()));
    }
    return !docs.isEmpty();
}

void DocumentTabBar::takeMergedTabs(const QList<TabRef> &docs)
{
    if (docs.isEmpty())
        return;

    // The drag is in flight (a modal exec on the source), so the shared context
    // is live here. Mark it consumed so the source does not also detach a window
    // for tabs that have just found a home.
    if (dragContext) {
        dragContext->consumed = true;
        // Dropped back onto its own window: leave everything where it is rather
        // than opening duplicates of the files already on screen.
        if (dragContext->source == this)
            return;
        dragContext->movedElsewhere = true;
    }
    for (const TabRef &doc : docs)
        Q_EMIT mergeRequested(doc.first, doc.second, count());
}

void DocumentTabBar::takeMergedTab(const QString &path, int page)
{
    takeMergedTabs(QList<TabRef>{TabRef(path, page)});
}

void DocumentTabBar::setInsertIndicator(int index)
{
    const int clamped = index < 0 ? -1 : qMin(index, count());
    if (insertIndicator == clamped)
        return;
    insertIndicator = clamped;
    update();
}

DocumentTabWidget::DocumentTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBar(new DocumentTabBar(this));
}

void DocumentTabBar::mousePressEvent(QMouseEvent *event)
{
    pressIndex = -1;
    pressWholeWindow = false;

    QTabBar::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    const int index = tabAt(event->pos());
    pressGlobal = event->globalPosition().toPoint();
    if (index >= 0) {
        pressIndex = index;
        return;
    }

    // The blank strip beside the tabs is the handle for "move this whole window".
    // QTabBar ignores a press there, and a press that is not taken keeps no grab
    // of its own, so the moves that would turn it into a drag would be lost the
    // moment the pointer left a row that is thirty pixels tall. Taking the event
    // and holding the grab is what lets the gesture start at all.
    pressWholeWindow = true;
    event->accept();
    grabMouse();
    holdingPressGrab = true;
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent *event)
{
    if ((pressIndex >= 0 || pressWholeWindow) && (event->buttons() & Qt::LeftButton)
        && (event->globalPosition().toPoint() - pressGlobal).manhattanLength()
               >= QApplication::startDragDistance()) {
        const int index = pressIndex;
        const bool window = pressWholeWindow;
        pressIndex = -1; // one drag per press
        pressWholeWindow = false;
        const QPoint where = event->globalPosition().toPoint();
        if (window)
            beginWindowDrag(where);
        else
            beginDrag(index, where);
        return;
    }
    QTabBar::mouseMoveEvent(event);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    pressIndex = -1;
    pressWholeWindow = false;
    if (holdingPressGrab) {
        releaseMouse();
        holdingPressGrab = false;
    }
    QTabBar::mouseReleaseEvent(event);
}

void DocumentTabBar::beginDrag(int index, const QPoint &globalPos)
{
    if (!pathForIndex)
        return;
    const QString path = pathForIndex(index);
    if (path.isEmpty())
        return;
    const TabRef doc(path, pageForIndex ? pageForIndex(index) : 0);

    // The tab itself rides under the cursor, grabbed from the strip where it
    // stands, with the spot the pointer caught it by as the hot spot.
    const QRect slot = tabRect(index);
    runDrag(QList<TabRef>{doc}, index, grab(slot), mapFromGlobal(globalPos) - slot.topLeft());
}

void DocumentTabBar::beginWindowDrag(const QPoint &globalPos)
{
    if (!pathForIndex || count() == 0)
        return;

    // Every tab this window holds, in the order they sit. Dragging the blank
    // strip is "move this window's documents", not "move one of them", so the
    // whole list travels and the whole window goes with it when they land.
    QList<TabRef> docs;
    for (int index = 0; index < count(); ++index) {
        const QString path = pathForIndex(index);
        if (path.isEmpty())
            continue;
        docs.append(TabRef(path, pageForIndex ? pageForIndex(index) : 0));
    }
    if (docs.isEmpty())
        return;

    runDrag(docs, -1, grab(), mapFromGlobal(globalPos));
}

void DocumentTabBar::runDrag(const QList<TabRef> &docs, int sourceIndex, const QPixmap &ghost,
                             const QPoint &hotSpot)
{
    QStringList lines;
    for (const TabRef &doc : docs)
        lines << QStringLiteral("%1\t%2").arg(doc.first).arg(doc.second);

    auto *mime = new QMimeData;
    mime->setData(QLatin1String(kTabMimeType), lines.join(QLatin1Char('\n')).toUtf8());

    DragContext context;
    context.source = this;
    context.sourceIndex = sourceIndex;
    dragContext = &context;

    // The drag takes a grab of its own; the one held over from the press has to
    // be handed back first or the two fight over the pointer.
    if (holdingPressGrab) {
        releaseMouse();
        holdingPressGrab = false;
    }

    QDrag drag(this);
    drag.setMimeData(mime);

    // Without a pixmap Qt falls back to a plain arrow and, over anything that
    // does not take the drop, a red "no entry" sign -- which reads as "this drag
    // is broken" even though the release still moves the tab. A picture of what
    // is being carried says otherwise, and it is what every browser does.
    if (!ghost.isNull()) {
        drag.setPixmap(ghost);
        drag.setHotSpot(hotSpot);

        // The trailing image above only paints while some action is live. Over
        // the desktop or another program the action drops to Ignore, where Qt
        // draws the forbidden sign instead -- and a detach means letting go over
        // exactly that. Swap the "no entry" for a small picture of the tab, so
        // carrying one out looks like carrying one out right up to the release.
        drag.setDragCursor(ghost.scaled(64, 64, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation),
                           Qt::IgnoreAction);
    }

    drag.exec(Qt::MoveAction);

    dragContext = nullptr;

    // Nothing consumed the drop as a merge, so this was not a hand-off to a
    // window that read the drag. That still has two outcomes, told apart by where
    // the button was let go -- not by the drag's own bookkeeping, which only
    // proves an answer when a target actually ran its drop handler:
    //
    //   * over one of our other windows that somehow did not take the drop --
    //     a child that swallowed the event, or the strip on a window that was not
    //     the drop site -- the tabs should still merge in there, so this reports
    //     the release point and lets the window look; and
    //   * over nothing of ours at all (the desktop, another program), which for a
    //     tab is "pull this out into its own window" and for a whole window means
    //     it stays the window it already is.
    //
    // The release point is read now rather than from the press, because the two
    // differ by exactly the drag.
    const QPoint released = QCursor::pos();
    const bool wholeWindow = sourceIndex < 0;
    if (context.movedElsewhere) {
        if (wholeWindow)
            Q_EMIT handedOffWindow();
        else
            Q_EMIT handedOff(sourceIndex);
    } else if (!context.consumed) {
        if (wholeWindow)
            Q_EMIT windowMergeRequested(released);
        else
            Q_EMIT detachRequested(sourceIndex, released);
    }
}

void DocumentTabBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (isTabDrag(event->mimeData())) {
        setInsertIndicator(dropIndexAt(event, this));
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DocumentTabBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (isTabDrag(event->mimeData())) {
        setInsertIndicator(dropIndexAt(event, this));
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DocumentTabBar::dragLeaveEvent(QDragLeaveEvent *event)
{
    setInsertIndicator(-1);
    event->accept();
}

void DocumentTabBar::dropEvent(QDropEvent *event)
{
    QList<TabRef> docs;
    if (!readTabs(event->mimeData(), docs)) {
        event->ignore();
        return;
    }
    const int target = dropIndexAt(event, this);
    setInsertIndicator(-1);

    // One drop, two meanings, and the difference is whose drag this is. A bar
    // that is its own source is this window's strip, so the tab is only moving
    // along it -- reorder, which needs the source's index and so is the one case
    // a live context is required for. Everything else is a tab arriving from
    // elsewhere, which this window takes by opening it. That goes through
    // takeMergedTabs, the same path a drop on the page body uses: a merge must
    // land wherever it is let go, so it is never gated on the bookkeeping.
    if (dragContext && dragContext->source == this) {
        dragContext->consumed = true;
        if (dragContext->sourceIndex >= 0 && dragContext->sourceIndex != target)
            Q_EMIT reorderRequested(dragContext->sourceIndex, target);
    } else {
        takeMergedTabs(docs);
    }
    event->acceptProposedAction();
}

void DocumentTabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);

    if (insertIndicator < 0 || count() == 0)
        return;

    // A line where releasing the drag would insert the tab: the left edge of the
    // slot it points at, or the right edge of the last tab when it would append.
    // Drawn over the base strip so it tracks the theme's highlight.
    const QRect last = tabRect(insertIndicator >= count() ? count() - 1 : insertIndicator);
    const int x = insertIndicator >= count() ? last.right() : last.left();

    QPainter p(this);
    p.setClipRect(event->rect());
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    const int top = qMin(tabRect(0).top(), last.top());
    const int bottom = qMax(tabRect(count() - 1).bottom(), last.bottom());
    p.drawLine(x, top, x, bottom);
}
