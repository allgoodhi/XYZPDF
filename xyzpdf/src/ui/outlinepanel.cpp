#include "outlinepanel.h"

#include <QHeaderView>
#include <QStandardItem>

namespace {

// The page a tree item points at, kept in the model so a click can read it back.
// -1 means the bookmark names no page; such an item is shown but not selectable.
constexpr int kPageRole = Qt::UserRole;
// The destination the file itself wrote for this entry, carried so a click can land at the place
// inside the page rather than at its top. Default for an entry no file row matched.
constexpr int kDestRole = Qt::UserRole + 1;

} // namespace

OutlinePanel::OutlinePanel(QWidget *parent)
    : QTreeView(parent)
{
    model = new QStandardItemModel(this);

    setModel(model);
    // One column, no header: a bookmark tree has nothing to label, and a header
    // over it is just a bar that steals a row.
    setHeaderHidden(true);
    // A bookmark that is too long for the column is ellipsised, not wrapped --
    // wrapped rows make the indentation, which is the whole meaning of the tree,
    // hard to follow.
    setTextElideMode(Qt::ElideRight);
    setUniformRowHeights(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);

    connect(this, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        const int page = index.data(kPageRole).toInt();
        if (page >= 0)
            Q_EMIT jumpToLocation(page, index.data(kDestRole).value<BookmarkDest>());
    });
}

void OutlinePanel::setOutline(const Outline &outline)
{
    model->clear();
    model->setColumnCount(1);
    addNodes(outline.nodes, nullptr);
    // Opened to the top level: the reader sees the chapters at a glance and
    // expands the one they want, rather than a wall of every bookmark or a tree
    // where the first thing they see is collapsed shut.
    expandToDepth(0);
}

bool OutlinePanel::hasOutline() const
{
    return model->rowCount() > 0;
}

void OutlinePanel::addNodes(const QVector<OutlineNode> &nodes, QStandardItem *under)
{
    for (const OutlineNode &node : nodes) {
        auto *item = new QStandardItem(node.title);
        item->setData(node.page, kPageRole);
        item->setData(QVariant::fromValue(node.dest), kDestRole);
        // A bookmark with no target page is kept -- dropping it would lose a
        // real entry the document has -- but it cannot be clicked to a page, so
        // it is not selectable and reads as a heading rather than a link.
        if (node.page < 0) {
            item->setSelectable(false);
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        if (under)
            under->appendRow(item);
        else
            model->appendRow(item);
        addNodes(node.children, item);
    }
}
