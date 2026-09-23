// Which pages to turn, and which way -- in the file.
//
// This program already has a rotate: the 视图 toolbar button, which turns the
// pages on screen and is forgotten when the tab closes. That one is a reading
// convenience; this one writes /Rotate into a document, which is what a reader
// wants when the page was scanned upside down and the file will keep saying so to
// everyone who opens it. The two have to be worded apart, because a person who
// pressed the wrong one ends up with a file they did not mean to make.
//
// The page-choice half is the delete dialog's, range and all, for the same
// reason: one set of numbers means one thing across the program.
#pragma once

#include <QDialog>
#include <QVector>

class DocumentEditor;

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

class RotatePagesDialog : public QDialog
{
    Q_OBJECT

public:
    RotatePagesDialog(QWidget *parent, int currentPage, const DocumentEditor *editor);

    // Zero-based, in the order the range parser produced them.
    QVector<int> pagesToRotate() const;
    // Quarter turns to add clockwise: 1 for 向右转 90°, 2 for 180°, 3 for 向左转
    // 90°. PDF stores it this way, so this is where the choice lands.
    int quarterTurns() const;

private:
    void updateSummary();
    // Which of the three direction radios is on, as the words the summary uses.
    QString directionName() const;

    int currentPageIndex = 0;
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    QRadioButton *currentChoice = nullptr;
    QRadioButton *customChoice = nullptr;
    QLineEdit *customEdit = nullptr;
    QRadioButton *rightTurn = nullptr;
    QRadioButton *upsideDown = nullptr;
    QRadioButton *leftTurn = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
};
