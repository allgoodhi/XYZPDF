// Which words to stamp on which pages, and how pale -- in the file.
//
// The screen has no watermark of its own to be confused with, so this dialog's whole wording
// job is the other direction: a reader who is used to a stamp in a viewer's annotation layer
// needs to see that this one is in the paper. It is drawn once, as a picture, and the save
// that carries it out writes it into every page it names -- see tests/watermarkprobe.cpp for
// why a picture rather than text operators, in short so that copying a passage, 划线翻译 and
// 导出 Word do not carry the stamp along with the words under it.
//
// The page-choice half is the rotate dialog's, range and all, with one difference worth
// noticing: it opens on 全部 rather than on 当前页, because the words this box asks for are
// usually meant for the whole document, and a reader who wants one page types one number.
#pragma once

#include "../edit/documenteditor.h"

#include <QDialog>

class DocumentEditor;

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

class WatermarkDialog : public QDialog
{
    Q_OBJECT

public:
    WatermarkDialog(QWidget *parent, const DocumentEditor *editor);

    // What the reader asked for, in the struct the pending list holds. `pages` is never empty
    // when this is usable: a box still on 全部 is turned into those page numbers here, before
    // the editor ever sees it, so the operation a save replays carries the list a reader could
    // check rather than a flag that means "however many there happen to be".
    DocumentEditor::Watermark request() const;

private:
    void updateSummary();

    // Pages in the file on disk: the numbering a reader types, because it is the one on screen.
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    QLineEdit *textEdit = nullptr;
    QSpinBox *opacityEdit = nullptr;
    QRadioButton *allChoice = nullptr;
    QRadioButton *customChoice = nullptr;
    QLineEdit *customEdit = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
};
