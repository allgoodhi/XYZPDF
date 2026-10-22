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

TextSearch::Snippet TextSearch::snippetAround(const QString &text, int from, int to, int context)
{
    Snippet snippet;
    if (text.isEmpty())
        return snippet;

    from = qBound(0, from, int(text.length()));
    to = qBound(from, to, int(text.length()));
    if (from == to)
        return snippet;

    context = qMax(0, context);
    const int first = qMax(0, from - context);
    const int last = qMin(int(text.length()), to + context);

    // Where each character of the window ended up after the line breaks were
    // folded. A Windows break is two characters and one space, so an index
    // taken from the page is not an index into the row -- and the bold run is
    // drawn from those indices.
    QVector<int> atInRow(last - first + 1, 0);
    int rowLength = 0;
    for (int at = first; at < last; ++at) {
        atInRow[at - first] = rowLength;
        const QChar character = text.at(at);
        if (character == QLatin1Char('\r') && at + 1 < last
            && text.at(at + 1) == QLatin1Char('\n')) {
            // One break, not two, and not two spaces either: a row that showed
            // the CRLF as "  " reads like a typo in the document.
            atInRow[at + 1 - first] = rowLength;
            snippet.text.append(QLatin1Char(' '));
            ++rowLength;
            ++at;
            continue;
        }
        if (character == QLatin1Char('\n') || character == QLatin1Char('\r')
            || character == QChar(0x2028) || character == QChar(0x2029)) {
            snippet.text.append(QLatin1Char(' '));
        } else {
            snippet.text.append(character);
        }
        ++rowLength;
    }
    atInRow[last - first] = rowLength;

    snippet.from = atInRow[from - first];
    snippet.to = atInRow[to - first];
    if (first > 0) {
        snippet.text.prepend(QChar(0x2026));
        ++snippet.from;
        ++snippet.to;
    }
    if (last < text.length())
        snippet.text.append(QChar(0x2026));
    return snippet;
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
        Match match;
        match.page = page;
        match.from = at;
        match.to = at + length;
        // Cut here, while the page's text is in hand. The results list needs a
        // row of context per hit, and asking the document for the page again
        // would be a second PDFium read behind the lock every render takes.
        match.snippet = snippetAround(text, at, at + length);
        matches.append(match);
    }
    return matches;
}
