#include "documenttab.h"

#include "src/core/appsettings.h"
#include "src/core/windowdialogs.h"
#include "src/document/pagemetrics.h"
#include "src/document/qtpdfbackend.h"
#include "src/export/imageexport.h"
#include "src/print/printjob.h"
#include "src/render/pagerenderer.h"
#include "src/theme/thememanager.h"
#include "src/translate/translator.h"
#include "src/ui/findbar.h"
#include "src/ui/imageexportdialog.h"
#include "src/ui/ocrdialog.h"
#include "src/ui/outlinepanel.h"
#include "src/ui/printdialog.h"
#include "src/ui/translatedialog.h"
#include "src/view/pageview.h"

#include <QApplication>
#include <QAtomicInt>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace {

// How much memory the view may keep rendered beyond what is on screen.
//
// A fraction of the cache's budget rather than a number of pages: the page is
// what varies (8 MB on one document, 50 on the next), and the cache budget is
// the number the user can actually change. An eighth leaves the prefetch
// modest next to the cache it exists to make hit, and drops to nothing on a
// document whose pages are big enough that one of them is worth more than the
// allowance -- which is the right answer: rendering a page nobody is looking at
// is not worth fifty megabytes.
qint64 prefetchAllowanceFor(qint64 cacheBytes)
{
    return qMax<qint64>(0, cacheBytes / 8);
}

// How many pixels to put on the recognition crop's longer side.
//
// Recognition quality lives on the height of the letters in pixels: the same
// page at 50% on screen is often too small to read reliably, and at 800% it is
// a waste. Sixteen hundred -- roughly 300 dpi for ordinary text -- is where the
// answers stop improving, so that is the target rather than either extreme.
constexpr double kOcrTargetPixels = 1600.0;

// The whole page is what gets rendered to produce a crop from, so the crop's
// scale is also capped where the whole page stays a *UI-thread-sized* job --
// this render is synchronous, on the thread that draws, and four million
// pixels is a few dozen milliseconds on this machine's documents. A tiny box on
// a huge page would otherwise ask for a fifty-thousand-pixel rendering to get
// its two hundred.
constexpr qint64 kOcrMaximumPagePixels = 4LL * 1024 * 1024;

// Below this, on the crop's longer side, the picture is brought up before it
// goes to the recognizer. The cap above can leave a small box at two hundred
// pixels, which reads badly; doubling costs nothing off-thread and is the
// difference between "nothing found" and the four words that were there.
constexpr int kOcrMinimumCropPixels = 480;

} // namespace

