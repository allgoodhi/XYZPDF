// The bookmark tree, built from a flat list with levels.
//
// No PDF anywhere near it: what is being tested is the rule for turning
// nesting depth into parents, and that rule is where the mistakes live. A first
// row at level 3, a level that drops by two, a bookmark that names no page --
// all of those are in real documents, and all of them are cheap to check here.

#include "../src/document/outline.h"

#include <QString>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

OutlineRow row(const QString &title, int level, int page)
{
    OutlineRow entry;
    entry.title = title;
    entry.level = level;
    entry.page = page;
    return entry;
}

// Two lists the way this file means them: same rows in the same order, with the same three numbers.
// `location` is deliberately not compared -- nothing here puts a value in it, and the box that reads
// a tree back out (§71) has a column for each of the three and none for the fourth.
bool sameRows(const QVector<OutlineRow> &left, const QVector<OutlineRow> &right)
{
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.size(); ++index) {
        if (left.at(index).title != right.at(index).title
            || left.at(index).level != right.at(index).level
            || left.at(index).page != right.at(index).page)
            return false;
    }
    return true;
}

} // namespace

int main()
{
    // An empty document has an empty tree, and asking for anything out of it
    // says so rather than returning a node that is not there.
    {
        const Outline tree = outlineFromRows({});
        check(tree.isEmpty(), "no rows means no tree");
        check(tree.count() == 0, "an empty tree counts zero nodes");
        check(tree.nodeAt(0) == nullptr, "an empty tree has no node 0");
    }

    // The ordinary case: chapter, sections, one subsection.
    {
        const Outline tree = outlineFromRows({
            row(QStringLiteral("第一章"), 1, 0),
            row(QStringLiteral("1.1"), 2, 1),
            row(QStringLiteral("1.2"), 2, 2),
            row(QStringLiteral("1.2.1"), 3, 3),
            row(QStringLiteral("第二章"), 1, 4),
        });
        check(tree.nodes.size() == 2, "two chapters at the top");
        check(tree.count() == 5, "every node at every depth is counted");
        check(tree.nodes.at(0).children.size() == 2, "the first chapter has two sections");
        check(tree.nodes.at(0).children.at(1).children.size() == 1,
              "the second section has one subsection");
        check(tree.nodes.at(1).children.isEmpty(), "the second chapter has none");

        const OutlineNode *flat = tree.nodeAt(3);
        check(flat != nullptr && flat->title == QStringLiteral("1.2.1"),
              "depth-first order, as a sidebar draws it");
        check(tree.nodeAt(8) == nullptr, "one past the end is nothing");
    }

    // A first row deeper than the root: it becomes a root rather than being
    // dropped, because it is a real bookmark.
    {
        const Outline tree = outlineFromRows({row(QStringLiteral("深处"), 4, 7)});
        check(tree.nodes.size() == 1, "a first row at level 4 is still a root");
        check(tree.nodes.at(0).page == 7, "and keeps its page");
    }

    // A level that jumps down by two, then back up. The jump attaches one step
    // under the deepest row so far.
    {
        const Outline tree = outlineFromRows({
            row(QStringLiteral("a"), 1, 0),
            row(QStringLiteral("b"), 3, 1),
            row(QStringLiteral("c"), 2, 2),
        });
        check(tree.nodes.size() == 1, "all three hang off one root");
        check(tree.nodes.at(0).children.size() == 2, "b and c are siblings under a");
        check(tree.nodes.at(0).children.at(1).title == QStringLiteral("c"),
              "c came back up a level and landed beside b");
    }

    // A bookmark that names no page. It has to keep -1: turning it into page 0
    // would open the first page when the user clicked a heading that is not
    // a jump at all.
    {
        const Outline tree = outlineFromRows({row(QStringLiteral("无目标"), 1, -1)});
        check(tree.nodes.at(0).page == -1, "a bookmark without a page keeps -1");
    }

    // ---- reading the list back out of a tree (§71: the box that edits bookmarks) ----
    //
    // outlineFromRows only went in. This is the other direction, and the two checks that matter are
    // that the depth-first order and the 1-based levels come out the way a sidebar draws them, and
    // that going out and back lands on the same tree -- because the box hands the model a list it
    // read off the file's own tree, and a round trip that lost nesting would be a silent rewrite of
    // somebody's table of contents.
    {
        const QVector<OutlineRow> in = {
            row(QStringLiteral("第一章"), 1, 0),
            row(QStringLiteral("1.1"), 2, 3),
            row(QStringLiteral("1.1.1"), 3, 4),
            row(QStringLiteral("1.2"), 2, 6),
            row(QStringLiteral("第二章"), 1, 10),
            row(QStringLiteral("没有页"), 2, -1),
        };
        const Outline tree = outlineFromRows(in);
        const QVector<OutlineRow> out = outlineRowsOf(tree);

        check(out.size() == in.size(),
              QStringLiteral("every bookmark survives the round trip out (%1 rows came back)")
                  .arg(out.size()));
        check(sameRows(out, in),
              QStringLiteral("with the same titles, levels and pages, in the order a sidebar draws "
                             "them (%1 rows came back)")
                  .arg(out.size()));

        // The list read back is the list the writer takes, so assembling it again has to hand over
        // the same tree. This is the check that would go red if `level` were counted from 0 on one
        // side and from 1 on the other -- §64's bug, wearing a different hat.
        const Outline again = outlineFromRows(out);
        check(again.nodes.size() == tree.nodes.size() && again.count() == tree.count(),
              QStringLiteral("and building the tree from that list gives the same shape: %1 roots, "
                             "%2 nodes")
                  .arg(again.nodes.size())
                  .arg(again.count()));
        check(!again.nodes.isEmpty() && again.nodes.at(0).children.size() == 2
                  && !again.nodes.at(0).children.isEmpty()
                  && again.nodes.at(0).children.at(0).children.size() == 1,
              "two levels down it is still a child of the same parent, not a root");
        check(again.count() == 6, "and nothing was quietly dropped or duplicated in the middle");
    }

    // A tree that was never a legal list: outlineFromRows normalizes a level that jumps, so the list
    // read back is the normalized one. Said out loud because a caller that compared what it read
    // against what it typed would find a difference the file itself never had.
    {
        const Outline tree = outlineFromRows(
            {row(QStringLiteral("a"), 1, 0), row(QStringLiteral("b"), 3, 1)});
        const QVector<OutlineRow> out = outlineRowsOf(tree);
        check(out.size() == 2 && out.at(1).level == 2,
              QStringLiteral("a first jump of two levels comes back as the level it was placed at: "
                             "%1")
                  .arg(out.at(1).level));
    }

    // ---- how far one row's block reaches ----
    //
    // The 上移/下移 buttons move a bookmark *and everything under it*, so "which rows are that" is
    // asked in one place and the box only acts on the answer. A leaf is the case that has to be
    // boring: the block is one row long.
    {
        const QVector<OutlineRow> rows = {
            row(QStringLiteral("A"), 1, 0),
            row(QStringLiteral("A1"), 2, 1),
            row(QStringLiteral("A1a"), 3, 2),
            row(QStringLiteral("A2"), 2, 3),
            row(QStringLiteral("B"), 1, 4),
        };
        check(outlineSubtreeEnd(rows, 0) == 4, "A's block is itself plus its three descendants");
        check(outlineSubtreeEnd(rows, 1) == 3, "A1's is itself and A1a");
        check(outlineSubtreeEnd(rows, 2) == 3, "a leaf's block is one row long");
        check(outlineSubtreeEnd(rows, 3) == 4, "A2 ends where B begins");
        check(outlineSubtreeEnd(rows, 4) == 5, "and the last row ends at the end of the list");
        check(outlineSubtreeEnd(rows, -1) == -1 && outlineSubtreeEnd(rows, 5) == 5,
              "out of range answers with the index it was given rather than a block of nothing");
    }

    if (failures == 0) {
        std::cout << "the bookmark tree holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
