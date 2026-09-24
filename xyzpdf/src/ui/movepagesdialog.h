// Which pages to move, and where to put them.
//
// The gesture a reader would reach for is a drag -- grab a thumbnail and drop it
// somewhere else. That is not what this is, and the reason is the one in docs/
// store-submission.md §12: the view does not move while edits are pending. The
// thumbnails on the left are the pages as the file on disk has them, so a drag
// would start from a picture of a document that is not the one being built, and
// the drop would land at a position the reader cannot see. A range box and a
// target, on the other hand, name pages by the numbers printed on the screen,
// which is the same rule 删除页面 and 旋转页面 follow.
//
// The page-choice half is those dialogs', range parser and all, so that "1-3,5"
// means one thing in this program -- including the blank box, which means "nothing
// said yet" here and not "every page".
//
// What a move is here: the named pages travel as one block and keep the order they
// have among themselves. That is what makes "把第 2、4 页移到最后" a single action
// rather than two, and it is what the summary says out loud, because a reader who
// meant two separate moves would get a different document from the one they asked
// for and would have no way to find out until the file was written.
#pragma once

#include "../edit/documenteditor.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

class MovePagesDialog : public QDialog
{
    Q_OBJECT

public:
    MovePagesDialog(QWidget *parent, int currentPage, const DocumentEditor *editor);

    // Zero-based, in the order the range parser produced them.
    QVector<int> pagesToMove() const;
    DocumentEditor::MoveTarget target() const;
    // The page the block lands behind, zero-based, or -1 when the target is not
    // MoveTarget::AfterPage. Out-of-range numbers are not filtered here: the editor
    // refuses those, and the dialog only greys its own button.
    int anchorPage() const;

private:
    void updateSummary();
    // Which of the three target radios is on, as the words the summary uses.
    QString targetName() const;
    // The number typed in the 第 __ 页之后 box, or -1 when it does not hold one.
    int anchorFromBox() const;

    int currentPageIndex = 0;
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    QRadioButton *currentChoice = nullptr;
    QRadioButton *customChoice = nullptr;
    QLineEdit *customEdit = nullptr;
    QRadioButton *frontChoice = nullptr;
    QRadioButton *backChoice = nullptr;
    QRadioButton *behindChoice = nullptr;
    QLineEdit *anchorEdit = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
};
