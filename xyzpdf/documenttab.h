// One open document: the backend, its rendering, and the view showing it.
//
// ---- why the window and the tab are separate classes ----
//
// Everything that belongs to *a document* lives here -- which page is being
// read, the zoom, the rotation, the cache of rendered pages, and from M1 the
// sidebar state, the search results and the selection. Everything that belongs
// to *the program* -- menus, the toolbar, recent files, the theme, settings --
// stays in MainWindow.
//
// The division is what makes more than one document possible at all, and doing
// it later means moving every one of those members: they are all set from menu
// actions today, and the actions would each have to grow "and apply it to the
// right tab". See docs/PLAN.md §7.1.
#pragma once

#include "src/document/document.h"
#include "src/document/outline.h"
#include "src/document/textsearch.h"
#include "src/export/wordexport.h"
#include "src/render/rendercache.h"
#include "src/services/documentoutput.h"
#include "src/services/filechangenotice.h"
#include "src/services/foregroundjobs.h"
#include "src/services/ocrsession.h"
#include "src/services/searchsession.h"
#include "src/services/wordexportsession.h"
#include "src/translate/keystore.h"
#include "src/translate/translator.h"
#include "src/ui/propertiesdialog.h"
#include "src/view/pagelayout.h"
#include "src/view/pageview.h"
#include "src/view/zoomcontroller.h"

#include <QFuture>
#include <QPointer>
#include <QRectF>
#include <QSharedPointer>
#include <QVector>
#include <QWidget>

#include <memory>

class DocumentEditor;
class FindBar;
class OutlinePanel;
class PageRenderer;
class PageView;
class QtPdfBackend;
class QTabWidget;
class SearchPanel;
class ThumbnailPanel;
class TranslateDialog;
class QAtomicInt;

