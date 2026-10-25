#include "documenttab.h"

#include "src/core/appsettings.h"
#include "src/core/stagingcleanup.h"
#include "src/core/windowdialogs.h"
#include "src/document/bookmarkposition.h"
#include "src/document/documentfactory.h"
#include "src/document/formfill.h"
#include "src/edit/documenteditor.h"
#include "src/render/pagerenderer.h"
#include "src/theme/thememanager.h"
#include "src/ui/bookmarkdialog.h"
#include "src/ui/deletepagesdialog.h"
#include "src/ui/findbar.h"
#include "src/ui/mergepagesdialog.h"
#include "src/ui/movepagesdialog.h"
#include "src/ui/outlinepanel.h"
#include "src/ui/propertiesdialog.h"
#include "src/ui/protectpagesdialog.h"
#include "src/ui/rotatepagesdialog.h"
#include "src/ui/searchpanel.h"
#include "src/ui/signaturepanel.h"
#include "src/ui/splitpagesdialog.h"
#include "src/ui/thumbnailpanel.h"
#include "src/ui/watermarkdialog.h"
#include "src/view/pageview.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPrintDialog>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>


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

// A file under the Windows apps install root. Its ACLs let an app write only its
// own private data, and the file a reader opened there is part of the package --
// so 保存 has one sentence for it rather than a Windows error number, and 另存为 is
// the way out. Checked by path segment, because the attribute is not what stops
// the replace: the directory's ACL is.
bool isWindowsAppsPath(const QString &path)
{
    return QDir::toNativeSeparators(path).toCaseFolded().contains(
        QStringLiteral("\\windowsapps\\"));
}

} // namespace

