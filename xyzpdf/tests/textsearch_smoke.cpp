// What counts as a match, with no PDF anywhere near it.
//
// The rules that are worth pinning down are the ones a reader notices: that
// case does not matter unless asked, that two overlapping occurrences are one
// result rather than two (or a count appears that does not match what is on
// screen), and that a needle which is not there -- or is empty, or is longer
// than the page -- is answered with nothing rather than with everything.

#include "../src/document/textsearch.h"

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

QString describe(const QVector<TextSearch::Match> &matches)
{
    QStringList parts;
    for (const TextSearch::Match &match : matches)
        parts.append(QStringLiteral("%1:[%2,%3)").arg(match.page).arg(match.from).arg(match.to));
    return parts.join(QLatin1Char(' '));
}

// The matches of one needle in one page of text, which is what every case here
// is about.
QVector<TextSearch::Match> find(const QString &text, const QString &needle,
                                bool caseSensitive = false)
{
    return TextSearch::matchesInPage(7, text, needle, caseSensitive);
}

} // namespace

int main()
{
    // ---- the ordinary case ----
    {
        const auto matches = find(QStringLiteral("the cat sat on the mat"), QStringLiteral("at"));
        check(matches.size() == 3,
              QStringLiteral("three \"at\" in the sentence (got %1: %2)")
                  .arg(matches.size())
                  .arg(describe(matches)));
        check(matches.first().from == 5 && matches.first().to == 7,
              QStringLiteral("the first one at 5..7 (got %1)").arg(describe(matches)));
        check(matches.first().page == 7, "carrying the page it was found on");
    }

    // ---- case, both ways ----
    {
        check(find(QStringLiteral("XYZPDF"), QStringLiteral("xyzpdf")).size() == 1,
              "lower case finds upper case by default");
        check(find(QStringLiteral("XYZPDF"), QStringLiteral("xyzpdf"), true).isEmpty(),
              "and does not when case matters");
        check(find(QStringLiteral("xyzpdf"), QStringLiteral("XYZPDF"), true).isEmpty(),
              "the other way round too");
        check(find(QStringLiteral("xyzpdf"), QStringLiteral("XYZPDF")).size() == 1,
              "and the default finds it either way");
    }

    // ---- two occurrences that overlap are one match ----
    //
    // "aa" in "aaa": two highlights drawn on the same character look like one,
    // and a count of two for one visible mark is a count nobody trusts.
    {
        const auto matches = find(QStringLiteral("aaa"), QStringLiteral("aa"));
        check(matches.size() == 1,
              QStringLiteral("\"aa\" in \"aaa\" is one match (got %1)").arg(matches.size()));
        check(matches.first().to == 2, "ending at the end of the first one");

        const auto twice = find(QStringLiteral("aaaa"), QStringLiteral("aa"));
        check(twice.size() == 2,
              QStringLiteral("but \"aa\" in \"aaaa\" is two (got %1)").arg(twice.size()));
        check(twice.at(0).from == 0 && twice.at(1).from == 2,
              QStringLiteral("at the two positions that do not overlap (got %1)")
                  .arg(describe(twice)));
    }

    // ---- nothing to find, or nothing to look in ----
    {
        check(find(QStringLiteral("the cat sat"), QStringLiteral("dog")).isEmpty(),
              "a needle that is not there is not found");
        check(find(QStringLiteral("the cat sat"), QString()).isEmpty(),
              "an empty needle matches nothing rather than everything");
        check(find(QString(), QStringLiteral("cat")).isEmpty(),
              "and an empty page has nothing in it");
        check(find(QStringLiteral("cat"), QStringLiteral("cat and a dog")).isEmpty(),
              "a needle longer than the page is not found in it");
        check(find(QStringLiteral("cat"), QStringLiteral("cat")).size() == 1,
              "a needle as long as the page is found");
    }

    // ---- what a document of one language needs ----
    //
    // Chinese has no spaces, so a search is a substring search over a run of
    // characters -- which is the same thing this function does, and the reason
    // there is no word splitting anywhere in it.
    {
        const QString page = QStringLiteral("ZYNQ_MINI 开发板使用前说明");
        const auto matches = find(page, QStringLiteral("开发板"));
        check(matches.size() == 1, "a Chinese phrase is found");
        check(page.mid(matches.first().from, matches.first().to - matches.first().from)
                  == QStringLiteral("开发板"),
              "and the range it names is exactly that text");
        check(find(page, QStringLiteral("开发板"), true).size() == 1,
              "with case sensitivity having nothing to do with it");

        const auto both = find(QStringLiteral("前说明前面的说明"), QStringLiteral("说明"));
        check(both.size() == 2, QStringLiteral("and every occurrence is found (got %1)")
                                    .arg(both.size()));
    }

    // ---- a match that spans a line break ----
    //
    // Which is ordinary in a PDF: the text of a wrapped line has a line break in
    // it, and a search for two words at the end of one line and the start of the
    // next finds them as the text has them -- a reader typing a phrase across a
    // line break gets the phrase, which is what they meant.
    {
        const auto matches =
            find(QStringLiteral("the first line\nsecond line"), QStringLiteral("line\nsec"));
        check(matches.size() == 1, "a match across a line break is found");
        check(matches.first().from == 10,
              QStringLiteral("at the position it starts at (got %1)").arg(matches.first().from));
    }

    if (failures == 0) {
        std::cout << "the search rules hold\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
