#include "settingsdialog.h"

#include "../core/appsettings.h"
#include "../core/languages.h"
#include "../core/recentdocuments.h"
#include "../core/windowdialogs.h"
#include "../render/rendercache.h"
#include "../theme/thememanager.h"
#include "../ui/languagedialog.h"
#include "../windows/shellintegration.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
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

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    // Cancel puts back what was there. The theme is the one that matters: it
    // has already been applied to every window, and leaving it changed after a
    // Cancel would be the dialog disagreeing with itself.
    connect(this, &QDialog::rejected, this, [this] {
        AppSettings::setTheme(originalTheme);
        ThemeManager::apply(originalTheme);
        AppSettings::setRenderCacheBytes(originalCache);
        RecentDocuments::setEnabled(originalRecent);
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
