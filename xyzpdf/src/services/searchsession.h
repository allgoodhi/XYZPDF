// The search of one open document: the scan, the matches it came back with, and
// which of them the reader is looking at.
//
// Cut out of DocumentTab (docs/review-03.md §1.2) because a search is a thing
// that "takes the document and does a job with it, for a while, and can be
// stopped" -- and the tab was holding the matches, the cancel flag, the running
// futures, the bar's label and the sidebar's list of rows at the same time.
//
// What stays with the tab: which document to read (it is handed in through
// `document`, and a search must never be still running when that document is
// replaced -- see cancelPending()/waitUntilFinished() and DocumentTab::drainRenders),
// and everything the window is told about through DocumentTab's own signals.
//
// What is here: the arithmetic of a scan and the three panels it speaks to --
// the bar, the sidebar's list and the page's highlights -- which is the
// "联动" half. They are borrowed pointers, not owned: the tab builds them.
#pragma once

#include "../document/textlayer.h"
#include "../document/textsearch.h"

#include <QObject>
#include <QFuture>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <functional>

class Document;
class FindBar;
class PageView;
class QAtomicInt;
class QTabWidget;
class SearchPanel;

class SearchSession : public QObject
{
    Q_OBJECT

public:
    // `document` answers with the document to read, or null when the tab has
    // none -- which is the same question `opened` used to answer when all of
    // this lived in the tab.
    SearchSession(FindBar *bar, SearchPanel *panel, PageView *view, QTabWidget *sidebar,
                  std::function<Document *()> document, QObject *parent = nullptr);
    ~SearchSession() override;

    // ---- text ----
    //
    // The scan runs off the UI thread, which is not a detail: text() is a PDFium
    // call per page and every one of them takes the same global lock as the
    // renderer, so a five-hundred-page document searched on the thread that
    // draws is a window that does not repaint for as long as it takes. See
    // document.h's note about which calls may sit on that thread.
    void start(const QString &needle, bool caseSensitive, bool wholeWord);
    void clear();
    // Moves to the next (+1) or previous (-1) match, wrapping round. Doing
    // nothing when there is nothing to move to.
    void step(int delta);
    // Looks at one match by index -- the sidebar's list is the only control
    // that names a match rather than asking for the next one. Out-of-range
    // indexes do nothing.
    void focus(int index);

    // What is being searched for, and whether case or whole words matter. Two
    // views of one search -- the bar in the tab and the box in the window's
    // toolbar -- are told by these what the other one is showing, so that
    // neither can drift.
    QString needle() const { return searchNeedle; }
    bool caseSensitive() const { return searchCase; }
    bool wholeWord() const { return searchWhole; }
    int matchCount() const { return int(searchMatches.size()); }
    // Which match is being looked at, 0-based, or -1 for none.
    int currentIndex() const { return currentMatchIndex; }
    // Whether a scan of this document is still running.
    bool running() const;

    // ---- the wait before the document is replaced ----
    //
    // Two steps, in this order, wherever a document is about to go away: the
    // flag first so that the scan stops at the next page boundary rather than
    // reading a document that is being torn down under it, then the wait.
    // DocumentTab::drainRenders() and ~DocumentTab both do this.
    void cancelPending();
    void waitUntilFinished();

Q_SIGNALS:
    // How the search is going: which match is in front, how many there are, and
    // whether the count is still being worked out.
    void changed(int current, int total, bool searching);

private:
    // The matches a scan came back with, in page order.
    void applyResults(const QVector<TextSearch::Match> &matches);
    // One match as the range the view draws and scrolls to, or an empty range
    // for an index that is not one.
    PageTextRange rangeOf(int index) const;
    // Hands the view the matches to draw and which one is in front, and tells
    // the bar what to say.
    void updateHighlights();

    FindBar *findBar = nullptr;
    SearchPanel *searchPanel = nullptr;
    PageView *pageView = nullptr;
    // The tab's sidebar, for the one thing this class does with it: a search is
    // the reason to look at its results tab when it is already open.
    QTabWidget *sidebar = nullptr;
    std::function<Document *()> documentProvider;

    // ---- search state ----
    //
    // The matches, in page order, and which of them is in front. Replaced
    // wholesale whenever a scan finishes: a search is a question with one
    // answer, and half of two answers is not a result.
    QVector<TextSearch::Match> searchMatches;
    int currentMatchIndex = -1;
    // What is being searched for. One string for the search, and both of the
    // controls that can type it -- the bar in the tab and the box in the
    // window's toolbar -- show this one.
    QString searchNeedle;
    // Whether case matters for it, which is a setting of the search rather than
    // of either control: the toolbar box has no checkbox of its own, so it
    // searches the way the bar was last set.
    bool searchCase = false;
    bool searchWhole = false;
    // The flag the running scan checks between pages, and the identity of the
    // scan it belongs to: a scan whose flag is no longer `searchCancel` is
    // answering a question this search has stopped asking, and its answer is
    // dropped.
    QSharedPointer<QAtomicInt> searchCancel;
    // The scans that have not finished, so that one is never still running when
    // the document it is reading is replaced -- in DocumentTab's destructor and
    // in its open(), both of which call drainRenders().
    QVector<QFuture<QVector<TextSearch::Match>>> searchTasks;
};
