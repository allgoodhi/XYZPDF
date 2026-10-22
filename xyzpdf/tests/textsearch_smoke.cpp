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

    // ---- the row of context around a hit ----
    //
    // What the results list puts beside the page number (handoff-01 A). The
    // rules that are worth pinning are the ones a reader sees: the hit is where
    // the bold goes, the row is one line even when the page put a break in the
    // middle of it, an ellipsis marks only the ends that were actually cut, and
    // neither end can claim to be the page's own beginning or end when it is not.
    {
        // English, cut on both sides. Twenty characters before the hit is
        // exactly what the row has room for, so the run before the bold is a
        // length this can check rather than describe.
        const QString english =
            QStringLiteral("The quick brown fox jumps over the lazy dog and then keeps running "
                           "to the end of the field.");
        const auto matches = find(english, QStringLiteral("lazy"));
        check(matches.size() == 1, "one hit to put in the row");
        if (!matches.isEmpty()) {
            const TextSearch::Snippet snippet = matches.first().snippet;
            check(snippet.text.startsWith(QChar(0x2026)) && snippet.text.endsWith(QChar(0x2026)),
                  QStringLiteral("an ellipsis at each end that the cut left out (got \"%1\")")
                      .arg(snippet.text));
            check(snippet.text.mid(snippet.from, snippet.to - snippet.from)
                      == QStringLiteral("lazy"),
                  QStringLiteral("the bold range is the hit itself (got \"%1\")")
                      .arg(snippet.text.mid(snippet.from, snippet.to - snippet.from)));
            check(snippet.from == 21,
                  QStringLiteral("twenty characters before it, plus the ellipsis (got %1)")
                      .arg(snippet.from));
            check(snippet.text.contains(QStringLiteral("jumps over the lazy dog")),
                  QStringLiteral("and the words on both sides are the page's own (got \"%1\")")
                      .arg(snippet.text));
        }

        // Chinese: no spaces to count and no word boundary to rely on, so the
        // same rule has to hold on characters alone.
        const QString chinese = QStringLiteral(
            "在项目里，搜索是最常被用到的功能之一。阅读器的搜索结果列表要把命中的那一处上下文"
            "带出来，方便读者判断是不是要找的地方。这里再补一句，好让右边也走出二十个字，"
            "两端都真的被裁到。");
        const auto chineseMatches = find(chinese, QStringLiteral("上下文"));
        check(chineseMatches.size() == 1, "the Chinese sentence has the needle once");
        if (!chineseMatches.isEmpty()) {
            const TextSearch::Snippet snippet = chineseMatches.first().snippet;
            check(snippet.text.mid(snippet.from, snippet.to - snippet.from)
                      == QStringLiteral("上下文"),
                  QStringLiteral("which is the bold range in its row (got \"%1\")")
                      .arg(snippet.text.mid(snippet.from, snippet.to - snippet.from)));
            check(snippet.text.startsWith(QChar(0x2026)) && snippet.text.endsWith(QChar(0x2026)),
                  QStringLiteral("cut on both sides (got \"%1\")").arg(snippet.text));
        }

        // A hit at the very start of the page has nothing in front of it, so a
        // leading ellipsis there would be a claim about a cut that never was.
        {
            const TextSearch::Snippet start = TextSearch::snippetAround(
                QStringLiteral("XYZPDF starts this page and keeps going for a while after that."),
                0, 6);
            check(!start.text.startsWith(QChar(0x2026)),
                  QStringLiteral("a hit at the page's start has no ellipsis in front (got \"%1\")")
                      .arg(start.text));
            check(start.text.endsWith(QChar(0x2026)) && start.from == 0 && start.to == 6,
                  "and is cut only at the far end");
        }
        // And the mirror image: the hit ends the page, so nothing is left out
        // behind it.
        {
            const QString text = QStringLiteral("The last words on this page are XYZPDF");
            const TextSearch::Snippet end =
                TextSearch::snippetAround(text, int(text.length()) - 6, int(text.length()));
            check(end.text.endsWith(QStringLiteral("XYZPDF")) && !end.text.endsWith(QChar(0x2026)),
                  QStringLiteral("a hit at the page's end is not cut behind it (got \"%1\")")
                      .arg(end.text));
            check(end.text.startsWith(QChar(0x2026)), "but is still cut in front");
        }

        // Across a line break: the row is one line, and the word after the
        // break is separated by a space -- one space, not a newline and not
        // the two characters of a Windows break.
        {
            const QString text = QStringLiteral(
                "first\nsecond needle and more text after that keeps going on");
            const auto matches = find(text, QStringLiteral("needle"));
            check(matches.size() == 1, "the hit after a line break is found");
            if (!matches.isEmpty()) {
                const TextSearch::Snippet snippet = matches.first().snippet;
                check(!snippet.text.contains(QLatin1Char('\n'))
                          && !snippet.text.contains(QLatin1Char('\r')),
                      QStringLiteral("the row has no line break left in it (got \"%1\")")
                          .arg(snippet.text));
                check(snippet.text.contains(QStringLiteral("first second")),
                      QStringLiteral("the break between the two lines is one space (got \"%1\")")
                          .arg(snippet.text));
                check(snippet.text.mid(snippet.from, snippet.to - snippet.from)
                          == QStringLiteral("needle"),
                      "and the bold still lands on the hit");
            }

            const QString windows = QStringLiteral("alpha\r\nalpha\r\nalpha needle alpha alpha");
            const TextSearch::Snippet folded =
                TextSearch::snippetAround(windows, int(windows.indexOf(QStringLiteral("needle"))),
                                          int(windows.indexOf(QStringLiteral("needle"))) + 6, 40);
            check(!folded.text.contains(QStringLiteral("  ")),
                  QStringLiteral("a Windows break folds to one space, not two (got \"%1\")")
                      .arg(folded.text));
        }

        // A hit that itself spans the break: the bold has to cover both lines'
        // worth of it, which is where an index computed on the page instead of
        // on the row would land somewhere else.
        {
            const QString text = QStringLiteral("the first line\nsecond line and more");
            const auto matches = find(text, QStringLiteral("line\nsec"));
            check(matches.size() == 1, "a phrase across a break is one hit");
            if (!matches.isEmpty()) {
                const TextSearch::Snippet snippet = matches.first().snippet;
                check(snippet.text.mid(snippet.from, snippet.to - snippet.from)
                          == QStringLiteral("line sec"),
                      QStringLiteral("and its bold range covers the folded break (got \"%1\")")
                          .arg(snippet.text.mid(snippet.from, snippet.to - snippet.from)));
            }
        }

        // No context asked for is the match alone, still marked on both sides
        // when it is not the whole page.
        {
            const TextSearch::Snippet bare =
                TextSearch::snippetAround(QStringLiteral("abcNEEDLEdef"), 3, 9, 0);
            check(bare.text == QStringLiteral("…NEEDLE…") && bare.from == 1 && bare.to == 7,
                  QStringLiteral("a bare snippet is the hit with an ellipsis each side (got "
                                 "\"%1\" at %2..%3)")
                      .arg(bare.text)
                      .arg(bare.from)
                      .arg(bare.to));
        }

        // Nothing to point at: an empty or backwards range answers with nothing
        // rather than with text no match belongs to.
        {
            const TextSearch::Snippet empty = TextSearch::snippetAround(QStringLiteral("abc"), 2, 2);
            check(empty.text.isEmpty() && empty.from == 0 && empty.to == 0,
                  "an empty range has no row");
            const TextSearch::Snippet backwards =
                TextSearch::snippetAround(QStringLiteral("abc"), 2, 1);
            check(backwards.text.isEmpty(), "and neither has a backwards one");
        }
    }

    if (failures == 0) {
        std::cout << "the search rules hold\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
