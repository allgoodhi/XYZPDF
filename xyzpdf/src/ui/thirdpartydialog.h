#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPlainTextEdit;

// The list of components, and the licence of whichever one is selected.
//
// A dialog of its own rather than a tab in the settings: this is not a
// preference and there is nothing to decide here.
class ThirdPartyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThirdPartyDialog(QWidget *parent = nullptr);

private:
    void showComponent(int row);

    QListWidget *list = nullptr;
    QLabel *heading = nullptr;
    QPlainTextEdit *text = nullptr;
};
