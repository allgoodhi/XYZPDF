#include "splitpagesdialog.h"

#include "../core/windowdialogs.h"
#include "../edit/documenteditor.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

SplitPagesDialog::SplitPagesDialog(QWidget *parent, const DocumentEditor *document)
    : QDialog(parent)
    , pageTotal(document ? document->pageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("拆分文件"));
    WindowDialogs::makeWindowModal(this, parent);
    // Wider than the four-line summary needs at first glance: its last line is a file
    // name, and a name that wraps in the middle of "01 (1-5)" stops being the example it
    // was put there to be (docs/store-submission.md §18).
    resize(560, 300);

    auto *layout = new QVBoxLayout(this);

    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("每"), this));
    sizeEdit = new QSpinBox(this);
    // The upper bound is one less than the document rather than the document itself:
    // 每 40 页一份 of 40 pages writes one file, which is not a split, and DocumentEditor
    //::splitInto() refuses that. A box that could not be pressed is a worse answer than a
    // range that starts at two pieces.
    sizeEdit->setRange(1, qMax(1, pageTotal - 1));
    // Half the book is the default because it is the smallest answer that is certainly a
    // split; 每 1 页一份 is certainly not what somebody meant when they have 400 pages.
    // When a split is already pending this box opens on that size, so pressing 确定 again
    // reads as changing the number rather than as a second thing to undo.
    const int recorded = editor ? editor->splitPieceSize() : 0;
    sizeEdit->setValue(recorded > 0 ? recorded : qMax(1, pageTotal / 2));
    sizeEdit->setSuffix(tr(" 页一份"));
    sizeRow->addWidget(sizeEdit);
    sizeRow->addStretch();
    layout->addLayout(sizeRow);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(sizeEdit, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateSummary(); });
    layout->addWidget(buttons);

    updateSummary();
    sizeEdit->setFocus();
}

int SplitPagesDialog::pieceSize() const
{
    return sizeEdit->value();
}

void SplitPagesDialog::updateSummary()
{
    const int size = sizeEdit->value();
    const QVector<DocumentEditor::PieceRange> pieces = DocumentEditor::pieceRanges(pageTotal, size);
    if (pieces.isEmpty()) {
        // Only a document with no pages reaches here, which the tab does not let happen;
        // saying it is better than a button that would write nothing.
        summaryLabel->setText(tr("这份文档没有可以拆的页面。"));
        okayButton->setText(tr("确定"));
        okayButton->setEnabled(false);
        return;
    }

    // Which pages, in the numbering a save writes. Listed up to six pieces and then cut
    // short, because 每 1 页一份 of a 600-page book is 600 ranges and the last one is the
    // piece a reader checks the arithmetic against, so it is the one worth keeping.
    QStringList listed;
    const auto rangeText = [](const DocumentEditor::PieceRange &piece) {
        return DocumentEditor::pageSpan(piece.first, piece.last);
    };
    for (int index = 0; index < pieces.size() && index < 6; ++index)
        listed << rangeText(pieces.at(index));
    if (pieces.size() > 6)
        listed << QStringLiteral("…") << rangeText(pieces.last());

    // The separator is behind tr() because it is text a reader sees: joining the ranges
    // with a literal 、 put CJK punctuation into the English interface, which read
    // "1-21、22-42" on screen.
    QString line = tr("会写成 %1 份，每份最多 %n 页：%2。", nullptr, size)
                       .arg(pieces.size())
                       .arg(listed.join(tr("、")));

    // The names come from the destination a reader types later, so this is the one part of
    // the answer that cannot be shown as a list of files. It is still shown, and still
    // computed by piecePath(), because the alternative is a reader meeting the naming for
    // the first time in a folder they already have N files in.
    const QString example = QFileInfo(
        DocumentEditor::piecePath(editor->path(), 0, int(pieces.size()), pieces.first())).fileName();
    line += tr("\n\n文件名是从保存时填的那个名字切出来的，例如第一份会叫「%1」。").arg(example);

    if (pageTotal != (editor ? editor->sourcePageCount() : pageTotal)) {
        line += tr("\n\n这些页号是照着要写出的那一份数的（%1 页），不是屏幕上那一份（%2 页）——"
                   "前面的修改没有改到界面。")
                    .arg(pageTotal)
                    .arg(editor->sourcePageCount());
    }
    if (const int imported = editor ? editor->importedPageCount() : 0; imported > 0) {
        line += tr("\n\n可是这 %1 页里有 %2 页是合并进来的，屏幕上没有它们的页号，所以哪几页该进"
                   "哪一份是说不出来的。先撤销那几次合并再拆。")
                    .arg(pageTotal)
                    .arg(imported);
    }
    if (const int before = editor ? editor->splitPieceSize() : 0; before > 0 && before != size) {
        line += tr("\n\n这会把前面记下的「每 %n 页一份」换掉；撤销一次回到那一个。", nullptr, before);
    }
    // The wait is a product, not a count: a save replays the whole document once per piece,
    // which measured 129 ms for 40 pieces of a 40-page file and 183 ms for 4 pieces of a
    // 600-page one. Both are under a tenth of the ~12,000 page-times-piece this line starts
    // speaking at, and that is where it begins to be seconds -- 每 1 页一份 of a 600-page
    // book is 600 replays. A progress box that appears to hang is what gets blamed.
    if (qint64(pieces.size()) * qint64(pageTotal) >= 12000) {
        line += tr("\n\n这要写 %1 个文件，每个都要把整份文档重读一遍，会等一会儿。").arg(pieces.size());
    }
    // §77, said where it matters most: a split is the command that sends pieces to different people,
    // and before it each piece carried every answer typed anywhere in the form -- invisible on the
    // page, present in /AcroForm. Now a piece carries only the answers of the fields it can draw.
    // The field names stay in every piece (that is what a spread field needs, §52), so the sentence
    // says which of the two goes.
    if (editor && editor->hasFormFields()) {
        line += tr("\n\n这份文档有表单字段：拆出来的每一份只带着它自己那些页上字段的答案，"
                   "别的页上填的内容不会跟着过去（字段本身在每份里都还登记着）。");
    }
    // §47, and the case a split is the only way to make: a thread that is perfectly legal inside
    // one document -- a reply on page 12 answering a note on page 3 -- stops being one the moment
    // the two pages are written as two files. Counted from the number in the box, so changing it
    // changes the sentence before anything is pressed (§80's rule).
    if (const int severed = editor ? editor->repliesAtRiskForSplit(size) : 0; severed > 0) {
        line += tr("\n\n这 %n 条回答和所回答的批注会被拆到两个文件里去，在各自的文件里都成为"
                   "独立的批注（写的字都还在）。",
                   nullptr, severed);
    }
    // The packet describes the whole document's form, and no piece is the whole document: each of
    // them would carry every answer typed anywhere in it, which is what §55 measured coming out of
    // a split. A save that writes pieces writes no XFA into any of them.
    if (editor && editor->hasXfa()) {
        line += tr("\n\n这份文档还带着一份 XFA 表单：拆出来的文件里不会再有它，别的阅读器会改用表单"
                   "字段那一份。");
    }
    // Said every time, like the other three dialogs here do, and more needed here than
    // there: a split is the one entry in the list that writes no page differently, so the
    // only thing it changes is how many files come out at the end.
    line += tr("\n\n原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，"
               "这 %1 个文件才会一起出现。")
                .arg(pieces.size());

    summaryLabel->setText(line);
    okayButton->setText(tr("拆成 %1 份").arg(pieces.size()));
    okayButton->setEnabled(!(editor && editor->importedPageCount() > 0));
}
