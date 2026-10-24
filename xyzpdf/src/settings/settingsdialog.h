// Everything the user can decide, in one window.
//
// Changes apply as they are made rather than on an OK button: the theme has to
// be shown to be judged, and a dialog that previews a theme without applying it
// is a dialog that lies about what the window will look like. What that costs
// is an undo path, so Cancel restores what was there when the dialog opened --
// kept in restore() below rather than by disabling the live application.
#pragma once

#include "../theme/theme.h"
#include "../translate/keystore.h"

#include <QDialog>
#include <QHash>

#include <memory>

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

    // Puts the translation service in front, for the 「去设置」 button in the box
    // that appears when translation has not been configured yet. The dialog is
    // one window with groups rather than pages; this is the part of "open the
    // translation page" that still means something here.
    void focusTranslateSection();

Q_SIGNALS:
    // Something that the rest of the program has to react to changed. The
    // theme and the language are applied here; the cache size and the shell
    // entries are not, because the window that owns a document is the one that
    // knows what to do about them.
    void settingsChanged();

protected:
    // The 「去设置」 request arrives before the dialog is on screen, and focus set
    // on a hidden widget is not the same thing as focus -- the first widget in the
    // tab order would take it when the window appears instead. So the wish is
    // recorded and carried out in showEvent.
    void showEvent(QShowEvent *event) override;

private:
    void chooseLanguage();
    void refreshRecentControls();
    // The translation key belongs to the service, not to the field: switching
    // the service loads that service's key, and leaving the field writes the
    // typed one to the service it was typed for. Both go through the credential
    // manager; QSettings never sees a key.
    void loadKeyFor(const QString &providerId);
    bool commitKeyEdit();
    void updateConsentControls(const QString &providerId);
    void rememberKey(const QString &providerId);
    void rememberConsent(const QString &providerId);

    QComboBox *themeBox = nullptr;
    QPushButton *languageButton = nullptr;
    QSpinBox *cacheBox = nullptr;
    QCheckBox *recentBox = nullptr;
    QPushButton *clearRecentButton = nullptr;
    QComboBox *translateProviderBox = nullptr;
    QLineEdit *translateKeyEdit = nullptr;
    QCheckBox *translateConsentBox = nullptr;
    QComboBox *translateTriggerBox = nullptr;
    QCheckBox *shellBox = nullptr;
    QLabel *shellNote = nullptr;
    QCheckBox *revocationBox = nullptr;
    // Which service the text in the key field belongs to, and whether it has
    // been edited since it was loaded. The service can change under the field,
    // so the field alone does not say whose key it is.
    QString keyFieldProvider;
    bool keyDirty = false;
    bool focusTranslateOnShow = false;
    // Created once; also what reads a key back for the field. In the program it
    // is the Windows credential manager.
    std::unique_ptr<Translate::KeyStore> translateKeys;

    // What was in effect when the dialog opened, for Cancel.
    AppTheme originalTheme = AppTheme::Default;
    bool originalRecent = true;
    bool originalShell = false;
    bool originalRevocation = false;
    qint64 originalCache = 0;
    QString originalProvider;
    QString originalTrigger;
    // Captured the first time each one is changed, so Cancel can put back every
    // key and consent the dialog touched -- not only the provider that happened
    // to be selected when it opened.
    QHash<QString, QString> originalKeys;
    QHash<QString, bool> originalConsents;
};