class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(QWidget *parent = nullptr);
    ~DocumentTab() override;

    // Opens a document, replacing whatever this tab had. NeedsPassword means
    // the caller asks for one and calls this again; lastError() says why
    // anything else failed.
    Document::Status open(const QString &path, const QString &password = QString());
    Document::Status lastStatus() const { return status; }
    QString lastError() const;

    bool isOpen() const { return opened; }
    QString documentPath() const;

    // What the tab shows: the file name, with a trailing star while this tab is
    // holding edits that no file has been written for. The star is the only
    // warning a reader gets before closing a tab, and it has to be on the tab
    // strip while they are still deciding rather than in a dialog afterwards.
    QString title() const;

    int pageCount() const;
    int currentPage() const;
    void setCurrentPage(int page);

    double zoom() const;
    void setZoom(double zoom);
    void setZoomMode(ZoomController::Mode mode);
    ZoomController::Mode zoomMode() const;

    void rotateBy(int degrees);
    int rotation() const;

    void setLayoutMode(PageLayout::Mode mode);
    PageLayout::Mode layoutMode() const;

    // What a drag on a page does: pan it, select its text, or draw a box to
    // recognize. A property of the view, kept per tab -- and the window applies
    // the one the user last chose to every tab it opens, because a mode that
    // resets itself when a document is opened is a mode people give up on.
    void setTool(PageView::Tool tool);
    PageView::Tool tool() const;

    // The memory budget for rendered pages. Applied immediately: a user who
    // lowers it to free memory gets the memory back rather than getting it back
    // the next time they scroll.
    void setCacheLimit(qint64 bytes);

    // ---- a job that holds this object on the stack ----
    //
    // Why the tab must not be taken apart while a print or an image export is
    // running -- including the 关闭标签 that used to delete it from inside the
    // event pump -- is written at the top of src/services/foregroundjobs.h, and
    // the counting lives there now that the two jobs are in DocumentOutput.
    // The name stays here because this is what MainWindow asks about and what a
    // test stands open on a tab: it is the guard over this tab's own counter.
    class ForegroundJob
    {
    public:
        explicit ForegroundJob(DocumentTab *owner)
            : scope(owner ? owner->jobs.get() : nullptr)
        {
        }
        ForegroundJob(const ForegroundJob &) = delete;
        ForegroundJob &operator=(const ForegroundJob &) = delete;

    private:
        ForegroundJobs::Scope scope;
    };

    bool hasForegroundJob() const { return jobs->busy(); }

    // Whether renders are still on their way. The window asks before deciding
    // that a launch with no window has nothing left to do, and a launch that
    // was told to open a file has exactly that: no window, and a page being
    // rendered for it.
    bool isBusy() const;

    // ---- text ----
    void selectAll();
    void clearSelection();
    bool hasSelection() const;
    QString selectedText() const;
    // The selection, on the clipboard. Does nothing when there is none -- the
    // window's Copy action is disabled in that case, and a menu item reached by
    // a keyboard shortcut anyway should not clear what was on the clipboard.
    void copySelection();

    // ---- translation ----
    //
    // The selected words, sent to the configured service and answered in a
    // window of its own. Does nothing when there is nothing selected, exactly
    // like copySelection -- the action that reaches it is disabled then anyway.
    void translateSelection();

    // Translate an explicit piece of text -- the words just recognized from a
    // box, or a selection. The shared path behind translateSelection(); public so
    // the recognition window's "翻译" button can reach it.
    //
    // Before any byte leaves the machine this checks the two things the reader
    // must have done: chosen a service with a key, and agreed to send selected
    // text to it. Neither is a silent default -- an unconfigured program says so
    // and offers the settings page, and a configured service is asked about once
    // per provider. See Translate::requestGate.
    void translateText(const QString &text);

    // Test seam only: the store the translation path reads its key from. The
    // program installs nothing here and uses the Windows credential manager; a
    // test installs an in-memory store so it can neither read a real key nor
    // write to the machine it runs on. See src/translate/keystore.h.
    void setTranslateKeyStoreForTesting(std::unique_ptr<Translate::KeyStore> store);

    // ---- search ----
    //
    // The scanning, the matches and what the bar and the sidebar's list say
    // about them live in SearchSession; these are the tab's callers -- the
    // window's toolbar and keys, the bar's own signals and the tests -- and the
    // answers they ask for. The scan runs off the UI thread, which is not a
    // detail: see the note at the top of src/services/searchsession.h.
    void startSearch(const QString &needle, bool caseSensitive, bool wholeWord = false);
    void clearSearch();
    // What is being searched for, and whether case matters. Two views of one
    // search -- the bar in this tab and the box in the window's toolbar -- are
    // told by these what the other one is showing, so that neither can drift.
    QString searchText() const { return search->needle(); }
    bool searchCaseSensitive() const { return search->caseSensitive(); }
    bool searchWholeWord() const { return search->wholeWord(); }
    // Moves to the next (+1) or previous (-1) match, wrapping round. Doing
    // nothing when there is nothing to move to.
    void stepSearch(int delta);
    int searchMatchCount() const { return search->matchCount(); }
    // Which match is being looked at, 0-based, or -1 for none.
    int currentSearchMatch() const { return search->currentIndex(); }
    bool isSearching() const;

    // The bar the needle is typed into: shown if it is hidden, focused either
    // way. Ctrl+F twice is "type over the last search" or "go to the next one",
    // and both of those want the caret in the field.
    void showFindBar();
    FindBar *findBarWidget() const { return findBar; }
    // The sidebar tab that lists the hits, for the window and for tests. The
    // list itself is fed by this class; the panel only shows what it is given.
    SearchPanel *searchPanelWidget() const { return searchPanel; }

    PageView *view() const { return pageView; }

    // ---- 书签大纲侧栏 ----
    //
    // The bookmark sidebar belongs to the tab (docs/PLAN.md §7.1): each open
    // document has its own outline, and the window's toggle drives whichever tab
    // is in front. setSidebarVisible shows or hides it; sidebarVisible reports
    // the wish even when there is nothing to show -- the window keeps its menu
    // item checked to match, and an empty document simply has nothing to reveal.
    void setSidebarVisible(bool visible);
    bool sidebarVisible() const { return sidebarWanted; }

    // Night reading for this document. The view inverts the paper as it paints;
    // this also chooses the surround, because the desk a black page lies on has
    // to be dark too or the effect reads as a bug.
    void setNightInverted(bool inverted);
    bool nightInverted() const;
    // Whether this document actually has bookmarks to show, so the window can
    // disable the toggle on a document that has none.
    bool hasOutline() const;

    // Whether the open document carries AcroForm fields.
    //
    // Asked because this reader draws none of them: Qt PDF's annotation flag
    // covers every /Subtype except /Widget, measured six ways in
    // docs/store-submission.md §82 and §94 -- so a filled-in form reads as an
    // empty one on screen, in an export, and on paper. Until the fields are
    // drawn, the least this program can do is say so instead of showing a
    // blank box and letting the reader believe the file is empty.
    //
    // The answer comes from qpdf, in one catalogue lookup at open time. The pass that answers this
    // and the two below was re-measured when they were added: 2 ms for a 40-page file and 24 ms for
    // a 600-page one (tests/documenttab_smoke.cpp prints it), against the 22 ms §77 recorded for
    // the same open before it carried the XFA and the annotation-thread readings.
    bool hasFormFields() const { return formFieldsInSource; }

    // Whether the open document carries its form a second time, as XFA -- and whether that XFA is
    // the dynamic kind, whose pages no reader can draw without running the packet.
    //
    // Asked from the same one open-time reading as hasFormFields() above, because this version
    // neither displays nor edits what is in there, and 文档 says so on the way in rather than
    // letting a reader find an empty-looking page. The two answers are separate because the two
    // situations are not the same size: an XFA document with static AcroForm fields underneath the
    // packet still shows those fields in another reader, while a dynamic one generally shows a
    // single "please wait" page there.
    bool hasXfa() const { return xfaInSource; }
    bool hasDynamicXfa() const { return dynamicXfaInSource; }

    // Whether the thumbnail strip is showing pending edits rather than the file
    // as it is on disk (docs/review-03.md §1.3). The main view always shows the
    // file; the strip shows what a save would write. See refreshThumbnailRows().
    bool thumbnailsShowPendingEdits() const { return thumbnailsPreviewEdits; }

    // ---- 打印 ----
    //
    // Both of these run in DocumentOutput: the dialogs, the render loop and the
    // progress box. Why they stay in the foreground -- a QPrinter's paint device
    // being a GUI-thread thing -- is written at the top of
    // src/services/documentoutput.h; what the tab adds is the guard, because the
    // object those jobs stand on is this one.
    void print();

    // Writes every page out as a PNG into a folder the reader chooses.
    void exportImages();

    // Reads the open file again, putting the reader back where they were.
    //
    // For the document that is being regenerated while it is being read -- a
    // LaTeX or Word export, which is most of why a reader reaches for F5 --
    // and the only way to see a change on disk short of closing the tab
    // (docs/review-03.md §3.4). Returns false, having said why, when there is
    // nothing to read again: no document, a file that has gone, pending edits
    // the reader has not agreed to lose, or a print or export standing on this
    // object (ForegroundJob).
    //
    // Page, zoom, zoom mode, layout and rotation are carried across. The page
    // number rather than the scroll position: a regenerated document has a
    // different length, and a pixel offset into the old one points at nothing
    // in the new.
    bool reload(QString *error);

    // ---- the file changing under the reader ----
    //
    // The watcher, the debounce and the bar above the page are
    // FileChangeNotice's; these are the two questions a reader sees -- whether
    // the bar is up and what it says -- asked as accessors rather than a widget
    // pointer, because how it is built is not the test's business. Why the bar
    // is a notice and not a reload is written at the top of
    // src/services/filechangenotice.h.
    bool fileChangeNoticeVisible() const;
    QString fileChangeNoticeText() const;

    // What the file says about itself, in a dialog (PLAN M2's 文档属性对话框).
    //
    // Here rather than in MainWindow because the answers come from the backend,
    // and the backend is this class's: the window would otherwise need a way to
    // reach through a tab to the document, which is the thing the 1 : N split
    // exists to avoid.
    void showProperties();

    // The same answers without the dialog. Split out so that "what this tab
    // would show" can be checked without an exec() to escape from: a test that
    // has to find a modal dialog and press a button on it hangs for ten minutes
    // when the dialog does not appear, and the thing worth asserting here is
    // the gathering, not Qt's ability to open a window.
    PropertiesDialog::Info propertiesInfo() const;

    // ---- 编辑（M5）----
    //
    // Structural edits -- taking pages out, turning them -- recorded against this
    // document and written out only when the reader names a file for them to go
    // into. The document on screen is not what this changes: there is no path from
    // here back onto the file being read (docs/PLAN.md §7.3: 写盘只发生在编辑模式
    // 的显式保存, and AGENTS.md's 永不写用户的 PDF 文件), and the saved result
    // opens in a tab of its own rather than replacing the one under the reader.
    //
    // What ties these together is that they share one list: 删除页面、旋转页面、
    // 移动页面、合并文件进来、拆分文件、加密 / 去掉口令 and 加水印 record an operation, 撤销
    // takes the last one back, and 保存修改为 writes whatever the list says. Each of the seven
    // can be run again before anything is saved.
    //
    // The merge is the one that reaches outside this document: the pages it adds
    // come from other files, which the reader picks and can give page ranges for.
    // Those files are read at save time and never written into either -- and the
    // saved result cannot be named after any of them, which is the editor's rule
    // rather than this one's.
    //
    // The split and the 加密 are the two that change no page at all: the document stays one
    // document on screen and in the list, and what those two change is what a save writes --
    // how many files come out and what they are called, or what each of them requires to
    // open. They are therefore the two whose effect is invisible until the save, which is
    // why both dialogs say so in those words.
    void deletePages();
    void rotatePages();
    void movePages();
    void mergeFiles();
    void splitPages();
    // 加密 / 去掉口令: the other entry in this menu that changes no page. One slot covers
    // both directions because the dialog does, and because the way to take back a 加密 that
    // was just recorded is 去掉口令 -- two slots would have to each know about the other.
    // Like the split, what this decides only reaches a file at the save.
    void protectPages();
    // 加水印: the seventh, and the one that does not belong to either of the groups above. It
    // moves no page and changes no page's count, so 撤销 here is again just taking one entry
    // out of the list -- but it is the only one of the seven that writes into a page's own
    // content stream, which is why its dialog spends a line of the summary saying the marks
    // are a picture rather than text. Like the two above, nothing reaches a file until 保存.
    void addWatermark();
    // 书签: the eighth, and the only one whose subject is something the reader is looking at in
    // another window -- the sidebar's own tree is what this box opens on, and what it writes replaces
    // wholesale (§68). Like 水印 and 加密 it moves no page, so 撤销 is again one entry out of the
    // list; unlike all seven before it, the request is a *list*, so the box compares what it shows
    // against what it opened with and greys 确定 when they are the same.
    void editOutline();
    void undoEdit();
    void redoEdit();
    // Names the file to write into, replays the list onto the source and writes
    // it. Under a 拆分文件 the name that is typed is the one the pieces are cut from:
    // the same dialog, and N files come out instead of one, all of them staged before
    // any of them is moved into place. Returns false when there was nothing to save,
    // the reader backed out of the save box, or the write failed -- in the last case
    // the reason has already been said, since every caller of this is a menu item.
    bool saveEdits();

    bool canUndoEdits() const;
    bool canRedoEdits() const;
    // "删除 3 页" -- what the next undo (or redo) would take back, for the menu
    // item that names it. Empty when there is nothing to undo.
    QString undoEditName() const;
    QString redoEditName() const;
    // The whole list in one line, for the question a reader gets when closing a
    // tab would throw it away: naming the edits is the only way that question can
    // be answered. Empty when there are none.
    QString pendingEditsSummary() const;
    // Whether this tab is holding edits that no file has yet. What the window
    // asks before closing a tab or a window: the edits live only here, so
    // closing is losing them, and nothing else tells the reader.
    bool hasUnsavedEdits() const;

    // ---- 导出 ----
    //
    // The reading itself is WordExportSession's -- why it is not on the thread
    // that draws, and why a second one is refused rather than queued, is written
    // at the top of src/services/wordexportsession.h. These are the tab's three
    // handles on it: start, ask, stop. The answer arrives in the signals below,
    // which the window is what words and shows.
    void exportToWord(const QString &path);
    bool isExporting() const;
    // The reader has asked for it to stop, through the window's progress box.
    // The same flag the destructor sets, and for the same reason: the export
    // looks at it between pages, which is the only place it can look -- and
    // because the file is written at the very end, an export stopped this way
    // leaves nothing behind to explain.
    void cancelWordExport();