DocumentTab::DocumentTab(QWidget *parent)
    : QWidget(parent)
    , backend(std::make_unique<QtPdfBackend>())
    , renderer(std::make_unique<PageRenderer>())
{
    findBar = new FindBar(this);
    // Hidden until it is asked for: a reader who is not searching has no use for
    // a search box, and a strip of chrome above every page is what "which page
    // am I on" stops being obvious about.
    findBar->hide();

    pageView = new PageView(this);
    outlinePanel = new OutlinePanel(this);
    // The bookmarks sit beside the page, not above or below it: a reader follows
    // an outline while looking at the page it points into. Hidden until a
    // document turns out to have bookmarks -- a empty sidebar is a strip of
    // nothing taking a third of the window.
    outlinePanel->hide();
    auto *readingSplit = new QSplitter(Qt::Horizontal, this);
    readingSplit->addWidget(outlinePanel);
    readingSplit->addWidget(pageView);
    readingSplit->setStretchFactor(0, 0); // the sidebar keeps its own width
    readingSplit->setStretchFactor(1, 1); // the page takes whatever is left
    readingSplit->setCollapsible(1, false); // the page is never squeezed away

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(findBar);
    layout->addWidget(readingSplit);

    connect(outlinePanel, &OutlinePanel::jumpToPage, this, [this](int page) {
        pageView->goToPage(page);
    });

    connect(findBar, &FindBar::searchRequested, this, &DocumentTab::startSearch);
    connect(findBar, &FindBar::stepRequested, this, &DocumentTab::stepSearch);
    connect(findBar, &FindBar::closed, this, [this] {
        findBar->hide();
        // The highlights go with the bar: a page covered in amber with nothing
        // on screen to say what for is a page nobody can read.
        clearSearch();
        pageView->setFocus(Qt::OtherFocusReason);
    });

    // Fit width on opening, which is what a reader is read at. Not "fit page":
    // on a tall window that leaves a document at 60% with a grey band each side,
    // and every reader that has tried it has been asked to change it back.
    pageView->setZoomMode(ZoomController::Mode::FitWidth);

    // Every tab gets its own budget rather than sharing one: two open documents
    // then cost twice as much, which is the honest accounting -- and a shared
    // budget would have the two cache entries evicting each other.
    cache.setMaximumBytes(AppSettings::renderCacheBytes(RenderCache::DefaultMaximumBytes));
    pageView->setPrefetchBudget(prefetchAllowanceFor(cache.maximumBytes()));

    // The order of these two connections is the order the view emits them, and
    // it is load-bearing: the view invalidates before it asks for anything, so
    // the request that follows carries the generation this bump creates. See
    // the note on PageView::invalidated().
    connect(pageView, &PageView::invalidated, this, &DocumentTab::invalidateRenders);
    connect(pageView, &PageView::pageImageNeeded, this, &DocumentTab::onPageImageNeeded);
    connect(pageView, &PageView::currentPageChanged, this, [this](int page) {
        Q_EMIT pageChanged(page, pageCount());
    });
    connect(pageView, &PageView::zoomChanged, this, &DocumentTab::zoomChanged);
    connect(pageView, &PageView::rotationChanged, this, &DocumentTab::rotationChanged);
    connect(pageView, &PageView::selectionChanged, this, &DocumentTab::selectionChanged);
    connect(pageView, &PageView::regionCaptured, this, &DocumentTab::onRegionCaptured);
    connect(pageView, &PageView::copyRequested, this, &DocumentTab::copySelection);
    connect(pageView, &PageView::translateRequested, this, &DocumentTab::translateSelection);
    connect(pageView, &PageView::linkActivated, this, &DocumentTab::onLinkActivated);
    // "划线即翻": when the reader asked for it, finishing a selection translates
    // it without a second click. The default ("menu") does nothing here, so the
    // reader who only wants the right-click path is not interrupted.
    connect(pageView, &PageView::selectionFinished, this, [this] {
        if (AppSettings::translateTrigger() == QLatin1String("auto"))
            translateSelection();
    });
    connect(pageView, &PageView::toolChanged, this, &DocumentTab::toolChanged);
    connect(renderer.get(), &PageRenderer::rendered, this, &DocumentTab::onPageRendered);
    connect(renderer.get(), &PageRenderer::discarded, this, &DocumentTab::onDiscarded);

    applyThemeColors();
}

DocumentTab::~DocumentTab()
{
    // Where the reader left off, remembered for the next time this file opens.
    // Done first, while the document is still here to ask; a tab that never
    // opened (a failed load) has nothing worth remembering.
    if (opened) {
        AppSettings::DocumentSession session;
        session.page = currentPage();
        session.zoomMode = static_cast<int>(zoomMode());
        session.zoomPercent = zoom();
        session.layoutMode = static_cast<int>(layoutMode());
        AppSettings::saveDocumentSession(documentPath(), session);
    }

    // A scan reads through the backend, and the backend is about to be
    // destroyed with this object. Cancelled and waited for here rather than
    // left to finish: a worker holding a pointer to a document that has been
    // freed is not a race that shows up before it crashes.
    if (searchCancel)
        searchCancel->storeRelaxed(1);
    for (QFuture<QVector<TextSearch::Match>> &task : searchTasks)
        task.waitForFinished();

    // The export reads the same document by the same means, so it is cancelled
    // and waited for here for the same reason -- and it is the one that would
    // take longest to notice, because a document is a lot of pages to read.
    if (exportCancel)
        exportCancel->storeRelaxed(1);
    if (wordExportTask.isRunning())
        wordExportTask.waitForFinished();
}