DocumentTab::DocumentTab(QWidget *parent)
    : QWidget(parent)
    , backend(createDocument())
    , renderer(std::make_unique<PageRenderer>())
{
    findBar = new FindBar(this);
    // Hidden until it is asked for: a reader who is not searching has no use for
    // a search box, and a strip of chrome above every page is what "which page
    // am I on" stops being obvious about.
    findBar->hide();

    // ---- the notice that the file changed under the reader ----
    //
    // The watcher, the 400 ms debounce and the bar above the page are
    // FileChangeNotice's. What it cannot know is what "read that file again"
    // means for this tab, so that is the one question handed to it here -- see
    // the note at the top of src/services/filechangenotice.h for why the bar is
    // a notice rather than a reload, and for the one change it can never hear.
    notice = std::make_unique<FileChangeNotice>(this,
                                                [this](QString *error) { return reload(error); });

    pageView = new PageView(this);
    outlinePanel = new OutlinePanel(this);
    thumbnailPanel = new ThumbnailPanel(this);
    searchPanel = new SearchPanel(this);
    signaturePanel = new SignaturePanel(this);

    // The reading sidebar is one panel with four tabs -- the document's bookmarks, a column of
    // page thumbnails, the hits of the current search, and the signatures the file carries --
    // because all four are "get me to a place in this document, or tell me about it", and a
    // reader switches between them, not looks at them at once. It sits beside the page, hidden
    // until opened (see sidebarWanted).
    //
    // The 签名 tab is the one that is not always there: it is removed for a document with no
    // signatures, in applySidebarVisibility() rather than here, because the tab has to answer the
    // document that is open now and the one that replaces it.
    sidebar = new QTabWidget(this);
    sidebar->setObjectName(QStringLiteral("readingSidebar"));
    sidebar->setTabPosition(QTabWidget::North);
    sidebar->addTab(outlinePanel, tr("大纲"));
    sidebar->addTab(thumbnailPanel, tr("缩略图"));
    sidebar->addTab(searchPanel, tr("搜索"));
    sidebar->addTab(signaturePanel, tr("签名"));
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
    layout->addWidget(notice->widget());
    // The reading area takes every spare pixel, and the two bars above it only
    // what they ask for. Without the 1 the layout split the spare height evenly:
    // after Ctrl+F the find bar measured 350-360 px in a 700-720 px tab, half the
    // window, with every test green -- the gui-probe branch saw it on screen
    // (handoff-01 D) and documenttab_smoke now pins it as heights.
    layout->addWidget(readingSplit, 1);

    // A bookmark click lands where the file said, not just on its page (§75's leftovers: a real
    // book's 184 bookmarks are all /FitH, and Qt's own Location role reports 0,0 for those). The
    // destination comes back from the panel joined by title and page against the file's own rows;
    // what is left is to say it in the page's drawn coordinates, which needs the page's size and
    // the reader's rotation -- both of which live here rather than in a tree view.
    connect(outlinePanel, &OutlinePanel::jumpToLocation, this,
            [this](int page, const BookmarkDest &dest) {
                if (page < 0 || page >= pageCount())
                    return;
                pageView->goToLocation(page,
                                       bookmarkTopOffset(dest, backend->pageSize(page),
                                                         pageView->rotation()));
            });
    connect(thumbnailPanel, &ThumbnailPanel::jumpToPage, this, [this](int page) {
        pageView->goToPage(page);
    });
    // One connection for every place the edit list changes -- there are eight
    // of them, and the strip has to follow all of them, undo and redo included.
    connect(this, &DocumentTab::editStateChanged, this, &DocumentTab::refreshThumbnailRows);

    // The search: the scan, the matches and the two lists they fill live in
    // SearchSession, which is handed the bar, the sidebar's results tab and the
    // view to draw on.
    //
    // Every service asks this one question for the document instead of holding
    // one, because a document is replaced by open() -- and a job that outlived
    // the document it started against is the crash drainRenders() exists to
    // prevent. Null is the same answer `opened` used to give.
    const auto openDocument = [this] {
        return opened ? backend.get() : nullptr;
    };
    search = std::make_unique<SearchSession>(findBar, searchPanel, pageView, sidebar,
                                             openDocument);
    connect(search.get(), &SearchSession::changed, this, &DocumentTab::searchChanged);

    // The signature check: one verification per opened document, on a worker, answering into the
    // sidebar's fourth tab and into the window's status line. It is asked for the setting rather
    // than reading it, so that a test can answer "no network" without writing a preference.
    signatures = std::make_unique<SignatureSession>();
    signatures->revocationCheckWanted = [] {
        return AppSettings::signatureRevocationCheckEnabled()
            && AppSettings::signatureRevocationConsentGiven();
    };
    connect(signatures.get(), &SignatureSession::verified, this, [this] {
        signaturePanel->setSignatures(signatures->signatures());
        applySidebarVisibility();
        Q_EMIT signaturesVerified();
    });
    connect(signaturePanel, &SignaturePanel::jumpToPage, this, [this](int page) {
        setCurrentPage(page);
    });

    // The Word export is the same job with a different reading loop, so it is
    // assembled the same way and reports through the tab's own two signals.
    wordExport = std::make_unique<WordExportSession>(openDocument);
    connect(wordExport.get(), &WordExportSession::progress, this, &DocumentTab::wordExportProgress);
    connect(wordExport.get(), &WordExportSession::reportFinished, this,
            &DocumentTab::wordExportFinished);

    // The PowerPoint export is assembled the same way; it reports through its own
    // pair of signals so the window can word a finish that has slides in it.
    powerPointExport = std::make_unique<PptExportSession>(openDocument);
    connect(powerPointExport.get(), &PptExportSession::progress, this,
            &DocumentTab::powerPointExportProgress);
    connect(powerPointExport.get(), &PptExportSession::reportFinished, this,
            &DocumentTab::powerPointExportFinished);

    // Printing and exporting to images are the two jobs that run in front of the
    // reader, on this thread, pumping events between pages -- so they are the
    // two that hold the tab's foreground count, and the window asks that count
    // before taking the tab apart.
    jobs = std::make_unique<ForegroundJobs>();
    output = std::make_unique<DocumentOutput>(this, pageView, jobs.get(), openDocument);
    connect(output.get(), &DocumentOutput::imageExportFinished, this,
            &DocumentTab::imageExportFinished);

    // The box the reader drags on a page: its own render, the recognizer, and the
    // window the answer goes into. The words it reads out may be asked for in
    // another language, and that is a different service's job -- so the request
    // comes back through here.
    ocr = std::make_unique<OcrSession>(this, pageView, openDocument);
    connect(ocr.get(), &OcrSession::regionCopied, this, &DocumentTab::regionCopied);
    connect(ocr.get(), &OcrSession::translateRequested, this, &DocumentTab::translateText);

    // Putting the reader's words into another language: which service, whether it
    // has a key, and whether the reader has agreed to talk to it are settled there.
    // The one thing it cannot do itself is open the settings, which is the
    // window's -- so the 「去设置」 button comes back out through this signal.
    translation = std::make_unique<TranslateSession>(this);
    connect(translation.get(), &TranslateSession::setupRequested, this,
            &DocumentTab::translationSetupRequested);

    connect(findBar, &FindBar::searchRequested, this, &DocumentTab::startSearch);
    connect(findBar, &FindBar::stepRequested, this, &DocumentTab::stepSearch);
    connect(findBar, &FindBar::listAllRequested, this, [this] {
        // The one route that opens a closed sidebar: the reader asked for the
        // list itself, so the list is what is put in front of them.
        setSidebarVisible(true);
        sidebar->setCurrentWidget(searchPanel);
    });
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
    connect(pageView, &PageView::regionCaptured, ocr.get(), &OcrSession::captureRegion);
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
    // A field changed, so the picture the cache holds for that page is a picture of
    // the page as it was. The token does not change -- same page, same band, same
    // size, same rotation -- and token equality ignores the generation, so nothing
    // else in this pipeline would notice. See the note on PageView::formPageStale().
    connect(pageView, &PageView::formPageStale, this, [this](int page) {
        cache.dropPage(page);
        Q_EMIT cacheChanged(cache.usedBytes(), cache.maximumBytes());
    });
    // Both of these change what 撤销 means, what the title says and whether 保存 has
    // anything to write, which is exactly what editStateChanged is for.
    connect(pageView, &PageView::formFocusChanged, this, [this] {
        Q_EMIT editStateChanged();
        Q_EMIT titleChanged(title());
    });
    connect(pageView, &PageView::formEdited, this, [this] {
        Q_EMIT editStateChanged();
        Q_EMIT titleChanged(title());
    });
    connect(renderer.get(), &PageRenderer::rendered, this, &DocumentTab::onPageRendered);
    connect(renderer.get(), &PageRenderer::discarded, this, &DocumentTab::onDiscarded);

    applyThemeColors();
}

DocumentTab::~DocumentTab()
{
    // Before anything else: the view routes keystrokes and clicks into the form
    // layer, and the form layer lives on `backend`, which goes with this object's
    // members while pageView -- a child widget -- goes later with the base class.
    // Clearing the surface is what keeps a focus change during teardown from being a
    // call into a document that has already been closed.
    pageView->setFormSurface(nullptr);
    form = nullptr;

    // First, and before anything it would report about is taken apart: a
    // watcher whose document has gone would answer a change with a bar for a
    // tab nobody can see. The watcher is a child and would go with this object
    // anyway; saying so here keeps "the path is watched exactly as long as the
    // tab is open on it" true at every point rather than only at the end.
    notice->unwatch();

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
    search->cancelPending();
    search->waitUntilFinished();

    // The export reads the same document by the same means, so it is cancelled
    // and waited for here for the same reason -- and it is the one that would
    // take longest to notice, because a document is a lot of pages to read.
    wordExport->cancelPending();
    wordExport->waitUntilFinished();
    powerPointExport->cancelPending();
    powerPointExport->waitUntilFinished();

    // The signature verification reads the same file, so it is waited for by the same means. It
    // cannot be cut short in the middle of a call into Windows, which is why this one waits
    // rather than cancels.
    signatures->cancelPending();
    signatures->waitUntilFinished();

    // The thumbnails render through the same backend on their own worker, so the
    // same wait applies: it must finish before the document it is reading is
    // destroyed with this object.
    if (thumbnailPanel)
        thumbnailPanel->cancelAndDrain();
}

