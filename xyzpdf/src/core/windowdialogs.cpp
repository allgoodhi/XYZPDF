#include "windowdialogs.h"

#include <QInputDialog>
#include <QPushButton>
#include <QWidget>

namespace WindowDialogs {

void makeWindowModal(QDialog *dialog, QWidget *parent)
{
    if (!dialog)
        return;
    // With no parent there is no window to be modal to, and a window-modal
    // dialog with no parent blocks nothing at all -- which would be worse than
    // blocking too much. Those few cases stay application modal on purpose.
    dialog->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
}

namespace {

QMessageBox::StandardButton ask(QWidget *parent, QMessageBox::Icon icon, const QString &title,
                                const QString &text, QMessageBox::StandardButtons buttons,
                                QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, text, QMessageBox::NoButton, parent);
    box.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);
    makeWindowModal(&box, parent);
    box.exec();
    return box.standardButton(box.clickedButton());
}

QString pickFile(QWidget *parent, QFileDialog::AcceptMode accept, QFileDialog::FileMode mode,
                 const QString &caption, const QString &directory, const QString &filter,
                 QString *selectedFilter, QFileDialog::Options options, QStringList *chosen)
{
    QFileDialog dialog(parent, caption, directory, filter);
    dialog.setAcceptMode(accept);
    dialog.setFileMode(mode);
    dialog.setOptions(options);
    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    makeWindowModal(&dialog, parent);
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    if (selectedFilter)
        *selectedFilter = dialog.selectedNameFilter();
    const QStringList files = dialog.selectedFiles();
    if (chosen)
        *chosen = files;
    return files.isEmpty() ? QString() : files.first();
}

} // namespace

QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons,
                                    QMessageBox::StandardButton defaultButton)
{
    return ask(parent, QMessageBox::Warning, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton information(QWidget *parent, const QString &title,
                                        const QString &text,
                                        QMessageBox::StandardButtons buttons,
                                        QMessageBox::StandardButton defaultButton)
{
    return ask(parent, QMessageBox::Information, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons,
                                     QMessageBox::StandardButton defaultButton)
{
    return ask(parent, QMessageBox::Critical, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons,
                                     QMessageBox::StandardButton defaultButton)
{
    return ask(parent, QMessageBox::Question, title, text, buttons, defaultButton);
}

int askChoice(QWidget *parent, const QString &title, const QString &text,
              const QStringList &choices, int defaultChoice)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
    QList<QAbstractButton *> buttons;
    for (const QString &choice : choices)
        buttons << box.addButton(choice, QMessageBox::AcceptRole);
    if (defaultChoice >= 0 && defaultChoice < buttons.size()) {
        if (auto *button = qobject_cast<QPushButton *>(buttons.at(defaultChoice)))
            box.setDefaultButton(button);
    }
    makeWindowModal(&box, parent);
    box.exec();
    return int(buttons.indexOf(box.clickedButton()));
}

QString getExistingDirectory(QWidget *parent, const QString &caption, const QString &directory)
{
    QFileDialog dialog(parent, caption, directory);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    makeWindowModal(&dialog, parent);
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &directory,
                        const QString &filter, QString *selectedFilter,
                        QFileDialog::Options options)
{
    return pickFile(parent, QFileDialog::AcceptSave, QFileDialog::AnyFile, caption, directory,
                    filter, selectedFilter, options, nullptr);
}

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &directory,
                        const QString &filter, QString *selectedFilter,
                        QFileDialog::Options options)
{
    return pickFile(parent, QFileDialog::AcceptOpen, QFileDialog::ExistingFile, caption,
                    directory, filter, selectedFilter, options, nullptr);
}

QStringList getOpenFileNames(QWidget *parent, const QString &caption, const QString &directory,
                             const QString &filter, QString *selectedFilter,
                             QFileDialog::Options options)
{
    QStringList chosen;
    pickFile(parent, QFileDialog::AcceptOpen, QFileDialog::ExistingFiles, caption, directory,
             filter, selectedFilter, options, &chosen);
    return chosen;
}

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode mode, const QString &text, bool *ok)
{
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextEchoMode(mode);
    dialog.setTextValue(text);
    dialog.setInputMode(QInputDialog::TextInput);
    makeWindowModal(&dialog, parent);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? dialog.textValue() : QString();
}

} // namespace WindowDialogs
