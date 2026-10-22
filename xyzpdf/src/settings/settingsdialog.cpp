#include "settingsdialog.h"

#include "../core/appsettings.h"
#include "../core/languages.h"
#include "../core/recentdocuments.h"
#include "../core/windowdialogs.h"
#include "../render/rendercache.h"
#include "../theme/thememanager.h"
#include "../translate/translator.h"
#include "../ui/languagedialog.h"
#include "../windows/shellintegration.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// The cache limit is set in whole megabytes. A number in bytes would be a
// field nobody could read, and the difference between 256 MB and 256 MB plus
// one byte is not a decision anybody makes.
constexpr qint64 kMegabyte = 1024LL * 1024LL;

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置"));
    setObjectName(QStringLiteral("settingsDialog"));
    resize(520, 420);

    originalTheme = ThemeManager::currentTheme();
    originalRecent = RecentDocuments::isEnabled();
    originalShell = AppSettings::shellIntegrationEnabled();
    originalCache = AppSettings::renderCacheBytes(RenderCache::DefaultMaximumBytes);
    originalProvider = AppSettings::translateProvider();
    originalTrigger = AppSettings::translateTrigger();
    translateKeys = Translate::makeSystemKeyStore();

    auto *layout = new QVBoxLayout(this);

    // ---- 外观 ----
    {
        auto *group = new QGroupBox(tr("外观"), this);
        auto *form = new QFormLayout(group);

        themeBox = new QComboBox(group);
        themeBox->addItem(tr("浅色"), appThemeId(AppTheme::Default));
        themeBox->addItem(tr("深色"), appThemeId(AppTheme::DefaultDark));
        themeBox->setCurrentIndex(themeBox->findData(appThemeId(originalTheme)));
        form->addRow(tr("主题"), themeBox);

        languageButton = new QPushButton(group);
        form->addRow(tr("语言"), languageButton);

        layout->addWidget(group);

        connect(themeBox, &QComboBox::currentIndexChanged, this, [this] {
            const AppTheme chosen = appThemeFromId(themeBox->currentData().toString());
            AppSettings::setTheme(chosen);
            ThemeManager::apply(chosen);
            Q_EMIT settingsChanged();
        });
        connect(languageButton, &QPushButton::clicked, this, &SettingsDialog::chooseLanguage);
    }

    // ---- 阅读 ----
    {
        auto *group = new QGroupBox(tr("阅读"), this);
        auto *form = new QFormLayout(group);

        cacheBox = new QSpinBox(group);
        cacheBox->setRange(16, 4096);
        cacheBox->setSuffix(tr(" MB"));
        cacheBox->setValue(int(originalCache / kMegabyte));
        form->addRow(tr("页面缓存上限"), cacheBox);

        auto *note = new QLabel(
            tr("已渲染的页面留在内存里，超过上限时最久没看的那一页被丢弃。"), group);
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(QString(), note);

        layout->addWidget(group);

        connect(cacheBox, &QSpinBox::valueChanged, this, [this](int megabytes) {
            AppSettings::setRenderCacheBytes(qint64(megabytes) * kMegabyte);
            Q_EMIT settingsChanged();
        });
    }

    // ---- 翻译 ----
    {
        auto *group = new QGroupBox(tr("翻译"), this);
        auto *form = new QFormLayout(group);

        translateProviderBox = new QComboBox(group);
        // Empty is a real choice: both services below need a key, so there is no
        // "works out of the box" option any more. The old free Google endpoint
        // was removed -- see translator.h.
        translateProviderBox->addItem(tr("未选择"), QString());
        translateProviderBox->addItem(Translate::providerName(Translate::Provider::DeepL),
                                      QStringLiteral("deepl"));
        translateProviderBox->addItem(Translate::providerName(Translate::Provider::GoogleCloud),
                                      QStringLiteral("google"));
        translateProviderBox->setCurrentIndex(
            qMax(0, translateProviderBox->findData(originalProvider)));
        form->addRow(tr("翻译服务"), translateProviderBox);

        translateKeyEdit = new QLineEdit(group);
        translateKeyEdit->setEchoMode(QLineEdit::Password);
        form->addRow(tr("API 密钥"), translateKeyEdit);

        translateConsentBox = new QCheckBox(group);
        form->addRow(QString(), translateConsentBox);

        translateTriggerBox = new QComboBox(group);
        translateTriggerBox->addItem(tr("选中后手动（右键菜单 / 工具栏）"), QStringLiteral("menu"));
        translateTriggerBox->addItem(tr("选中即自动翻译"), QStringLiteral("auto"));
        translateTriggerBox->setCurrentIndex(
            qMax(0, translateTriggerBox->findData(originalTrigger)));
        form->addRow(tr("触发方式"), translateTriggerBox);

        auto *note = new QLabel(
            tr("翻译只在你主动触发时进行。密钥保存在 Windows 凭据管理器中，不会写进设置文件。"), group);
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(QString(), note);

        layout->addWidget(group);

        connect(translateProviderBox, &QComboBox::currentIndexChanged, this, [this] {
            // Finish the key edit for the service that is being left before the
            // field is repurposed for the next one.
            commitKeyEdit();
            const QString chosen = translateProviderBox->currentData().toString();
            AppSettings::setTranslateProvider(chosen);
            loadKeyFor(chosen);
            updateConsentControls(chosen);
            Q_EMIT settingsChanged();
        });
        // The key is written when focus leaves the field rather than on every
        // keystroke: a password box typed one character at a time would otherwise
        // rewrite the credential a dozen times for one paste.
        connect(translateKeyEdit, &QLineEdit::textChanged, this, [this] { keyDirty = true; });
        connect(translateKeyEdit, &QLineEdit::editingFinished, this, &SettingsDialog::commitKeyEdit);
        connect(translateConsentBox, &QCheckBox::toggled, this, [this](bool checked) {
            const QString provider = translateProviderBox->currentData().toString();
            if (provider.isEmpty())
                return;
            rememberConsent(provider);
            AppSettings::setTranslateConsent(provider, checked);
            Q_EMIT settingsChanged();
        });
        connect(translateTriggerBox, &QComboBox::currentIndexChanged, this, [this] {
            AppSettings::setTranslateTrigger(translateTriggerBox->currentData().toString());
            Q_EMIT settingsChanged();
        });

        loadKeyFor(originalProvider);
        updateConsentControls(originalProvider);
    }

    // ---- 最近文件 ----
    {
        auto *group = new QGroupBox(tr("最近文件"), this);
        auto *form = new QFormLayout(group);

        recentBox = new QCheckBox(tr("记住我打开过的文档"), group);
        form->addRow(QString(), recentBox);

        clearRecentButton = new QPushButton(tr("清除列表"), group);
        form->addRow(QString(), clearRecentButton);

        auto *note = new QLabel(tr("关掉这一项会同时清空已经记下的列表。列表只存在本机。"), group);
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(QString(), note);

        layout->addWidget(group);

        connect(recentBox, &QCheckBox::toggled, this, [this](bool enabled) {
            RecentDocuments::setEnabled(enabled);
            // Turning it off clears the list, so the button's enabled state
            // changes with it rather than staying live over an empty list.
            refreshRecentControls();
            Q_EMIT settingsChanged();
        });
        connect(clearRecentButton, &QPushButton::clicked, this, [this] {
            RecentDocuments::clear();
            refreshRecentControls();
            Q_EMIT settingsChanged();
        });
    }

    // ---- 系统集成 ----
    {
        auto *group = new QGroupBox(tr("资源管理器"), this);
        auto *form = new QFormLayout(group);

        shellBox = new QCheckBox(tr("在右键菜单里显示“用 XYZPDF 打开”"), group);
        form->addRow(QString(), shellBox);

        shellNote = new QLabel(group);
        shellNote->setWordWrap(true);
        shellNote->setEnabled(false);
        form->addRow(QString(), shellNote);

        layout->addWidget(group);

        connect(shellBox, &QCheckBox::toggled, this, [this](bool enabled) {
            QString error;
            const QString classesRoot = ShellIntegration::defaultClassesRoot();
            const bool ok = enabled
                                ? ShellIntegration::registerVerbs(
                                      QCoreApplication::applicationFilePath(), classesRoot, &error)
                                : ShellIntegration::unregisterVerbs(classesRoot, &error);
            if (!ok) {
                WindowDialogs::warning(this, tr("资源管理器"), error);
                // Put the box back rather than leaving it claiming something
                // that did not happen. Signals are blocked so that the handler
                // is not entered again by its own correction.
                const QSignalBlocker blocker(shellBox);
                shellBox->setChecked(!enabled);
                return;
            }
            AppSettings::setShellIntegrationEnabled(enabled);
            Q_EMIT settingsChanged();
        });
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        // The field may still have focus, in which case editingFinished has not
        // run yet. A key the credential manager refused must not be followed by
        // an accepting dialog.
        if (commitKeyEdit())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    // Cancel puts back what was there. The theme is the one that matters: it
    // has already been applied to every window, and leaving it changed after a
    // Cancel would be the dialog disagreeing with itself.
    connect(this, &QDialog::rejected, this, [this] {
        AppSettings::setTheme(originalTheme);
        ThemeManager::apply(originalTheme);
        AppSettings::setRenderCacheBytes(originalCache);
        RecentDocuments::setEnabled(originalRecent);
        AppSettings::setTranslateProvider(originalProvider);
        AppSettings::setTranslateTrigger(originalTrigger);
        // Every key and consent the dialog touched, not only the selected
        // service's: switching the combo and typing a key for the other service
        // is a change too, and Cancel has to mean all of it.
        for (auto it = originalKeys.constBegin(); it != originalKeys.constEnd(); ++it) {
            if (it.value().isEmpty())
                translateKeys->remove(it.key(), nullptr);
            else
                translateKeys->write(it.key(), it.value(), nullptr);
        }
        for (auto it = originalConsents.constBegin(); it != originalConsents.constEnd(); ++it)
            AppSettings::setTranslateConsent(it.key(), it.value());
        if (AppSettings::shellIntegrationEnabled() != originalShell) {
            QString error;
            const QString classesRoot = ShellIntegration::defaultClassesRoot();
            if (originalShell) {
                ShellIntegration::registerVerbs(QCoreApplication::applicationFilePath(),
                                                classesRoot, &error);
            } else {
                ShellIntegration::unregisterVerbs(classesRoot, &error);
            }
            AppSettings::setShellIntegrationEnabled(originalShell);
        }
        Q_EMIT settingsChanged();
    });

    refreshRecentControls();

    const QString current = AppSettings::languageCode();
    languageButton->setText(current == Languages::systemCode()
                                ? tr("跟随系统")
                                : Languages::nativeName(current));
    {
        const QSignalBlocker blocker(shellBox);
        shellBox->setChecked(originalShell);
    }
    shellNote->setText(ShellIntegration::isRegistered(ShellIntegration::defaultClassesRoot())
                           ? tr("已写入当前用户的注册表（不需要管理员权限）。"
                                "Windows 11 上它出现在“显示更多选项”里。")
                           : tr("只写入当前用户，不影响其他账户，也不需要管理员权限。"));
}

