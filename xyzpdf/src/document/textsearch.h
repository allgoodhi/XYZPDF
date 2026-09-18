// Finding a phrase in a page's text.
//
// The text itself comes from the document; what a *match* is -- whether case
// matters, what happens when two occurrences overlap, which of them counts --
// is decided here, so that it can be tested without a PDF anywhere near it.
//
// A page at a time, like everything else that turns character indices into
// boxes: the search across a document is a loop over pages, and where that loop
// runs (and how it is stopped) is the caller's business.
#pragma once

#include <QString>
#include <QVector>

namespace TextSearch {

struct Match
{
    int page = -1;
    // Half-open, like every other range of characters in this program: [from,
    // to) is the text that matched.
    int from = 0;
    int to = 0;
};

// Every occurrence of `needle` in `text`, in order, as matches on `page`.
//
// Case-insensitive unless asked otherwise, which is what every other reader
// defaults to -- and what is needed for the ordinary case of somebody typing a
// name in lower case.
//
// Overlapping occurrences are reported once, not twice: searching "aa" in "aaa"
// answers once, at the first character. Two highlights drawn on top of each
// other look like one, and a count that says two while the reader can see one
// is a count nobody trusts afterwards. The search resumes at the end of the
// match, so both "aa" in "aaaa" are found.
QVector<Match> matchesInPage(int page, const QString &text, const QString &needle,
                             bool caseSensitive);

} // namespace TextSearch
