#include "textsearch.h"

QVector<TextSearch::Match> TextSearch::matchesInPage(int page, const QString &text,
                                                     const QString &needle, bool caseSensitive)
{
    QVector<Match> matches;
    // An empty needle is in every position of every text, which is a match count
    // of infinity rather than a search result; a needle longer than the page
    // cannot be in it.
    if (needle.isEmpty() || text.isEmpty() || needle.length() > text.length())
        return matches;

    const Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const int length = needle.length();
    for (int at = text.indexOf(needle, 0, sensitivity); at >= 0;
         at = text.indexOf(needle, at + length, sensitivity)) {
        matches.append(Match{page, at, at + length});
    }
    return matches;
}
