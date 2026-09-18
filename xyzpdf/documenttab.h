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

class FindBar;
class OcrDialog;
class PageRenderer;
class PageView;
class QtPdfBackend;
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

    // What the tab shows: the file name, and an asterisk-free name because v1
    // never modifies the file.
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
    // a QPointer, because the user may close it first.
    void startRecognition(const QImage &crop, QPointer<OcrDialog> dialog);

    // Emits the progress signal, and only ever runs on this object's thread: the
    // export reports from a worker, and this is how the report gets here.
    void notifyExportProgress(int done, int total);

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
    // Set while open() is draining renders, so that a stray event delivered from
    // inside that wait cannot start a second open on this tab.
    bool opening = false;
    // Results that arrived for a question nobody is asking any more. Counted
    // rather than only dropped, so that "it happens all the time" is a number
    // rather than an impression.
    int discardedCount = 0;
};
