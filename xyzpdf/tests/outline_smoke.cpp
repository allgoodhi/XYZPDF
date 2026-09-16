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

    if (failures == 0) {
        std::cout << "the bookmark tree holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
