#include "mergepagesdialog.h"

#include "../core/appsettings.h"
#include "../core/windowdialogs.h"
#include "../print/printjob.h"

#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// One file and how many pages it has, held on the row's own first cell. The range is
// not stored separately: the cell's text is the range, which is what makes an edit of
// it the same event as a reader changing their mind.
int pathRole()
{
    return Qt::UserRole + 1;
}

int pageCountRole()
{
    return Qt::UserRole + 2;
}

// The identities the summary's simulation puts on the pages it is working out. They
// have to be numbers no real merge has handed out, because the whole point of the
// simulation is to pick *these* back out of the list at the end -- and the value itself
// changes nothing about where they land, which is all the plan is asked for. A real
// identity is 1 or above and rises with every merge (DocumentEditor::nextMergeId), so
// negative numbers are the ones nothing else uses.
int probeMergeId(int row)
{
    return -(row + 1);
}

constexpr int FileColumn = 0;
constexpr int CountColumn = 1;
constexpr int RangeColumn = 2;

} // namespace

MergePagesDialog::MergePagesDialog(QWidget *parent, const DocumentEditor *document)
    : QDialog(parent)
    , pageTotal(document ? document->sourcePageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("合并文件进来"));
    WindowDialogs::makeWindowModal(this, parent);
    resize(560, 460);

    auto *layout = new QVBoxLayout(this);

    auto *filesGroup = new QGroupBox(tr("要合并进来的文件"), this);
    auto *filesLayout = new QVBoxLayout(filesGroup);

    table = new QTableWidget(0, 3, filesGroup);
    table->setHorizontalHeaderLabels(
        {tr("文件"), tr("一共"), tr("要合入的页码")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Only the last column is editable, and that is the one a reader has a reason to
    // change: the file is what they picked, and the page count is that file's own
    // fact. Making the whole row editable would let a name be typed over a path that
    // was already looked over, and the row would then promise one file and merge
    // another.
    table->setEditTriggers(QAbstractItemView::DoubleClicked
                           | QAbstractItemView::EditKeyPressed);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(FileColumn, 250);
    table->setColumnWidth(CountColumn, 60);
    // At least four rows of the list are visible. This dialog's whole promise is the
    // order of this list, and a list that has to be scrolled a row at a time cannot be
    // read well enough to be ordered. The summary below it grows with the wording, so
    // without a floor of its own the table is what the layout squeezes: in English, with
    // three files picked, it came out showing no rows at all.
    table->setMinimumHeight(4 * table->verticalHeader()->defaultSectionSize()
                            + table->horizontalHeader()->sizeHint().height() + 8);
    filesLayout->addWidget(table);

    auto *rowButtons = new QHBoxLayout;
    addButton = new QPushButton(tr("添加文件…"), filesGroup);
    removeButton = new QPushButton(tr("移除"), filesGroup);
    upButton = new QPushButton(tr("上移"), filesGroup);
    downButton = new QPushButton(tr("下移"), filesGroup);
    rowButtons->addWidget(addButton);
    rowButtons->addWidget(removeButton);
    rowButtons->addWidget(upButton);
    rowButtons->addWidget(downButton);
    rowButtons->addStretch(1);
    filesLayout->addLayout(rowButtons);

    auto *hint = new QLabel(tr("双击“要合入的页码”可以改，例如 1-3,5,8-10；那一列写的是"
                               "那个文件自己的页码。"),
                            filesGroup);
    hint->setWordWrap(true);
    filesLayout->addWidget(hint);

    layout->addWidget(filesGroup);

    auto *targetGroup = new QGroupBox(tr("合到哪儿"), this);
    auto *targets = new QVBoxLayout(targetGroup);
    frontChoice = new QRadioButton(tr("最前面"), targetGroup);
    backChoice = new QRadioButton(tr("最后面"), targetGroup);
    backChoice->setChecked(true);
    targets->addWidget(frontChoice);
    targets->addWidget(backChoice);

    auto *behindRow = new QHBoxLayout;
    // Worded the same way 移动页面 words it, for the same reason: a sentence taken
    // apart for a text field cannot be translated, because the words either side of
    // the number are the ones the number has to sit between -- and English puts them
    // the other way round.
    behindChoice = new QRadioButton(tr("指定页之后"), targetGroup);
    anchorEdit = new QLineEdit(targetGroup);
    // Bounded by the document on the screen, not by the document a save would write:
    // the page a reader points at has to be a page they can see.
    anchorEdit->setValidator(new QIntValidator(1, pageTotal < 1 ? 1 : pageTotal, this));
    anchorEdit->setPlaceholderText(tr("页码"));
    // Wide enough for its own placeholder, as 移动页面's box is: 56 px fits 页码 but
    // clips "Page number" down to "Pag…".
    anchorEdit->setFixedWidth(qMax(56, anchorEdit->fontMetrics().horizontalAdvance(
                                          anchorEdit->placeholderText()) + 24));
    behindRow->addWidget(behindChoice);
    behindRow->addWidget(anchorEdit);
    behindRow->addStretch(1);
    targets->addLayout(behindRow);

    layout->addWidget(targetGroup);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("合并这些文件"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, &MergePagesDialog::addFiles);
    connect(removeButton, &QPushButton::clicked, this, &MergePagesDialog::removeSelected);
    connect(upButton, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(table, &QTableWidget::cellChanged, this, [this](int, int column) {
        if (column == RangeColumn)
            handleRangeEdit();
    });
    connect(table, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { updateSummary(); });
    connect(behindChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(frontChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(backChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(anchorEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty() && !behindChoice->isChecked()) {
            QSignalBlocker guard(behindChoice);
            behindChoice->setChecked(true);
        }
        updateSummary();
    });

    updateSummary();
    addButton->setFocus();
}

void MergePagesDialog::addFiles()
{
    const QString start = AppSettings::lastOpenDirectory().isEmpty()
                              ? QDir::homePath()
                              : AppSettings::lastOpenDirectory();
    const QStringList chosen = WindowDialogs::getOpenFileNames(
        this, tr("要合并进来的 PDF"), start, tr("PDF 文档 (*.pdf)"));
    if (chosen.isEmpty())
        return;

    const QStringList refused = addPaths(chosen);
    if (refused.size() < chosen.size())
        AppSettings::setLastOpenDirectory(QFileInfo(chosen.last()).absolutePath());
    if (!refused.isEmpty()) {
        WindowDialogs::warning(this, tr("合并文件进来"),
                               refused.join(QStringLiteral("\n\n")));
    }
}

QStringList MergePagesDialog::addPaths(const QStringList &paths)
{
    // Looked over before a row is made, so that a file which cannot be merged is
    // refused while the reader still has the picker in front of them rather than after
    // they have arranged a list around it. This is where the encrypted case goes: no
    // password box here, and the sentence names the file.
    QStringList refused;
    int added = 0;
    for (const QString &path : paths) {
        int pageCount = 0;
        QString error;
        QVector<DocumentEditor::ReplyLink> links;
        if (!DocumentEditor::inspectImport(path, &pageCount, &error, nullptr, &links)) {
            refused.append(error);
            continue;
        }
        // Kept for the row's own count of what merging it would cost (§47). Same pass, same file:
        // a second look would be a second chance for the file to answer differently.
        replyLinksByPath.insert(path, links);
        const int row = table->rowCount();
        table->insertRow(row);
        fillingTable = true;
        auto *file = new QTableWidgetItem(QFileInfo(path).fileName());
        file->setFlags(file->flags() & ~Qt::ItemIsEditable);
        file->setData(pathRole(), path);
        file->setData(pageCountRole(), pageCount);
        file->setToolTip(path);
        table->setItem(row, FileColumn, file);
        auto *count = new QTableWidgetItem(tr("%n 页", nullptr, pageCount));
        count->setFlags(count->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, CountColumn, count);
        // The whole file, written out rather than left blank: blank means "nothing said
        // yet" everywhere else in this program's edit dialogs, and a range the reader
        // can see is a range they can check.
        //
        // And it is pageSpan()'s answer, like the other four exits: a one-page donor used to
        // seed "1-1" here, which parses the same but reads like a file that cannot count --
        // found on the screen (docs/store-submission.md §31), not in the source survey.
        table->setItem(row, RangeColumn, new QTableWidgetItem(
                                           DocumentEditor::pageSpan(0, pageCount - 1)));
        fillingTable = false;
        selectRow(row);
        ++added;
    }
    if (added > 0)
        updateSummary();
    return refused;
}

void MergePagesDialog::removeSelected()
{
    const int row = currentRow();
    if (row < 0)
        return;
    table->removeRow(row);
    selectRow(qMin(row, table->rowCount() - 1));
    updateSummary();
}

void MergePagesDialog::moveSelected(int delta)
{
    const int row = currentRow();
    const int into = row + delta;
    if (row < 0 || into < 0 || into >= table->rowCount())
        return;
    // The three cells are swapped rather than the rows moved, because a row here is
    // only its cells: the path lives on the first one, and taking the row out and
    // putting it back elsewhere would have to recreate them anyway.
    fillingTable = true;
    for (int column = 0; column < table->columnCount(); ++column) {
        QTableWidgetItem *from = table->takeItem(row, column);
        QTableWidgetItem *to = table->takeItem(into, column);
        table->setItem(row, column, to);
        table->setItem(into, column, from);
    }
    fillingTable = false;
    selectRow(into);
    updateSummary();
}

void MergePagesDialog::handleRangeEdit()
{
    if (fillingTable)
        return;
    updateSummary();
}

int MergePagesDialog::currentRow() const
{
    const int row = table->currentRow();
    return row >= 0 && row < table->rowCount() ? row : -1;
}

int MergePagesDialog::rowCount() const
{
    return table->rowCount();
}

QString MergePagesDialog::rangeOfRow(int row) const
{
    QTableWidgetItem *item = table->item(row, RangeColumn);
    return item ? item->text().trimmed() : QString();
}

QVector<int> MergePagesDialog::pagesOfRow(int row) const
{
    const QString spec = rangeOfRow(row);
    if (spec.isEmpty())
        return {};
    // Bounded by the file's own page count, which is the numbering the reader was
    // looking at when they typed it. Out-of-range numbers are dropped by the same
    // parser 打印 uses, and a row left holding nothing is the summary's to say so.
    const int pageCount = table->item(row, FileColumn)
                              ? table->item(row, FileColumn)->data(pageCountRole()).toInt()
                              : 0;
    return Print::parsePageRange(spec, pageCount);
}

QVector<MergePagesDialog::Entry> MergePagesDialog::rows() const
{
    QVector<Entry> out;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *file = table->item(row, FileColumn);
        if (!file)
            continue;
        Entry entry;
        entry.path = file->data(pathRole()).toString();
        entry.pageCount = file->data(pageCountRole()).toInt();
        entry.pages = pagesOfRow(row);
        entry.replyLinks = replyLinksByPath.value(entry.path);
        out.append(entry);
    }
    return out;
}

QVector<MergePagesDialog::Entry> MergePagesDialog::mergesToRecord() const
{
    QVector<Entry> recorded = rows();
    // The correction the header promises. A merge lands beside the target the reader
    // named, not beside the block the row before it added -- so asking for three files
    // "behind page 12" in list order would write them 3, 2, 1, and the list would be a
    // picture of an order nothing kept. Recording them the other way round puts each one
    // in front of the ones already there, which is the list. 最后面 needs no reversal:
    // each block goes to the end of what is already there, so the list order is kept by
    // going down it.
    if (target() != DocumentEditor::MoveTarget::Back)
        std::reverse(recorded.begin(), recorded.end());
    return recorded;
}

void MergePagesDialog::updateSummary()
{
    using Move = DocumentEditor::MoveTarget;

    const int listRows = rowCount();
    const int anchor = anchorPage();
    const int splitting = editor ? editor->splitPieceSize() : 0;
    const bool behind = target() == Move::AfterPage;
    const bool anchorMissing = behind && anchor < 0;
    const bool anchorPastEnd = behind && anchor >= pageTotal;

    // The same entries the editor will be asked for, planned the same way a save will
    // plan them -- one row after another over the running list -- because the number
    // this has to print is where the batch lands in the file that gets written, and
    // nothing less than the real arithmetic says that.
    const QVector<Entry> recorded = mergesToRecord();
    QVector<DocumentEditor::PageRef> present = editor ? editor->pageOrder()
                                                      : QVector<DocumentEditor::PageRef>{};
    int added = 0;
    bool anchorWasGone = false;
    bool saidAnchor = false;
    int probe = 0;
    for (const Entry &entry : recorded) {
        const DocumentEditor::MergePlan plan = DocumentEditor::planMerge(
            present, entry.pages, probeMergeId(probe++), target(), anchor);
        present = plan.order;
        added += plan.pagesAdded;
        if (!saidAnchor) {
            anchorWasGone = plan.anchorWasGone;
            saidAnchor = true;
        }
    }
    // Where the batch starts in the document a save writes. The probe identities are
    // what makes this answerable: every other page in the list was already there, and
    // the ones this dialog is about to add are the negative-numbered ones it stamped.
    int landsAt = present.size();
    for (int index = 0; index < present.size(); ++index) {
        if (present.at(index).source < 0 && index < landsAt)
            landsAt = index;
    }

    QString line;
    // The first row that has not said which pages, named by its file. A blank cell is
    // "nothing said yet" and not "the whole file" -- the same guard the other edit
    // dialogs keep -- and a list of four files with one silent in it is a promise that
    // cannot be kept, so the button waits until every row is a real request.
    QString unheard;
    for (const Entry &entry : rows()) {
        if (entry.pages.isEmpty()) {
            unheard = tr("%1 还没有说出要合入哪几页。").arg(QFileInfo(entry.path).fileName());
            break;
        }
    }
    if (listRows == 0)
        line = tr("还没有加上要合并进来的文件。");
    else if (splitting > 0)
        // Said here rather than after the click, which is what 拆分文件 has done since
        // §16 and what §20 caught this dialog not doing: the model already refuses a
        // merge while a split is pending, so a lit button was a promise the box could not
        // keep. The sentence is DocumentEditor::mergeFile()'s own, byte for byte -- one
        // Chinese string is one English string, and a reader who met this refusal twice
        // must not meet it in two wordings (i18n_smoke pins that). It replaces the
        // arithmetic rather than sitting under it, because the page numbers the
        // simulation prints are where a batch would land in a file that will not be
        // written: a number with nothing to count.
        line = tr("这份文档已经说了要拆成每 %n 页一份。先撤销那一步，再把文件合并进来。",
                  nullptr, splitting);
    else if (!unheard.isEmpty())
        line = unheard;
    else if (anchorMissing)
        line = tr("要说合并到第几页之后。");
    else if (anchorPastEnd)
        line = tr("这份文档一共 %n 页，没有第 %1 页。", nullptr, pageTotal).arg(anchor + 1);
    else if (added == 0)
        line = tr("没有页可以合并进来。");
    else {
        // One page is not a range, and it is not "them" either. The wording splits on the
        // count for the same reason 拆分's status line splits on `fileCount` (see
        // mainwindow.cpp's two wordings): a number that changes the words around it has to
        // change them here too, or the box says 连着 and pages 40-40 about a single page.
        // The number itself is DocumentEditor::pageSpan()'s, which the split's file names
        // and 拆分文件's own list already read from.
        //
        // "1 页" is written into the singular sentence rather than substituted, and the file
        // count next to it stays a `%1`: every row of the list contributes at least one page
        // (a row with nothing in its range is answered by 还没有说出哪几页 above), so one page
        // in total means exactly one file -- which is what lets the English say "file" and
        // "page" both in the singular without a second branch.
        const QString span = DocumentEditor::pageSpan(landsAt, landsAt + added - 1);
        line = added == 1
                   ? tr("%1 个文件一共取 1 页，合到%2，按列表的顺序排在第 %3 页。")
                         .arg(recorded.size())
                         .arg(targetName())
                         .arg(span)
                   : tr("%n 个文件一共取 %1 页，合到%2，按列表的顺序连着排在第 %3 页。",
                        nullptr, recorded.size())
                         .arg(added)
                         .arg(targetName())
                         .arg(span);
        if (anchorWasGone) {
            // The same sentence 移动页面 has for the same case: the page named as the
            // anchor is one an unsaved change took out, and the block goes into the gap
            // it left rather than nowhere.
            line += tr("\n\n第 %1 页在还没保存的修改里已经删掉了，所以是排在它后面那页前面。")
                        .arg(anchor + 1);
        }
        if (editor && editor->importedPageCount() > 0) {
            line += tr("\n\n这份文档里已经有 %n 页是合并进来的了。", nullptr, editor->importedPageCount());
        }
        // Said here rather than discovered in 删除页面: an imported page has no number
        // on the screen, so the only dialogs that can name one are this one and 移动
        // 页面, and a reader who wants one gone needs to know that before the merge,
        // not after.
        line += tr("\n\n合并进来的页没有屏幕上的页码：要改它们或把它们取回去，只能撤销"
                   "那次合并。");
        // §47, counted against the rows as they stand right now rather than against a merge this
        // dialog already made. A page copy carries the annotations printed on it and not the
        // objects they point at, so every reply among the pages coming in lands as a note of its
        // own -- including one that answered a note on the very page beside it (§78 measured two
        // copies of exactly that shape, both with no thread left).
        int severed = 0;
        for (const Entry &entry : rows())
            severed += DocumentEditor::repliesAtRiskForImport(entry.replyLinks, entry.pages);
        if (severed > 0) {
            line += tr("\n\n这些页上有 %n 条是对另一条批注的回答：合并只把页上的批注带进来，带不"
                       "进回答与被回答之间的联系，保存出来成为独立的批注（写的字都还在）。",
                       nullptr, severed);
        }
        // The form's second copy, and the reason a merge is one of the operations that has to drop
        // it: the packet describes the pages of the document it was written for, and this save
        // writes a document with more of them than that (§55).
        if (editor && editor->hasXfa()) {
            line += tr("\n\n这份文档还带着一份 XFA 表单：合进过页面的文件里不会再有它，别的阅读器会"
                       "改用表单字段那一份。");
        }
    }
    // Same sentence the other three edit dialogs end with, for the same reason.
    summaryLabel->setText(tr("%1\n\n原来那份文件不会改动。这一步只是记下修改，"
                             "要它成为一个文件，还得走保存。")
                              .arg(line));

    // A dialog picks its size once, when it first appears, and this summary is not the
    // same height then as it is after the files are in: the sentence that opens with is
    // two lines, the accepted one is five. Qt does not grow the window for that -- it
    // squeezed the list past its own minimum height instead, and in English with three
    // files the third row ended up underneath 添加文件…/移除/上移/下移.
    // The layout's own minimum is not enough to go by either: for a word-wrapped label
    // it counts the label's minimumSizeHint -- three lines of whatever fits -- rather
    // than the height this sentence needs at this width, so it has to be added back.
    // Only ever taller: a window the reader dragged is theirs to keep, and removing
    // files shrinks the sentence but not the dialog.
    const int needed = layout()->totalMinimumSize().height()
                       + summaryLabel->sizeHint().height()
                       - summaryLabel->minimumSizeHint().height();
    if (height() < needed)
        resize(width(), needed);

    okayButton->setEnabled(editor && listRows > 0 && splitting == 0 && unheard.isEmpty()
                           && !anchorMissing && !anchorPastEnd && added > 0);
}

QString MergePagesDialog::targetName() const
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

DocumentEditor::MoveTarget MergePagesDialog::target() const
{
    if (frontChoice->isChecked())
        return DocumentEditor::MoveTarget::Front;
    if (behindChoice->isChecked())
        return DocumentEditor::MoveTarget::AfterPage;
    return DocumentEditor::MoveTarget::Back;
}

int MergePagesDialog::anchorPage() const
{
    if (target() != DocumentEditor::MoveTarget::AfterPage)
        return -1;
    return anchorFromBox();
}

int MergePagesDialog::anchorFromBox() const
{
    bool typed = false;
    const int number = anchorEdit->text().trimmed().toInt(&typed);
    if (!typed || number < 1)
        return -1;
    // Past the end is passed back as typed rather than clamped, so the sentence above
    // the buttons says the number the reader wrote.
    return number - 1;
}

void MergePagesDialog::selectRow(int row)
{
    if (row < 0 || row >= table->rowCount())
        return;
    table->setCurrentCell(row, RangeColumn);
}
