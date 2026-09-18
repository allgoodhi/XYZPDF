#include "wrappingtoolbararea.h"

#include <QLayout>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolButton>

namespace {

// The gap between the "…" and the group it follows, and between it and the right
// edge of the window: enough to read as separate, not enough to be a hole.
constexpr int kMoreButtonInset = 6;

// Less than this and a group is not squeezed onto the row at all. What would be
// left of it is a fragment whose items are hidden anyway, and a row that ends in
// a sliver of a button reads as a drawing mistake rather than as chrome.
constexpr int kMinimumGroupWidth = 120;

} // namespace

WrappingToolBarArea::WrappingToolBarArea(QWidget *parent)
    : QWidget(parent)
{
    // Named so that the theme can paint the strip: it has to be the same surface
    // as the toolbars in it, or the space the "…" sits in looks like a hole cut
    // out of the chrome.
    setObjectName(QStringLiteral("toolBarStrip"));

    more = new QToolButton(this);
    more->setObjectName(QStringLiteral("toolBarMoreButton"));
    more->setText(QStringLiteral("…"));
    more->setToolTip(tr("展开工具栏，显示放不下的按钮"));
    // Not in the tab order: this is a mouse gesture, and a reader pressing Tab
    // through the window should go from the last control to the page.
    more->setFocusPolicy(Qt::NoFocus);
    connect(more, &QToolButton::clicked, this, [this] { setExpanded(!expanded); });
}

QToolBar *WrappingToolBarArea::addGroup(const QString &objectName, const QString &title)
{
    auto *bar = new QToolBar(title, this);
    bar->setObjectName(objectName);
    bar->setMovable(false);
    // Text rather than icons: this program has no icon set, and a row of blank
    // buttons is worse than a row of words.
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bars.append(bar);
    return bar;
}

void WrappingToolBarArea::setExpanded(bool expand)
{
    if (expanded == expand)
        return;
    expanded = expand;
    more->setToolTip(expanded ? tr("收起工具栏") : tr("展开工具栏，显示放不下的按钮"));
    relayout();
}

QSize WrappingToolBarArea::sizeHint() const
{
    int total = 0;
    int tallest = 0;
    for (QToolBar *bar : bars) {
        total += bar->sizeHint().width();
        tallest = qMax(tallest, bar->sizeHint().height());
    }
    // One row: what the strip wants is to fit everything, and it wants it in one
    // row. How much room that needs is the sum; how much it will accept is in
    // minimumSizeHint().
    return QSize(total, tallest);
}

QSize WrappingToolBarArea::minimumSizeHint() const
{
    int widest = 0;
    int tallest = 0;
    for (QToolBar *bar : bars) {
        widest = qMax(widest, bar->sizeHint().width());
        tallest = qMax(tallest, bar->sizeHint().height());
    }
    // The widest single group with the "…" beside it, and this is the number the
    // window's minimum width is set from: narrower than this and expanding could
    // not show that group whole even on a row of its own, which would make the
    // "…" a lie.
    return QSize(widest + more->sizeHint().width() + 2 * kMoreButtonInset, tallest);
}

void WrappingToolBarArea::silenceExtension(QToolBar *bar)
{
    // Qt gives a toolbar that has more items than room a chevron of its own at
    // its right end. This strip has a "…" a few pixels further right that says
    // the same thing and does something a reader can rely on, and for a while
    // both of them were on screen: two buttons, one meaning, and no way to tell
    // which was which.
    //
    // Activated first: the chevron is created lazily by the toolbar's own layout
    // pass, so until that has run there is nothing to find. It is then given no
    // size at all rather than hidden, because "hidden" is a state the toolbar's
    // layout sets for it on every pass -- it would be back on the next one -- and
    // a widget with no size is drawn nowhere whatever the layout decides. Qt's
    // own arithmetic is untouched, so which items fit is unchanged.
    if (QLayout *layout = bar->layout())
        layout->activate();
    if (QToolButton *extension =
            bar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"))) {
        extension->setFixedSize(0, 0);
    }
}

void WrappingToolBarArea::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

bool WrappingToolBarArea::overflows() const
{
    int total = 0;
    for (QToolBar *bar : bars)
        total += bar->sizeHint().width();
    return total > width();
}

void WrappingToolBarArea::relayout()
{
    if (bars.isEmpty())
        return;

    int tallest = 0;
    for (QToolBar *bar : bars)
        tallest = qMax(tallest, bar->sizeHint().height());

    const int available = width();
    // The button is there exactly while something does not fit on one row: that
    // is what it is for, and it is also the only moment a reader could want it.
    // In the expanded state it stays for as long as the rows are more than one,
    // which is where it becomes the way back.
    const bool showMore = overflows();
    const QSize moreSize = more->sizeHint();
    const int moreWidth = moreSize.width() + 2 * kMoreButtonInset;
    const int usable = qMax(0, available - (showMore ? moreWidth : 0));

    if (expanded) {
        // As many rows as it takes, every group at the width it asked for, and
        // nothing squeezed: this is what the reader asked to see.
        int x = 0;
        int y = 0;
        int rowHeight = 0;
        for (QToolBar *bar : bars) {
            const int room = (y == 0 ? usable : available);
            if (x > 0 && x + bar->sizeHint().width() > room) {
                y += rowHeight;
                x = 0;
                rowHeight = 0;
            }
            const int width = qMin(bar->sizeHint().width(), (y == 0 ? usable : available));
            bar->setGeometry(x, y, width, tallest);
            bar->show();
            x += width;
            rowHeight = qMax(rowHeight, tallest);
        }
    } else {
        // One row. What fits is shown whole; the group that straddles the end of
        // the row is squeezed into what is left -- and a squeezed QToolBar puts
        // its own tail away, which is exactly what the "…" is warning about --
        // and the groups after it are not shown at all.
        int x = 0;
        for (int index = 0; index < bars.size(); ++index) {
            QToolBar *bar = bars[index];
            const int room = usable - x;
            const int wanted = bar->sizeHint().width();
            if (wanted <= room) {
                bar->setGeometry(x, 0, wanted, tallest);
                bar->show();
                x += wanted;
                continue;
            }
            if (room >= kMinimumGroupWidth) {
                bar->setGeometry(x, 0, room, tallest);
                bar->show();
                silenceExtension(bar);
            } else {
                bar->hide();
            }
            // And everything after it is behind the "…" as well.
            for (int rest = index + 1; rest < bars.size(); ++rest)
                bars[rest]->hide();
            break;
        }
    }

    more->setVisible(showMore);
    if (showMore) {
        // A button and not a slab: the height of an ordinary control in this
        // chrome, sat in the middle of the row rather than filling it, so that it
        // reads as one more thing at the end of the row and not as the row's lid.
        const int height = qMin(tallest, moreSize.height());
        more->setGeometry(available - moreSize.width() - kMoreButtonInset,
                          (tallest - height) / 2, moreSize.width(), height);
        // Painted last, so that it is over the few pixels it shares with the
        // group it is squeezed up against and not under them.
        more->raise();
    }

    // The height is the rows that were needed, not one row with the rest clipped
    // off: the lower rows are drawn by this widget, and a widget too short for its
    // children cuts them in half. Set here rather than reported through
    // heightForWidth() because the number of rows is a function of the width, and
    // this is the moment the width is known.
    int needed = tallest;
    for (QToolBar *bar : bars) {
        if (bar->isVisible())
            needed = qMax(needed, bar->y() + bar->height());
    }
    if (height() != needed)
        setFixedHeight(needed);
}
