#include "searchsession.h"

#include "../document/document.h"
#include "../ui/findbar.h"
#include "../ui/searchpanel.h"
#include "../view/pageview.h"

#include <QAtomicInt>
#include <QFutureWatcher>
#include <QTabWidget>
#include <QtConcurrent/QtConcurrentRun>

SearchSession::SearchSession(FindBar *bar, SearchPanel *panel, PageView *view, QTabWidget *sidebar,
                             std::function<Document *()> document, QObject *parent)
    : QObject(parent)
    , findBar(bar)
    , searchPanel(panel)
    , pageView(view)
    , sidebar(sidebar)
    , documentProvider(std::move(document))
{
    // The list of hits is the one control that names a match instead of asking
    // for the next one.
    connect(searchPanel, &SearchPanel::matchChosen, this, &SearchSession::focus);
}

SearchSession::~SearchSession() = default;

void SearchSession::start(const QString &needle, bool caseSensitive, bool wholeWord)
{
    searchCase = caseSensitive;
    searchWhole = wholeWord;

    // What is being searched for, kept here rather than in either of the two
    // controls that can type it: this is the answer the tab reports, and both
    // the bar's label and the toolbar's count are filled in from it. Without
    // this line the count has nothing to describe -- which is exactly how the
    // label came to be empty.
    searchNeedle = needle;

    // Whichever control the needle was typed into, the other one shows it. The
    // window's toolbar box and the tab's bar are two views of one search, and
    // two controls showing different needles for the same highlighted pages is
    // a state a user cannot reason about.
    if (findBar)
        findBar->setText(needle);

    // Whatever was running is answering an old question now: its flag is
    // cleared, and its results are recognised as stale when they arrive -- by
    // their flag not being the current one.
    if (searchCancel)
        searchCancel->storeRelaxed(1);
    for (auto task = searchTasks.begin(); task != searchTasks.end();) {
        if (task->isFinished())
            task = searchTasks.erase(task);
        else
            ++task;
    }

    searchMatches.clear();
    currentMatchIndex = -1;
    // New question, new answer: rows from the last scan belong to the last
    // needle, and would otherwise sit under "搜索中…" as if they still counted.
    searchPanel->clearMatches();

    Document *document = documentProvider ? documentProvider() : nullptr;
    if (needle.isEmpty() || !document) {
        updateHighlights();
        return;
    }

    // If the sidebar is already open, a search is the reason to look at its
    // third tab. A closed sidebar is left closed: taking a third of the window
    // because something was typed is what 列出全部 exists to avoid. Asked as
    // "not hidden" rather than isVisible(), which is also false while this tab
    // is not the one in front -- the sidebar is still open for when it is.
    if (!sidebar->isHidden())
        sidebar->setCurrentWidget(searchPanel);

    const auto cancel = QSharedPointer<QAtomicInt>::create(0);
    searchCancel = cancel;
    const int pages = document->pageCount();

    updateHighlights(); // "searching", with nothing found yet

    auto *watcher = new QFutureWatcher<QVector<TextSearch::Match>>(this);
    connect(watcher, &QFutureWatcher<QVector<TextSearch::Match>>::finished, this,
            [this, watcher, cancel] {
                const QVector<TextSearch::Match> found = watcher->result();
                watcher->deleteLater();
                // An answer to a question this search has stopped asking. The
                // flag is compared rather than a counter incremented, because
                // the object identity is what the running scan was given.
                if (cancel != searchCancel)
                    return;
                applyResults(found);
            });

    QFuture<QVector<TextSearch::Match>> task =
        QtConcurrent::run([document, cancel, needle, caseSensitive, wholeWord, pages] {
            QVector<TextSearch::Match> found;
            for (int page = 0; page < pages; ++page) {
                if (cancel->loadRelaxed())
                    return found; // abandoned: the page after this one is not worth reading
                found += TextSearch::matchesInPage(page, document->text(page), needle,
                                                   caseSensitive, wholeWord);
            }
            return found;
        });
    watcher->setFuture(task);
    searchTasks.append(task);
}

