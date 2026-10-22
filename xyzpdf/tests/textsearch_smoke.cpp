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

// The same with "whole word" asked for, which is the other half of the options
// a reader has (docs/PLAN.md M2: 大小写与全词选项).
QVector<TextSearch::Match> findWhole(const QString &text, const QString &needle,
                                     bool caseSensitive = false)
{
    return TextSearch::matchesInPage(7, text, needle, caseSensitive, true);
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

    // ---- whole words ----
    //
    // The option every reader has and this one did not (docs/review-03.md
    // §3.2). What makes it worth a test of its own is not "cat is not in
    // catalogue" -- it is what the rule does to a language that has no word
    // boundaries at all, which is the language this program is mostly read in.
    {
        const QString line = QStringLiteral("the cat, a catalogue and a bobcat");
        check(find(line, QStringLiteral("cat")).size() == 3,
              "without the option, cat is found three times in cat/catalogue/bobcat");
        const auto whole = findWhole(line, QStringLiteral("cat"));
        check(whole.size() == 1, QStringLiteral("with it, once (got %1: %2)")
                                     .arg(whole.size()).arg(describe(whole)));
        check(!whole.isEmpty() && whole.first().from == 4,
              "and it is the standalone one, the comma after it being a boundary");

        check(findWhole(QStringLiteral("cat"), QStringLiteral("cat")).size() == 1,
              "the start and the end of the page are boundaries too");
        check(findWhole(QStringLiteral("cat_1"), QStringLiteral("cat")).isEmpty(),
              "an underscore is part of a word, as everywhere else");
        check(findWhole(QStringLiteral("cat2"), QStringLiteral("cat")).isEmpty(),
              "and so is a digit");
        check(findWhole(QStringLiteral("CAT sat"), QStringLiteral("cat")).size() == 1,
              "the option does not quietly turn case sensitivity on");
        check(findWhole(QStringLiteral("CAT sat"), QStringLiteral("cat"), true).isEmpty(),
              "nor off");

        // The needle's own ends decide which boundaries are asked for: nothing
        // precedes a bracket in "(c" that could make it part of a longer word.
        check(findWhole(QStringLiteral("f(cat)"), QStringLiteral("(cat)")).size() == 1,
              "a needle wrapped in punctuation is matched as it would be without the option");
    }

    // ---- whole words in a language that has none ----
    //
    // Chinese and Japanese are written without spaces, so there is no boundary
    // to find. The rule is therefore written in terms of the characters rather
    // than of the option: Han and kana are not word characters, so a Chinese
    // needle asks for no boundaries and the option changes nothing. The
    // alternative -- treating them as letters, which QChar does -- would make
    // this box answer "no matches" on every Chinese document.
    {
        const QString page = QStringLiteral("前说明前面的说明");
        check(find(page, QStringLiteral("说明")).size() == 2, "two occurrences without the option");
        check(findWhole(page, QStringLiteral("说明")).size() == 2,
              "and the same two with it -- there are no word boundaries to fail");

        check(TextSearch::isWordCharacter(QLatin1Char('a')), "a letter is a word character");
        check(TextSearch::isWordCharacter(QLatin1Char('7')), "and a digit");
        check(TextSearch::isWordCharacter(QLatin1Char('_')), "and an underscore");
        check(!TextSearch::isWordCharacter(QLatin1Char(' ')), "a space is not");
        check(!TextSearch::isWordCharacter(QLatin1Char(',')), "nor is punctuation");
        check(!TextSearch::isWordCharacter(QChar(0x8bf4)), "nor a Han character");
        check(!TextSearch::isWordCharacter(QChar(0x30a2)), "nor katakana");
        check(TextSearch::isWordCharacter(QChar(0xd55c)),
              "but Hangul is, because Korean puts spaces between its words");
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
