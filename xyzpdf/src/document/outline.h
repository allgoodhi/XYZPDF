// The bookmark tree.
//
// Built here rather than handed straight out of Qt's model, for two reasons
// that both come down to testability. Qt's QPdfBookmarkModel is a *flat* list
// with a Level column -- it is how the sidebar draws indentation -- so a tree
// has to be assembled from it somewhere. Doing it in a function that takes
// plain rows means the whole thing is tested by outline_smoke without a PDF
// anywhere near it, and the rules that are easy to get wrong (a first row at
// level 3, a level that drops by two, a bookmark pointing at no page) get
// tested directly.
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
    // Nesting depth, outermost first. Qt numbers levels from 1.
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
