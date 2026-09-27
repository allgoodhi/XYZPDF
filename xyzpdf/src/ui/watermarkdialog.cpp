#include "watermarkdialog.h"

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
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

WatermarkDialog::WatermarkDialog(QWidget *parent, const DocumentEditor *document)
    : QDialog(parent)
    , pageTotal(document ? document->sourcePageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("加水印"));
    WindowDialogs::makeWindowModal(this, parent);
    // Wider than the two controls need: the summary ends with a sentence about a drawing
    // nobody has to find in the file, and a wrapped quote of the reader's own words stops
    // being a check on them.
    resize(520, 360);

    auto *layout = new QVBoxLayout(this);

    // ---- 字 ----
    {
        auto *group = new QGroupBox(tr("水印文字"), this);
        auto *rows = new QVBoxLayout(group);

        textEdit = new QLineEdit(group);
        textEdit->setPlaceholderText(tr("例如 机密、作废、内部资料"));
        rows->addWidget(textEdit);

        auto *opacityRow = new QHBoxLayout;
        opacityRow->addWidget(new QLabel(tr("不透明度"), group));
        opacityEdit = new QSpinBox(group);
        // 1..100 because those are the two ends a reader can check the page against: 0 is
        // invisible and addWatermark() refuses it, 100 is ink over the words under it.
        opacityEdit->setRange(1, 100);
        opacityEdit->setSingleStep(5);
        opacityEdit->setSuffix(QStringLiteral(" %"));
        // A QSpinBox that is never told a value opens at its minimum, and the minimum here is 1 --
        // the real window showed a box offering 「DRAFT」，1% 不透明度 to a reader who touched
        // nothing, which is a watermark nobody can see. The number is read from the struct rather
        // than written again so the box cannot drift away from what the model calls a default.
        opacityEdit->setValue(DocumentEditor::Watermark {}.opacityPercent);
        opacityRow->addWidget(opacityEdit);
        opacityRow->addStretch();
        rows->addLayout(opacityRow);

        layout->addWidget(group);
    }

    // ---- 哪几页 ----
    {
        auto *group = new QGroupBox(tr("要盖水印的页面"), this);
        auto *choices = new QVBoxLayout(group);

        allChoice = new QRadioButton(tr("全部（%n 页）", nullptr, pageTotal), group);
        allChoice->setChecked(true);
        choices->addWidget(allChoice);

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
        connect(allChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
        connect(textEdit, &QLineEdit::textChanged, this, [this] { updateSummary(); });
        connect(opacityEdit, qOverload<int>(&QSpinBox::valueChanged), this,
                [this] { updateSummary(); });

        layout->addWidget(group);
    }

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Opening on what is already pending is what makes pressing 确定 again read as changing
    // the words rather than as a second thing to undo -- the split box's rule, for the same
    // reward. The numbers go back into the box as a comma list because that is what this
    // parser reads, not a new arithmetic to spell ranges out.
    const DocumentEditor::Watermark recorded =
        editor ? editor->watermarkToWrite() : DocumentEditor::Watermark();
    if (!recorded.text.isEmpty()) {
        QSignalBlocker guard(textEdit);
        textEdit->setText(recorded.text);
        opacityEdit->setValue(recorded.opacityPercent);
        if (!recorded.pages.isEmpty() && recorded.pages.size() < pageTotal) {
            QStringList spelled;
            for (int page : recorded.pages)
                spelled << QString::number(page + 1);
            customChoice->setChecked(true);
            customEdit->setText(spelled.join(QLatin1Char(',')));
        }
    }

    updateSummary();
    textEdit->setFocus();
}

