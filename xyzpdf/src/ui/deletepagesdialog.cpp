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
    // The count above is the document a save writes, while every number in the box
    // above it is a screen page number. Those two stop being the same as soon as a
    // merge has run, and a reader who checks "43 页" against a 40-page document they
    // are looking at needs to be told where the other three came from -- and that
    // this dialog has no way to reach them.
    if (editor && editor->importedPageCount() > 0) {
        // The total is what the English agrees with, and it can be one: 删除页面 has no
        // floor at its door the way 拆分文件 does, so a document deleted down to a single
        // imported page reaches here and says "1 of those 1 pages" without this branch.
        line += tr("\n\n这 %n 页里有 %1 页是合并进来的，屏幕上没有它们的页码，"
                   "这里删不到它们。",
                   nullptr, editor->pageCount())
                    .arg(editor->importedPageCount());
    }
    // Said before the press, because §77 made 删除页面 able to take content out of the file a save
    // writes: a field's answer is stored on the field, but it belongs to the page the field is drawn
    // on, and a file that does not contain that page does not carry the answer either. The field
    // stays registered (that is what the reader's own form layout says), so the sentence has to name
    // both halves or it reads as "the field is gone".
    if (editor && editor->hasFormFields()) {
        line += tr("\n\n这份文档有表单字段：字段填的答案属于它画在哪一页上，删掉的那一页上填的答案"
                   "不会出现在保存出来的文件里（字段本身还登记着）。");
    }
    // §47, and counted off this request rather than off the last one the list accepted: the save
    // takes the dead thread off a reply whose note's page is going (§78), which leaves the words
    // standing as a note of their own. That is a thing the reader is about to cause, so it is said
    // while the range box still has time to answer.
    if (const int lost = editor ? editor->repliesAtRiskForDelete(pages) : 0; lost > 0) {
        line += tr("\n\n这 %n 条回答会失去所回答的批注，保存出来成为独立的批注（写的字都还在）。",
                   nullptr, lost);
    }
    // XFA is the form's second copy, kept as XML that describes this document's pages by number.
    // Take pages out and it describes a document that does not exist, and nothing in the write
    // path can fix a copy it cannot read -- so a save whose pages changed writes none of it
    // (docs/store-submission.md §55, and DocumentEditor::dropXfaForThisDocument()).
    if (editor && editor->hasXfa()) {
        line += tr("\n\n这份文档还带着一份 XFA 表单：删过页面的文件里不会再有它，别的阅读器会改用"
                   "表单字段那一份。");
    }
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
