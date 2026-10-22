#include "outline.h"

namespace {

// Depth-first, appending each node's address.
//
// Addresses into the caller's tree, which must therefore not be modified while
// the result is in use -- the two callers below both finish with it before
// returning.
void flatten(const QVector<OutlineNode> &nodes, QVector<const OutlineNode *> *into)
{
    for (const OutlineNode &node : nodes) {
        into->append(&node);
        flatten(node.children, into);
    }
}

// The same walk, counting how deep each node sits, because the flat list's `level` is depth plus
// one (see outline.h: Qt's own Level role counts from 0 and the read that fills rows adds one).
void flattenRows(const QVector<OutlineNode> &nodes, int level, QVector<OutlineRow> *into)
{
    for (const OutlineNode &node : nodes) {
        OutlineRow row;
        row.title = node.title;
        row.level = level;
        row.page = node.page;
        row.location = node.location;
        row.dest = node.dest;
        into->append(row);
        flattenRows(node.children, level + 1, into);
    }
}

// The tree in the same depth-first order outlineRowsOf() reports, its nodes visited in step with a
// list of matches. `cursor` runs once per node; the caller has already checked that list's length.
void attachDestinations(const QVector<OutlineRow> &fileRows, const QVector<int> &matches, int *cursor,
                        QVector<OutlineNode> *nodes)
{
    for (OutlineNode &node : *nodes) {
        const int fileIndex = *cursor < matches.size() ? matches.at(*cursor) : -1;
        ++*cursor;
        if (fileIndex >= 0)
            node.dest = fileRows.at(fileIndex).dest;
        attachDestinations(fileRows, matches, cursor, &node.children);
    }
}

} // namespace

int Outline::count() const
{
    QVector<const OutlineNode *> flat;
    flatten(nodes, &flat);
    return flat.size();
}

const OutlineNode *Outline::nodeAt(int index) const
{
    QVector<const OutlineNode *> flat;
    flatten(nodes, &flat);
    if (index < 0 || index >= flat.size())
        return nullptr;
    return flat.at(index);
}

Outline outlineFromRows(const QVector<OutlineRow> &rows)
{
    Outline tree;
    // The ancestors of the row being placed, each named by its index inside its
    // parent's children.
    //
    // Indices rather than pointers, because appending to a QVector may
    // reallocate and move what is already in it: a stack of OutlineNode* held
    // across an append points into freed memory, and it does so on exactly the
    // documents with enough bookmarks to make the vector grow. The list is
    // walked from the root on each row instead, which is a few pointer hops
    // into a vector that is small by construction.
    QVector<int> path;

    for (const OutlineRow &row : rows) {
        // A level deeper than anything seen so far hangs off the deepest row
        // there is; a level shallower than the root is the root.
        const int depth = qBound(0, row.level - 1, path.size());
        path.resize(depth);

        QVector<OutlineNode> *children = &tree.nodes;
        for (const int index : std::as_const(path))
            children = &(*children)[index].children;

        OutlineNode node;
        node.title = row.title;
        node.page = row.page;
        node.location = row.location;
        node.dest = row.dest;
        children->append(node);

        path.append(children->size() - 1);
    }
    return tree;
}

QVector<OutlineRow> outlineRowsOf(const Outline &tree)
{
    QVector<OutlineRow> rows;
    flattenRows(tree.nodes, 1, &rows);
    return rows;
}

QVector<int> matchOutlineRows(const QVector<OutlineRow> &fileRows,
                              const QVector<OutlineRow> &rows)
{
    QVector<bool> consumed(fileRows.size(), false);
    QVector<int> matched;
    matched.reserve(rows.size());
    for (const OutlineRow &row : rows) {
        int found = -1;
        for (int i = 0; i < fileRows.size(); ++i) {
            if (!consumed.at(i) && fileRows.at(i).title == row.title
                && fileRows.at(i).page == row.page) {
                found = i;
                consumed[i] = true;
                break;
            }
        }
        matched.append(found);
    }
    return matched;
}

void attachOutlineDestinations(Outline &tree, const QVector<OutlineRow> &fileRows)
{
    // outlineRowsOf() is the same depth-first walk attachDestinations() makes, so one match per
    // node can be applied in step. The tree here is a fresh read (Qt's, on open), so every node
    // still carries the default destination; there is no older one being overwritten.
    const QVector<int> matches = matchOutlineRows(fileRows, outlineRowsOf(tree));
    int cursor = 0;
    attachDestinations(fileRows, matches, &cursor, &tree.nodes);
}

int outlineSubtreeEnd(const QVector<OutlineRow> &rows, int index)
{
    if (index < 0 || index >= rows.size())
        return index;
    // Everything after it that sits deeper belongs to this row; the first row at the same depth or
    // shallower is where the block ends -- and a row with no children returns index + 1, which is
    // why the callers do not need a special case for a leaf.
    const int level = rows.at(index).level;
    int end = index + 1;
    while (end < rows.size() && rows.at(end).level > level)
        ++end;
    return end;
}