void SettingsDialog::chooseLanguage()
{
    LanguageDialog dialog(this);
    // The picker is built from the languages this build carries, so it is
    // window-modal like every other dialog here -- see windowdialogs.h.
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString chosen = dialog.chosenCode();
    if (chosen.isEmpty())
        return;
    AppSettings::setLanguageCode(chosen);
    languageButton->setText(chosen == Languages::systemCode()
                                ? tr("跟随系统")
                                : Languages::nativeName(chosen));
    // Said out loud rather than applied: retranslating a running window means
    // rebuilding every string already built, and the ones held in model data
    // would not be. The dialog in the language picker says the same thing.
    WindowDialogs::information(this, tr("语言"), tr("语言更改将在下次启动时生效。"));
}

void SettingsDialog::refreshRecentControls()
{
    const bool enabled = RecentDocuments::isEnabled();
    {
        const QSignalBlocker blocker(recentBox);
        recentBox->setChecked(enabled);
    }
    clearRecentButton->setEnabled(enabled && !RecentDocuments::all().isEmpty());
}

void SettingsDialog::focusTranslateSection()
{
    // The wish, not the focus: the 「去设置」 request reaches the window before this
    // dialog has ever been on a screen, and focus asked for then lands on
    // whichever widget the tab order picks when it appears.
    focusTranslateOnShow = true;
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!focusTranslateOnShow)
        return;
    focusTranslateOnShow = false;
    translateProviderBox->setFocus(Qt::OtherFocusReason);
}

