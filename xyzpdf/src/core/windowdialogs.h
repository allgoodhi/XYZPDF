// Message boxes and file pickers that block one window instead of the program.
//
// Qt's static helpers -- QMessageBox::warning, QFileDialog::getExistingDirectory
// and the rest -- all end in QDialog::exec(), and exec() makes a dialog
// application modal unless somebody has already said otherwise. In a program
// with one window that is invisible. This program opens a window per document
// and, from M3, more than one window at a time, so it means that picking a file
// in one window freezes every other window, including documents the user is
// only reading.
//
// These are the same calls with the same arguments, made window modal. There is
// no other difference: nothing here changes what a dialog says or returns.
//
// ---- what "window modal" actually gets you on Windows ----
//
// Measured rather than assumed, in XYZip's dialogs_smoke: with a window-modal
// parent, Windows disables the parent window and leaves every other top-level
// window enabled. With the application-modal form it disables all of them. So
// the answer to "does Windows even allow this" is yes, and the freezing was
// ours.
//
// A dialog opened from a dialog blocks its whole chain up to the window it came
// from, which is what somebody would expect, and nothing outside it.
#pragma once

#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QString>
#include <QStringList>

class QWidget;

namespace WindowDialogs {

// Applies the modality every dialog in this program should have. Exposed so
// that dialogs built by hand can use the same rule, and so the rule itself can
// be tested rather than repeated.
void makeWindowModal(QDialog *dialog, QWidget *parent);

QMessageBox::StandardButton warning(
    QWidget *parent, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

QMessageBox::StandardButton information(
    QWidget *parent, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

QMessageBox::StandardButton critical(
    QWidget *parent, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

QMessageBox::StandardButton question(
    QWidget *parent, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::StandardButtons(QMessageBox::Yes
                                                                        | QMessageBox::No),
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

QString getExistingDirectory(QWidget *parent, const QString &caption,
                             const QString &directory = QString());

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &directory,
                        const QString &filter, QString *selectedFilter = nullptr,
                        QFileDialog::Options options = QFileDialog::Options());

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &directory,
                        const QString &filter, QString *selectedFilter = nullptr,
                        QFileDialog::Options options = QFileDialog::Options());

QStringList getOpenFileNames(QWidget *parent, const QString &caption = QString(),
                             const QString &directory = QString(),
                             const QString &filter = QString(),
                             QString *selectedFilter = nullptr,
                             QFileDialog::Options options = QFileDialog::Options());

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode mode, const QString &text, bool *ok);

} // namespace WindowDialogs