void DocumentTab::startSearch(const QString &needle, bool caseSensitive, bool wholeWord)
{
    search->start(needle, caseSensitive, wholeWord);
}

void DocumentTab::stepSearch(int delta)
{
    search->step(delta);
}

void DocumentTab::clearSearch()
{
    search->clear();
}

bool DocumentTab::isSearching() const
{
    return searchRunning();
}

bool DocumentTab::searchRunning() const
{
    return search->running();
}

void DocumentTab::exportToWord(const QString &path)
{
    if (isExporting())
        return;
    wordExport->exportToWord(path);
}

void DocumentTab::exportToPowerPoint(const QString &path)
{
    if (isExporting())
        return;
    powerPointExport->exportToPowerPoint(path);
}

bool DocumentTab::isExporting() const
{
    return wordExport->isRunning() || powerPointExport->isRunning();
}

void DocumentTab::cancelWordExport()
{
    wordExport->cancel();
}

void DocumentTab::cancelPowerPointExport()
{
    powerPointExport->cancel();
}

QSizeF DocumentTab::powerPointSlideSize() const
{
    Document *document = opened ? backend.get() : nullptr;
    return document ? ::powerPointSlideSize(*document) : QSizeF();
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
    // Document::load() replaces what the document is, and a render that is
    // still running on the renderer's thread is inside the old one. A backend
    // may serialise its calls behind a global lock -- Qt PDF does -- and this is
    // what fell outside it: measured while this was being written, reopening a
    // file with a render in flight ended in a fail-fast inside the reload. It
    // took a test that opens documents as fast as it can to hit it -- a *view*
    // render is almost never still running when somebody opens the next file --
    // but "almost never" on a 500-page scan is a reader who opens a second
    // document while the first is still painting.
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
    search->cancelPending();
    // And the thumbnails, which render through the same backend on their own
    // worker; cancelled here and waited for below, so a reload cannot free the
    // document out from under a render still reading it.
    if (thumbnailPanel)
        thumbnailPanel->cancel();
    // And the export, which reads the document the same way a search does and is
    // waited for below on the same grounds. Told to stop here rather than left to
    // run out: a search of one page is quick, an export of five hundred is not.
    wordExport->cancelPending();
    powerPointExport->cancelPending();
    // And the signature check, which reads the file again -- once through PDFium for the
    // signature dictionaries, once through qpdf for the field names and pages. It holds no
    // pointer to this tab's document so it cannot dangle, but a verification of the file the
    // reader is about to replace would answer about bytes that are on their way out, and the
    // panel would show an answer for the wrong document.
    signatures->cancelPending();

    // Pumping the event loop is how the renders arrive -- they are queued
    // signals -- and it is bounded, because work that never comes back must not
    // make opening a file impossible.
    QElapsedTimer timer;
    timer.start();
    constexpr int kGiveUpAfter = 2000;
    while ((renderer->inFlight() > 0 || searchRunning() || isExporting()
            || thumbnailPanel->rendering() || signatures->running())
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

    const Document::Status result = loadDocument(path, password);
    opening = false;
    return result;
}

Document::Status DocumentTab::loadDocument(const QString &path, const QString &password)
{
    // The renderer is pointed away for the length of the load and back at the
    // backend only if it succeeds, so that no render is queued against a document
    // that is being replaced -- or, after a failed load, against one with no pages.
    // (F8 wrote this for Qt PDF, whose document object had to be replaced on every
    // load: one that had read a plain file refused the right key for an encrypted
    // one. PDFium's load() tears down and reopens in one call under its lock.)
    renderer->setDocument(nullptr);
    const Document::Status result = backend->load(path, password);
    status = result;

    if (result != Document::Status::Ready) {
        opened = false;
        documentPassword.clear();
        formFieldsInSource = false;
        xfaInSource = false;
        dynamicXfaInSource = false;
        sourceHasScript = false;
        // No document, no fields to fill. Order matters: the view drops its form
        // surface before the backend it points at is closed, and before the view is
        // emptied of the pages that surface drew.
        form = nullptr;
        pageView->setFormSurface(nullptr);
        // The tab is on no file now, and the old file's changes are no longer
        // this tab's to report -- including the failure that may have been
        // caused by a half-written replacement of that very file.
        notice->unwatch();
        notice->hideNotice();
        sourceOutlineRows.clear();
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
    renderer->setDocument(backend.get());
    // And the view is given the document to ask its own questions of: which
    // character is under this point, and what is in this range. Borrowed only,
    // and only for text -- images still come through this class, which is what
    // keeps them off the thread that draws.
    pageView->setDocument(backend.get());
    // The form surface is the same object, seen as the half that takes input. Null
    // for an engine that has no form layer at all -- which the view reads as "the page
    // owns every gesture", the behaviour it had before G1.
    form = dynamic_cast<FormFill *>(backend.get());
    pageView->setFormSurface(form);
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

    // The thumbnail tab's background pass can start now; the bookmark tab waits one block,
    // because what it shows has to be joined with the file's own reading first.
    thumbnailPanel->start(sizes, backend.get());

    // Does this file have form fields? Asked here, once, through a qpdf that is
    // opened and dropped again rather than kept: holding a second handle on the
    // reader's file for the life of the tab is what the editor does only once
    // somebody edits, and this is a question, not an edit. The cost was measured
    // before it was put on this path -- 0 ms on the fixtures, 22 ms on a
    // 600-page file. A file qpdf cannot read is not a form: the reader is
    // already looking at it through Qt PDF, and a second opinion that disagrees
    // is not worth a sentence.
    //
    // The same open hands back the file's own bookmark rows, destinations included -- this class
    // keeps no QPDF, and the sidebar needs them to know where inside a page a click should land.
    // That reading already happens inside DocumentEditor::open() for the writer's sake, so the
    // sidebar costs no second pass over the file.
    formFieldsInSource = false;
    xfaInSource = false;
    dynamicXfaInSource = false;
    sourceHasScript = false;
    sourceOutlineRows.clear();
    QStringList lockedFieldNames;
    {
        DocumentEditor probe;
        QString ignored;
        if (probe.open(path, password, &ignored)) {
            formFieldsInSource = probe.hasFormFields();
            xfaInSource = probe.hasXfa();
            dynamicXfaInSource = probe.hasDynamicXfa();
            sourceHasScript = probe.hasFormScript();
            sourceOutlineRows = probe.sourceOutlineRows();
            // The fields a signature's /Lock seals, from the same parse. PDFium has no
            // API for /Lock and on chromium/8111 does not enforce one either (measured:
            // a sealed field took the keyboard like any other), so the form layer is
            // told here, before the first gesture can reach it. An unsigned file (or one
            // whose signatures seal nothing) hands it an empty list, which also clears
            // whatever the previous document had.
            lockedFieldNames = probe.lockedFieldNames();
        }
    }
    if (form)
        form->setLockedFieldNames(lockedFieldNames);
    // Qt is the right source for the titles and pages the panel draws and the wrong one for the
    // place inside the page (§75 measured 0,0 for every /FitH), so its tree is joined with qpdf's
    // by title and page. A file qpdf could not read keeps its tree with default destinations, and
    // clicks go to page tops exactly as they did before.
    Outline sidebarOutline = backend->outline();
    attachOutlineDestinations(sidebarOutline, sourceOutlineRows);
    outlinePanel->setOutline(sidebarOutline);
    // Open on whichever tab has something to lead with -- bookmarks if the file
    // has them, otherwise the thumbnails, which it always does.
    sidebar->setCurrentIndex(outlinePanel->hasOutline() ? 0 : 1);
    applySidebarVisibility();
    // A signed document gets its answer checked; an unsigned one does not get a thread, a second
    // parse, or a tab. The panel is filled when the check comes back, through the signal wired in
    // the constructor.
    signaturePanel->setSignatures({});
    signatures->reset();
    startSignatureVerification();

    // From here until the next open() or this object's end, this is the file
    // whose changes the tab reports. watch() also drops the previous path,
    // which is the whole of "switching documents stops watching the old one" --
    // a reload calls open() with the same path and gets a fresh watch.
    notice->watch(path);
    // And a new document has no change to be told about: an old bar here would
    // sit above a page it was never about. This also clears a refusal that the
    // bar might be showing from the last time the button was pressed.
    notice->hideNotice();

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

void DocumentTab::translateSelection()
{
    translateText(selectedText().trimmed());
}

void DocumentTab::translateText(const QString &text)
{
    translation->translateText(text);
}

void DocumentTab::setTranslateKeyStoreForTesting(std::unique_ptr<Translate::KeyStore> store)
{
    translation->setKeyStoreForTesting(std::move(store));
}

void DocumentTab::setSidebarVisible(bool visible)
{
    if (sidebarWanted == visible)
        return;
    sidebarWanted = visible;
    applySidebarVisibility();
    Q_EMIT sidebarWantedChanged();
}

void DocumentTab::applySidebarVisibility()
{
    // The sidebar is worth showing for any open document now that it holds
    // thumbnails as well as bookmarks -- a page always has a picture, even when
    // the file has no bookmarks. So the only conditions are "the reader asked"
    // and "there is a document".
    sidebar->setVisible(sidebarWanted && opened);

    // The 签名 page is the one whose contents depend on the file: an unsigned document has
    // nothing to put in it, and a tab that is always there invites the reader to open an empty
    // list and wonder. A verification that has not come back yet keeps the page: the document
    // does carry signatures -- the backend said so at open -- and a page that appears a second
    // later in front of a reader who is already looking at the sidebar is worse than one that
    // waits for the answer it is about to show.
    if (opened) {
        const bool hasAny = backend->signatureCount() > 0;
        const int index = sidebar->indexOf(signaturePanel);
        if (index >= 0)
            sidebar->setTabVisible(index, hasAny);
    }
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
    output->print();
}

void DocumentTab::exportImages()
{
    output->exportImages();
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

bool DocumentTab::reload(QString *error)
{
    const auto refuse = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };

    if (!opened)
        return refuse(tr("这个标签里没有打开的文档。"));
    if (hasForegroundJob())
        return refuse(tr("这个标签正在打印或导出，完成之后才能重新加载。"));
    if (hasUnsavedEdits()) {
        // Asked rather than refused outright: the edits are a list of
        // operations against the pages of the document that is about to be
        // replaced, so they cannot survive this -- but it is the reader's call.
        const auto answer = WindowDialogs::question(
            window(), tr("重新加载"),
            tr("「%1」还带着没有写进任何文件的修改：%2。"
               "\n\n重新加载会把这些修改丢掉。要继续吗？")
                .arg(QFileInfo(documentPath()).fileName(), pendingEditsSummary()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return refuse(QString());
    }

    const QString path = documentPath();
    // Asked before the open rather than after it: open() empties the tab on a
    // failure, which is right for "open this other file" and wrong for "read
    // this one again" -- a file that has been deleted or is halfway through
    // being rewritten would take the document off the screen.
    if (!QFileInfo::exists(path))
        return refuse(tr("%1 不在了，所以没有可以重新加载的东西。").arg(QFileInfo(path).fileName()));

    // The key this document was opened with: the probe below has to open the
    // same encrypted bytes the reload will.
    const QString password = documentPassword;

    // Read the new bytes once before open() takes this tab apart.
    //
    // backend->load() closes the document it is holding when the new bytes are
    // not a usable PDF, and open() empties the view on a failure. Both are
    // right for "open this other file" and wrong for "read this one again":
    // a file caught halfway through being rewritten would take the page the
    // reader was looking at off the screen, and the only way back would be to
    // open it again. The probe reads the same bytes and is thrown away; if it
    // says no, the reader keeps the document they had and gets the sentence.
    //
    // The price is a second read of the file on every reload -- F5 and the
    // change bar alike. The gap between this read and the one open() does
    // could let the file change again; then open() fails and empties the tab.
    // Closing that window would mean loading into a second backend and
    // swapping it in, which is a change to how the tab owns its document
    // rather than a line in this function.
    {
        const std::unique_ptr<Document> probe = createDocument();
        if (probe->load(path, password) != Document::Status::Ready) {
            const QString why = probe->lastError();
            return refuse(why.isEmpty()
                              ? tr("重新加载 %1 失败。").arg(QFileInfo(path).fileName())
                              : why);
        }
    }

    const int page = pageView->currentPage();
    const double zoom = pageView->zoom();
    const ZoomController::Mode mode = pageView->zoomMode();
    const PageLayout::Mode layout = pageView->layoutMode();
    const int rotation = pageView->rotation();

    if (open(path, password) != Document::Status::Ready) {
        const QString why = lastError();
        return refuse(why.isEmpty() ? tr("重新加载 %1 失败。").arg(QFileInfo(path).fileName()) : why);
    }

    pageView->setLayoutMode(layout);
    if (mode == ZoomController::Mode::Percent)
        pageView->setZoom(zoom);
    else
        pageView->setZoomMode(mode);
    pageView->setRotation(rotation);
    // Clamped by the new document's length, which is the whole reason this is
    // a page number: a file that was regenerated shorter has no page 40.
    if (page > 0)
        pageView->goToPage(qMin(page, pageCount() - 1));
    return true;
}

bool DocumentTab::fileChangeNoticeVisible() const
{
    return notice->noticeVisible();
}

QString DocumentTab::fileChangeNoticeText() const
{
    return notice->noticeText();
}

void DocumentTab::refreshThumbnailRows()
{
    if (!opened || !thumbnailPanel)
        return;

    // What a save would write, page by page. Without an editor that is the
    // document as it is; with one, it is the pending list played out --
    // pages taken out are not there, moved pages are where they will land,
    // merged pages are where they will arrive.
    const int count = pageCount();
    QVector<DocumentEditor::PageRef> order;
    if (editor) {
        order = editor->pageOrder();
    } else {
        order.reserve(count);
        for (int page = 0; page < count; ++page)
            order.append(DocumentEditor::PageRef{0, page});
    }

    bool differs = order.size() != count;
    QVector<ThumbnailPanel::Row> rows;
    rows.reserve(order.size());
    for (int row = 0; row < order.size(); ++row) {
        const DocumentEditor::PageRef &ref = order.at(row);
        ThumbnailPanel::Row shown;
        if (ref.source == 0) {
            shown.sourcePage = ref.page;
            shown.rotation = editor ? editor->pendingQuarterTurns(ref.page) * 90 : 0;
            shown.label = QString::number(row + 1);
            if (ref.page != row) {
                shown.toolTip = tr("原第 %1 页").arg(ref.page + 1);
                differs = true;
            }
            if (shown.rotation != 0)
                differs = true;
        } else {
            // A page from another file: this document cannot draw it, so the row
            // says where it comes from instead of showing an empty tile with a
            // number that means nothing.
            const QString file = QFileInfo(editor->importPathFor(ref.source)).fileName();
            shown.label = tr("%1（合并）").arg(row + 1);
            shown.toolTip = tr("来自 %1 的第 %2 页").arg(file).arg(ref.page + 1);
            differs = true;
        }
        rows.append(shown);
    }
    thumbnailPanel->setRows(rows);
    thumbnailPanel->setCurrentPage(pageView->currentPage());

    // The strip is no longer a picture of the file on disk, and the reader has to
    // be told so where they are looking: nine thumbnails beside a ten-page
    // document is otherwise a strip that looks broken.
    const int stripTab = sidebar->indexOf(thumbnailPanel);
    if (stripTab >= 0) {
        sidebar->setTabText(stripTab, differs ? tr("缩略图（修改预览）") : tr("缩略图"));
        sidebar->setTabToolTip(stripTab,
                               differs ? tr("这里显示的是保存之后的页面顺序和朝向；"
                                            "正在读的文件本身没有变")
                                       : QString());
    }
    thumbnailsPreviewEdits = differs;
}

QString DocumentTab::signatureSummary() const
{
    if (!opened || !signatures->answered())
        return {};
    return SignatureVerify::summary(signatures->signatures());
}

int DocumentTab::signatureValidCount() const
{
    return signatures->countWithVerdict(SignatureVerify::Verdict::Valid);
}

int DocumentTab::signatureRowCount() const
{
    return signaturePanel->signatureRowCount();
}

QString DocumentTab::signatureRowText(int row) const
{
    return signaturePanel->rowText(row);
}

int DocumentTab::signatureRowPage(int row) const
{
    return signaturePanel->pageOfRow(row);
}

void DocumentTab::selectSignatureRow(int row)
{
    // The press rather than the selection: what a reader does to a signature row is click it, and
    // the thing worth testing is that the click reaches the page.
    signaturePanel->pressRow(row);
}

bool DocumentTab::signaturePanelVisible() const
{
    const int index = sidebar->indexOf(signaturePanel);
    return index >= 0 && sidebar->isTabVisible(index);
}

void DocumentTab::startSignatureVerification()
{
    if (!opened || backend->signatureCount() <= 0)
        return;

    // The reader's agreement, asked the way the translation service asks its own (F1): once,
    // before the first connection, saying what will be contacted and what will not. Refusing
    // stores nothing, so the question comes again for the next document rather than being
    // answered once forever by a no.
    if (AppSettings::signatureRevocationCheckEnabled()
        && !AppSettings::signatureRevocationConsentGiven()) {
        QMessageBox box(this);
        box.setWindowTitle(tr("数字签名"));
        box.setIcon(QMessageBox::Question);
        box.setText(tr("检查证书有没有被吊销要联网。"));
        box.setInformativeText(
            tr("XYZPDF 会连接这份文档的证书里写着的吊销地址（CRL 文件或 OCSP 服务器），"
               "去问一次这张证书还作不作数。发出去的是证书里本来就带着的那两个地址，"
               "不是文档的内容，也不是你的任何文件。\n\n"
               "这会告诉对方这台机器正在看这一份文档。不确定就选「不检查」，"
               "签名结论里会写明「未联网检查吊销状态」。"));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.button(QMessageBox::Yes)->setText(tr("允许联网检查"));
        box.button(QMessageBox::No)->setText(tr("不检查"));
        if (box.exec() == QMessageBox::Yes)
            AppSettings::setSignatureRevocationConsent(true);
    }

    signatures->start(documentPath(), documentPassword);
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
    info.signatureCount = backend->signatureCount();
    // How many of them the check answered 有效. Zero while the verification is still running,
    // which is why the dialog's row only adds the second number once there is one to add --
    // "0 个有效" under a count of two would say the signatures failed when nothing has answered.
    info.validSignatureCount = signatures->answered() ? signatureValidCount() : -1;
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
    // The field first, and only when it has the keyboard and its own history to walk:
    // a reader who typed a name, deleted it and typed another one means the last
    // keystroke, not the last page operation (handoff-04 G1 4). No focus, or an empty
    // field history, and the structural list takes it -- which is the behaviour every
    // version before this had, unchanged.
    if (pageView->canUndoInField()) {
        pageView->undoInField();
        Q_EMIT editStateChanged();
        Q_EMIT titleChanged(title());
        return;
    }
    if (editor && editor->undo())
        Q_EMIT editStateChanged();
}

void DocumentTab::redoEdit()
{
    if (pageView->canRedoInField()) {
        pageView->redoInField();
        Q_EMIT editStateChanged();
        Q_EMIT titleChanged(title());
        return;
    }
    if (editor && editor->redo())
        Q_EMIT editStateChanged();
}

bool DocumentTab::canUndoEdits() const
{
    return pageView->canUndoInField() || (editor && editor->canUndo());
}

bool DocumentTab::canRedoEdits() const
{
    return pageView->canRedoInField() || (editor && editor->canRedo());
}

QString DocumentTab::undoEditName() const
{
    // Empty for a keystroke in a field: PDFium's field history has no names to read
    // back, and the window draws a bare 撤销 for an empty one. A made-up "撤销 字段"
    // would promise a description the list cannot give, and the reader would read the
    // same words for the second keystroke and the fifth.
    if (pageView->canUndoInField())
        return QString();
    return editor ? editor->undoDescription() : QString();
}

QString DocumentTab::redoEditName() const
{
    if (pageView->canRedoInField())
        return QString();
    return editor ? editor->redoDescription() : QString();
}

bool DocumentTab::hasFieldEdits() const
{
    return form && form->differingFieldCount() > 0;
}

bool DocumentTab::hasUnsavedEdits() const
{
    return (editor && editor->isDirty()) || hasFieldEdits();
}

QString DocumentTab::pendingEditsSummary() const
{
    QStringList parts;
    if (editor && editor->isDirty())
        parts.append(editor->pendingSummary());
    // Counted, and through %n rather than %1: the sentence is the one a reader reads
    // before throwing the work away, and "改了 3 个字段" says which of the four they
    // clicked is going with it -- while the AGENTS.md rule about a bare `.arg(n)` is
    // that the catalogue cannot see a plural it was not given (sec.93).
    const int fields = form ? form->differingFieldCount() : 0;
    if (fields > 0)
        parts.append(tr("改了 %n 个表单字段", nullptr, fields));
    return parts.join(tr("，"));
}

DocumentTab::ReadingState DocumentTab::readingState() const
{
    ReadingState state;
    state.page = pageView->currentPage();
    state.zoom = pageView->zoom();
    state.zoomMode = pageView->zoomMode();
    state.layout = pageView->layoutMode();
    state.rotation = pageView->rotation();
    state.sidebarTab = sidebar ? sidebar->currentIndex() : -1;
    return state;
}

void DocumentTab::restoreReadingState(const ReadingState &state)
{
    pageView->setLayoutMode(state.layout);
    if (state.zoomMode == ZoomController::Mode::Percent)
        pageView->setZoom(state.zoom);
    else
        pageView->setZoomMode(state.zoomMode);
    pageView->setRotation(state.rotation);
    // Clamped by the file that was just written: an in-place save can have fewer
    // pages than the one the reader was on (删除页面), and page 40 of a 20-page file
    // is not a place to put them.
    if (state.page > 0 && pageCount() > 0)
        pageView->goToPage(qMin(state.page, pageCount() - 1));
    if (sidebar && state.sidebarTab >= 0 && state.sidebarTab < sidebar->count())
        sidebar->setCurrentIndex(state.sidebarTab);
}

void DocumentTab::offerSaveAs(const QString &reason)
{
    const int choice = WindowDialogs::askChoice(
        window(), tr("保存"), reason + QStringLiteral("\n\n") + tr("要另存为一份新的吗？"),
        {tr("另存为…"), tr("取消")}, 0);
    if (choice == 0)
        saveEditsAs();
}

bool DocumentTab::saveEdits()
{
    const bool structural = editor && editor->isDirty();
    const int fields = form ? form->differingFieldCount() : 0;
    if (!structural && fields == 0)
        return false;
    if (hasForegroundJob()) {
        // A foreground job is holding the tab on the stack and reading the
        // document; the rules in src/services/foregroundjobs.h are unchanged by
        // F8, and a replace would pull the file out from under a print.
        WindowDialogs::warning(window(), tr("保存"),
                               tr("这个标签正在打印或导出，完成之后才能保存。"));
        return false;
    }
    const QString path = documentPath();
    if (path.isEmpty())
        return false;

    // Refused before anything is staged, because each of these is a fact about
    // where the file is rather than about the bytes: a hidden copy written beside
    // it would be perfect and still could not go back. 另存为 is the way out, and
    // the box names it rather than leaving the reader with a Windows error.
    const QFileInfo source(path);
    if (!source.exists()) {
        offerSaveAs(tr("%1 不在了，可能已经被移走，或者网络位置断开了。")
                        .arg(source.fileName()));
        return false;
    }
    if (isWindowsAppsPath(path)) {
        offerSaveAs(tr("%1 在 Windows 应用的安装目录里，这个程序改不了它。")
                        .arg(source.fileName()));
        return false;
    }
    if (!source.isWritable()) {
        offerSaveAs(tr("%1 是只读文件，写不回去。").arg(source.fileName()));
        return false;
    }
    if (!QFileInfo(source.absolutePath()).isWritable()) {
        offerSaveAs(tr("%1 所在的文件夹不允许写入。").arg(source.fileName()));
        return false;
    }
    if (structural && editor->pieceCount() > 1) {
        offerSaveAs(tr("这份文档还带着拆分，写回原文件只能写成一个文件。"));
        return false;
    }

    const ReadingState state = readingState();
    // With no structural list the written file keeps the protection the source has, and
    // the key this tab opened it with is the key it still asks for. With one, the list
    // decides -- a pending 加密 replaces it, a carried /Encrypt keeps it.
    const QString passwordAfter = structural ? editor->passwordOfWrittenFile() : documentPassword;

    // The copy is written and checked before this tab lets go of anything: a
    // staging failure leaves the original and the view exactly as they were.
    //
    // Two stages when the reader did both kinds of thing: PDFium writes the field
    // answers beside the source, and the structural replay reads *that* file instead of
    // the source, so one save carries both and neither writer has to know the other's
    // format (handoff-04 G1 6). With only field answers there is no second stage, and
    // the first one's check is the same qpdf reopen the structural one runs.
    QString fieldStage;
    QString staged;
    QString error;
    bool staged_ok = true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (fields > 0)
        staged_ok = form->stageFieldValues(source.absolutePath(), &fieldStage, &error);
    if (staged_ok && structural) {
        staged_ok = editor->stageInPlace(&staged, &error, fieldStage);
        // The replay has read it. It is ours, it is hidden, and it is not the reader's
        // document, so it does not stay in their folder -- and its record goes with it, which
        // is what keeps the list this program reads on the next launch about files that are
        // still sitting there.
        if (!fieldStage.isEmpty()) {
            StagingCleanup::removeStaged(fieldStage);
            fieldStage.clear();
        }
    } else if (staged_ok && fields > 0) {
        staged_ok = DocumentEditor::verifyStagedFile(fieldStage, backend->pageCount(),
                                                     documentPassword, &error);
        if (staged_ok)
            staged = fieldStage;
    }
    QApplication::restoreOverrideCursor();
    if (!staged_ok) {
        if (!fieldStage.isEmpty())
            StagingCleanup::removeStaged(fieldStage);
        WindowDialogs::warning(window(), tr("保存"), error);
        return false;
    }

    // No reader of the original may still be inside it when the replace happens:
    // drainRenders() stops the search, the thumbnails and the export and waits for
    // them, and backend->close() lets go of the pages, the document and the file
    // handle (Document::close()). The order is F8's and cannot move. PDFium opens
    // the file with FILE_SHARE_DELETE, so ReplaceFileW would get through even with
    // it open -- the close is here for the rule, not for the engine: nothing of ours
    // reads the old bytes while they are swapped.
    //
    // The renderer is pointed away first so that no render is queued against a
    // document that is about to have no pages; loadDocument() points it back.
    drainRenders();
    notice->beginSelfWrite();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    renderer->setDocument(nullptr);
    // The view stops routing input before the document under the form layer goes:
    // a keystroke landing between this and the reopen would be a call into a form
    // environment with no pages.
    pageView->setFormSurface(nullptr);
    form = nullptr;
    backend->close();
    quint32 windowsError = 0;
    const bool replaced = DocumentEditor::replaceSource(path, staged, &windowsError);
    QApplication::restoreOverrideCursor();

    if (!replaced) {
        // The original is byte-identical; the staged file is ours and goes, and its record
        // goes with it -- but only if it went. If something else has the folder locked, this
        // is the one case where leaving the file is the right answer, and the list is what
        // makes the next launch try again without anybody asking. The editor was never
        // touched, so the reader keeps their work and the file they were reading comes back on
        // screen before the box is answered.
        StagingCleanup::removeStaged(staged);
        notice->endSelfWrite();
        const Document::Status back = loadDocument(path, documentPassword);
        if (back == Document::Status::Ready)
            restoreReadingState(state);
        offerSaveAs(tr("没能把新文件换上去（Windows 错误 %1）。原来的文件没有改动，"
                       "这些修改还留在这里。")
                        .arg(windowsError));
        return false;
    }

    // The swap is what this file was written for, so there is nothing at that path any more
    // -- ReplaceFileW moved it into the original's name. Only the record is left to clear, and
    // clearing it is the whole of the promise: a path this program stops recording is a path
    // it will never delete again.
    StagingCleanup::forget(staged);

    // The file now holds the list, so the tab stops being dirty. The reopen uses
    // the key the written file asks for: a 加密 that was pending replaced the old
    // one, and a carried /Encrypt still needs the key this tab opened with.
    editor.reset();
    Q_EMIT editStateChanged();
    const Document::Status result = loadDocument(path, passwordAfter);
    // After loadDocument(), because that starts a fresh watch, and watch()/unwatch()
    // is where a stale self-write signature is dropped. The size and time are the
    // bytes we just put there; the watcher's queued event for them is recognized
    // in FileChangeNotice::onFileChangeSettled() and does not raise the bar.
    notice->endSelfWrite(QFileInfo(path).size(),
                         QFileInfo(path).lastModified().toMSecsSinceEpoch());
    if (result != Document::Status::Ready) {
        WindowDialogs::warning(
            window(), tr("保存"),
            tr("新文件已经放回原处，但重新打开它失败：%1").arg(lastError()));
        return false;
    }
    restoreReadingState(state);
    Q_EMIT documentSavedInPlace(path);
    return true;
}

bool DocumentTab::saveEditsAs()
{
    // Not `editor->isDirty()`: a tab whose only change is a filled field has no
    // structural list, and 另存为 is the way to put those answers in a file.
    if (!hasUnsavedEdits())
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
        window(), tr("另存为"), suggested, tr("PDF 文档 (*.pdf)"));
    if (destination.isEmpty())
        return false;

    QString error;
    if (saveEditsTo(destination, &error))
        return true;
    if (!error.isEmpty())
        WindowDialogs::warning(window(), tr("另存为"), error);
    return false;
}

bool DocumentTab::saveEditsTo(const QString &destination, QString *error)
{
    const auto refuse = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (destination.isEmpty())
        return refuse(QString());
    const bool structural = editor && editor->isDirty();
    const int fields = form ? form->differingFieldCount() : 0;
    if (!structural && fields == 0)
        return refuse(tr("没有待保存的修改。"));
    if (hasForegroundJob())
        return refuse(tr("这个标签正在打印或导出，完成之后才能保存。"));

    const QString source = documentPath();
    if (source.isEmpty())
        return refuse(tr("这个标签里没有打开的文档。"));
    // The editor that writes the bytes. A fields-only save has no list to replay, but
    // it still goes through DocumentEditor, because that is where the staged-name,
    // overwrite-checked, qpdf-verified way out to a new file lives -- and a second copy
    // of that walk is how a 另存为 would start meaning something different from a 保存.
    DocumentEditor *session = editSession();
    if (!session)
        return refuse(tr("这份文档现在没法写出。"));

    const ReadingState state = readingState();
    const QString changes = pendingEditsSummary();
    const int pages = backend->pageCount();
    QString writeError;
    QStringList written;

    // The first of the two stages, beside the file being read: PDFium's own copy of the
    // document with the reader's answers committed into it. Nothing here touches the
    // source -- this is a hidden file of ours, and the writer below is the one that
    // chooses the name the reader typed.
    QString fieldStage;
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString stageError;
        const bool staged_ok = fields <= 0
            || form->stageFieldValues(QFileInfo(source).absolutePath(), &fieldStage,
                                      &stageError);
        QApplication::restoreOverrideCursor();
        if (!staged_ok)
            return refuse(stageError);
    }

    // Synchronous, with the wait cursor over it, rather than off the thread that
    // draws: QPDF reads the file's structure and copies its streams, and neither
    // needs a renderer, a lock shared with one, or a progress report -- there is
    // one page-tree to read and one file to write, so the dialog this would go in
    // could only spin. docs/store-submission.md §10 has the measured cost.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool done = session->saveAs(destination, &writeError, &written, fieldStage);
    if (!fieldStage.isEmpty())
        StagingCleanup::removeStaged(fieldStage);
    QApplication::restoreOverrideCursor();
    if (!done)
        return refuse(writeError);

    AppSettings::setLastOpenDirectory(QFileInfo(destination).absolutePath());
    // The name to open comes from the save rather than from the box: under a split the
    // typed name is not a file that exists, and the first piece is. One tab opens even
    // when forty came out, because the check a reader makes on a split set is whether
    // the first file is the one they meant -- and the status line names the count so
    // they know how many are in the folder before going to look.
    const QString first = written.isEmpty() ? destination : written.first();
    // The password rides along because the copy may well be locked: making the
    // reader type the key again, in a different box, one line after pressing
    // 确定, is a second chance to get it wrong rather than a feature.
    const QString password = session->passwordOfWrittenFile();

    // The new file has to be readable before this tab moves to it. If it is not,
    // the bytes are still there to look at but the document on screen -- and the
    // pending list in the editor -- stay where they are.
    {
        const std::unique_ptr<Document> probe = createDocument();
        if (probe->load(first, password) != Document::Status::Ready) {
            const QString why = probe->lastError();
            return refuse(
                tr("修改已经写成 %1，但这个程序打不开它。原来的文档和这里的修改都还在。"
                   "\n\n%2")
                    .arg(first, why));
        }
    }

    // open() clears the editor on purpose: the pending list is in the file now,
    // and this tab is about that file rather than about the one it came from.
    if (open(first, password) != Document::Status::Ready) {
        const QString why = lastError();
        return refuse(why.isEmpty() ? tr("写好的 %1 打不开。").arg(first) : why);
    }
    restoreReadingState(state);
    Q_EMIT documentSavedAs(first, changes, pages, int(written.size()), password);
    return true;
}