void SettingsDialog::loadKeyFor(const QString &providerId)
{
    QString key;
    if (!providerId.isEmpty())
        translateKeys->read(providerId, &key, nullptr);
    {
        // setText would otherwise count as an edit and write the key back on the
        // next focus change -- which is harmless, but makes "dirty" meaningless.
        const QSignalBlocker blocker(translateKeyEdit);
        translateKeyEdit->setText(key);
    }
    keyFieldProvider = providerId;
    keyDirty = false;
    translateKeyEdit->setEnabled(!providerId.isEmpty());
    translateKeyEdit->setPlaceholderText(providerId.isEmpty()
                                             ? tr("先选择翻译服务")
                                             : tr("填写所选服务的 API 密钥"));
}

bool SettingsDialog::commitKeyEdit()
{
    if (!keyDirty || keyFieldProvider.isEmpty())
        return true;
    rememberKey(keyFieldProvider);
    QString error;
    if (!translateKeys->write(keyFieldProvider, translateKeyEdit->text().trimmed(), &error)) {
        // The field is left as typed and still dirty: the reader can retry or
        // cancel, and Cancel restores the key that was there.
        WindowDialogs::warning(this, tr("翻译"), error);
        return false;
    }
    keyDirty = false;
    Q_EMIT settingsChanged();
    return true;
}

void SettingsDialog::updateConsentControls(const QString &providerId)
{
    const bool configured = !providerId.isEmpty();
    {
        const QSignalBlocker blocker(translateConsentBox);
        translateConsentBox->setChecked(configured
                                        && AppSettings::translateConsentGiven(providerId));
    }
    translateConsentBox->setEnabled(configured);
    translateConsentBox->setText(
        configured
            ? tr("允许把选中的文字发送给 %1")
                  .arg(Translate::providerName(Translate::providerFromId(providerId)))
            : tr("允许把选中的文字发送给所选的翻译服务"));
}

void SettingsDialog::rememberKey(const QString &providerId)
{
    if (providerId.isEmpty() || originalKeys.contains(providerId))
        return;
    QString key;
    translateKeys->read(providerId, &key, nullptr);
    originalKeys.insert(providerId, key);
}

void SettingsDialog::rememberConsent(const QString &providerId)
{
    if (providerId.isEmpty() || originalConsents.contains(providerId))
        return;
    originalConsents.insert(providerId, AppSettings::translateConsentGiven(providerId));
}
