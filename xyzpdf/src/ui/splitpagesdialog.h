// How many pages a piece should hold, which is the only question a split has.
//
// ---- why one number and not a list of ranges ----
//
// 每 N 页一份 is what docs/PLAN.md §4 asks for, and it is also the only form this dialog
// can make honest. A box that took "1-3, 7-9, 12" would be a page picker whose numbers
// are positions in a document the reader is looking at, while a save writes a document
// that 删页 may have already shrunk and 移动页面 may have already reordered (those two
// leave the view alone -- docs/store-submission.md §12). A fixed piece size is stated
// once and applied to whatever the saved document turns out to be, so it stays true no
// matter what gets edited after this box closes, and the ranges the summary lists are
// computed by DocumentEditor::pieceRanges() -- the same function the save lays files
// down with -- rather than by a second copy of that arithmetic here.
//
// ---- what the reader is agreeing to ----
//
// Nothing is written. 拆分文件 records one entry in the undo list, and its whole visible
// effect is that a later 保存修改为 writes N files instead of one. That is unusual enough
// among the four commands in this menu -- each of which changes which pages exist or
// where they sit -- that the summary says it in those words, and shows what the first
// piece of such a set would be called, since a piece's name is derived from a destination
// the reader has not typed yet.
//
// ---- what this dialog does not do ----
//
// It cannot be used while a merge is pending: an imported page carries a number from a
// file the reader can no longer see, so "第 6-10 页" would name pages of two different
// documents at once. The summary says so and the button stays greyed for the same reason
// the delete dialog greys it when nothing was ever named.
#pragma once

#include <QDialog>

class DocumentEditor;
class QLabel;
class QSpinBox;
class QPushButton;

class SplitPagesDialog : public QDialog
{
    Q_OBJECT

public:
    SplitPagesDialog(QWidget *parent, const DocumentEditor *document);

    // Pages per piece, as the box holds it. Which piece set that produces is not asked
    // here: the caller hands this number to DocumentEditor::splitInto() and that is where
    // pieceRanges() decides.
    int pieceSize() const;

private:
    void updateSummary();

    // Pages of the document a save would write -- `pageCount()`, not `sourcePageCount()`
    // -- because a piece is cut out of that one. The two differ as soon as 删页 or
    // 合并文件进来 is pending, which is what the summary says out loud.
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    QSpinBox *sizeEdit = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
};