void SearchSession::applyResults(const QVector<TextSearch::Match> &matches)
{
    searchMatches = matches;
    currentMatchIndex = -1;

    // The first match at or after the page being read, which is where a reader
    // who searches expects to be taken: the next one from where they are, not
    // the first one in the document.
    const int page = pageView->currentPage();
    for (int index = 0; index < searchMatches.size(); ++index) {
        if (searchMatches.at(index).page >= page) {
            currentMatchIndex = index;
            break;
        }
    }
    if (currentMatchIndex < 0 && !searchMatches.isEmpty())
        currentMatchIndex = 0; // the hit is behind them: wrap round to the beginning

    searchPanel->setMatches(searchMatches, currentMatchIndex);
    updateHighlights();
    if (currentMatchIndex >= 0)
        pageView->showRange(rangeOf(currentMatchIndex));
}

PageTextRange SearchSession::rangeOf(int index) const
{
    if (index < 0 || index >= searchMatches.size())
        return {};
    const TextSearch::Match &match = searchMatches.at(index);
    return PageTextRange{match.page, match.from, match.to};
}

void SearchSession::updateHighlights()
{
    QVector<PageTextRange> ranges;
    ranges.reserve(searchMatches.size());
    for (int index = 0; index < searchMatches.size(); ++index)
        ranges.append(rangeOf(index));

    const PageTextRange current = currentMatchIndex >= 0 ? rangeOf(currentMatchIndex)
                                                        : PageTextRange();
    pageView->setSearchHighlights(ranges, current);

    // The statistics, said in the two places a search can be driven from. The
    // bar in the tab had a label for them from the day it was written, and
    // nothing ever filled it in: the count was only ever visible through the
    // tab's own API, which is not where a reader looks for it.
    if (findBar)
        findBar->setResultCount(currentMatchIndex, int(searchMatches.size()), running());
    // The row in front follows the match in front, whichever way it moved --
    // the bar's buttons, the window's keys, or a press on another row.
    searchPanel->setCurrentMatch(currentMatchIndex);
    Q_EMIT changed(currentMatchIndex, int(searchMatches.size()), running());
}

void SearchSession::step(int delta)
{
    if (searchMatches.isEmpty())
        return;

    const int count = int(searchMatches.size());
    if (currentMatchIndex < 0)
        currentMatchIndex = delta >= 0 ? 0 : count - 1;
    else
        // Adding count first so that stepping back from the first match lands on
        // the last one rather than on -1: the two ends of a document are next to
        // each other, which is what every reader does.
        currentMatchIndex = ((currentMatchIndex + delta) % count + count) % count;

    updateHighlights();
    pageView->showRange(rangeOf(currentMatchIndex));
}

void SearchSession::focus(int index)
{
    if (index < 0 || index >= searchMatches.size())
        return;
    // One way round for every route into a match: the index moves here, and
    // everything that reads it -- the page's highlight, the bar's count, the
    // list's own selected row -- is filled from updateHighlights().
    currentMatchIndex = index;
    updateHighlights();
    pageView->showRange(rangeOf(index));
}

void SearchSession::clear()
{
    if (searchCancel)
        searchCancel->storeRelaxed(1);
    searchMatches.clear();
    currentMatchIndex = -1;
    searchNeedle.clear();
    searchPanel->clearMatches();
    updateHighlights();
}

bool SearchSession::running() const
{
    for (const QFuture<QVector<TextSearch::Match>> &task : searchTasks) {
        if (!task.isFinished())
            return true;
    }
    return false;
}

void SearchSession::cancelPending()
{
    if (searchCancel)
        searchCancel->storeRelaxed(1);
}

void SearchSession::waitUntilFinished()
{
    // A scan reads through the document, and the document is about to go away.
    // Cancelled and waited for here rather than left to finish: a worker holding
    // a pointer to a document that has been freed is not a race that shows up
    // before it crashes.
    for (QFuture<QVector<TextSearch::Match>> &task : searchTasks)
        task.waitForFinished();
}
