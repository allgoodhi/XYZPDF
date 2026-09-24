#include "movepagesdialog.h"

#include "../core/windowdialogs.h"
#include "../print/printjob.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

MovePagesDialog::MovePagesDialog(QWidget *parent, int currentPage,
                                 const DocumentEditor *document)
    : QDialog(parent)
    , currentPageIndex(currentPage)
    , pageTotal(document ? document->sourcePageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("移动页面"));
    WindowDialogs::makeWindowModal(this, parent);
    resize(440, 380);

    auto *layout = new QVBoxLayout(this);

    auto *group = new QGroupBox(tr("要移动的页面"), this);
    auto *choices = new QVBoxLayout(group);

    currentChoice = new QRadioButton(tr("当前页（第 %1 页）").arg(currentPage + 1), group);
    // The one starting point that is never a guess: the page a reader is looking at
    // is the page they have a reason to move, and the two dialogs this one sits
    // beside have been reaching for it all along. It is also what makes the dialog
    // usable the moment it opens -- the range box below starts empty on purpose, and
    // an empty box names nothing.
    currentChoice->setChecked(true);
    choices->addWidget(currentChoice);

    auto *customRow = new QHBoxLayout;
    customChoice = new QRadioButton(tr("指定页码"), group);
    customEdit = new QLineEdit(group);
    customEdit->setPlaceholderText(tr("例如 1-3,5,8-10"));
    customRow->addWidget(customChoice);
    customRow->addWidget(customEdit, 1);
    choices->addLayout(customRow);

    connect(customEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty() && !customChoice->isChecked()) {
            QSignalBlocker guard(customChoice);
            customChoice->setChecked(true);
        }
        updateSummary();
    });
    connect(customChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(currentChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });

    layout->addWidget(group);

    auto *targetGroup = new QGroupBox(tr("移到"), this);
    auto *targets = new QVBoxLayout(targetGroup);
    frontChoice = new QRadioButton(tr("最前面"), targetGroup);
    backChoice = new QRadioButton(tr("最后面"), targetGroup);
    backChoice->setChecked(true);
    targets->addWidget(frontChoice);
    targets->addWidget(backChoice);

    auto *behindRow = new QHBoxLayout;
    // "指定页之后" and a box beside it rather than "第 [__] 页之后" in three
    // widgets: a sentence taken apart for a text field cannot be translated, because
    // the words either side of the number are the ones the number has to sit between
    // -- and English puts them in the other order. One label and one box says the
    // same thing in both languages.
    behindChoice = new QRadioButton(tr("指定页之后"), targetGroup);
    anchorEdit = new QLineEdit(targetGroup);
    // One page, not a range: a block moves behind one page or it does not have a
    // place. Bounded by the document, so the box cannot be talked into a number that
    // was never a page -- which is a different refusal from a page an unsaved change
    // deleted, and the summary below keeps those two apart.
    anchorEdit->setValidator(new QIntValidator(1, pageTotal < 1 ? 1 : pageTotal, this));
    anchorEdit->setPlaceholderText(tr("页码"));
    // Wide enough for its own placeholder: 56 px fits 页码 but clips "Page number" down
    // to "Pag…", and this box has no label of its own besides the sentence to its left.
    anchorEdit->setFixedWidth(qMax(56, anchorEdit->fontMetrics().horizontalAdvance(
                                          anchorEdit->placeholderText()) + 24));
    behindRow->addWidget(behindChoice);
    behindRow->addWidget(anchorEdit);
    behindRow->addStretch(1);
    targets->addLayout(behindRow);

    connect(anchorEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty() && !behindChoice->isChecked()) {
            QSignalBlocker guard(behindChoice);
            behindChoice->setChecked(true);
        }
        updateSummary();
    });
    connect(frontChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(backChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(behindChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });

    layout->addWidget(targetGroup);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("移动这些页"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateSummary();
    customEdit->setFocus();
}

void MovePagesDialog::updateSummary()
{
    using Move = DocumentEditor::MoveTarget;

    const QVector<int> pages = pagesToMove();
    const int anchor = anchorPage();
    const bool behind = target() == Move::AfterPage;
    // Two ways the 第 __ 页之后 box is not yet an answer: nothing in it, and a
    // number past the end of the document. Neither reaches planMove(), which reads
    // an anchor it cannot find as "the page an earlier change deleted" -- a true
    // statement about some numbers and a wrong one about these.
    const bool anchorMissing = behind && anchor < 0;
    const bool anchorPastEnd = behind && anchor >= pageTotal;
    const DocumentEditor::MovePlan plan = editor
        ? editor->planMove(pages, target(), anchor)
        : DocumentEditor::MovePlan{};

    QString line;
    if (pages.isEmpty())
        line = tr("还没有说出要移动哪几页。");
    else if (anchorMissing)
        line = tr("要说移到第几页之后。");
    else if (anchorPastEnd)
        line = tr("这份文档一共 %2 页，没有第 %1 页。").arg(anchor + 1).arg(pageTotal);
    else if (plan.pagesMoved == 0)
        line = tr("这些页在前面的修改里已经删掉了，这里没有可以移动的。");
    else if (plan.anchorIsMoved)
        line = tr("第 %1 页就在要移动的页里，这样移等于没移。").arg(anchor + 1);
    else if (!plan.changesOrder)
        line = tr("这些页已经在那个位置上了，这样移不会把它们挪动。");
    else {
        // Where they end up, said as a range: the block arrives in one piece and in
        // the order it already had, so this is a promise the save can keep rather
        // than a description of what the reader just typed.
        line = tr("把 %1 页移到%2：移完它们连着排在第 %3-%4 页。")
                   .arg(plan.pagesMoved)
                   .arg(targetName())
                   .arg(plan.landsAt + 1)
                   .arg(plan.landsAt + plan.pagesMoved);
        if (plan.anchorWasGone) {
            line += plan.anchorUsed >= 0
                ? tr("\n\n第 %1 页在还没保存的修改里已经删掉了，所以是排在第 %2 页前面。")
                      .arg(anchor + 1)
                      .arg(plan.anchorUsed + 1)
                : tr("\n\n第 %1 页在还没保存的修改里已经删掉了，而它后面没有页了，所以放到最后。")
                      .arg(anchor + 1);
        }
    }
    // Same sentence the other two edit dialogs end with, for the same reason.
    summaryLabel->setText(tr("%1\n\n原来那份文件不会改动。这一步只是记下修改，"
                             "要它成为一个文件，还得走保存。")
                              .arg(line));

    okayButton->setEnabled(editor && !anchorMissing && !anchorPastEnd && plan.usable());
}

QString MovePagesDialog::targetName() const
{
    using Move = DocumentEditor::MoveTarget;
    switch (target()) {
    case Move::Front:
        return tr("最前面");
    case Move::AfterPage:
        return tr("第 %1 页之后").arg(anchorPage() + 1);
    case Move::Back:
        break;
    }
    return tr("最后面");
}

QVector<int> MovePagesDialog::pagesToMove() const
{
    if (currentChoice->isChecked())
        return {currentPageIndex};
    // Blank is "nothing said yet", not "every page" -- the same guard the other two
    // edit dialogs have, and the reason is the same: the parser they share answers a
    // blank range with the whole document, which is right for printing and wrong for
    // anything that changes what gets written.
    const QString spec = customEdit->text().trimmed();
    if (spec.isEmpty())
        return {};
    return Print::parsePageRange(spec, pageTotal);
}

DocumentEditor::MoveTarget MovePagesDialog::target() const
{
    if (frontChoice->isChecked())
        return DocumentEditor::MoveTarget::Front;
    if (behindChoice->isChecked())
        return DocumentEditor::MoveTarget::AfterPage;
    return DocumentEditor::MoveTarget::Back;
}

int MovePagesDialog::anchorPage() const
{
    if (target() != DocumentEditor::MoveTarget::AfterPage)
        return -1;
    return anchorFromBox();
}

int MovePagesDialog::anchorFromBox() const
{
    bool typed = false;
    const int number = anchorEdit->text().trimmed().toInt(&typed);
    if (!typed || number < 1)
        return -1;
    // Past the end is passed back as it was typed rather than clamped: the summary
    // has to say which page the reader meant, and a number that was quietly moved
    // would make the sentence below the choices disagree with the box above it.
    return number - 1;
}
