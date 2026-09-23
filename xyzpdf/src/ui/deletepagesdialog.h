// Which pages to take out of a document.
//
// The range is the same one the print dialog and the image export take
// (Print::parsePageRange), so "1-3,5" means the same three things to this
// program. What this dialog has that they do not is a running total: a delete is
// the first command in this program that can make a document smaller, and the
// number it says it will leave behind is computed by the same function the editor
// enforces (DocumentEditor::planDelete), not by a second one here. That is why it
// is handed the editor rather than a page count: with unsaved changes already
// piled up in it, "how many are left" is a question about the editor, not about
// the file.
//
// There is no "全部" choice, because naming every page is the one request that
// cannot be granted -- a PDF with no pages in it is not a PDF.
#pragma once

#include <QDialog>
#include <QVector>

class DocumentEditor;

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

class DeletePagesDialog : public QDialog
{
    Q_OBJECT

public:
    // `editor` is what the reader is looking at, and is only asked what the
    // document would look like after this request -- it changes nothing.
    DeletePagesDialog(QWidget *parent, int currentPage, const DocumentEditor *editor);

    // Zero-based, in the order the range parser produced them.
    QVector<int> pagesToDelete() const;

private:
    // The sentence below the choices, and whether OK is pressable. Called
    // whenever either input changes.
    void updateSummary();

    int currentPageIndex = 0;
    // The numbering the reader sees, which is the file on disk -- the view has not
    // moved, so pages an earlier unsaved change removed are still on screen.
    int pageTotal = 0;
    const DocumentEditor *editor = nullptr;

    QRadioButton *currentChoice = nullptr;
    QRadioButton *customChoice = nullptr;
    QLineEdit *customEdit = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
};