DocumentEditor::Watermark WatermarkDialog::request() const
{
    DocumentEditor::Watermark asked;
    asked.text = textEdit->text().trimmed();
    asked.opacityPercent = opacityEdit->value();

    if (allChoice->isChecked()) {
        for (int page = 0; page < pageTotal; ++page)
            asked.pages.append(page);
        return asked;
    }
    // A blank box means "nothing said yet", not "every page". The print parser reads an empty
    // range as the whole document -- right for printing, where no selection and all pages are
    // the same default -- and this dialog has a 全部 radio for saying that on purpose, so a
    // second way to say it would be a reader pressing Enter on four hundred pages by accident.
    const QString spec = customEdit->text().trimmed();
    if (spec.isEmpty())
        return asked;
    asked.pages = Print::parsePageRange(spec, pageTotal);
    return asked;
}

void WatermarkDialog::updateSummary()
{
    const DocumentEditor::Watermark asked = request();
    // The count the sentence shows is the count the save will draw on: pages named that an
    // earlier 删页 took out are not there to ink, and `addWatermark()` drops them the same way
    // `rotatePages()` does, so one number is read off one function here too.
    const int present = editor ? editor->pagesStillThere(asked.pages) : asked.pages.size();

    QString line;
    const bool hasWords = !asked.text.isEmpty();
    const bool hasPages = present > 0;
    if (!hasWords)
        line = tr("还没有说要盖什么字。");
    else if (asked.pages.isEmpty())
        line = tr("还没有说出要盖哪几页。");
    else if (!hasPages)
        line = tr("这些页在前面的修改里已经删掉了，没有可以盖的。");
    else if (present < asked.pages.size())
        line = tr("「%1」，%2% 不透明度。说好的 %3 页里有 %n 页还在，只盖那 %n 页。",
                  nullptr, present)
                   .arg(asked.text)
                   .arg(asked.opacityPercent)
                   .arg(asked.pages.size());
    else
        line = tr("「%1」，%2% 不透明度，盖在 %n 页上。", nullptr, present)
                   .arg(asked.text)
                   .arg(asked.opacityPercent);

    if (hasWords && hasPages) {
        // Said because it is the one thing about a watermark a reader cannot see from the box:
        // the marks are one picture laid over the page, so the words under them stay the only
        // ones a selection, a translation or a Word export can reach. The alternative --
        // drawing them as text -- measured 13 extractable characters turning into 30.
        line += tr("\n\n这些字是画上去的一张图，不是文档里的文字：选中、复制、翻译、导出都只会碰到"
                   "原来的字，看不到这行水印。");
    }

    if (const int imported = editor ? editor->importedPageCount() : 0; imported > 0 && hasPages) {
        line += tr("\n\n前面合并进来的那 %n 页屏幕上没有页号，所以不在这 %1 页里，也不会带上水印。",
                   nullptr, imported)
                    .arg(present);
    }

    const DocumentEditor::Watermark before =
        editor ? editor->watermarkToWrite() : DocumentEditor::Watermark();
    bool again = false;
    if (!before.text.isEmpty() && hasWords && hasPages) {
        if ((again = (before == asked))) {
            // The editor's own "not a change" refusal, said before the press rather than in a
            // warning box after it -- the shape §22 settled on for the 加密 box, which greys
            // exactly the requests protectWith() turns down. A lit button here would dirty the
            // tab and hand out an undo row that undoes nothing.
            line += tr("\n\n这跟前面已经记下的一条一模一样，按下不会多改什么 —— 要改就换一个词，"
                       "或者动一下不透明度。");
        } else {
            line += tr("\n\n这会把前面记下的「%1」换掉；撤销一次回到那一个。").arg(before.text);
        }
    }

    // Same ending as the other five dialogs here, for the same reason: nothing on this screen
    // has written a file, and a watermark is the one of them that changes what a page looks
    // like, which is exactly when a reader most needs to know the original is untouched.
    line += tr("\n\n原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。");

    summaryLabel->setText(line);
    okayButton->setText(hasPages && hasWords ? tr("盖这 %n 页", nullptr, present) : tr("确定"));
    okayButton->setEnabled(hasWords && hasPages && !again);
}