Q_SIGNALS:
    void titleChanged(const QString &title);
    void pageChanged(int page, int pageCount);
    void zoomChanged(double zoom);
    void rotationChanged(int degrees);
    void selectionChanged();
    void toolChanged(PageView::Tool tool);
    // How the search is going: which match is in front, how many there are, and
    // whether the count is still being worked out.
    void searchChanged(int current, int total, bool searching);
    // The reader asked for the sidebar somewhere other than the window's menu --
    // the find bar's 列出全部. The window re-reads the wish so its 导航侧栏 item
    // cannot say the panel is shut while it is open.
    void sidebarWantedChanged();
    // The render cache's used bytes, for the status bar's honesty about memory.
    void cacheChanged(qint64 usedBytes, qint64 limitBytes);
    // The Word export's two answers, relayed from WordExportSession: how far it
    // has got (pages written of pages in the document), and what it wrote or why
    // it did not. The window is what words them.
    void wordExportProgress(int done, int total);
    void wordExportFinished(const WordExportReport &report);
    // A page-image export finished: how many files and where, for the window to
    // say so in the status bar.
    void imageExportFinished(const QString &directory, int pages);
    // A box drawn with the snapshot tool has been put on the clipboard, at
    // this size in pixels. The window says so: this class has no status bar,
    // and §95 measured that it never did.
    void regionCopied(int width, int height);
    // An edit wrote a new document: where it is, what the list of operations was
    // ("删除 3 页"), how many pages the whole document has, how many files came
    // out -- which is one unless a 拆分文件 was in that list, in which case `path` is
    // the first piece -- and the password the copy asks for, empty when it asks for none.
    // The last of those is not the typed one but the one that fits the file that came out
    // (DocumentEditor::passwordOfWrittenFile()), because a save that carried the source's
    // own encryption through wrote a copy locked with the key this tab already has. The
    // window opens the result in a tab of its own -- the tab this came from is still
    // showing the file the edits were made against, which is the whole design.
    void documentSavedAs(const QString &path, const QString &changes, int pageCount,
                         int fileCount, const QString &password);
    // The list of pending edits changed: something to undo, or nothing left to
    // undo. What the window redraws the 文档 menu and the tab's title from.
    void editStateChanged();
    // The reader asked to be taken to the translation settings -- the
    // 「去设置」 button in the box that appears when translation has no service or
    // key yet. The window owns the settings dialog, so the tab asks rather than
    // opening one of its own.
    void translationSetupRequested();

