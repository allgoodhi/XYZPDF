// Which other files to merge into this document, which pages of each, and where the
// whole batch goes.
//
// ---- why a list of files rather than one ----
//
// docs/PLAN.md §4 has 合并 PDF 作为 M5 的一项，and the gesture a reader means by it is
// "these three files, in this order, into one". A dialog that takes one file at a time
// can be pressed three times to get there, and then the order the three landed in is an
// accident of where each one was pointed -- so the list is the dialog, and the list's
// order is the order the saved file gets. That last promise is the reason there is a
// mergesToRecord() at all rather than a plain accessor over the rows: see its comment.
//
// ---- what the numbers mean, which is three things at once ----
//
// A page range in a row is numbered against *that row's file*, because that is the
// document a reader was looking at when they chose it -- the same rule 打印 uses for a
// file being printed. The 指定页之后 box below is numbered against the document on the
// screen, because that is the only one a reader can see. Both are the numbering every
// other dialog here uses, and neither is a position in the file a save writes: an
// imported page has no number a reader can type, so the only way to take one back out
// is to undo the merge that brought it in. The summary says so, because a reader who
// merges three files and then opens 删除页面 will otherwise find a page count that does
// not match what is on the screen.
//
// ---- what this dialog does not do ----
//
// It does not show a preview of anything, and it does not open a password box. A file
// that needs a password to open is refused when it is picked, by name, with the reason
// in the sentence -- which is a decision rather than an omission: a password prompt
// here would belong to a file the reader had only just chosen, in a dialog whose whole
// job is to say which pages come in.
#pragma once

#include "../edit/documenteditor.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;

class MergePagesDialog : public QDialog
{
    Q_OBJECT

public:
    // One row: a file, how many pages it has, and which of them come in.
    struct Entry
    {
        QString path;
        int pageCount = 0;
        // Zero-based numbers within this file, as its own page count bounds them.
        QVector<int> pages;
    };

    MergePagesDialog(QWidget *parent, const DocumentEditor *document);

    // The rows, in the order the editor should be asked. Not always the order they are
    // listed in: a merge lands beside the target the reader named rather than beside the
    // block the previous row added, so pointing three files "behind page 12" one after
    // another would put them 3,2,1. This function is where that is corrected, and it is
    // one function rather than a loop in the caller so that the summary above the button
    // and the operations behind it cannot mean different orders.
    QVector<Entry> mergesToRecord() const;
    DocumentEditor::MoveTarget target() const;
    // The page of the opened document the batch lands behind, zero-based, or -1 when
    // the target is not MoveTarget::AfterPage. Out-of-range is passed back as typed:
    // the editor refuses those and the summary says which one it was.
    int anchorPage() const;

    // Look each of these over and make a row for every one that can be merged; the
    // sentences about the ones that cannot come back rather than being shown here,
    // because putting up a box is the caller's business -- 添加文件… says them under
    // its own title, and the picker is the only part of that which needs a window.
    QStringList addPaths(const QStringList &paths);

private:
    void addFiles();
    void removeSelected();
    void moveSelected(int delta);
    void handleRangeEdit();
    // The row the reader is pointing at, or -1.
    int currentRow() const;
    int rowCount() const;
    // What the range cell of one row holds, parsed against that row's own file. An
    // empty cell names nothing -- the same guard every other edit dialog has.
    QVector<int> pagesOfRow(int row) const;
    QString rangeOfRow(int row) const;
    // The same entries mergesToRecord() returns, for the summary to plan against.
    QVector<Entry> rows() const;
    void updateSummary();
    QString targetName() const;
    int anchorFromBox() const;
    void selectRow(int row);

    // Pages of the document on the screen, which is what the 指定页之后 box is bounded
    // by. Note that this is not the length of the document a save writes once a merge
    // is pending -- see the header.
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    QTableWidget *table = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;
    QPushButton *upButton = nullptr;
    QPushButton *downButton = nullptr;
    QRadioButton *frontChoice = nullptr;
    QRadioButton *backChoice = nullptr;
    QRadioButton *behindChoice = nullptr;
    QLineEdit *anchorEdit = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
    // Guards the table's own edit signal while this class fills the table in: a row
    // moved or a range rewritten by us is not a reader changing their mind.
    bool fillingTable = false;
};
