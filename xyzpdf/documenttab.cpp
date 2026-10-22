#include "documenttab.h"

#include "src/core/appsettings.h"
#include "src/core/windowdialogs.h"
#include "src/document/pagemetrics.h"
#include "src/document/qtpdfbackend.h"
#include "src/edit/documenteditor.h"
#include "src/export/imageexport.h"
#include "src/print/printjob.h"
#include "src/render/pagerenderer.h"
#include "src/theme/thememanager.h"
#include "src/translate/translator.h"
#include "src/ui/bookmarkdialog.h"
#include "src/ui/deletepagesdialog.h"
#include "src/ui/bookmarkdialog.h"
#include "src/ui/findbar.h"
#include "src/ui/imageexportdialog.h"
#include "src/ui/mergepagesdialog.h"
#include "src/ui/movepagesdialog.h"
#include "src/ui/ocrdialog.h"
#include "src/ui/outlinepanel.h"
#include "src/ui/printdialog.h"
#include "src/ui/propertiesdialog.h"
#include "src/ui/protectpagesdialog.h"
#include "src/ui/rotatepagesdialog.h"
#include "src/ui/splitpagesdialog.h"
#include "src/ui/thumbnailpanel.h"
#include "src/ui/translatedialog.h"
#include "src/ui/watermarkdialog.h"
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
#include <QTabWidget>
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
    thumbnailPanel = new ThumbnailPanel(this);

    // The reading sidebar is one panel with two tabs -- the document's bookmarks
    // and a column of page thumbnails -- because both are "get me to a page", and
    // a reader switches between them, not looks at both at once. It sits beside
    // the page, hidden until opened (see sidebarWanted).
    sidebar = new QTabWidget(this);
    sidebar->setTabPosition(QTabWidget::North);
    sidebar->addTab(outlinePanel, tr("大纲"));
    sidebar->addTab(thumbnailPanel, tr("缩略图"));
    sidebar->hide();

    auto *readingSplit = new QSplitter(Qt::Horizontal, this);
    readingSplit->addWidget(sidebar);
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
    connect(thumbnailPanel, &ThumbnailPanel::jumpToPage, this, [this](int page) {
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
        // Keep the thumbnail strip's highlight in step with the page in view.
        thumbnailPanel->setCurrentPage(page);
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

    // The thumbnails render through the same backend on their own worker, so the
    // same wait applies: it must finish before the document it is reading is
    // destroyed with this object.
    if (thumbnailPanel)
        thumbnailPanel->cancelAndDrain();
}

void DocumentTab::startSearch(const QString &needle, bool caseSensitive, bool wholeWord)
{
    searchCase = caseSensitive;
    searchWhole = wholeWord;

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
        QtConcurrent::run([backendPointer, cancel, needle, caseSensitive, wholeWord, pages] {
            QVector<TextSearch::Match> found;
            for (int page = 0; page < pages; ++page) {
                if (cancel->loadRelaxed())
                    return found; // abandoned: the page after this one is not worth reading
                found += TextSearch::matchesInPage(page, backendPointer->text(page), needle,
                                                   caseSensitive, wholeWord);
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
    QColor backdrop = colors.pageBackdrop;
    QColor placeholder = colors.pagePlaceholder;
    QColor edge = colors.pageBorder;
    if (pageView->nightInverted()) {
        // Night reading inverts the paper in the paint pass, which leaves the
        // surround and the placeholder alone -- and a near-black page floating
        // on a light grey field is exactly what makes the mode look broken. The
        // placeholder is "paper not yet drawn", so it has to match the paper.
        backdrop = QColor(28, 29, 32);
        placeholder = QColor(52, 54, 58);
        edge = QColor(74, 76, 82);
    }
    pageView->setColors(backdrop, placeholder, edge, colors.accent);
}

void DocumentTab::setNightInverted(bool inverted)
{
    if (pageView->nightInverted() == inverted)
        return;

    // The cache holds images, and an image of a page is now a different image.
    // Cleared before the view is told, because telling it re-asks for the pages in
    // view -- and a re-ask that found the old answer still in the cache would
    // paint the colours the reader has just turned away from.
    cache.clear();
    pageView->setNightInverted(inverted);
    applyThemeColors();
    Q_EMIT cacheChanged(cache.usedBytes(), cache.maximumBytes());
}

bool DocumentTab::nightInverted() const
{
    return pageView->nightInverted();
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
    // And the thumbnails, which render through the same backend on their own
    // worker; cancelled here and waited for below, so a reload cannot free the
    // document out from under a render still reading it.
    if (thumbnailPanel)
        thumbnailPanel->cancel();
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
    while ((renderer->inFlight() > 0 || searchRunning() || isExporting()
            || thumbnailPanel->rendering())
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
    // A tab is one document at a time, and an undo list belongs to the document,
    // not to the tab: operations recorded against pages of the file that used to be
    // here would be replayed onto the new one.
    if (editor) {
        editor.reset();
        Q_EMIT editStateChanged();
    }

    const Document::Status result = backend->load(path, password);
    status = result;
    opening = false;

    if (result != Document::Status::Ready) {
        opened = false;
        documentPassword.clear();
        formFieldsInSource = false;
        clearSearch();
        // The view is emptied rather than left showing the previous document:
        // a page from another file under a tab named after this one is worse
        // than an empty surface -- and its text goes with it, so that a click
        // cannot select characters out of the document that used to be open.
        pageView->setDocument(nullptr);
        pageView->setRotation(0);
        pageView->clearDocument();
        outlinePanel->setOutline({});
        thumbnailPanel->start({}, nullptr);
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
    // Only ever the password that produced a Ready document, and it goes back to
    // empty with the next failed open above.
    documentPassword = password;

    pageView->setPageSizes(sizes);

    // The reading panel's two tabs, filled once the document is known to be open:
    // its bookmarks, and a background pass that renders every page's thumbnail.
    outlinePanel->setOutline(backend->outline());
    thumbnailPanel->start(sizes, backend.get());

    // Does this file have form fields? Asked here, once, through a qpdf that is
    // opened and dropped again rather than kept: holding a second handle on the
    // reader's file for the life of the tab is what the editor does only once
    // somebody edits, and this is a question, not an edit. The cost was measured
    // before it was put on this path -- 0 ms on the fixtures, 22 ms on a
    // 600-page file. A file qpdf cannot read is not a form: the reader is
    // already looking at it through Qt PDF, and a second opinion that disagrees
    // is not worth a sentence.
    formFieldsInSource = false;
    {
        DocumentEditor probe;
        QString ignored;
        if (probe.open(path, password, &ignored))
            formFieldsInSource = probe.hasFormFields();
    }
    // Open on whichever tab has something to lead with -- bookmarks if the file
    // has them, otherwise the thumbnails, which it always does.
    sidebar->setCurrentIndex(outlinePanel->hasOutline() ? 0 : 1);
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
    // The star is the whole of the unsaved-edits warning: it has to be visible
    // while the reader is still looking at the tab, which is the only moment at
    // which "don't close that yet" is of any use to them.
    return hasUnsavedEdits() ? QFileInfo(path).fileName() + QStringLiteral(" *")
                             : QFileInfo(path).fileName();
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

    // Night reading: the inversion belongs to the image, not to the paint, so it
    // happens once here and everything after it -- the cache, the view, the count
    // of bytes held -- sees the page as it will be shown.
    //
    // InvertRgb rather than the default, because these images carry an alpha
    // channel and inverting that would flip the transparency along with the ink.
    QImage shown = image;
    if (!shown.isNull() && pageView->nightInverted())
        shown.invertPixels(QImage::InvertRgb);

    // The cache keeps only what came back, and only when it is worth keeping:
    // an image that produced nothing is not a result to remember, and the view
    // has to hear about it so that the page is asked for again.
    if (!shown.isNull())
        cache.insert(token, shown);
    if (opened && token.page < pageCount())
        pageView->setPageImage(token, shown);
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
    // The pixels go in before the recognition answer can, so that "复制图片" is
    // available even if the engine finds nothing to read -- which for a chart or
    // a photograph is the usual outcome, and not a reason to withhold the picture.
    // It is handed the screen's own colours, because that is the picture the
    // reader chose. Recognition still gets `cropImage` as the renderer produced
    // it: dark-on-light is what the engine expects, and inverting it to match a
    // night screen would be a way to make the words harder to read for the one
    // consumer that has no eyes.
    dialog->setCropImage(cropImage, pageView->nightInverted());
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
    // The sidebar is worth showing for any open document now that it holds
    // thumbnails as well as bookmarks -- a page always has a picture, even when
    // the file has no bookmarks. So the only conditions are "the reader asked"
    // and "there is a document".
    sidebar->setVisible(sidebarWanted && opened);
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

    // From here to the end of the method this tab must not be taken apart: the
    // settings dialog and the progress box both pump the event loop, and a
    // 关闭标签 landing in either one would delete the object this call is
    // standing in. See DocumentTab::ForegroundJob.
    const ForegroundJob job(this);

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

    // The same reason printing holds one: two dialogs that pump the loop, and
    // a render loop between them that does it once per page.
    const ForegroundJob job(this);

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

DocumentEditor *DocumentTab::editSession()
{
    if (!opened)
        return nullptr;
    if (!editor) {
        auto session = std::make_unique<DocumentEditor>();
        QString error;
        // The reader is looking at this document through the renderer right now,
        // so a refusal here cannot be "the file is broken" -- it is "what you can
        // read, this program cannot write", which is what the sentence says.
        if (!session->open(documentPath(), documentPassword, &error)) {
            WindowDialogs::warning(window(), tr("修改这份文档"), error);
            return nullptr;
        }
        editor = std::move(session);
    }
    return editor.get();
}

PropertiesDialog::Info DocumentTab::propertiesInfo() const
{
    PropertiesDialog::Info info;
    if (!opened)
        return info;
    info.path = documentPath();
    info.fileBytes = QFileInfo(info.path).size();
    info.pageCount = pageCount();
    // The page being read, not page 1: a PDF may mix page sizes, and the size a
    // reader wants to know is the size of what is in front of them.
    info.sizedPage = pageView->currentPage();
    info.pagePoints = backend->pageSize(info.sizedPage);
    info.metadata = backend->metadata();
    return info;
}

void DocumentTab::showProperties()
{
    if (!opened)
        return;
    PropertiesDialog dialog(window(), propertiesInfo());
    dialog.exec();
}

void DocumentTab::deletePages()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    DeletePagesDialog dialog(window(), pageView->currentPage(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!document->deletePages(dialog.pagesToDelete(), &error)) {
        // The dialog ran the same arithmetic on the same editor, so this is the
        // case where the two disagree -- and the editor's answer is the one that
        // stands, since it is what a save would replay.
        WindowDialogs::warning(window(), tr("删除页面"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::rotatePages()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    RotatePagesDialog dialog(window(), pageView->currentPage(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!document->rotatePages(dialog.pagesToRotate(), dialog.quarterTurns(), &error)) {
        WindowDialogs::warning(window(), tr("旋转页面"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::movePages()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    MovePagesDialog dialog(window(), pageView->currentPage(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!document->movePages(dialog.pagesToMove(), dialog.target(), dialog.anchorPage(),
                             &error)) {
        WindowDialogs::warning(window(), tr("移动页面"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::mergeFiles()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    MergePagesDialog dialog(window(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // One operation per row rather than one for the whole list, because 撤销 then
    // takes back a single file's worth -- which is what a reader who merged three
    // and liked two of them asks for. The dialog hands back the rows in the order
    // they have to be recorded in, so the sequence the summary described and the
    // sequence a save replays are read off the same list.
    const QVector<MergePagesDialog::Entry> rows = dialog.mergesToRecord();
    QString error;
    int recorded = 0;
    for (const MergePagesDialog::Entry &entry : rows) {
        if (!document->mergeFile(entry.path, entry.pages, dialog.target(),
                                 dialog.anchorPage(), &error))
            break;
        ++recorded;
    }
    if (recorded > 0)
        Q_EMIT editStateChanged();
    if (recorded < rows.size()) {
        // The dialog asked the same question of these files when the reader picked
        // them, so a refusal now means one of them moved in between -- it is
        // closing the box that is stale, not the picking. What did go in stays in:
        // taking rows back behind a reader's back would leave the list disagreeing
        // with the one sentence they just read, and 撤销 is the way to undo it.
        if (recorded > 0)
            error += tr("\n\n列表里前面 %n 个文件已经记下了，要退回去用撤销。", nullptr, recorded);
        WindowDialogs::warning(window(), tr("合并文件进来"), error);
    }
}

void DocumentTab::splitPages()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    if (document->pageCount() < 2) {
        // Said here rather than in the box: a dialog whose only control has one legal
        // value is not a question, and 拆分 of a one-page document is the one case where
        // the reader could not have meant it. The merge guard, by contrast, is the
        // dialog's to say -- it has the page ranges on screen while it is open.
        WindowDialogs::warning(window(), tr("拆分文件"),
                               tr("这份要写出的文档只有 %n 页，拆不出第二个文件来。",
                                  nullptr, document->pageCount()));
        return;
    }

    SplitPagesDialog dialog(window(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!document->splitInto(dialog.pieceSize(), &error)) {
        WindowDialogs::warning(window(), tr("拆分文件"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::protectPages()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    ProtectPagesDialog dialog(window(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // One dialog, two calls, because the two directions differ in only one thing: taking
    // the protection off has no parameters to hand over. Everything else -- which boxes are
    // showing, whether the request is the answer a save would already give -- is the
    // editor's to decide, off the same struct the dialog filled in.
    const DocumentEditor::Protection asked = dialog.request();
    QString error;
    const bool done = asked.what == DocumentEditor::Protection::What::Decrypt
                          ? document->removeProtection(&error)
                          : document->protectWith(asked, &error);
    if (!done) {
        WindowDialogs::warning(window(), tr("加密 / 去掉口令"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::addWatermark()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    WatermarkDialog dialog(window(), document);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // The dialog filled the struct it hands back, so the sentence the reader just read and
    // the request the list holds cannot be two different answers -- and the same refusal this
    // call makes behind the dialog's back is the one the smoke test checks.
    QString error;
    if (!document->addWatermark(dialog.request(), &error)) {
        WindowDialogs::warning(window(), tr("加水印"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::editOutline()
{
    DocumentEditor *document = editSession();
    if (!document)
        return;

    // The tree the sidebar is showing right now, which is the file's own bookmarks: what the box opens
    // on when nothing has been asked for yet, and what "you have not changed anything" is measured
    // against. Read from the renderer rather than handed to the editor, because the editor
    // deliberately never looks at a /Outlines it was not asked to replace -- and §70 pinned what it
    // then does with the tree it inherits (all of it, including a bookmark whose page has gone).
    BookmarkDialog dialog(window(), document, backend->outline());
    if (dialog.exec() != QDialog::Accepted)
        return;

    // The dialog's summary and this call read the same list, so the sentence the reader just agreed to
    // and the operation the save replays cannot be two answers about which bookmark points where --
    // and the model's own refusals (a level of 0, a page past the file) are the backstop behind a box
    // that greys 确定 for both.
    QString error;
    if (!document->setOutline(dialog.rows(), &error)) {
        WindowDialogs::warning(window(), tr("编辑书签"), error);
        return;
    }
    Q_EMIT editStateChanged();
}

void DocumentTab::undoEdit()
{
    if (editor && editor->undo())
        Q_EMIT editStateChanged();
}

void DocumentTab::redoEdit()
{
    if (editor && editor->redo())
        Q_EMIT editStateChanged();
}

bool DocumentTab::canUndoEdits() const
{
    return editor && editor->canUndo();
}

bool DocumentTab::canRedoEdits() const
{
    return editor && editor->canRedo();
}

QString DocumentTab::undoEditName() const
{
    return editor ? editor->undoDescription() : QString();
}

QString DocumentTab::redoEditName() const
{
    return editor ? editor->redoDescription() : QString();
}

bool DocumentTab::hasUnsavedEdits() const
{
    return editor && editor->isDirty();
}

QString DocumentTab::pendingEditsSummary() const
{
    return editor ? editor->pendingSummary() : QString();
}

bool DocumentTab::saveEdits()
{
    if (!editor || !editor->isDirty())
        return false;

    // Seeded with the source document's own name plus a suffix, in the folder it
    // came from -- the same shape the Word export uses. The name is part of the
    // interface, so it goes through the catalogue: what a reader should see in a
    // save box is a word in their own language, not one in somebody else's.
    const QFileInfo source(documentPath());
    QString suggested = tr("%1 已修改.pdf").arg(source.completeBaseName());
    if (!source.absolutePath().isEmpty())
        suggested = source.absolutePath() + QLatin1Char('/') + suggested;

    const QString destination = WindowDialogs::getSaveFileName(
        window(), tr("把这些修改存成一个新文件"), suggested, tr("PDF 文档 (*.pdf)"));
    if (destination.isEmpty())
        return false;

    // Synchronous, with the wait cursor over it, rather than off the thread that
    // draws: QPDF reads the file's structure and copies its streams, and neither
    // needs a renderer, a lock shared with one, or a progress report -- there is
    // one page-tree to read and one file to write, so the dialog this would go in
    // could only spin. docs/store-submission.md §10 has the measured cost.
    const QString changes = editor->pendingSummary();
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList written;
    const bool done = editor->saveAs(destination, &error, &written);
    QApplication::restoreOverrideCursor();

    if (!done) {
        WindowDialogs::warning(window(), tr("保存修改"), error);
        return false;
    }
    AppSettings::setLastOpenDirectory(QFileInfo(destination).absolutePath());
    // The list is still pending, so the star stays on this tab and 撤销 still has
    // something to take back: what was written is a copy of the source with the
    // operations played onto it, not the document being read.
    Q_EMIT editStateChanged();
    // The name to open comes from the save rather than from the box: under a split the
    // typed name is not a file that exists, and the first piece is. One tab opens even
    // when forty came out, because the check a reader makes on a split set is whether
    // the first file is the one they meant -- and the status line names the count so
    // they know how many are in the folder before going to look.
    //
    // The password rides along because the copy may well be locked and the reader has just
    // typed the key: making them type it again, in a different box, one line after pressing
    // 确定, is a second chance to get it wrong rather than a feature. Empty here means the
    // copy opens with no prompt at all.
    Q_EMIT documentSavedAs(written.isEmpty() ? destination : written.first(), changes,
                           editor->pageCount(), int(written.size()),
                           editor->passwordOfWrittenFile());
    return true;
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
