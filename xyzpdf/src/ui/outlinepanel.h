// The bookmark tree, as a navigable sidebar.
//
// A QTreeView over a model built from the Outline the document reports (see
// src/document/outline.h -- the tree is assembled there, tested there, and this
// is only its view). Clicking an entry asks to go to the page it names; an entry
// that names no page is drawn but not selectable, because a bookmark that jumps
// nowhere is a thing real documents contain and silently sending the reader to
// page 0 would be a lie.
//
// It belongs to a DocumentTab, not to the window: each open document has its own
// bookmarks, and a sidebar that followed the tab in front is the whole point of
// the tab-per-document split (docs/PLAN.md §7.1).
#pragma once

#include "../document/outline.h"

#include <QTreeView>

class QStandardItem;
class QStandardItemModel;

class OutlinePanel : public QTreeView
{
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget *parent = nullptr);

    // Rebuild the tree from a document's bookmarks. Empty clears it (and leaves
    // hasOutline() false, which is how the tab decides whether to show the panel
    // at all).
    void setOutline(const Outline &outline);
    bool hasOutline() const;

Q_SIGNALS:
    // The reader chose an entry that names a page. Zero-based, like the rest of
    // the program's page numbers.
    void jumpToPage(int page);

private:
    void addNodes(const QVector<OutlineNode> &nodes, QStandardItem *under);

    QStandardItemModel *model = nullptr;
};
