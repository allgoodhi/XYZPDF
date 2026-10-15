#include "bookmarkdialog.h"

#include "../core/windowdialogs.h"
#include "../edit/documenteditor.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// The three columns, in the order the model's flat list holds them. Named rather than counted at the
// call sites, because the summary has to say which column a cell is in, and a message pointing at the
// wrong one is worse than no message at all.
enum Column
{
    TitleColumn = 0,
    LevelColumn = 1,
    PageColumn = 2,
    ColumnCount = 3
};

QString cellText(QTableWidget *box, int row, int column)
{
    const QTableWidgetItem *cell = box->item(row, column);
    return cell ? cell->text().trimmed() : QString();
}

// The 层级 column is the cell's own text, and that is a measured choice rather than the simpler one:
// §72 read this box off the real window and the spin box the first version put *inside* the cell was
// absent from the UIA tree entirely -- the column showed a number on screen while a screen reader got
// an empty cell. So the number lives in the item now, which is also what the reader edits.
//
// What the spin's range used to make untypeable is refused before the press instead: a blank, a word,
// a 0 and a negative all come back as 0 here, and firstProblem() greys 确定 for such a row. Nothing is
// clamped, for §69's reason -- a level that quietly moved a bookmark would be a different tree from
// the one the reader arranged.
int levelOf(QTableWidget *box, int row)
{
    bool parsed = false;
    const int typed = cellText(box, row, LevelColumn).toInt(&parsed);
    return parsed ? typed : 0;
}

// The page cell as a 0-based position: -1 for a blank cell, which is a bookmark that names no page.
//
// Anything that does not parse also comes back -1, and that is safe only because firstProblem() greys
// 确定 for such a row: an unparseable page must be fixed, never quietly rewritten as "this bookmark
// points nowhere". The two cases are told apart by the cell's own text, not by this number.
int pageOf(QTableWidget *box, int row)
{
    const QString text = cellText(box, row, PageColumn);
    if (text.isEmpty())
        return -1;
    bool parsed = false;
    const int typed = text.toInt(&parsed);
    if (!parsed || typed < 1)
        return -1;
    // 1-based in the cell: this is the number a reader counts on the screen (§70), and every other
    // page number in this program says itself that way.
    return typed - 1;
}

// The first row that cannot be written, and which of the four things about it is wrong.
//
// The row comes before the kind on purpose: a reader scans the table top to bottom, so a message that
// reported the *kind* first would name row 7's page while row 2 is the thing they just typed, and the
// box would look like it was answering a different question than the one on screen. Order inside a row
// is name, then level, then page-not-a-number, then page-past-the-end -- the left-to-right order of
// the columns.
struct Problem
{
    enum Kind
    {
        None,
        Name,
        Level,
        NotNumber,
        Beyond
    };
    int row = -1;
    Kind kind = None;
};

Problem firstProblem(QTableWidget *box, const QVector<OutlineRow> &rows, int pageTotal)
{
    for (int index = 0; index < rows.size(); ++index) {
        if (rows.at(index).title.isEmpty())
            return {index, Problem::Name};
        // Asked of the list rather than of the cell text, so the box and the model answer with the same
        // number: the level became free text in §74, and a column that can hold anything needs its
        // bound stated where the reader can still see it -- not enforced by a control they cannot read.
        if (rows.at(index).level < 1)
            return {index, Problem::Level};
        const QString text = cellText(box, index, PageColumn);
        if (!text.isEmpty()) {
            bool parsed = false;
            const int typed = text.toInt(&parsed);
            if (!parsed || typed < 1)
                return {index, Problem::NotNumber};
            if (typed > pageTotal)
                return {index, Problem::Beyond};
        }
    }
    return {-1, Problem::None};
}

// Is the table still showing exactly what the box opened with? Compared on the three columns and
// nothing else: `location` has no column here, and our reader never fills it in (qtpdfbackend.cpp
// reads the Title, Page and Level roles and stops), so a comparison that included it would report
// "you changed something" on a box nobody touched, the moment a read ever does carry a position.
// What that leaves unsaid -- a bookmark aiming at a spot halfway down a page comes back out of this
// box aimed at the page's top -- is recorded in docs/store-submission.md §71 as an open consequence,
// not as a case this function pretends not to see.
bool unchangedFrom(const QVector<OutlineRow> &asked, const QVector<OutlineRow> &opening)
{
    if (asked.size() != opening.size())
        return false;
    for (int index = 0; index < asked.size(); ++index) {
        if (asked.at(index).title != opening.at(index).title
            || asked.at(index).level != opening.at(index).level
            || asked.at(index).page != opening.at(index).page)
            return false;
    }
    return true;
}

} // namespace

