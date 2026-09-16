// What a selection is, between the mouse and the page.
//
// A selection is two positions -- where the drag started and where the pointer
// is now -- and everything else follows from them. Each position is a page and
// a character index on it, which is what the backend can answer and what
// survives a re-render, a zoom or a scroll: the text does not move when the
// page is drawn bigger.
//
// ---- why this is its own file ----
//
// The folding from two positions to the ranges to draw is the part with the
// cases in it: dragging backwards, dragging across a page boundary, a click
// that selects nothing, a "select all" that ends at the last character of the
// last page. Every one of those is arithmetic, and arithmetic is what a test
// can check. Taking it out of the view means textlayer_smoke can check all of
// them without a PDF, a window or an event loop -- and the view is left with
// what only a view can do: turning mouse positions into character indices and
// painting the result.
#pragma once

#include <QString>
#include <QVector>

#include <functional>

// One page's share of a selection.
struct PageTextRange
{
    int page = -1;
    // First character, inclusive.
    int from = 0;
    // One past the last character.
    int to = 0;

    bool isEmpty() const { return page < 0 || to <= from; }
    bool operator==(const PageTextRange &other) const
    {
        return page == other.page && from == other.from && to == other.to;
    }
};

// The per-page ranges a selection covers, in page order.
//
// `charactersOn` answers how many characters a page has. It is a function rather
// than a Document so that this can be tested without a PDF, and so that the
// caller decides where the number comes from -- the backend keeps the page's
// text loaded, and asking it per page during a drag is one call that does not
// touch the renderer.
QVector<PageTextRange> selectionRanges(int anchorPage, int anchorIndex,
                                       int focusPage, int focusIndex,
                                       const std::function<int(int)> &charactersOn);

// Whether the two positions are the same place: a click, which selects nothing.
bool isEmptySelection(int anchorPage, int anchorIndex, int focusPage, int focusIndex);

// The text of a selection, from each page's own text.
//
// A newline goes between pages: the last word of one page and the first of the
// next are not adjacent words, and pasting them together silently is how a
// copied quotation comes out wrong in a way nobody notices until it is
// published. No newline is added at the end.
QString selectionText(const QVector<PageTextRange> &ranges,
                      const std::function<QString(int)> &textOf);
