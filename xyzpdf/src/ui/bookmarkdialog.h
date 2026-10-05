// Which bookmarks a document should come back with -- the whole list, every time.
//
// This is the only box in the program that edits a *tree*, and it deliberately shows the tree as a
// flat table with a 层级 number instead of drawing indentation: the flat list is the shape the model
// takes (DocumentEditor::setOutline), the shape outlineFromRows assembles, and the shape the file's
// seven keys get computed from (§68), so drawing a second visual nesting would be a second place
// where "is this a child of that" could disagree -- §64's lesson, that a tree can be walked right and
// still be numbered wrong, is exactly a bug between two renderings of one structure.
//
// The unit is the list rather than one bookmark because that is what the writer rebuilds from: a
// per-item edit would have to patch `/Prev` `/Next` `/First` `/Last` (tests/outlineprobe.cpp measured
// that one broken link loses a whole span in every reader, silently), and a box that only lets you
// change one row at a time is how the two answers appear.
//
// The page column is the numbering §70 settled: a page of the file **as it was opened**, which is
// what the reader counts on screen, because nothing here is applied before a save. A blank cell is a
// bookmark that names no page -- not "unset", not "every page" (the 删除页面 box's empty range means
// "nothing said yet"; here an empty page is an answer, and the summary says which one it is).
#pragma once

#include "../document/outline.h"
#include "../edit/documenteditor.h"

#include <QDialog>

class DocumentEditor;

class QLabel;
class QPushButton;
class QTableWidget;

class BookmarkDialog : public QDialog
{
    Q_OBJECT

public:
    // `documentBookmarks` is the tree the reader is looking at in the sidebar right now; it is what
    // the box opens on when nothing has been asked for yet, and what "you changed nothing, so pressing
    // this would do nothing" is measured against. When a tree *is* pending, the box opens on that
    // instead -- the same rule the other five boxes follow, for the same reward: pressing 确定 again
    // reads as correcting the list rather than as a second one to undo twice.
    BookmarkDialog(QWidget *parent, const DocumentEditor *editor, const Outline &documentBookmarks);

    // The table as the model reads it: one entry per row, 1-based depths, `page` -1 for a blank cell.
    // Rows whose numbers do not parse come back the way the summary describes them: an empty 页码 is
    // -1, and anything that is not a number keeps the row's place so the message about it can name
    // the row the reader has to fix.
    QVector<OutlineRow> rows() const;

private:
    void rebuild(const QVector<OutlineRow> &rows, int select);
    void addRow();
    void removeRow();
    void moveRow(int direction);
    int selectedRow() const;
    void updateSummary();

    // The file's own page count, which is the bound a typed page number has (§70) -- not the count of
    // pages still standing in the pending list.
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    // What the box opened on. Compared against the table to decide "this is not a change".
    QVector<OutlineRow> opening;

    QTableWidget *table = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;
    QPushButton *upButton = nullptr;
    QPushButton *downButton = nullptr;
};
