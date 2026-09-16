// Folding two positions into the ranges a selection covers.
//
// This is where the cases are: dragging backwards, dragging across a page
// boundary, a page in the middle with no text at all, a click that selects
// nothing, and a range that outlived the text it came from. Every one of them
// is arithmetic -- which is why it is a file of its own, tested with page
// lengths from a table rather than from a PDF.

#include "../src/document/textlayer.h"

#include <QString>
#include <QVector>

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

// Page lengths, so that the folding can be checked without a document.
std::function<int(int)> lengths(const QVector<int> &pages)
{
    return [pages](int page) {
        return page >= 0 && page < pages.size() ? pages.at(page) : 0;
    };
}

QString describe(const QVector<PageTextRange> &ranges)
{
    QStringList parts;
    for (const PageTextRange &range : ranges)
        parts.append(QStringLiteral("p%1[%2,%3)").arg(range.page).arg(range.from).arg(range.to));
    return parts.isEmpty() ? QStringLiteral("(nothing)") : parts.join(QStringLiteral(" "));
}

void checkRanges(const QVector<PageTextRange> &actual, const QVector<PageTextRange> &expected,
                 const QString &what)
{
    if (actual == expected)
        return;
    std::cerr << "FAIL: " << what.toStdString() << "\n      expected "
              << describe(expected).toStdString() << "\n      got      "
              << describe(actual).toStdString() << '\n';
    ++failures;
}

} // namespace

int main()
{
    const auto ten = lengths({10, 10, 10});

    // ---- one page ----
    {
        checkRanges(selectionRanges(0, 3, 0, 8, ten), {{0, 3, 8}}, "a range on one page");
        // Backwards is the same selection. Getting this wrong is not a crash: it
        // is a selection that disappears when the pointer moves up, which reads
        // as "it works sometimes".
        checkRanges(selectionRanges(0, 8, 0, 3, ten), {{0, 3, 8}},
                    "dragging backwards selects the same text");
        // A click: the same position twice.
        check(selectionRanges(0, 5, 0, 5, ten).isEmpty(), "a click selects nothing");
        check(isEmptySelection(0, 5, 0, 5), "and says so");
        check(!isEmptySelection(0, 5, 0, 6), "while a drag of one character does not");
    }

    // ---- what is not there ----
    {
        checkRanges(selectionRanges(0, -5, 0, 4, ten), {{0, 0, 4}}, "a negative index is the start");
        checkRanges(selectionRanges(0, 3, 0, 999, ten), {{0, 3, 10}},
                    "an index past the end is the end");
        check(selectionRanges(-1, 0, 0, 3, ten).isEmpty(), "no page at all selects nothing");
        check(selectionRanges(0, 0, 0, 3, lengths({0})).isEmpty(),
              "and a page with no text selects nothing");
    }

    // ---- two adjacent pages ----
    {
        // The end of the first page to the beginning of the second: no text in
        // between, so nothing is selected even though two pages are named.
        checkRanges(selectionRanges(0, 10, 1, 0, ten), {},
                    "the end of one page to the start of the next is empty");
        checkRanges(selectionRanges(0, 7, 1, 3, ten), {{0, 7, 10}, {1, 0, 3}},
                    "part of one page and part of the next");
        checkRanges(selectionRanges(1, 3, 0, 7, ten), {{0, 7, 10}, {1, 0, 3}},
                    "and the same dragged the other way");
    }

    // ---- across pages, with pages in between ----
    {
        checkRanges(selectionRanges(0, 8, 3, 4, lengths({10, 5, 7, 9})),
                    {{0, 8, 10}, {1, 0, 5}, {2, 0, 7}, {3, 0, 4}},
                    "every page in between is selected whole");
        // A page in the middle with no text at all is skipped rather than
        // selected as an empty range: there is nothing to draw or to copy.
        checkRanges(selectionRanges(0, 8, 3, 4, lengths({10, 0, 7, 9})),
                    {{0, 8, 10}, {2, 0, 7}, {3, 0, 4}},
                    "a page with no text is left out");
        // A last page with no text: the selection ends at the page before it.
        checkRanges(selectionRanges(0, 8, 2, 0, lengths({10, 5, 0})), {{0, 8, 10}, {1, 0, 5}},
                    "and so is a last page with nothing on it");
    }

    // ---- the text ----
    {
        const auto textOf = [](int page) {
            switch (page) {
            case 0:
                return QStringLiteral("first page");
            case 1:
                return QStringLiteral("second");
            default:
                return QString();
            }
        };
        check(selectionText({{0, 0, 5}}, textOf) == QStringLiteral("first"),
              "a range on one page is that part of its text");
        const QString across = selectionText({{0, 6, 10}, {1, 0, 6}}, textOf);
        check(across == QStringLiteral("page\nsecond"),
              QStringLiteral("pages are joined with a newline, not run together (got \"%1\")")
                  .arg(across));
        check(!across.endsWith(QLatin1Char('\n')), "and nothing is added at the end");
        // A range that outlived the text it came from: clamped rather than
        // taking a substring past the end, which is undefined in the best case
        // and a crash in the worst.
        check(selectionText({{0, 5, 500}}, textOf) == QStringLiteral(" page"),
              "a range past the end of the text stops at the end");
        check(selectionText({{0, 500, 600}}, textOf).isEmpty(),
              "and a range entirely past the end is nothing");
        check(selectionText({}, textOf).isEmpty(), "no ranges, no text");
    }

    if (failures == 0) {
        std::cout << "the selection folding holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
