// Every hit of the current search, one row each, as the third tab of the
// reading sidebar.
//
// The find bar says how many there are and which one is in front; this says
// *where* they are. Each row is a page number and the page's own words around
// the hit (TextSearch::Snippet), with the hit itself in bold, so a reader can
// pick the occurrence they meant instead of stepping through all of them.
//
// ---- why the cap ----
//
// A page of prose can hold a thousand hits for a common word, and a document
// can hold a thousand pages. Rows are cheap compared with pages, but not free:
// the row a reader can use is the one near the hit they are looking at, and
// nobody scrolls a list of fifty thousand rows to find it. The first thousand
// are listed and one last row says how many were left out -- a number, so that
// "there are more" is not a thing the reader has to guess.
//
// ---- what it is not ----
//
// Not the search itself: matching, stepping and the highlight on the page are
// DocumentTab's, and this only shows what came back and asks to be taken to a
// row. A row pressed is a match index, not a row number, so the two can drift
// without the jump going to the wrong hit.
#pragma once

#include "../document/textsearch.h"

#include <QListWidget>

class SearchPanel : public QListWidget
{
    Q_OBJECT

public:
    explicit SearchPanel(QWidget *parent = nullptr);

    // How many matches are listed before the "and N more" row takes over.
    static constexpr int MaximumRows = 1000;

    // Rebuilds the list from a scan's answer. `current` is the match in front,
    // -1 for none. Rows past MaximumRows are not built; the last row counts
    // them instead.
    void setMatches(const QVector<TextSearch::Match> &matches, int current);
    void clearMatches();

    // Marks the row of match `index` as the one in front and brings it into
    // view. An index that has no row -- negative, or past the thousandth --
    // clears the mark rather than pointing at a row that is not that match.
    void setCurrentMatch(int index);

    // What a row stands for, for the tab that answers a press and for a test
    // that would otherwise have to read pixels: the index into the matches
    // handed to setMatches() (-1 for the "not listed" row), the text the reader
    // sees, and where the bold run sits inside that text.
    int matchIndexOfRow(int row) const;
    QString rowText(int row) const;
    int boldFromOfRow(int row) const;
    int boldToOfRow(int row) const;

    // How many matches were left out of the list; zero when all of them fit.
    int omittedCount() const { return omitted; }

Q_SIGNALS:
    // The reader pressed a row. `index` is into the matches handed to
    // setMatches(); the "not listed" row never sends this.
    void matchChosen(int index);

private:
    int omitted = 0;
};
