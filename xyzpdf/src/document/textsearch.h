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

#include <QChar>
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

// Whether a character is one that a word is made of, for the "whole word"
// option below.
//
// Letters, digits and the underscore, as everywhere else -- *except* that Han
// characters and Japanese kana are not. That exception is the whole of what
// "whole word" can honestly mean in CJK: Chinese and Japanese are written
// without spaces, so there is no boundary to find, and a rule that asked for
// one would make the option silently answer "no matches" on a Chinese document
// -- which is the language this program is mostly read in. Hangul is left as a
// word character because Korean does put spaces between its words.
bool isWordCharacter(QChar character);

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
// `wholeWord` asks for occurrences that are not part of a longer word: "cat"
// then finds the one in "the cat sat" and not the one in "catalogue". The test
// is applied at each end of the needle and only where the needle's own
// character there is a word character, so a needle that begins or ends in
// punctuation -- or in Chinese -- is matched as it would be with the option
// off. See isWordCharacter().
QVector<Match> matchesInPage(int page, const QString &text, const QString &needle,
                             bool caseSensitive, bool wholeWord = false);

} // namespace TextSearch
