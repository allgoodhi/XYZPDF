#include "protectpagesdialog.h"

#include "../core/windowdialogs.h"
#include "../edit/documenteditor.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

using Protection = DocumentEditor::Protection;

ProtectPagesDialog::ProtectPagesDialog(QWidget *parent, const DocumentEditor *document)
    : QDialog(parent)
    , editor(document)
{
    setWindowTitle(tr("加密 / 去掉口令"));
    WindowDialogs::makeWindowModal(this, parent);
    // Taller than the other 文档 dialogs, because this one has six boxes to offer and four
    // things that have to be read before 确定 means anything: the six are a request rather
    // than a lock, a lost password is not recoverable, R6 keeps out old readers, and
    // nothing here writes a file. A summary that has to be scrolled is not read.
    resize(560, 520);

    auto *layout = new QVBoxLayout(this);

    const Protection pending = editor ? editor->protectionToWrite() : Protection();

    auto *directionGroup = new QGroupBox(tr("要做的事"), this);
    auto *directions = new QVBoxLayout(directionGroup);
    encryptChoice = new QRadioButton(tr("加上口令和权限"), directionGroup);
    decryptChoice = new QRadioButton(tr("去掉口令和权限"), directionGroup);
    // Opens on 加密 except when the last thing recorded was 去掉口令, so that the row that
    // would ask for a password nobody meant to type again is not the one showing.
    (pending.what == Protection::What::Decrypt ? decryptChoice : encryptChoice)->setChecked(true);
    directions->addWidget(encryptChoice);
    directions->addWidget(decryptChoice);
    layout->addWidget(directionGroup);

    passwordGroup = new QGroupBox(tr("打开口令"), this);
    auto *passwordRows = new QVBoxLayout(passwordGroup);
    auto *passwordRow = new QHBoxLayout;
    auto *passwordLabel = new QLabel(tr("口令"), passwordGroup);
    passwordRow->addWidget(passwordLabel);
    passwordEdit = new QLineEdit(passwordGroup);
    passwordEdit->setEchoMode(QLineEdit::Password);
    // Buddied because without it the two fields answer to the *same* name: read through
    // UI Automation both edits came back named 打开口令, which is the group's title, so the
    // one thing a reader cannot tell from the screen reader alone is which of the two boxes
    // it is typing into -- the one distinction these two fields have.
    passwordLabel->setBuddy(passwordEdit);
    passwordRow->addWidget(passwordEdit, 1);
    passwordRows->addLayout(passwordRow);
    // Asked twice because the answer cannot be checked any other way: nothing here can
    // recover or even report a password once the file is written, so a single mistyped
    // character is only ever found out by being locked out.
    auto *confirmRow = new QHBoxLayout;
    auto *confirmLabel = new QLabel(tr("再输一遍"), passwordGroup);
    confirmRow->addWidget(confirmLabel);
    confirmEdit = new QLineEdit(passwordGroup);
    confirmEdit->setEchoMode(QLineEdit::Password);
    confirmLabel->setBuddy(confirmEdit);
    confirmRow->addWidget(confirmEdit, 1);
    passwordRows->addLayout(confirmRow);
    layout->addWidget(passwordGroup);

    // The limit is stated in the title rather than only in the summary, because it is the
    // thing a reader needs while choosing each of the six: what they are ticking is a
    // request the file writes down, not a lock the file holds.
    permissionGroup = new QGroupBox(tr("允许的用途（这些限制写在文件里，不是强制的）"), this);
    auto *permissions = new QVBoxLayout(permissionGroup);
    // Two to a row, and the two that default to off last: what is *not* allowed is the
    // part a reader checks, and a column of six makes them read all six twice to find it.
    printBox = new QCheckBox(tr("允许打印"), permissionGroup);
    extractBox = new QCheckBox(tr("允许复制文字和图片"), permissionGroup);
    annotateBox = new QCheckBox(tr("允许添加或修改批注"), permissionGroup);
    formBox = new QCheckBox(tr("允许填写表单"), permissionGroup);
    assemblyBox = new QCheckBox(tr("允许插入、删除或旋转页面"), permissionGroup);
    otherBox = new QCheckBox(tr("允许其它修改"), permissionGroup);
    // Opened on what the document itself says rather than on this program's defaults: a
    // reader who came to change one permission should not have to re-derive the other five
    // from memory. For a file with no /Encrypt this is the struct's own defaults, since
    // QPDF answers "allowed" to every flag of an unencrypted document and none of those
    // answers is the file's.
    const Protection basis = editor
        ? (pending.what == Protection::What::Encrypt ? pending : editor->sourceProtection())
        : Protection();
    printBox->setChecked(basis.allowPrint);
    extractBox->setChecked(basis.allowExtract);
    annotateBox->setChecked(basis.allowAnnotate);
    formBox->setChecked(basis.allowFormFilling);
    assemblyBox->setChecked(basis.allowAssembly);
    otherBox->setChecked(basis.allowModifyOther);
    auto *firstPair = new QHBoxLayout;
    firstPair->addWidget(printBox);
    firstPair->addWidget(extractBox);
    auto *secondPair = new QHBoxLayout;
    secondPair->addWidget(annotateBox);
    secondPair->addWidget(formBox);
    auto *thirdPair = new QHBoxLayout;
    thirdPair->addWidget(assemblyBox);
    thirdPair->addWidget(otherBox);
    permissions->addLayout(firstPair);
    permissions->addLayout(secondPair);
    permissions->addLayout(thirdPair);
    layout->addWidget(permissionGroup);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const auto refresh = [this] { updateSummary(); };
    connect(encryptChoice, &QRadioButton::toggled, this, refresh);
    connect(decryptChoice, &QRadioButton::toggled, this, refresh);
    connect(passwordEdit, &QLineEdit::textChanged, this, refresh);
    connect(confirmEdit, &QLineEdit::textChanged, this, refresh);
    for (QCheckBox *box : { printBox, extractBox, annotateBox, formBox, assemblyBox, otherBox })
        connect(box, &QCheckBox::toggled, this, refresh);

    updateSummary();
    if (encryptChoice->isChecked())
        passwordEdit->setFocus();
}

