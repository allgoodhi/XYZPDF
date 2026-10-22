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

// A match's surroundings, folded onto one line for the results list.
//
// `text` is the page's own characters around the match -- not a paraphrase --
// with line breaks turned into single spaces so that one row of a list can hold
// what was several lines of the page. An ellipsis (U+2026) is put at either end
// that the cut left out, so that a row starting in the middle of a sentence does
// not read as the start of one.
//
// `from`/`to` are the match inside `text`, half-open like every other range
// here, for whoever draws it in bold. They are positions in `text`, not in the
// page: folding and the two ellipses move every character after them.
struct Snippet
{
    QString text;
    int from = 0;
    int to = 0;
};

struct Match
{
    int page = -1;
    // Half-open, like every other range of characters in this program: [from,
    // to) is the text that matched.
    int from = 0;
    int to = 0;
    // The text around the match, cut down to one row. Filled by matchesInPage()
    // while the page's text is already in hand -- a caller never has to ask the
    // document for the page a second time, which matters because reading a page
    // is a PDFium call behind the same global lock as rendering.
    Snippet snippet;
};

// The characters of `text` from `context` before `from` to `context` after `to`,
// folded onto one line and marked so the match can be found again. Both ends
// are clamped to the page, and an ellipsis is added only where something was
// actually left out -- so a hit at the very start of a page has none in front of
// it, and that is a fact a test can tell apart from a cut.
//
// `from`/`to` are clamped too, and an empty or backwards range answers with an
// empty snippet rather than with text no match belongs to.
Snippet snippetAround(const QString &text, int from, int to, int context = 20);

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
