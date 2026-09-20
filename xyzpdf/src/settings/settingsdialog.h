// Everything the user can decide, in one window.
//
// Changes apply as they are made rather than on an OK button: the theme has to
// be shown to be judged, and a dialog that previews a theme without applying it
// is a dialog that lies about what the window will look like. What that costs
// is an undo path, so Cancel restores what was there when the dialog opened --
// kept in restore() below rather than by disabling the live application.
#pragma once

#include "../theme/theme.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

Q_SIGNALS:
    // Something that the rest of the program has to react to changed. The
    // theme and the language are applied here; the cache size and the shell
    // entries are not, because the window that owns a document is the one that
    // knows what to do about them.
    void settingsChanged();

private:
    void chooseLanguage();
    void refreshRecentControls();

    QComboBox *themeBox = nullptr;
    QPushButton *languageButton = nullptr;
    QSpinBox *cacheBox = nullptr;
    QCheckBox *recentBox = nullptr;
    QPushButton *clearRecentButton = nullptr;
    QComboBox *translateProviderBox = nullptr;
    QLineEdit *translateKeyEdit = nullptr;
    QComboBox *translateTriggerBox = nullptr;
    QCheckBox *shellBox = nullptr;
    QLabel *shellNote = nullptr;

    // What was in effect when the dialog opened, for Cancel.
    AppTheme originalTheme = AppTheme::Default;
    bool originalRecent = true;
    bool originalShell = false;
    qint64 originalCache = 0;
    QString originalProvider;
    QString originalKey;
    QString originalTrigger;
};
