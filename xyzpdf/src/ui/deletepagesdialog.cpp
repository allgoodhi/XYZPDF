#include "deletepagesdialog.h"

#include "../core/windowdialogs.h"
#include "../edit/documenteditor.h"
#include "../print/printjob.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

DeletePagesDialog::DeletePagesDialog(QWidget *parent, int currentPage,
                                     const DocumentEditor *document)
    : QDialog(parent)
    , currentPageIndex(currentPage)
    , pageTotal(document ? document->sourcePageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("删除页面"));
    WindowDialogs::makeWindowModal(this, parent);
    resize(440, 220);

    auto *layout = new QVBoxLayout(this);

    auto *group = new QGroupBox(tr("要删除的页面"), this);
    auto *choices = new QVBoxLayout(group);

    currentChoice = new QRadioButton(tr("当前页（第 %1 页）").arg(currentPage + 1), group);
    choices->addWidget(currentChoice);

    auto *customRow = new QHBoxLayout;
    customChoice = new QRadioButton(tr("指定页码"), group);
    customChoice->setChecked(true);
    customEdit = new QLineEdit(group);
    customEdit->setPlaceholderText(tr("例如 1-3,5,8-10"));
    customRow->addWidget(customChoice);
    customRow->addWidget(customEdit, 1);
    choices->addLayout(customRow);

    connect(customEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        // Typing into the box is asking for a custom range, the same shortcut the
        // export dialog gives. The summary is retaken on every keystroke rather
        // than only when the choice changes, because "1-" names nothing and "1-3"
        // names three, and the line has to say which of those it is.
        if (!text.trimmed().isEmpty() && !customChoice->isChecked()) {
            QSignalBlocker guard(customChoice);
            customChoice->setChecked(true);
        }
        updateSummary();
    });
    connect(customChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(currentChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });

    layout->addWidget(group);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("删除这些页"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateSummary();
    customEdit->setFocus();
}

void DeletePagesDialog::updateSummary()
{
    const QVector<int> pages = pagesToDelete();
    const DocumentEditor::DeletePlan plan = editor
        ? editor->planDelete(pages)
        : DocumentEditor::DeletePlan{};

    QString line;
    if (plan.pagesToRemove == 0 && plan.alreadyGone > 0)
        // Contradicting the reader is the thing to avoid here: they did name
        // pages, and it is the unsaved changes already in this document that took
        // those pages out.
        line = tr("这些页在前面的修改里已经删掉了，这里没有可删的。");
    else if (plan.pagesToRemove == 0)
        line = tr("还没有说出要删哪几页。");
    else if (plan.leavesNothing)
        line = tr("这样一页也不剩了，而一个不剩的 PDF 是打不开的。留着至少一页。");
    else
        line = tr("现在这份有 %1 页：再删 %2 页，剩下 %3 页。")
                   .arg(editor ? editor->pageCount() : 0)
                   .arg(plan.pagesToRemove)
                   .arg(plan.pagesRemaining);
    // Said every time, because this is the first command in the program that can
    // make a document smaller, and what a reader needs to know before pressing the
    // button is that the file they opened is not the file that changes -- and that
    // this step does not write anything yet either.
    summaryLabel->setText(tr("%1\n\n原来那份文件不会改动。这一步只是记下修改，"
                             "要它成为一个文件，还得走保存。")
                              .arg(line));

    okayButton->setEnabled(plan.pagesToRemove > 0 && !plan.leavesNothing);
}

QVector<int> DeletePagesDialog::pagesToDelete() const
{
    if (currentChoice->isChecked())
        return {currentPageIndex};
    // Blank means "nothing said yet", not "every page" -- see the same guard in the
    // rotate dialog. Only the delete side has a floor that stops the whole document
    // from going, and a floor that says "这样一页也不剩了" is a worse answer to a
    // reader who never typed anything than a greyed button.
    const QString spec = customEdit->text().trimmed();
    if (spec.isEmpty())
        return {};
    return Print::parsePageRange(spec, pageTotal);
}
