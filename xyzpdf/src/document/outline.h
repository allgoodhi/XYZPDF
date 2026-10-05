// The bookmark tree.
//
// Built here rather than handed straight out of Qt's model, for two reasons
// that both come down to testability. Qt's QPdfBookmarkModel is a *hierarchy* --
// the rows under its root are only the top level, and the rest hang below them
// (a real book with 184 bookmarks reports 14 under the root, measured by
// tests/outlineprobe.cpp) -- so somebody has to walk it and somebody has to
// assemble the tree. Doing the assembly in a function that takes plain rows
// means the whole thing is tested by outline_smoke without a PDF anywhere near
// it, and the rules that are easy to get wrong (a first row at level 3, a level
// that drops by two, a bookmark pointing at no page) get tested directly.
//
// The M1 sidebar will hold a QPdfBookmarkModel of its own for the view; this is
// the tree the rest of the program asks questions of.
#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

// One bookmark as the library reports it.
struct OutlineRow
{
    QString title;
    // Nesting depth, outermost first, counting from 1 -- the number
    // outlineFromRows assembles against. Qt's own Level role counts from 0, so
    // the read that fills these rows adds one rather than passing it through.
    int level = 1;
    // Zero-based, or -1 for a bookmark that names no page -- which real
    // documents do contain, and which must not become page 0.
    int page = -1;
    // Where on that page, in points. Invalid when the bookmark only names a
    // page.
    QPointF location;
};

struct OutlineNode
{
    QString title;
    int page = -1;
    QPointF location;
    QVector<OutlineNode> children;
};

struct Outline
{
    QVector<OutlineNode> nodes;

    bool isEmpty() const { return nodes.isEmpty(); }

    // Every node, at every depth, not just the top level.
    //
    // Walks the tree each time rather than keeping a flat list beside it: the
    // tree is small (a 500-page document with a bookmark per chapter is a few
    // hundred nodes) and a second copy is a second thing to keep in step.
    int count() const;

    // Depth-first, in the order a sidebar draws them, or nullptr when the index
    // is out of range.
    const OutlineNode *nodeAt(int index) const;
};

// Assembles the tree: each row becomes a child of the nearest preceding row
// with a smaller level, or a root when there is none.
//
// A level that jumps (1 then 3) attaches one step under the deepest row so far
// rather than inventing empty parents -- a tree has no room for a bookmark that
// is not there, and dropping the row would lose a real bookmark.
Outline outlineFromRows(const QVector<OutlineRow> &rows);

// The same list read back out of a tree, depth-first, which is the order a sidebar draws
// (and therefore the order §71's 编辑书签 box hands back).
//
// The inverse of outlineFromRows, and it exists because that one only goes in: a file read off
// disk arrives as a tree (QPdfBookmarkModel, since §64), and everything that *writes* bookmarks
// takes the flat list, so the step from one to the other has to live beside the step back rather
// than being re-derived by whoever needs it. Read them in this direction and the round trip is
// exact -- tree, rows, tree -- because the rows come out of a tree that already agreed on where
// everything sits. Read the other way, rows -> tree -> rows is only the same list when the input
// was legal to begin with: a first row at level 3 or a level that drops by two gets normalized by
// outlineFromRows, and the re-read list is the normalized one. That asymmetry is the reason this
// function takes a tree at all instead of being a flag on the row list.
QVector<OutlineRow> outlineRowsOf(const Outline &tree);

// How far the block starting at `index` reaches: the index one past its last descendant, so the
// block's own length is the difference. A row and everything hanging under it move together, which
// is why the size of that run is something more than one caller needs to know.
//
// A single number rather than a moved list because it is the whole answer to "which rows belong to
// this one" -- the same reason pieceRanges() and planMove() are each one function: a sidebar's 上移
// and the test that checks what it did must not each count the children their own way. An index out
// of range comes back as `index`, which is the harmless answer (a block of nothing).
int outlineSubtreeEnd(const QVector<OutlineRow> &rows, int index);