void DocumentTab::startSearch(const QString &needle, bool caseSensitive)
{
    searchCase = caseSensitive;

    // What is being searched for, kept here rather than in either of the two
    // controls that can type it: this is the answer searchText() gives, and
    // both the bar's label and the toolbar's count are filled in from it.
    // Without this line the count has nothing to describe -- which is exactly
    // how the label came to be empty.
    searchNeedle = needle;

    // Whichever control the needle was typed into, the other one shows it. The
    // window's toolbar box and this tab's bar are two views of one search, and
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

    if (needle.isEmpty() || !opened) {
        updateSearchHighlights();
        return;
    }

    const auto cancel = QSharedPointer<QAtomicInt>::create(0);
    searchCancel = cancel;
    QtPdfBackend *backendPointer = backend.get();
    const int pages = backend->pageCount();

    updateSearchHighlights(); // "searching", with nothing found yet

    auto *watcher = new QFutureWatcher<QVector<TextSearch::Match>>(this);
    connect(watcher, &QFutureWatcher<QVector<TextSearch::Match>>::finished, this,
            [this, watcher, cancel] {
                const QVector<TextSearch::Match> found = watcher->result();
                watcher->deleteLater();
                // An answer to a question this tab has stopped asking. The flag
                // is compared rather than a counter incremented, because the
                // object identity is what the running scan was given.
                if (cancel != searchCancel)
                    return;
                applySearchResults(found);
            });

    QFuture<QVector<TextSearch::Match>> task =
        QtConcurrent::run([backendPointer, cancel, needle, caseSensitive, pages] {
            QVector<TextSearch::Match> found;
            for (int page = 0; page < pages; ++page) {
                if (cancel->loadRelaxed())
                    return found; // abandoned: the page after this one is not worth reading
                found += TextSearch::matchesInPage(page, backendPointer->text(page), needle,
                                                   caseSensitive);
            }
            return found;
        });
    watcher->setFuture(task);
    searchTasks.append(task);
}

void DocumentTab::applySearchResults(const QVector<TextSearch::Match> &matches)
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

    updateSearchHighlights();
    if (currentMatchIndex >= 0)
        pageView->showRange(rangeOf(currentMatchIndex));
}

PageTextRange DocumentTab::rangeOf(int index) const
{
    if (index < 0 || index >= searchMatches.size())
        return {};
    const TextSearch::Match &match = searchMatches.at(index);
    return PageTextRange{match.page, match.from, match.to};
}

void DocumentTab::updateSearchHighlights()
{
    QVector<PageTextRange> ranges;
    ranges.reserve(searchMatches.size());
    for (int index = 0; index < searchMatches.size(); ++index)
        ranges.append(rangeOf(index));

    const PageTextRange current = currentMatchIndex >= 0 ? rangeOf(currentMatchIndex)
                                                        : PageTextRange();
    pageView->setSearchHighlights(ranges, current);

    // The statistics, said in the two places a search can be driven from. The
    // bar in this tab had a label for them from the day it was written, and
    // nothing ever filled it in: the count was only ever visible through the
    // tab's own API, which is not where a reader looks for it.
    if (findBar)
        findBar->setResultCount(currentMatchIndex, int(searchMatches.size()), isSearching());
    Q_EMIT searchChanged(currentMatchIndex, int(searchMatches.size()), isSearching());
}

void DocumentTab::stepSearch(int delta)
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

    updateSearchHighlights();
    pageView->showRange(rangeOf(currentMatchIndex));
}

void DocumentTab::clearSearch()
{
    if (searchCancel)
        searchCancel->storeRelaxed(1);
    searchMatches.clear();
    currentMatchIndex = -1;
    searchNeedle.clear();
    updateSearchHighlights();
}

bool DocumentTab::isSearching() const
{
    return searchRunning();
}

bool DocumentTab::searchRunning() const
{
    for (const QFuture<QVector<TextSearch::Match>> &task : searchTasks) {
        if (!task.isFinished())
            return true;
    }
    return false;
}

void DocumentTab::exportToWord(const QString &path)
{
    if (!opened || path.isEmpty() || wordExportTask.isRunning())
        return;

    // A flag of its own, handed to the worker. When this tab's document is about
    // to be replaced -- open(), and the destructor -- it is set, and the export
    // stops at the page it is on rather than reading a document that is being
    // torn down under it. The same arrangement the search uses, for the same
    // reason; see drainRenders().
    exportCancel = QSharedPointer<QAtomicInt>::create(0);
    const QSharedPointer<QAtomicInt> cancel = exportCancel;

    auto *watcher = new QFutureWatcher<WordExportReport>(this);
    connect(watcher, &QFutureWatcher<WordExportReport>::finished, this, [this, watcher] {
        const WordExportReport report = watcher->result();
        watcher->deleteLater();
        Q_EMIT wordExportFinished(report);
    });

    WordExportOptions options;
    options.cancel = cancel;
    // Progress comes from the worker thread, so it is handed to this object's
    // thread rather than run where it is called: a page image is a render, a
    // scanned document is minutes of them, and the window has to keep painting
    // while they happen. Queued through a QPointer, because by the time the
    // message arrives the tab may be gone.
    QPointer<DocumentTab> alive(this);
    options.progress = [alive](int done, int total) {
        if (!alive)
            return;
        QMetaObject::invokeMethod(
            alive.data(),
            [alive, done, total] {
                if (alive)
                    alive->notifyExportProgress(done, total);
            },
            Qt::QueuedConnection);
    };

    // Qualified: this class has a method of the same name, which is the one the
    // window calls, and the free function is the one that does the reading.
    auto *document = backend.get();
    wordExportTask = QtConcurrent::run(
        [document, path, options] { return ::exportToWord(*document, path, options); });
    watcher->setFuture(wordExportTask);
}

