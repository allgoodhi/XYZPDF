// The one choice the Excel export has before it writes: whether the pages that
// have no table come along as their text, or are left out.
//
// It is a dialog rather than a checkbox buried in the save picker because the
// native picker cannot hold one, and because the choice changes what the file
// contains -- a reader who exports a report and gets only the tables has to be
// able to see that it was a choice.
#pragma once

#include <QDialog>

class QCheckBox;

class ExcelExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExcelExportDialog(QWidget *parent);

    // True when the reader asked for the text of pages that have no table.
    bool exportText() const;

private:
    QCheckBox *textBox = nullptr;
};