BookmarkDialog::BookmarkDialog(QWidget *parent, const DocumentEditor *document,
                               const Outline &documentBookmarks)
    : QDialog(parent)
    , pageTotal(document ? document->sourcePageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("编辑书签"));
    WindowDialogs::makeWindowModal(this, parent);
    // Wide, and wider than the other boxes here for one measured reason: the first version at
    // 620x480 cut its own third column off the edge of the window -- the header read "Page (empty =
    // no page" with the cells half outside, seen in the PrintWindow picture of §72, which is the only
    // way this harness notices that a layout does not fit. Nothing in a count can see that.
    resize(800, 480);

    auto *layout = new QVBoxLayout(this);

    table = new QTableWidget(this);
    table->setColumnCount(ColumnCount);
    table->setHorizontalHeaderLabels({tr("标题"), tr("层级"), tr("页码（留空 = 不落页）")});
    // The two numeric columns get their width from what they hold -- a level is one or two digits, a
    // page number is a few digits -- and everything left over goes to the names, which is the only
    // column a reader is actually reading. Stretching both ends, as the first version did, pushed the
    // last header off the right edge.
    table->horizontalHeader()->setSectionResizeMode(TitleColumn, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(LevelColumn, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(PageColumn, QHeaderView::Fixed);
    table->setColumnWidth(LevelColumn, 90);
    table->setColumnWidth(PageColumn, 180);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                           | QAbstractItemView::EditKeyPressed);
    layout->addWidget(table, 1);

    auto *buttons = new QHBoxLayout;
    addButton = new QPushButton(tr("加一条"), table);
    removeButton = new QPushButton(tr("删一条"), table);
    // Said on the button rather than discovered afterwards: a bookmark with children has nowhere for
    // them to go on their own, and a box that left them behind would be quietly re-nesting them under
    // whatever row precedes them -- which is exactly how outlineFromRows reads a level that jumps.
    removeButton->setToolTip(tr("删掉选中的那一条，以及挂在它下面的所有书签"));
    upButton = new QPushButton(tr("上移"), table);
    downButton = new QPushButton(tr("下移"), table);
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    buttons->addWidget(upButton);
    buttons->addWidget(downButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = box->button(QDialogButtonBox::Ok);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);

    // Opening on the tree already pending, when there is one, is the rule the other six boxes follow:
    // a second request is the reader correcting the first, so pressing 确定 again has to read as
    // changing the list rather than as a second one to undo twice.
    opening = editor && editor->outlineAsked() ? editor->outlineToEdit()
                                               : outlineRowsOf(documentBookmarks);

    connect(addButton, &QPushButton::clicked, this, [this] { addRow(); });
    connect(removeButton, &QPushButton::clicked, this, [this] { removeRow(); });
    connect(upButton, &QPushButton::clicked, this, [this] { moveRow(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { moveRow(1); });

    // The table's own signals are wired once, here: rebuild() replaces cells and rows, not the table,
    // so connecting them there would add a second and third route into updateSummary() on every
    // edit -- which is how a box ends up running its own arithmetic against a stale list.
    connect(table, &QTableWidget::itemChanged, this, [this] { updateSummary(); });
    connect(table, &QTableWidget::currentCellChanged, this, [this] { updateSummary(); });

    rebuild(opening, opening.isEmpty() ? -1 : 0);
}

QVector<OutlineRow> BookmarkDialog::rows() const
{
    QVector<OutlineRow> asked;
    asked.reserve(table->rowCount());
    for (int index = 0; index < table->rowCount(); ++index) {
        OutlineRow row;
        row.title = cellText(table, index, TitleColumn);
        row.level = levelOf(table, index);
        row.page = pageOf(table, index);
        asked.append(row);
    }
    return asked;
}

void BookmarkDialog::rebuild(const QVector<OutlineRow> &rows, int select)
{
    // The whole table is rebuilt from the list rather than patched, for the same reason the writer
    // rebuilds the tree: the row count, the per-row cells and the selection all have to agree with one
    // list, and a patch that misses one of them leaves the box showing something the reader did not
    // ask for. Signals are blocked while the cells go in so the summary does not run once per column
    // per row against a half-filled table.
    QSignalBlocker tableGuard(table);
    table->clearContents();
    table->setRowCount(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        const OutlineRow &row = rows.at(index);
        table->setItem(index, TitleColumn, new QTableWidgetItem(row.title));

        auto *level = new QTableWidgetItem(QString::number(row.level));
        level->setTextAlignment(Qt::AlignCenter);
        table->setItem(index, LevelColumn, level);

        auto *page = new QTableWidgetItem(row.page < 0 ? QString() : QString::number(row.page + 1));
        page->setTextAlignment(Qt::AlignCenter);
        table->setItem(index, PageColumn, page);
    }
    tableGuard.unblock();

    if (select >= 0 && select < rows.size())
        table->setCurrentCell(select, TitleColumn);

    updateSummary();
}

void BookmarkDialog::addRow()
{
    QVector<OutlineRow> asked = rows();
    const int at = selectedRow();

    OutlineRow added;
    added.title = tr("新书签");
    int where = asked.size();
    if (at < 0) {
        added.level = 1;
    } else {
        // Beside the row the reader is on, not under it: a new bookmark is a sibling of what was
        // selected, and one that silently became its child would be a tree nobody arranged. So it goes
        // in after the selected row's whole subtree -- which is the one job outlineSubtreeEnd() has.
        added.level = asked.at(at).level;
        where = outlineSubtreeEnd(asked, at);
    }
    asked.insert(where, added);
    rebuild(asked, where);
}

void BookmarkDialog::removeRow()
{
    const int at = selectedRow();
    if (at < 0)
        return;
    QVector<OutlineRow> asked = rows();
    // The row and everything hanging under it, for the reason removeButton's tooltip gives.
    const int end = outlineSubtreeEnd(asked, at);
    for (int index = end - 1; index >= at; --index)
        asked.removeAt(index);
    rebuild(asked, qMin(at, asked.size() - 1));
}

void BookmarkDialog::moveRow(int direction)
{
    const int at = selectedRow();
    if (at < 0)
        return;
    QVector<OutlineRow> asked = rows();
    const int end = outlineSubtreeEnd(asked, at);
    const int level = asked.at(at).level;

    if (direction < 0) {
        // The neighbour above is a sibling only if it sits at the same level. A shallower row is this
        // block's parent, and moving up past a parent would take the children out of the tree they are
        // in -- so 上移 is off for a first child, not silently wrong. (updateSummary() greys it the
        // same way; this is the backstop for a click that raced the greying.)
        const int before = at - 1;
        if (before < 0 || asked.at(before).level != level)
            return;
        QVector<OutlineRow> moved;
        moved.reserve(asked.size());
        for (int index = 0; index < before; ++index)
            moved.append(asked.at(index));
        for (int index = at; index < end; ++index)
            moved.append(asked.at(index));
        for (int index = before; index < at; ++index)
            moved.append(asked.at(index));
        for (int index = end; index < asked.size(); ++index)
            moved.append(asked.at(index));
        rebuild(moved, before);
        return;
    }

    const int after = end;
    if (after >= asked.size() || asked.at(after).level != level)
        return;
    const int afterEnd = outlineSubtreeEnd(asked, after);
    QVector<OutlineRow> moved;
    moved.reserve(asked.size());
    for (int index = 0; index < at; ++index)
        moved.append(asked.at(index));
    for (int index = after; index < afterEnd; ++index)
        moved.append(asked.at(index));
    for (int index = at; index < end; ++index)
        moved.append(asked.at(index));
    for (int index = afterEnd; index < asked.size(); ++index)
        moved.append(asked.at(index));
    // Selected stays with the block that moved, which is now after the sibling it swapped with.
    rebuild(moved, at + (afterEnd - after));
}

int BookmarkDialog::selectedRow() const
{
    const int at = table->currentRow();
    return at >= 0 && at < table->rowCount() ? at : -1;
}

void BookmarkDialog::updateSummary()
{
    const QVector<OutlineRow> asked = rows();
    const Problem problem = firstProblem(table, asked, pageTotal);

    QString line;
    bool okay = false;
    if (problem.kind != Problem::None) {
        const int shown = problem.row + 1;
        if (problem.kind == Problem::Name)
            line = tr("第 %1 行还没有名字。").arg(shown);
        else if (problem.kind == Problem::Level)
            line = tr("第 %1 行的层级要写成 1 或更大的数字：1 是顶层，往下每深一层加 1。").arg(shown);
        else if (problem.kind == Problem::NotNumber)
            line = tr("第 %1 行的页码不是数字。要一条不指向任何一页的书签，把那一格留空。")
                       .arg(shown);
        else
            line = tr("第 %1 行指的页不在这份文件里（它打开时是 %2 页）。")
                       .arg(shown)
                       .arg(pageTotal);
    } else if (unchangedFrom(asked, opening)) {
        // Refused before the press rather than after. The model would accept this -- it only refuses an
        // identical *pending* request (§69) -- and the reader would get a dirty tab plus an undo row
        // that puts the file back exactly as it was. Nothing but this box can see the document's own
        // tree, so this is the one place the comparison can be made.
        line = opening.isEmpty()
                   ? tr("这份文档本来就没有书签，你现在也没要任何一条 —— 按下不会多改什么。")
                   : tr("这跟这份文档现在的书签一模一样，按下不会多改什么 —— 要改就改一行的名字、"
                        "层级或页码。");
    } else {
        okay = true;
        line = asked.isEmpty() ? tr("一条书签都不留：按下去，这份文档就没有书签了。")
                               : tr("按下去这份文档会有 %n 条书签。", nullptr, asked.size());

        // Counted here rather than in rows() because it is a fact about the list as arranged.
        int headings = 0;
        int gone = 0;
        for (const OutlineRow &row : asked) {
            if (row.page < 0)
                ++headings;
            else if (editor && !editor->pageStillExists(row.page))
                ++gone;
        }
        if (headings > 0)
            line += tr("\n\n其中 %n 条不指向任何一页。", nullptr, headings);

        // §70's owed sentence. A page an earlier 删页 took out is not an error, and the model's answer
        // (it stays in the tree, without a target) is the same one it gives for a page lost after the
        // ask -- so the box says it and lets the press through. The reader has to hear it *before*.
        //
        // Asked row by row, through pageStillExists(), and not with pagesStillThere(): that one
        // answers "how many *pages* of these still exist" and de-duplicates, while a bookmark list is
        // one row per bookmark with many rows sharing a page. Subtracting a distinct-page count from a
        // row count told a reader looking at a real book that 77 of their 184 bookmarks pointed at
        // deleted pages (docs/store-submission.md §72 -- read off the screen, after every headless
        // check had stayed green on fixtures that put exactly one bookmark on each page).
        if (gone > 0) {
            line += tr("\n\n有 %n 条指的页在前面的修改里已经删掉了：它们会留在树里，但不指向这一份的"
                       "任何一页。",
                       nullptr, gone);
        }

        // §75's third route used to be the only one available, and §80 said it out loud: the tree was
        // read as titles and page numbers and written back as /Fit, so *every* positional bookmark
        // lost its place on the page. §99 took route (a) instead -- the destination is read off the
        // file at open and carried by the rows that are still the rows they started as -- so the
        // sentence now has to be about the ones that no longer match: a rename, an insert at an
        // earlier slot, or a re-nest shifts the join, and those bookmarks come back at page-top
        // while the untouched ones keep their place. Counted off the request being asked, not off the
        // file, because after §99 the two numbers answer different questions and only one of them is
        // something the reader is about to cause.
        //
        // Not said when the number is 0, for the same reason §77 gives for the form sentence: a
        // warning about a loss that cannot happen here teaches the reader to ignore the ones that can.
        const int atRisk = editor ? editor->positionalAtRiskFor(asked) : 0;
        if (atRisk > 0) {
            line += tr("\n\n其中 %n 条书签指着页面内的某个位置，而这一次它们对不上文件里原来的条目："
                       "那几条写出去会变成整页适配。没被改动的书签，位置照旧带走。",
                       nullptr, atRisk);
        }

        if (editor && editor->outlineAsked()) {
            line += tr("\n\n这会把前面记下的那一条（%1 条）换掉；撤销一次回到那一个。")
                        .arg(editor->outlineToEdit().size());
        }
    }

    // Same ending as the other six boxes, for the same reason: nothing on this screen has written a
    // file, and a bookmark a reader cannot see until after the save is exactly what needs saying twice.
    line += tr("\n\n原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。");

    summaryLabel->setText(line);
    okayButton->setText(asked.isEmpty() ? tr("去掉全部书签")
                                       : tr("写这 %n 条书签", nullptr, asked.size()));
    okayButton->setEnabled(okay);

    // Which way a row can go is not arithmetic the box keeps to itself: it is the same "is there a
    // sibling on this side" question the summary would have to answer, so it is asked of the list once
    // here and the buttons say what cannot happen instead of doing nothing when clicked.
    const int at = selectedRow();
    removeButton->setEnabled(at >= 0);
    addButton->setEnabled(true);
    if (at < 0) {
        upButton->setEnabled(false);
        downButton->setEnabled(false);
        return;
    }
    const int level = asked.at(at).level;
    const int end = outlineSubtreeEnd(asked, at);
    upButton->setEnabled(at > 0 && asked.at(at - 1).level == level);
    downButton->setEnabled(end < asked.size() && asked.at(end).level == level);
}
