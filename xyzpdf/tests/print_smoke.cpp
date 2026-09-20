// The arithmetic of a print job, with no printer and no document.
//
// "Which pages does the range the reader typed name?" and "how does an n-up
// count become a grid?" are the two places a print job can be quietly wrong --
// an off-by-one sends every page one too early, a bad span parse prints the
// wrong pages -- and both are pure, so both are pinned here. The painting, which
// needs real hardware, is left to be checked by hand.

#include "../src/print/printjob.h"

#include <QPair>
#include <QString>
#include <QVector>

#include <iostream>

namespace {

int failures = 0;

QString describe(const QVector<int> &pages)
{
    QStringList parts;
    for (int page : pages)
        parts.append(QString::number(page));
    return parts.isEmpty() ? QStringLiteral("(none)") : parts.join(QLatin1Char(','));
}

void checkPages(const QVector<int> &actual, const QVector<int> &expected, const QString &what)
{
    if (actual == expected)
        return;
    std::cerr << "FAIL: " << what.toStdString() << "\n      expected "
              << describe(expected).toStdString() << "\n      got      "
              << describe(actual).toStdString() << '\n';
    ++failures;
}

void checkGrid(int nUp, int columns, int rows, const QString &what)
{
    const QPair<int, int> grid = Print::nUpGrid(nUp);
    if (grid.first == columns && grid.second == rows)
        return;
    std::cerr << "FAIL: " << what.toStdString() << "\n      expected " << columns << 'x' << rows
              << "\n      got      " << grid.first << 'x' << grid.second << '\n';
    ++failures;
}

} // namespace

int main(int, char **)
{
    using Print::parsePageRange;

    // An empty range is every page, zero-based.
    checkPages(parsePageRange(QString(), 4), {0, 1, 2, 3}, "empty means all");
    checkPages(parsePageRange(QStringLiteral("   "), 3), {0, 1, 2}, "whitespace means all too");

    // Spans and single pages, 1-based on paper.
    checkPages(parsePageRange(QStringLiteral("1-3,5"), 5), {0, 1, 2, 4}, "a span and a single page");
    checkPages(parsePageRange(QStringLiteral("2,2,2"), 5), {1}, "repeats collapse to one");
    checkPages(parsePageRange(QStringLiteral("5-3"), 5), {2, 3, 4}, "a reversed span reads forwards");
    checkPages(parsePageRange(QStringLiteral("4-100"), 5), {3, 4}, "a span past the end clamps");

    // Things that are not pages are skipped rather than crashing or guessing.
    checkPages(parsePageRange(QStringLiteral("0,6"), 5), {}, "out-of-range numbers name nothing");
    checkPages(parsePageRange(QStringLiteral("abc,3,,x"), 5), {2}, "junk tokens are ignored");
    checkPages(parsePageRange(QStringLiteral(" 4 "), 5), {3}, "spaces around a number are trimmed");

    // n-up grids.
    checkGrid(1, 1, 1, "one page per sheet");
    checkGrid(2, 2, 1, "two across");
    checkGrid(4, 2, 2, "four in a square");
    checkGrid(6, 3, 2, "six as three by two");
    checkGrid(9, 3, 3, "nine in a square");
    checkGrid(16, 4, 4, "sixteen in a square");
    checkGrid(7, 1, 1, "an unknown count falls back to one");

    if (failures == 0) {
        std::cout << "print smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
