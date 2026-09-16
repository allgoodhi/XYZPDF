// Choosing the interface language.
//
// Its own window, reached from its own entry in the menu bar rather than from
// inside the settings. Someone who has ended up in a language they cannot read
// has to be able to find this without reading anything, so the menu entry says
// "Language" in every language and this window shows each language written in
// itself.
#pragma once

#include <QDialog>

class QListWidget;

class LanguageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LanguageDialog(QWidget *parent = nullptr);

    // The code the user settled on, or an empty string if they cancelled.
    QString chosenCode() const;

private:
    QListWidget *list = nullptr;
    QString selected;
};
