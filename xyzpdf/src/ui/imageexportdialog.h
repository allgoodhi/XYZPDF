// The choices for exporting pages as pictures.
//
// Which pages, in what format, at what resolution, and where they go. The range
// reuses the same parser the print dialog uses (see Print::parsePageRange), so a
// reader who typed "1-3,5" for one gets the same meaning from the other. The
// folder is a plain line with a browse button rather than a dialog-in-a-dialog:
// the reader should see where the files are going before they commit.
#pragma once

#include "../export/imageexport.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

class ImageExportDialog : public QDialog
{
    Q_OBJECT

public:
    ImageExportDialog(QWidget *parent, int currentPage, int pageCount);

    // The pages to export, resolved from the range choice against the document's
    // page count -- empty when a custom range named nothing.
    QVector<int> pages() const;
    ImageExport::Format format() const;
    int dpi() const;
    QString directory() const;

private:
    void browse();

    int currentPageIndex = 0;
    int pageTotal = 0;

    QRadioButton *allRange = nullptr;
    QRadioButton *currentRange = nullptr;
    QRadioButton *customRange = nullptr;
    QLineEdit *customEdit = nullptr;
    QComboBox *formatBox = nullptr;
    QSpinBox *dpiBox = nullptr;
    QLineEdit *dirEdit = nullptr;
};