protected:
    // Theming: the page surface paints itself, so the application stylesheet
    // does not reach it and this is how it hears about a theme change.
    void changeEvent(QEvent *event) override;

private:
    void applyThemeColors();

    // Waits for the renders in flight to come back, before the document under
    // them is replaced. See the note in the definition.
    void drainRenders();

    void onPageImageNeeded(const RenderToken &token);
    void onPageRendered(const RenderToken &token, const QImage &image);
    void onDiscarded(const RenderToken &token);
    // Bumped whenever a result in flight would be an answer to an old question.
    void invalidateRenders();

    // A link clicked in the reading tool: jump to the page it names, or -- for
    // an external one -- ask first and then hand the URL to the browser.
    void onLinkActivated(const Link &link);

    // Whether a scan of this tab's document is still running, for the reload
    // that has to wait for it. See drainRenders().
    bool searchRunning() const;

    // Shows the sidebar only when the reader asked for it *and* the document has
    // bookmarks -- the one place those two conditions meet.
    void applySidebarVisibility();

    // The same for a translation: the request runs off the UI thread (a round
    // trip is a second or more), and the window is watched so a result nobody is
    // waiting for is dropped. `text` is copied into the worker, and so are the
    // service and key: the consent check belongs on the thread with the window,
    // and the worker must not reach into settings or the credential manager.
    void startTranslation(const QString &text, Translate::Provider provider, const QString &key,
                          QPointer<TranslateDialog> dialog);

    // The two boxes translateText can raise before a request: one explains that a
    // key is needed and offers the settings page, the other asks whether the
    // selected text may be sent to this service. Split out so the consent rule is
    // one place rather than a condition threaded through the translation path.
    void askForTranslationSetup();
    bool askForTranslationConsent(Translate::Provider provider, const QString &key);

    // Created on first use: a tab that never translates never touches the
    // credential manager.
    Translate::KeyStore *translateKeyStore();

    // The editor for this document, opening it on the first call. Null when the
    // document cannot be edited -- and the reason has already been said to the
    // reader, because every caller of this is a menu item they just pressed.
    //
    // Made on the first edit command rather than with the tab, because §7.3 makes
    // the editor the mark of edit mode: a tab that is only being read holds no
    // parser, no undo list, and no claim on the file it was opened from.
    DocumentEditor *editSession();

    // Points the thumbnail strip at the pages a save would write -- or at the
    // document as it is, when nothing that moves a page is pending -- and says
    // which of the two on the strip's tab.
    void refreshThumbnailRows();

    // Declared before the renderer, and therefore destroyed after it: the
    // renderer holds a pointer to this document, and a document that goes away
    // first leaves it holding freed memory. Members are destroyed in reverse
    // order of declaration, which is the only thing making this safe -- hence
    // the comment, because reordering these two looks harmless.
    std::unique_ptr<QtPdfBackend> backend;
    std::unique_ptr<PageRenderer> renderer;

    RenderCache cache;
    FindBar *findBar = nullptr;
    // The watcher, the debounce and the bar between the find bar and the page.
    // Built with the tab rather than on the first change, so that the tests and
    // the theme see one widget whose existence does not depend on what a writer
    // did -- see src/services/filechangenotice.h.
    std::unique_ptr<FileChangeNotice> notice;
    PageView *pageView = nullptr;
    // The left reading panel: bookmarks, page thumbnails and the hits of the
    // current search, one tab each. It is hidden until the reader opens it
    // (sidebarWanted), and shown for any open document -- thumbnails exist
    // whether or not the file has bookmarks.
    QTabWidget *sidebar = nullptr;
    OutlinePanel *outlinePanel = nullptr;
    ThumbnailPanel *thumbnailPanel = nullptr;
    SearchPanel *searchPanel = nullptr;
    // Whether the reader asked for the sidebar, kept separate from whether it is
    // shown: a document with no bookmarks cannot show one, but the window's menu
    // item still reflects what was asked.
    bool sidebarWanted = false;
    // How many foreground jobs (print, image export) are on this object's
    // stack, and the two jobs that put it there. The counter is declared first
    // and so outlives them; DocumentOutput holds a pointer to it.
    std::unique_ptr<ForegroundJobs> jobs;
    std::unique_ptr<DocumentOutput> output;
    // Set while opening; see hasFormFields().
    bool formFieldsInSource = false;
    // What the open-time probe answered about XFA, kept for hasXfa() / hasDynamicXfa() above.
    bool xfaInSource = false;
    bool dynamicXfaInSource = false;
    // The file's own bookmarks as qpdf read them at open: title, level, page and the destination
    // exactly as written. Qt's bookmark model, which fills the sidebar, has no place inside a page
    // for the shapes real books use, so the panel's rows are joined against these by title and
    // page (matchOutlineRows()) before a click can land at the file's place. Read from the same
    // throwaway editor open that answers hasFormFields(), so this costs no second file open.
    QVector<OutlineRow> sourceOutlineRows;
    // See thumbnailsShowPendingEdits().
    bool thumbnailsPreviewEdits = false;

    // ---- search ----
    //
    // The matches, the running scan and its cancel flag, and the bar's label and
    // the sidebar's rows that go with them. Declared after `backend` and so
    // destroyed before it: the session reaches the document through a provider
    // that asks this tab, and a search that outlived the document it was reading
    // is the crash drainRenders() exists to prevent.
    std::unique_ptr<SearchSession> search;

    // The export, for the same reason and by the same means: it reads the
    // document page by page, and a reload under it is the crash drainRenders()
    // exists to prevent. Its flag is set first, so that the wait is one page
    // rather than a whole document -- both of those live with the session.
    std::unique_ptr<WordExportSession> wordExport;

    // The box the reader drags on a page: the crop's own render, the recognizer,
    // and the window the answer goes into. It is not one of the jobs
    // drainRenders() stops for, and why not is written at the top of
    // src/services/ocrsession.h.
    std::unique_ptr<OcrSession> ocr;

    Document::Status status = Document::Status::FileError;
    bool opened = false;
    // What is being edited, when it is being edited: null until the first 文档
    // command, reset by every open(). Held rather than made per command, because
    // the operations pile up across dialog boxes and 撤销 has to reach the whole
    // list -- see the notes at the edit commands above.
    std::unique_ptr<DocumentEditor> editor;
    // The password the reader typed for this document, empty when it needed none.
    // Kept for the edit path, which opens the same file a second time through a
    // different library: asking again, in the same window, for a password that was
    // already given is a worse trade than holding it for as long as the document
    // is open -- where it is already held anyway, by the renderer.
    QString documentPassword;
    // The key store translation reads from. Null until the first translation,
    // then the Windows credential manager -- or, in a test, the in-memory store
    // handed in through setTranslateKeyStoreForTesting().
    std::unique_ptr<Translate::KeyStore> translateStore;
    // Set while open() is draining renders, so that a stray event delivered from
    // inside that wait cannot start a second open on this tab.
    bool opening = false;
    // Results that arrived for a question nobody is asking any more. Counted
    // rather than only dropped, so that "it happens all the time" is a number
    // rather than an impression.
    int discardedCount = 0;
};
