#include "languagedialog.h"

#include "../core/appsettings.h"
#include "../core/languages.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

LanguageDialog::LanguageDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Language"));
    setObjectName(QStringLiteral("languageDialog"));
    resize(360, 420);

    auto *layout = new QVBoxLayout(this);

    list = new QListWidget(this);
    list->setObjectName(QStringLiteral("languageList"));

    const QString current = AppSettings::languageCode();

    // "Follow the system" first, and it is the only row written in the current
    // interface language: it names a behaviour rather than a language, so there
    // is no "itself" to write it in.
    auto *systemItem = new QListWidgetItem(tr("跟随系统"), list);
    systemItem->setData(Qt::UserRole, Languages::systemCode());

    for (const Languages::Entry &entry : Languages::available()) {
        auto *item = new QListWidgetItem(entry.nativeName, list);
        item->setData(Qt::UserRole, entry.code);
    }

    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toString() == current) {
            list->setCurrentRow(row);
            break;
        }
    }
    if (!list->currentItem())
        list->setCurrentRow(0);

    layout->addWidget(list, 1);

    // Said plainly rather than applied live. Retranslating a running window
    // means every string already built has to be rebuilt, and the ones held in
    // model data would not be; a restart is honest and cannot half-work.
    auto *hint = new QLabel(tr("语言更改将在下次启动时生效。"), this);
    hint->setObjectName(QStringLiteral("languageHint"));
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Double-clicking a language is the obvious way to pick one, and it should
    // not also need the button.
    connect(list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    connect(this, &QDialog::accepted, this, [this] {
        if (QListWidgetItem *item = list->currentItem())
            selected = item->data(Qt::UserRole).toString();
    });
}

QString LanguageDialog::chosenCode() const
{
    return selected;
}
