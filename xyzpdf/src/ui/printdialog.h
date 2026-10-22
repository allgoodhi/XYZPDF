// The choices a print job needs that the system dialog does not offer.
//
// QPrintDialog already gives the printer, the copy count, collation, duplex and
// page order, and it is the dialog people know -- so those are reached through
// its button rather than reinvented here. What it does not offer is the things
// that are this program's own: a page range that can be several spans ("1-3,5"),
// whether a page is shrunk to fit or printed at its real size, and how many
// pages go on one sheet. Those are what this window is for.
#pragma once

#include "../print/printjob.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QLabel;
class QPrinter;
class QRadioButton;

class PrintDialog : public QDialog
{
    Q_OBJECT

public:
    // `printer` is not owned here; the dialog only opens QPrintDialog against it
    // so the user's choices land on it. `currentPage` and `pageCount` are for the
    // "current page" and "all pages" wording. `hasFormFields` adds the line that
    // says this program draws no field contents, so a filled form prints blank.
    PrintDialog(QWidget *parent, QPrinter *printer, int currentPage, int pageCount,
                bool hasFormFields);

    Print::Settings settings() const;

private:
    void choosePrinter();

    QPrinter *printer = nullptr;
    int pageTotal = 0;
    int currentPageIndex = 0;

    QRadioButton *allRange = nullptr;
    QRadioButton *currentRange = nullptr;
    QRadioButton *customRange = nullptr;
    QLineEdit *customEdit = nullptr;
    QComboBox *scaleBox = nullptr;
    QComboBox *nUpBox = nullptr;
    QLabel *printerLabel = nullptr;
};
