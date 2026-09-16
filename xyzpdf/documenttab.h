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
#include "src/ocr/ocrengine.h"
#include "src/render/rendercache.h"
#include "src/view/pagelayout.h"
#include "src/view/pageview.h"
#include "src/view/zoomcontroller.h"

#include <QPointer>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include <memory>

class OcrDialog;
class PageRenderer;
class PageView;
class QtPdfBackend;

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

    PageView *view() const { return pageView; }

Q_SIGNALS:
    void titleChanged(const QString &title);
    void pageChanged(int page, int pageCount);
    void zoomChanged(double zoom);
    void rotationChanged(int degrees);
    void selectionChanged();
    void toolChanged(PageView::Tool tool);
    // The render cache's used bytes, for the status bar's honesty about memory.
    void cacheChanged(qint64 usedBytes, qint64 limitBytes);

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
    // Runs the recognition off the UI thread and fills the window in when it
    // is done. The crop is copied into the worker; the window is watched with
    // a QPointer, because the user may close it first.
    void startRecognition(const QImage &crop, QPointer<OcrDialog> dialog);

    // Declared before the renderer, and therefore destroyed after it: the
    // renderer holds a pointer to this document, and a document that goes away
    // first leaves it holding freed memory. Members are destroyed in reverse
    // order of declaration, which is the only thing making this safe -- hence
    // the comment, because reordering these two looks harmless.
    std::unique_ptr<QtPdfBackend> backend;
    std::unique_ptr<PageRenderer> renderer;

    RenderCache cache;
    PageView *pageView = nullptr;

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