void DocumentTab::notifyExportProgress(int done, int total)
{
    Q_EMIT wordExportProgress(done, total);
}

bool DocumentTab::isExporting() const
{
    return wordExportTask.isRunning();
}

void DocumentTab::cancelWordExport()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void DocumentTab::showFindBar()
{
    findBar->show();
    findBar->focusField();
}

void DocumentTab::applyThemeColors()
{
    const ThemeColors colors = ThemeManager::currentColors();
    // The page gets its own greys rather than the interface's: a white page
    // needs a surround and an edge chosen for it, and the interface's border
    // colour is tuned for controls sitting on a white surface, not for a page
    // sitting on a grey one.
    //
    // The selection wash is the accent, not the palette's selection colour: the
    // palette's is a pale blue meant to sit behind a whole row of text in a
    // list, and over black glyphs on white paper it is almost invisible.
    pageView->setColors(colors.pageBackdrop, colors.pagePlaceholder, colors.pageBorder,
                        colors.accent);
}

void DocumentTab::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // The theme is applied as a stylesheet on the application, which reaches
    // every widget that Qt can paint -- and not the page surface, which paints
    // itself. This is how it finds out.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        applyThemeColors();
}

void DocumentTab::drainRenders()
{
    // ---- why the reload waits for the renderer ----
    //
    // QPdfDocument::load() replaces what the document is, and a render that is
    // still running on the renderer's thread is inside the old one. Qt
    // serialises PDFium calls behind a single global lock and this is what fell
    // outside it: measured while this was being written, reopening a file with a
    // render in flight ended in a fail-fast inside the reload. It took a test
    // that opens documents as fast as it can to hit it -- a *view* render is
    // almost never still running when somebody opens the next file -- but
    // "almost never" on a 500-page scan is a reader who opens a second document
    // while the first is still painting.
    //
    // So the renders are waited for. They come back carrying a generation that
    // the load is about to supersede and are dropped, which is the point:
    // what has to be true is that the render *finished* before the document it
    // was rendering changed.
    //
    // The search scan is waited for as well, and for exactly the same reason: it
    // reads every page's text through the same backend, one call at a time, and
    // a page of it still running when the document is replaced is the same race
    // with a different name. It is cancelled first, so that it stops at the next
    // page boundary rather than reading to the end of a document nobody is
    // going to see.
    if (searchCancel)
        searchCancel->storeRelaxed(1);
    // And the export, which reads the document the same way a search does and is
    // waited for below on the same grounds. Told to stop here rather than left to
    // run out: a search of one page is quick, an export of five hundred is not.
    if (exportCancel)
        exportCancel->storeRelaxed(1);

    // Pumping the event loop is how the renders arrive -- they are queued
    // signals -- and it is bounded, because work that never comes back must not
    // make opening a file impossible.
    QElapsedTimer timer;
    timer.start();
    constexpr int kGiveUpAfter = 2000;
    while ((renderer->inFlight() > 0 || searchRunning() || isExporting())
           && timer.elapsed() < kGiveUpAfter) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

Document::Status DocumentTab::open(const QString &path, const QString &password)
{
    // A drop or another Open arriving from inside drainRenders' event loop must
    // not start a second open on top of this one.
    if (opening)
        return status;
    opening = true;
    drainRenders();

    const Document::Status result = backend->load(path, password);
    status = result;
    opening = false;

    if (result != Document::Status::Ready) {
        opened = false;
        clearSearch();
        // The view is emptied rather than left showing the previous document:
        // a page from another file under a tab named after this one is worse
        // than an empty surface -- and its text goes with it, so that a click
        // cannot select characters out of the document that used to be open.
        pageView->setDocument(nullptr);
        pageView->setRotation(0);
        pageView->clearDocument();
        outlinePanel->setOutline({});
        applySidebarVisibility();
        Q_EMIT titleChanged(title());
        Q_EMIT pageChanged(0, 0);
        return result;
    }

    // Whatever was being searched for belonged to the document that has just
    // been replaced: the matches are character indices into its text.
    clearSearch();

    // The renderer is pointed at the new document first, because that is what
    // bumps the generation -- and the page sizes below make the view ask for
    // images immediately, synchronously, inside setPageSizes.
    renderer->setDocument(backend->pdfDocument());
    // And the view is given the document to ask its own questions of: which
    // character is under this point, and what is in this range. Borrowed only,
    // and only for text -- images still come through this class, which is what
    // keeps them off the thread that draws.
    pageView->setDocument(backend.get());
    // A new document's pages have nothing to do with the previous one's, and
    // they are keyed by page number and size, so they would be found and drawn.
    cache.clear();
    pageView->setRotation(0);

    QVector<QSizeF> sizes;
    const int count = backend->pageCount();
    sizes.reserve(count);
    for (int page = 0; page < count; ++page)
        sizes.append(backend->pageSize(page));

    // Before the sizes, not after. setPageSizes() lays the document out and
    // asks for the pages in view *synchronously*, and onPageImageNeeded refuses
    // to do anything while this tab is not open -- so setting this afterwards
    // threw away every request opening the document had just made. The view
    // recorded them as on their way and never asked again, which left the first
    // page a grey placeholder until something else happened to reschedule it.
    // documenttab_smoke catches it by opening a document and waiting for it.
    opened = true;

    pageView->setPageSizes(sizes);

    // The bookmarks, filled in once the document is known to be open. A document
    // with none leaves the panel empty and it stays hidden whatever the reader
    // asked for -- there is nothing to show.
    outlinePanel->setOutline(backend->outline());
    applySidebarVisibility();

    Q_EMIT titleChanged(title());
    Q_EMIT pageChanged(pageView->currentPage(), count);
    Q_EMIT cacheChanged(cache.usedBytes(), cache.maximumBytes());
    return result;
}

QString DocumentTab::lastError() const
{
    return backend->lastError();
}

QString DocumentTab::documentPath() const
{
    return backend->path();
}

QString DocumentTab::title() const
{
    const QString path = backend->path();
    if (path.isEmpty())
        return tr("（空标签）");
    return QFileInfo(path).fileName();
}

int DocumentTab::pageCount() const
{
    return backend->pageCount();
}

int DocumentTab::currentPage() const
{
    return pageView->currentPage();
}

void DocumentTab::setCurrentPage(int page)
{
    pageView->goToPage(page);
}

double DocumentTab::zoom() const
{
    return pageView->zoom();
}

void DocumentTab::setZoom(double zoom)
{
    pageView->setZoom(zoom);
}

void DocumentTab::setZoomMode(ZoomController::Mode mode)
{
    pageView->setZoomMode(mode);
}

ZoomController::Mode DocumentTab::zoomMode() const
{
    return pageView->zoomMode();
}

void DocumentTab::rotateBy(int degrees)
{
    pageView->setRotation(pageView->rotation() + degrees);
}

int DocumentTab::rotation() const
{
    return pageView->rotation();
}

void DocumentTab::setLayoutMode(PageLayout::Mode mode)
{
    pageView->setLayoutMode(mode);
}

PageLayout::Mode DocumentTab::layoutMode() const
{
    return pageView->layoutMode();
}

void DocumentTab::setTool(PageView::Tool tool)
{
    pageView->setTool(tool);
}

PageView::Tool DocumentTab::tool() const
{
    return pageView->tool();
}

void DocumentTab::setCacheLimit(qint64 bytes)
{
    cache.setMaximumBytes(bytes);
    // The prefetch allowance is a fraction of this, so it moves with it: a user
    // who lowers the budget expects the memory to come back, and prefetching is
    // memory.
    pageView->setPrefetchBudget(prefetchAllowanceFor(cache.maximumBytes()));
    Q_EMIT cacheChanged(cache.usedBytes(), cache.maximumBytes());
}

bool DocumentTab::isBusy() const
{
    return renderer->inFlight() > 0;
}

void DocumentTab::invalidateRenders()
{
    if (renderer->document())
        renderer->beginGeneration();
}

void DocumentTab::onPageImageNeeded(const RenderToken &requested)
{
    if (!opened)
        return;

    // The view asks in terms of the page; the generation is filled in here,
    // which is what keeps the view from having to know about generations at
    // all. Everything else in the token -- which page, which band of it, at
    // what size, which way up -- comes from the view, because it is the only
    // thing that knows what is on screen.
    RenderToken token = requested;
    token.generation = renderer->generation();

    // The cache first. Equality ignores the generation, so a page rendered
    // before a zoom change and asked for again at the same size is found here
    // -- which is what makes zooming out and back in instant.
    const QImage cached = cache.image(token);
    if (!cached.isNull()) {
        pageView->setPageImage(token, cached);
        return;
    }
    renderer->request(token);
}

void DocumentTab::onPageRendered(const RenderToken &token, const QImage &image)
{
    // Checked again here even though the renderer drops what it can tell is
    // stale: the renderer's generation is bumped by this object, and between
    // the two there is a window -- small, and not zero -- in which the tab knows
    // something the renderer does not. This is the last word, and the plan asks
    // for exactly one: a stale image never reaches the screen.
    if (!token.isCurrent(renderer->generation()))
        return;

    // The cache keeps only what came back, and only when it is worth keeping:
    // an image that produced nothing is not a result to remember, and the view
    // has to hear about it so that the page is asked for again.
    if (!image.isNull())
        cache.insert(token, image);
    if (opened && token.page < pageCount())
        pageView->setPageImage(token, image);
    Q_EMIT cacheChanged(cache.usedBytes(), cache.maximumBytes());
}

void DocumentTab::selectAll()
{
    pageView->selectAll();
}

void DocumentTab::clearSelection()
{
    pageView->clearSelection();
}

bool DocumentTab::hasSelection() const
{
    return pageView->hasSelection();
}

QString DocumentTab::selectedText() const
{
    return pageView->selectedText();
}

void DocumentTab::copySelection()
{
    const QString text = selectedText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
}

void DocumentTab::onDiscarded(const RenderToken &token)
{
    Q_UNUSED(token)
    ++discardedCount;
}

void DocumentTab::onRegionCaptured(int page, QRectF points)
{
    if (!opened || page < 0 || page >= backend->pageCount())
        return;
    const QSizeF pagePoints = backend->pageSize(page);
    if (pagePoints.isEmpty() || points.isEmpty())
        return;

    // The crop is rendered at a size of its own rather than the size it
    // happens to be on screen: the user may have zoomed out to see the page
    // they are cropping, and a crop that arrives at 40% reads badly no matter
    // how good the recognizer is.
    double scale = kOcrTargetPixels / qMax(points.width(), points.height());
    // The whole page is what gets rendered to cut the crop from, so the scale
    // is also capped where the whole page stays a UI-thread-sized job -- which
    // is what keeps a tiny box on a huge page from asking for a
    // fifty-thousand-pixel rendering to get its two hundred.
    const double pageCap =
        std::sqrt(double(kOcrMaximumPagePixels))
        / std::sqrt(double(pagePoints.width()) * double(pagePoints.height()));
    scale = qBound(1.0, scale, pageCap);

    const int rotation = pageView->rotation();
    const QSize full = PageMetrics::pixelsFor(PageMetrics::rotated(pagePoints, rotation), scale,
                                              1.0);
    // The box is in the page's own coordinates; the image it will be cut from
    // is the page the way it is drawn, which is a quarter turn away when the
    // reader has rotated it. One formula, shared with the view -- which is the
    // reason it lives in PageMetrics rather than here as a copy.
    const QRectF drawn = PageMetrics::rotatedRect(points, pagePoints, rotation);
    QRect crop = QRect(int(std::floor(drawn.left() * scale)),
                       int(std::floor(drawn.top() * scale)),
                       int(std::ceil(drawn.width() * scale)),
                       int(std::ceil(drawn.height() * scale)));
    crop = crop.intersected(QRect(QPoint(0, 0), full));
    if (crop.isEmpty())
        return;

    // The crop is rendered here, synchronously, and NOT through PageRenderer.
    // That is a decision with a story: an asynchronous crop render on the
    // renderer's threads, still in flight while the user reopens the document,
    // ended in a fail-fast inside the reload -- reproducible at will in the
    // integration test, and never seen before because a *view* render is never
    // still running when the next document loads. Rendering on this thread
    // makes the overlap impossible by construction: nothing that touches the
    // document runs concurrently with anything else that does. The cost is a
    // synchronous render of a few dozen milliseconds, capped by the pixel
    // budget above, paid at the moment the user releases a box they dragged --
    // the slow part, the recognition, still runs off this thread.
    RenderOptions options;
    options.rotationDegrees = rotation;
    options.annotations = true;
    options.clip = crop;
    options.scaledSize = full;
    const QImage cropImage = backend->render(page, crop.size(), options);
    if (cropImage.isNull())
        return;

    // The translate tool boxes a picture to translate it, so it goes straight
    // from the pixels to the translation window -- no intermediate recognition
    // result to click through. The recognize tool still shows what it read.
    if (pageView->tool() == PageView::Tool::Translate) {
        startRecognition(cropImage, nullptr, /*thenTranslate=*/true);
        return;
    }

    OcrDialog *dialog = new OcrDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // The recognized words can be translated from this window -- the path that
    // reads a scan or a picture into another language.
    connect(dialog, &OcrDialog::translateRequested, this, &DocumentTab::translateText);
    dialog->show();
    startRecognition(cropImage, dialog);
}

void DocumentTab::startRecognition(const QImage &crop, QPointer<OcrDialog> dialog,
                                   bool thenTranslate)
{
    // The crop is copied into the worker by value, so the view can go on
    // scrolling, zooming, even closing this tab while it runs -- nothing in
    // the task touches this object. The dialog is watched with a QPointer:
    // the user is free to close it, and a result nobody is waiting for is
    // dropped rather than kept alive to be shown into nothing.
    auto *watcher = new QFutureWatcher<Ocr::Result>(this);
    connect(watcher, &QFutureWatcher<Ocr::Result>::finished, this, [this, watcher, dialog, thenTranslate] {
        const Ocr::Result result = watcher->result();
        watcher->deleteLater();
        if (dialog)
            dialog->setResult(result);
        // Translate the recognized words when the reader either chose the
        // translate tool (thenTranslate) or turned on "划线即翻" for the recognize
        // tool. Only when there was something to read.
        const bool wantTranslate =
            thenTranslate || AppSettings::translateTrigger() == QLatin1String("auto");
        if (wantTranslate && result.ok && !result.text.trimmed().isEmpty())
            translateText(result.text);
    });
    watcher->setFuture(QtConcurrent::run([crop] {
        // Onto white paper first, and this is the whole trick. A PDF page has no
        // background: what the renderer hands back is the ink at full alpha over
        // transparent *black*, so the crop is a black rectangle with black text
        // on it, and the recognizer reads exactly nothing from it -- which is
        // what the first version of this feature did, on every picture, while
        // looking correct in every debug print. The view flattens a page the
        // same way before drawing it; this is the same step, for the same
        // reason.
        QImage input(crop.size(), QImage::Format_RGB32);
        input.fill(Qt::white);
        {
            QPainter painter(&input);
            painter.drawImage(0, 0, crop);
        }

        // A crop that came out small -- a few words boxed on a page whose
        // scale was capped -- is brought up before recognition. Doubling costs
        // nothing here, off the thread that draws, and is the difference
        // between "nothing found" and the words that were there.
        const int longerSide = qMax(input.width(), input.height());
        if (longerSide > 0 && longerSide < kOcrMinimumCropPixels) {
            input = input.scaled(longerSide * 2, longerSide * 2, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }
        return Ocr::recognize(input);
    }));
}

void DocumentTab::translateSelection()
{
    translateText(selectedText().trimmed());
}

void DocumentTab::translateText(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return; // the action is disabled without text; a shortcut is not

    // Shown before the request goes out, so the reader sees the words they asked
    // about immediately and a "translating…" line while the round trip happens --
    // the request itself runs off this thread, below.
    auto *dialog = new TranslateDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setPending(text);
    dialog->show();
    startTranslation(text, dialog);
}

void DocumentTab::setSidebarVisible(bool visible)
{
    sidebarWanted = visible;
    applySidebarVisibility();
}

void DocumentTab::applySidebarVisibility()
{
    outlinePanel->setVisible(sidebarWanted && outlinePanel->hasOutline());
}

bool DocumentTab::hasOutline() const
{
    return outlinePanel->hasOutline();
}

void DocumentTab::onLinkActivated(const Link &link)
{
    if (!link.url.isEmpty()) {
        // Handing an address that came out of a document to the browser is asked
        // about first (docs/PLAN.md M2): the URL may be longer than the words it
        // was drawn under, and once it opens, it has left this program.
        const QMessageBox::StandardButton answer = WindowDialogs::question(
            this, tr("打开链接"),
            tr("要在浏览器中打开这个地址：\n%1\n\n继续吗？").arg(link.url.toString()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            QDesktopServices::openUrl(link.url);
        return;
    }
    if (link.targetPage >= 0 && link.targetPage < pageCount())
        pageView->goToPage(link.targetPage);
}

void DocumentTab::print()
{
    if (!opened)
        return;

    QPrinter printer(QPrinter::HighResolution);
    PrintDialog dialog(window(), &printer, pageView->currentPage(), pageCount());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Print::Settings settings = dialog.settings();

    // A cancel-able progress dialog over a foreground job. Print::run calls the
    // callback once per sheet on this thread; pumping events here is what lets
    // the dialog repaint and its Cancel button be noticed between sheets.
    auto cancel = QSharedPointer<QAtomicInt>::create(0);
    QProgressDialog progress(tr("正在打印…"), tr("取消"), 0, 1, window());
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoReset(false);
    progress.setValue(0);

    const Print::Report report = Print::run(
        *backend, printer, settings,
        [&progress, &cancel](int done, int total) {
            progress.setMaximum(total);
            progress.setValue(done);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancel->storeRelaxed(1);
        },
        cancel.data());

    if (!report.ok && !report.cancelled)
        WindowDialogs::warning(window(), tr("打印"), report.error);
}

void DocumentTab::exportImages()
{
    if (!opened)
        return;

    ImageExportDialog dialog(window(), pageView->currentPage(), pageCount());
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString directory = dialog.directory();
    if (directory.isEmpty()) {
        WindowDialogs::warning(window(), tr("导出为图片"), tr("请先选择保存文件夹。"));
        return;
    }
    const QVector<int> pages = dialog.pages();
    if (pages.isEmpty()) {
        WindowDialogs::warning(window(), tr("导出为图片"), tr("页码范围里没有可导出的页面。"));
        return;
    }
    const QString base = QFileInfo(documentPath()).completeBaseName();

    // Foreground, like printing: each page is a render under the same lock the
    // view uses, and pumping events in the callback is what keeps the progress
    // box live and its Cancel button responsive between pages.
    auto cancel = QSharedPointer<QAtomicInt>::create(0);
    QProgressDialog progress(tr("正在导出为图片…"), tr("取消"), 0, pages.size(), window());
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoReset(false);
    progress.setValue(0);

    const ImageExport::Report report = ImageExport::run(
        *backend, pages, directory, base, dialog.format(), dialog.dpi(),
        [&progress, &cancel](int done, int total) {
            progress.setMaximum(total);
            progress.setValue(done);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancel->storeRelaxed(1);
        },
        cancel.data());

    if (report.cancelled)
        return;
    if (!report.ok) {
        WindowDialogs::warning(window(), tr("导出为图片"), report.error);
        return;
    }
    AppSettings::setLastOpenDirectory(directory);
    Q_EMIT imageExportFinished(report.directory, report.pages);
}

void DocumentTab::startTranslation(const QString &text, QPointer<TranslateDialog> dialog)
{
    // The same shape as a recognition: the text is copied into the worker, the
    // window is watched with a QPointer so a result nobody is waiting for is
    // dropped, and nothing in the task touches this object or the document -- so
    // a reload under a running translation cannot crash the way a render can.
    auto *watcher = new QFutureWatcher<Translate::Result>(this);
    connect(watcher, &QFutureWatcher<Translate::Result>::finished, this, [watcher, dialog] {
        const Translate::Result result = watcher->result();
        watcher->deleteLater();
        if (dialog)
            dialog->setResult(result);
    });
    watcher->setFuture(QtConcurrent::run([text] {
        const std::unique_ptr<Translate::Translator> translator = Translate::makeTranslator();
        return translator->translate(text);
    }));
}
