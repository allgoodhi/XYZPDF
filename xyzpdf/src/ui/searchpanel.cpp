#include "searchpanel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QListWidgetItem>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace {

constexpr int kMatchIndexRole = Qt::UserRole;
constexpr int kBoldFromRole = Qt::UserRole + 1;
constexpr int kBoldToRole = Qt::UserRole + 2;

// How much air above and below the row's one line of text.
constexpr int kRowPadding = 3;

// Draws a row as the page number, then the snippet with the hit in bold.
//
// A delegate rather than a label per row: the list can hold a thousand rows,
// and a thousand widgets is a thousand things to lay out and keep in step with
// a model that is replaced wholesale on every scan. The rows that are not on
// screen are never painted, and the text is the item's own DisplayRole -- so a
// screen reader reads the same sentence the painter draws.
class SearchRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QFont bold = opt.font;
        bold.setBold(true);
        const QFontMetrics normal(opt.font);
        const QFontMetrics heavy(bold);
        // The width is the view's business: a row that asked for the width of
        // its own snippet would give the sidebar a horizontal scrollbar.
        return QSize(0, qMax(normal.height(), heavy.height()) + 2 * kRowPadding);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        const int boldFrom = index.data(kBoldFromRole).toInt();
        const int boldTo = index.data(kBoldToRole).toInt();

        // The background, the selection wash and the focus rectangle are the
        // style's; clearing the text is what leaves the words to us, because
        // only part of the row is bold and one drawText call cannot say which.
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const QRect area = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        if (area.isEmpty() || text.isEmpty())
            return;

        QFont boldFont = opt.font;
        boldFont.setBold(true);
        const QFontMetrics normal(opt.font);
        const QFontMetrics heavy(boldFont);
        const QString ellipsis = QStringLiteral("…");

        // Measured character by character because the bold run is wider than
        // the text around it: an elision computed from one font would cut the
        // row at the right place for normal text and the wrong one for a hit.
        const auto fitting = [&](int available) {
            int used = 0;
            int count = 0;
            for (int at = 0; at < text.length(); ++at) {
                const QFontMetrics &metrics = at >= boldFrom && at < boldTo ? heavy : normal;
                const int width = metrics.horizontalAdvance(text.at(at));
                if (used + width > available)
                    break;
                used += width;
                ++count;
            }
            return count;
        };

        int shown = fitting(area.width());
        const bool truncated = shown < int(text.length());
        if (truncated)
            shown = fitting(area.width() - normal.horizontalAdvance(ellipsis));

        QColor colour = opt.palette.color(QPalette::Text);
        if (!(opt.state & QStyle::State_Enabled))
            colour = opt.palette.color(QPalette::Disabled, QPalette::Text);
        else if (opt.state & QStyle::State_Selected)
            colour = opt.palette.color(QPalette::HighlightedText);

        painter->save();
        painter->setClipRect(area);
        painter->setPen(colour);
        painter->setRenderHint(QPainter::TextAntialiasing);
        const int baseline = area.top()
                             + (area.height() + normal.ascent() - normal.descent()) / 2;

        int x = area.left();
        int at = 0;
        while (at < shown) {
            const bool isBold = at >= boldFrom && at < boldTo;
            int end = at + 1;
            while (end < shown && ((end >= boldFrom && end < boldTo) == isBold))
                ++end;
            const QString run = text.mid(at, end - at);
            painter->setFont(isBold ? boldFont : opt.font);
            painter->drawText(QPoint(x, baseline), run);
            x += (isBold ? heavy : normal).horizontalAdvance(run);
            at = end;
        }
        if (truncated) {
            painter->setFont(opt.font);
            painter->drawText(QPoint(x, baseline), ellipsis);
        }
        painter->restore();
    }
};

} // namespace

SearchPanel::SearchPanel(QWidget *parent)
    : QListWidget(parent)
{
    setObjectName(QStringLiteral("searchPanel"));
    setItemDelegate(new SearchRowDelegate(this));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Every row is one line of text, whatever the snippet's own line breaks
    // were -- the folding already happened in TextSearch::snippetAround().
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);

    connect(this, &QAbstractItemView::clicked, this, [this](const QModelIndex &index) {
        // The match the row stands for, not the row number -- the "not listed"
        // row has no match and must not look as though it had.
        if (!index.isValid())
            return;
        const int match = matchIndexOfRow(index.row());
        if (match >= 0)
            Q_EMIT matchChosen(match);
    });
}

void SearchPanel::setMatches(const QVector<TextSearch::Match> &matches, int current)
{
    clear();
    omitted = qMax(0, int(matches.size()) - MaximumRows);

    const int listed = qMin(int(matches.size()), MaximumRows);
    for (int index = 0; index < listed; ++index) {
        const TextSearch::Match &match = matches.at(index);
        const QString head = tr("第 %1 页").arg(match.page + 1) + QStringLiteral(" · ");
        auto *row = new QListWidgetItem(head + match.snippet.text);
        row->setData(kMatchIndexRole, index);
        row->setData(kBoldFromRole, head.length() + match.snippet.from);
        row->setData(kBoldToRole, head.length() + match.snippet.to);
        addItem(row);
    }

    if (omitted > 0) {
        auto *row = new QListWidgetItem(tr("还有 %n 处没有列出", nullptr, omitted));
        row->setData(kMatchIndexRole, -1);
        // Not selectable: there is no hit to go to, and a selection highlight on
        // a note about rows that are not there would read as one.
        row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
        addItem(row);
    }

    setCurrentMatch(current);
}

void SearchPanel::clearMatches()
{
    clear();
    omitted = 0;
}

void SearchPanel::setCurrentMatch(int index)
{
    if (index < 0 || index >= count() || matchIndexOfRow(index) != index) {
        setCurrentRow(-1);
        clearSelection();
        return;
    }
    setCurrentRow(index);
    if (QListWidgetItem *row = item(index))
        scrollToItem(row, QAbstractItemView::EnsureVisible);
}

int SearchPanel::matchIndexOfRow(int row) const
{
    const QListWidgetItem *entry = item(row);
    if (!entry || !entry->data(kMatchIndexRole).isValid())
        return -1;
    return entry->data(kMatchIndexRole).toInt();
}

QString SearchPanel::rowText(int row) const
{
    const QListWidgetItem *entry = item(row);
    return entry ? entry->text() : QString();
}

int SearchPanel::boldFromOfRow(int row) const
{
    const QListWidgetItem *entry = item(row);
    return entry ? entry->data(kBoldFromRole).toInt() : 0;
}

int SearchPanel::boldToOfRow(int row) const
{
    const QListWidgetItem *entry = item(row);
    return entry ? entry->data(kBoldToRole).toInt() : 0;
}
