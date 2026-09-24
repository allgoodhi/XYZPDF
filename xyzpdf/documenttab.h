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
#include "src/document/textsearch.h"
#include "src/export/wordexport.h"
#include "src/ocr/ocrengine.h"
#include "src/render/rendercache.h"
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
class OcrDialog;
class OutlinePanel;
class PageRenderer;
class PageView;
class QtPdfBackend;
class QTabWidget;
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
    void translateText(const QString &text);

    // ---- search ----
    //
    // The scan runs off the UI thread, which is not a detail: text() is a PDFium
    // call per page and every one of them takes the same global lock as the
    // renderer, so a five-hundred-page document searched on the thread that
    // draws is a window that does not repaint for as long as it takes. See
    // document.h's note about which calls may sit on that thread.
    void startSearch(const QString &needle, bool caseSensitive);
    void clearSearch();
    // What is being searched for, and whether case matters. Two views of one
    // search -- the bar in this tab and the box in the window's toolbar -- are
    // told by these what the other one is showing, so that neither can drift.
    QString searchText() const { return searchNeedle; }
    bool searchCaseSensitive() const { return searchCase; }
    // Moves to the next (+1) or previous (-1) match, wrapping round. Doing
    // nothing when there is nothing to move to.
    void stepSearch(int delta);
    int searchMatchCount() const { return int(searchMatches.size()); }
    // Which match is being looked at, 0-based, or -1 for none.
    int currentSearchMatch() const { return currentMatchIndex; }
    bool isSearching() const;

    // The bar the needle is typed into: shown if it is hidden, focused either
    // way. Ctrl+F twice is "type over the last search" or "go to the next one",
    // and both of those want the caret in the field.
    void showFindBar();
    FindBar *findBarWidget() const { return findBar; }

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

    // ---- 打印 ----
    //
    // Asks for the print choices, then renders and prints. Foreground, with a
    // cancel-able progress dialog: a QPrinter's paint device is a GUI-thread
    // thing, so this is not a worker-thread job the way an export is.
    void print();

    // Writes every page out as a PNG into a folder the reader chooses. Foreground
    // with a cancel-able progress dialog, like printing.
    void exportImages();

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
    // 移动页面、合并文件进来 and 拆分文件 record an operation, 撤销 takes the last one
    // back, and 保存修改为 writes whatever the list says. Each of the five can be run
    // again before anything is saved.
    //
    // The merge is the one that reaches outside this document: the pages it adds
    // come from other files, which the reader picks and can give page ranges for.
    // Those files are read at save time and never written into either -- and the
    // saved result cannot be named after any of them, which is the editor's rule
    // rather than this one's.
    //
    // The split is the one that changes no page at all: the document stays one
    // document on screen and in the list, and what a split changes is how many files a
    // save writes and what they are called. It is therefore also the one whose effect
    // is invisible until the save -- which is why its dialog says so in those words.
    void deletePages();
    void rotatePages();
    void movePages();
    void mergeFiles();
    void splitPages();
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
    // Writes the whole document's text out as a Word file, off the UI thread:
    // every page of it is a text call and a selection under the same lock the
    // renderer takes, so an export on the thread that draws is a window that
    // stops repainting until it is done. The answer arrives in the signal below,
    // which the window is what words and shows.
    //
    // One at a time: a second export while one is running is refused rather than
    // queued, because they would be writing the same file.
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
    // The render cache's used bytes, for the status bar's honesty about memory.
    void cacheChanged(qint64 usedBytes, qint64 limitBytes);
    // How far the export has got: pages written of pages in the document. Sent
    // from the worker thread, once per page -- a page with a picture on it is a
    // render, and a scanned document is minutes of work nobody can see.
    void wordExportProgress(int done, int total);
    // What an export wrote, or why it did not.
    void wordExportFinished(const WordExportReport &report);
    // A page-image export finished: how many files and where, for the window to
    // say so in the status bar.
    void imageExportFinished(const QString &directory, int pages);
    // An edit wrote a new document: where it is, what the list of operations was
    // ("删除 3 页"), how many pages the whole document has, and how many files came
    // out -- which is one unless a 拆分文件 was in that list, in which case `path` is
    // the first piece. The window opens the result in a tab of its own -- the tab this
    // came from is still showing the file the edits were made against, which is the
    // whole design.
    void documentSavedAs(const QString &path, const QString &changes, int pageCount,
                         int fileCount);
    // The list of pending edits changed: something to undo, or nothing left to
    // undo. What the window redraws the 文档 menu and the tab's title from.
    void editStateChanged();

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

    // The box the user dragged over a text-free part of a page: rendered at a
    // size of its own, then recognized, then answered in a window.
    void onRegionCaptured(int page, QRectF points);

    // A link clicked in the reading tool: jump to the page it names, or -- for
    // an external one -- ask first and then hand the URL to the browser.
    void onLinkActivated(const Link &link);

    // The matches a scan came back with, in page order.
    void applySearchResults(const QVector<TextSearch::Match> &matches);
    // One match as the range the view draws and scrolls to, or an empty range
    // for an index that is not one.
    PageTextRange rangeOf(int index) const;
    // Hands the view the matches to draw and which one is in front, and tells
    // the bar what to say.
    void updateSearchHighlights();
    // Whether a scan of this tab's is still running, for the reload that has to
    // wait for it. See drainRenders().
    bool searchRunning() const;
    // Runs the recognition off the UI thread and fills the window in when it
    // is done. The crop is copied into the worker; the window is watched with
    // a QPointer, because the user may close it first. When `thenTranslate` is
    // set (the translate tool boxing a picture), the recognized words are sent
    // straight to a translation and `dialog` is null.
    void startRecognition(const QImage &crop, QPointer<OcrDialog> dialog, bool thenTranslate = false);

    // Shows the sidebar only when the reader asked for it *and* the document has
    // bookmarks -- the one place those two conditions meet.
    void applySidebarVisibility();

    // The same for a translation: the request runs off the UI thread (a round
    // trip is a second or more), and the window is watched so a result nobody is
    // waiting for is dropped. `text` is copied into the worker.
    void startTranslation(const QString &text, QPointer<TranslateDialog> dialog);

    // Emits the progress signal, and only ever runs on this object's thread: the
    // export reports from a worker, and this is how the report gets here.
    void notifyExportProgress(int done, int total);

    // The editor for this document, opening it on the first call. Null when the
    // document cannot be edited -- and the reason has already been said to the
    // reader, because every caller of this is a menu item they just pressed.
    //
    // Made on the first edit command rather than with the tab, because §7.3 makes
    // the editor the mark of edit mode: a tab that is only being read holds no
    // parser, no undo list, and no claim on the file it was opened from.
    DocumentEditor *editSession();

    // Declared before the renderer, and therefore destroyed after it: the
    // renderer holds a pointer to this document, and a document that goes away
    // first leaves it holding freed memory. Members are destroyed in reverse
    // order of declaration, which is the only thing making this safe -- hence
    // the comment, because reordering these two looks harmless.
    std::unique_ptr<QtPdfBackend> backend;
    std::unique_ptr<PageRenderer> renderer;

    RenderCache cache;
    FindBar *findBar = nullptr;
    PageView *pageView = nullptr;
    // The left reading panel: bookmarks and page thumbnails, one tab each. It is
    // hidden until the reader opens it (sidebarWanted), and shown for any open
    // document -- thumbnails exist whether or not the file has bookmarks.
    QTabWidget *sidebar = nullptr;
    OutlinePanel *outlinePanel = nullptr;
    ThumbnailPanel *thumbnailPanel = nullptr;
    // Whether the reader asked for the sidebar, kept separate from whether it is
    // shown: a document with no bookmarks cannot show one, but the window's menu
    // item still reflects what was asked.
    bool sidebarWanted = false;

    // ---- search state ----
    //
    // The matches, in page order, and which of them is in front. Replaced
    // wholesale whenever a scan finishes: a search is a question with one
    // answer, and half of two answers is not a result.
    QVector<TextSearch::Match> searchMatches;
    int currentMatchIndex = -1;
    // What is being searched for. One string for the tab, and both of the
    // controls that can type it -- the bar in here and the box in the window's
    // toolbar -- show this one.
    QString searchNeedle;
    // Whether case matters for it, which is a setting of the search rather than
    // of either control: the toolbar box has no checkbox of its own, so it
    // searches the way the bar was last set.
    bool searchCase = false;
    // The flag the running scan checks between pages, and the identity of the
    // scan it belongs to: a scan whose flag is no longer `searchCancel` is
    // answering a question this tab has stopped asking, and its answer is
    // dropped.
    QSharedPointer<QAtomicInt> searchCancel;
    // The scans that have not finished, so that one is never still running when
    // the document it is reading is replaced -- in this object's destructor and
    // in open(), both of which call drainRenders().
    QVector<QFuture<QVector<TextSearch::Match>>> searchTasks;

    // The export, for the same reason and by the same means: it reads the
    // document page by page, and a reload under it is the crash drainRenders()
    // exists to prevent. Its flag is set first, so that the wait is one page
    // rather than a whole document.
    QFuture<WordExportReport> wordExportTask;
    QSharedPointer<QAtomicInt> exportCancel;

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
    // Set while open() is draining renders, so that a stray event delivered from
    // inside that wait cannot start a second open on this tab.
    bool opening = false;
    // Results that arrived for a question nobody is asking any more. Counted
    // rather than only dropped, so that "it happens all the time" is a number
    // rather than an impression.
    int discardedCount = 0;
};
