#include "textsearch.h"

namespace {

// Whether the character at `at` (which may be off either end of the text) ends
// a word, for the purpose of the match that stops there.
bool isBoundary(const QString &text, int at)
{
    if (at < 0 || at >= text.length())
        return true; // the start and the end of the page are boundaries
    return !TextSearch::isWordCharacter(text.at(at));
}

} // namespace

bool TextSearch::isWordCharacter(QChar character)
{
    if (!character.isLetterOrNumber() && character != QLatin1Char('_'))
        return false;
    switch (character.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
        // Written without spaces, so every position in a run of them is as much
        // a word boundary as any other. Treating them as word characters would
        // make "whole word" mean "never matches" on a Chinese page.
        return false;
    default:
        return true;
    }
}

QVector<TextSearch::Match> TextSearch::matchesInPage(int page, const QString &text,
                                                     const QString &needle, bool caseSensitive,
                                                     bool wholeWord)
{
    QVector<Match> matches;
    // An empty needle is in every position of every text, which is a match count
    // of infinity rather than a search result; a needle longer than the page
    // cannot be in it.
    if (needle.isEmpty() || text.isEmpty() || needle.length() > text.length())
        return matches;

    // Which ends have to fall on a boundary, decided by the needle rather than
    // by the option alone: "(c" asked for as a whole word has nothing to say
    // about what precedes the bracket, and neither has a needle in Chinese.
    const bool boundaryBefore = wholeWord && isWordCharacter(needle.front());
    const bool boundaryAfter = wholeWord && isWordCharacter(needle.back());

    const Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const int length = needle.length();
    for (int at = text.indexOf(needle, 0, sensitivity); at >= 0;
         at = text.indexOf(needle, at + length, sensitivity)) {
        if (boundaryBefore && !isBoundary(text, at - 1))
            continue;
        if (boundaryAfter && !isBoundary(text, at + length))
            continue;
        matches.append(Match{page, at, at + length});
    }
    return matches;
}