DocumentEditor::Protection ProtectPagesDialog::request() const
{
    Protection asked;
    asked.what = decryptChoice->isChecked() ? Protection::What::Decrypt : Protection::What::Encrypt;
    asked.password = passwordEdit->text();
    asked.allowPrint = printBox->isChecked();
    asked.allowExtract = extractBox->isChecked();
    asked.allowAssembly = assemblyBox->isChecked();
    asked.allowAnnotate = annotateBox->isChecked();
    asked.allowFormFilling = formBox->isChecked();
    asked.allowModifyOther = otherBox->isChecked();
    return asked;
}

void ProtectPagesDialog::updateSummary()
{
    const bool removing = decryptChoice->isChecked();
    passwordGroup->setEnabled(!removing);
    permissionGroup->setEnabled(!removing);

    const Protection asked = request();
    // The same two functions the save reads, so that the sentence above 确定 and the
    // parameters handed to the writer cannot come apart -- and so that the three refusals
    // DocumentEditor makes for these two requests are all visible here as a greyed button
    // with a reason, rather than as a warning box after the reader has pressed it.
    const Protection current = editor ? editor->protectionToWrite() : Protection();
    const bool sourceProtected = editor && editor->sourceIsProtected();

    QString line;
    bool usable = true;
    // Whether a password was asked for at all: it decides the button's wording as well as
    // the first sentence, and 只改权限 is a real request rather than a half-filled form.
    const bool keyed = !asked.password.isEmpty();

    if (removing) {
        if (current.what == Protection::What::Decrypt) {
            line = tr("前面已经记下了一次去掉口令，写出的那一份本来就不带口令，再要一次不会多去掉什么。"
                      "要退回去用撤销。");
            usable = false;
        } else if (!sourceProtected && current.what == Protection::What::Carry) {
            line = tr("正在打开的这份文件没有口令，也没有写下任何权限限制，所以这里没有东西可去掉。");
            usable = false;
        } else {
            line = tr("写出的那一份不需要口令就能打开，也不再写下这些权限限制。");
            if (sourceProtected)
                line += tr("\n\n原来那份文件是锁着的：它要的口令对写出的这一份不再管用。");
            if (current.what == Protection::What::Encrypt)
                line += tr("\n\n前面记下的那一次加密会被这一步顶掉。");
        }
    } else {
        line = keyed
            ? tr("写出的这一份要输入口令才能打开（AES-256，也就是 PDF 里的 R6）。")
            : tr("口令那一栏空着，所以写出的这一份不需要口令就能打开 —— 这里留下的只有下面那六项限制。");
        if (sourceProtected) {
            line += keyed
                ? tr("\n\n原来那份文件也有口令。写出的这一份只认这里重新填的这一个：这里没有把它原来"
                     "的口令填进来，把那个口令显示出来正是加密要挡住的事。")
                : tr("\n\n原来那份文件是要口令才能打开的，写出的这一份不要 —— 那个口令就此不管用了。");
        }
        if (passwordEdit->text() != confirmEdit->text()) {
            // Only ever under 加密, and only ever here: a second field nobody compared the
            // first to is a false sense of safety, and a mistyped key is only found out by
            // being locked out. Under 去掉口令 the two fields are greyed and their contents
            // are nobody's business.
            line += tr("\n\n两次输入的口令不一样。");
            usable = false;
        }
        if (asked == current) {
            // Encrypt on both sides, and every box agreeing with it: this is the editor's
            // "not a change" refusal, said before the press rather than after it.
            line += tr("\n\n这跟前面已经记下的一条一模一样，按下不会多改什么 —— 要改就改一个勾，"
                       "或者换一个口令。");
            usable = false;
        } else if (current.what != Protection::What::Carry) {
            line += tr("\n\n这会把前面记下的那一条顶掉；撤销一次回到那一个。");
        }
        // Both halves in one sentence, because they are the two ways this box is easy to
        // get wrong: the six are trusted only as far as a reader of the file agrees, while
        // the password is real and losing it is final.
        line += tr("\n\n那六项是写在文件里的一份请求：别的阅读器照不照办都打得开这份文件，任何一个"
                   "能打开它的程序也都能把这些限制去掉。只有口令是真的锁 —— 丢了没有办法找回来。");
        line += tr("\n\n这是 PDF 里较新的 R6（AES-256），比 Acrobat X 更早的阅读器打不开这种文件。");
        if (editor && editor->importedPageCount() > 0)
            line += tr("\n\n前面合并进来的那些页也会一起锁上：口令管的是整份文件，不只是屏幕上"
                       "看到的这些页。");
    }

    line += removing
        ? tr("\n\n原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，"
             "去掉口令的那一份才会出现。")
        : tr("\n\n原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，"
             "带上这些的那一份才会出现。");

    summaryLabel->setText(line);
    okayButton->setText(removing  ? tr("去掉口令")
                          : keyed ? tr("加密")
                                  : tr("只改权限"));
    okayButton->setEnabled(usable);
}
