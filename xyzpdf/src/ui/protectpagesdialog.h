// 加密 / 去掉口令: what a save should hand the file it writes about protection.
//
// ---- why two directions in one box ----
//
// They are one operation with the other sign on it: neither changes a page, and both only
// change the parameters the single writer at the end of a save is given -- see the header
// note in src/edit/documenteditor.h. Splitting them into two menu rows would give each
// dialog the job of explaining the other, and would break the case this box has to handle
// best: a file that already has a password. There the reader may want a different
// password, different permissions, or none at all, which is one question with three
// answers rather than two questions.
//
// ---- what the six boxes are, and what they are not ----
//
// The six are PDF's own /P flags, read and written through the same accessors this
// program's reader uses to answer "may I" (QPDF::allowPrintHighRes() and friends), so the
// box and the file are never two different answers. What they are not is a lock: nothing
// in the format enforces them, and measurement showed exactly how little they hold here
// (tests/encryptprobe.cpp) -- QPDF never records which password unlocked a file, so a
// session that opened a document by its user password can write that same document out
// with no restrictions at all, with no further key. That is why the group says out loud
// that these are a request rather than an enforced limit, and why the summary repeats it at
// the moment 确定 is within reach.
//
// The password field is the opposite: the content really is encrypted with it, so losing
// it really is losing the document. Two consequences are drawn here rather than hidden:
// the field opens empty even when the file being read has a password (filling it in would
// put the thing the lock exists to protect into a text box, and a reader who means to keep
// the same password is one keystroke away from typing it), and the summary says that
// writing a file with an empty field means nobody needs a key to open it -- the restriction
// then is only the six requests above.
//
// That R6/AES-256 keeps out readers older than Acrobat X is the format's rule and is said
// as one; it is the one sentence in this box that cannot be checked on this machine, since
// there is no old reader here to fail with.
//
// ---- what this dialog does not do ----
//
// Nothing is written. Like 拆分文件, this records one entry in the undo list and its whole
// visible effect lands at 保存修改为 -- so the source file on disk keeps its own protection
// whichever way this box is answered, and 去掉口令 of somebody else's file cannot damage it.
// The button stays greyed when the direction chosen has nothing to act on (taking the
// protection off a file that never had any) or when the request is the answer a save would
// already give; both refusals are the editor's to make as well, and it makes them off this
// same dialog's answer, so the two cannot disagree.
#pragma once

#include "../edit/documenteditor.h"

#include <QDialog>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

class ProtectPagesDialog : public QDialog
{
    Q_OBJECT

public:
    ProtectPagesDialog(QWidget *parent, const DocumentEditor *document);

    // Everything the boxes say, as the one struct a save reads. `what` carries the
    // direction, so the caller does not have to ask the two halves separately.
    //
    // Under 去掉口令 the password and the six flags are still filled in, because that is
    // what the boxes hold; nothing reads them, since taking the protection off has no
    // parameters. Returning a half-filled struct instead would put a second answer to
    // "what did the box say" behind which direction is showing.
    DocumentEditor::Protection request() const;

private:
    void updateSummary();

    const DocumentEditor *editor = nullptr;

    QRadioButton *encryptChoice = nullptr;
    QRadioButton *decryptChoice = nullptr;
    // Disabled together when 去掉口令 is showing: a question that will not be read is a
    // worse answer than a greyed one.
    QGroupBox *passwordGroup = nullptr;
    QGroupBox *permissionGroup = nullptr;
    QLineEdit *passwordEdit = nullptr;
    QLineEdit *confirmEdit = nullptr;
    QCheckBox *printBox = nullptr;
    QCheckBox *extractBox = nullptr;
    QCheckBox *annotateBox = nullptr;
    QCheckBox *formBox = nullptr;
    QCheckBox *assemblyBox = nullptr;
    QCheckBox *otherBox = nullptr;
    QLabel *summaryLabel = nullptr;
    QPushButton *okayButton = nullptr;
};
