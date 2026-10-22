// The page surface.
//
// ---- why not QPdfView ----
//
// QPdfView is a demonstration widget: it renders one page at a time with the
// scroll position as the only state. It has no cursor-anchored zoom (XYZip had
// to write that itself for its PDF preview), no page spacing, no placeholder
// while a page is being rendered, no rotation that survives a zoom, and no
// place to hang a selection or a highlight -- and the next milestone needs all
// of those, on the same widget that M5's editing handles will live on. Swapping
// it out later means moving every one of those, so it is not used from the
// start. See docs/PLAN.md §1.2.
//
// ---- what this class does and does not own ----
//
// It owns the scroll position, the layout arithmetic, the images on screen and
// the drawing. It does not own the document and does not render anything: it
// says which page -- and which part of a page -- it needs and at what size, and
// the tab hands back images. That split is what keeps the arithmetic testable
// (PageLayout, ZoomController) and the rendering off the UI thread
// (PageRenderer).
//
// ---- what it holds, and why that is bounded ----
//
// Two copies of a rendered page would be one copy too many: the tab keeps an
// LRU cache with a memory budget, and this keeps what is being drawn. So this
// keeps *only* the pages it is about to draw -- the ones in view and as many
// nearby ones as the prefetch allowance pays for -- and lets go of the rest the
// moment they stop being needed. The cache is what makes scrolling back
// instant; this is what keeps memory from growing with how far somebody has
// scrolled.
//
// An earlier version kept every page it had ever been given: an unbounded hash
// table that grew by 33-52 MB per page at the zoom this machine opens at, with
// the tab's 256 MB budget constraining nothing, because the images on screen
// were the other copy.
#pragma once

#include "pagelayout.h"
#include "zoomcontroller.h"

// Relative, like every other include from inside src/: the project root is not
// on the include path, and the files at the root reach into src/ by naming it.
#include "../document/document.h"
#include "../document/textlayer.h"
#include "../render/rendertoken.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QSizeF>
#include <QVector>

class PageView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    // What a mouse drag on the page does. One tool at a time, chosen by the
    // user rather than guessed from the pointer's position.
    //
    // ---- why explicit, after a version where it was not ----
    //
    // Dragging used to mean whichever of the two the pointer was pointing at:
    // an I-beam under the cursor meant "select text", an arrow meant "draw a
    // box". That was self-consistent and cost no interface, and it broke the
    // moment both features existed: selecting text in a scanned document -- or
    // anywhere the pointer happened to be between two lines -- was a box, and
    // there was no way to ask for the other one except by holding Alt, which is
    // a thing to be told rather than a thing to be discovered.
    //
    // With a tool chosen from the toolbar, each drag has exactly one meaning and
    // the pointer still says which: a hand, an I-beam, a crosshair.
    enum class Tool {
        // The paper follows the pointer. Nothing on the page is selected --
        // this is the tool for reading a document and moving around it.
        View,
        // Text is selected the way it always was: drag, double-click, Shift.
        Select,
        // A drag draws a box, and the text inside it is recognized from the
        // pixels -- which is the only way to read a scan or a picture.
        Recognize,
        // A drag means "translate what I point at": over text it selects the
        // words and translates them on release; over a picture it draws a box,
        // reads it with the recognizer, and translates that. The one tool that
        // answers a question rather than just marking up the page.
        Translate,
        // A drag draws the same box the recognizer draws, and what comes out
        // is the picture itself rather than the words in it. Its own tool
        // rather than a second button inside the recognition window: that
        // window is reached by asking for recognition, which runs the engine
        // over a crop somebody only wanted a picture of -- and nobody looking
        // for "copy this as an image" thinks to press 识别 first
        // (docs/review-03.md §3.3).
        Snapshot
    };

    explicit PageView(QWidget *parent = nullptr);

    // Page sizes in points, in page order. Empty means nothing is open.
    //
    // An empty size for a page (a document whose page could not be measured) is
    // treated as a page that is not drawn: the layout would otherwise have to
    // invent a size, and an invented size is a page that jumps when the real
    // one arrives.
    void setPageSizes(const QVector<QSizeF> &points);
    void clearDocument();
    int pageCount() const { return layout.pageCount(); }

    void setLayoutMode(PageLayout::Mode mode);
    PageLayout::Mode layoutMode() const { return layout.mode(); }

    // Zoom in logical pixels per point, and how it was arrived at. Setting the
    // mode does not change the factor: fit modes are recomputed on every
    // resize, and recomputing here as well would need a page size this class
    // has not chosen to care about -- the tab asks for fittingZoom().
    void setZoom(double zoom);
    double zoom() const { return layout.zoom(); }
    void setZoomMode(ZoomController::Mode mode);
    ZoomController::Mode zoomMode() const { return zoomController.mode(); }

    // What the current fit mode works out to for the page it fits, which is
    // what the toolbar's percentage box shows while fitted.
    double fittingZoom() const;

    // Clockwise, 0/90/180/270. Part of what an image is an answer to: rotating
    // while a render is in flight is exactly the case the generation counter
    // exists for, and the tab bumps it on rotationChanged.
    void setRotation(int degrees);
    int rotation() const { return rotationDegrees; }

    // How many bytes of nearby pages to keep rendered beyond the ones in view.
    //
    // Set by the tab from the render cache's budget, because that is the number
    // the user can change, and the point of prefetching is to spend a little
    // memory to hide a little latency -- not to render the neighbourhood. Zero
    // means only what is on screen.
    void setPrefetchBudget(qint64 bytes);

    // An image for a page, at whatever size and region was asked for. Kept
    // until it stops being needed.
    //
    // The token says what it is an answer to, and an image that is not an answer
    // to what this view is currently waiting for is dropped: a render that was
    // in flight when the zoom changed, or one that arrived after a new document
    // emptied the list. A null image is not an answer either -- it means the
    // render produced nothing, and the page has to be asked for again rather
    // than left as a placeholder for good.
    void setPageImage(const RenderToken &token, const QImage &image);

    // Whether a page has anything to draw rather than only a placeholder. The
    // status bar uses it to say that pages are still coming, and
    // documenttab_smoke uses it to check that a page does eventually appear --
    // which is what was broken while every signal here was in the right order
    // except one.
    bool hasImageFor(int page) const { return images.contains(page); }

    // Whether the image a page has covers what is on screen now, as opposed to
    // merely existing. A page held at one zoom and asked for at another, or a
    // band from one end of a long page while the reader is looking at the
    // other, is an image that is already on its way out -- and sampling the
    // screen before that one lands is how a test samples a placeholder and
    // reports the page grey. The status bar calls this state "rendering".
    bool isRendered(int page) const;

    // How many images are being held. The bounding above is the point, so this
    // is what a test can assert on.
    int heldImageCount() const { return images.size(); }

    // Where a page sits on screen, in viewport coordinates, or a null rectangle
    // for a page that is not shown. The one place that knows where the scroll
    // position has put it; the selection and highlight overlays in M2 ask the
    // same question, and so does the test that samples the screen.
    QRectF pageRectInViewport(int page) const;

    int currentPage() const { return layout.currentPage(); }
    void goToPage(int page);

    // ---- text ----
    //
    // The document, borrowed, for the questions a mouse asks of it: where is
    // the text under this point, and what is in this range. Not owned, and not
    // used for rendering -- images still come from the tab, which is what keeps
    // the rendering off this thread and testable on its own.
    //
    // Null means the view has no text to select, which is the honest state for
    // a document that failed to open.
    void setDocument(Document *document);

    void clearSelection();
    // Everything, from the first character of the first page to the last of the
    // last. Bounded by what the document has, so it is empty (rather than
    // everything) on a document of scans with no text layer.
    void selectAll();
    bool hasSelection() const;
    QString selectedText() const;
    // The ranges the selection covers, for a caller that wants to know where it
    // is -- and for the test that checks a drag backwards selects what a drag
    // forwards does.
    QVector<PageTextRange> selectedRanges() const;

    // The colours the view paints with, which come from the theme: the page
    // surface in a dark interface is not white, and the placeholder has to be
    // distinguishable from both the page and the backdrop. `selection` is the
    // colour the selected text is washed with.
    void setColors(const QColor &background, const QColor &placeholder, const QColor &border,
                   const QColor &selection);

    // Night reading: the paper is rendered inverted.
    //
    // A property of the render rather than a filter laid over the paint, because
    // that is what the rest of this class already assumes: an image is an answer
    // to a question, and "this page, inverted" is a different question from "this
    // page". So changing it invalidates what is held -- see setRotation, which is
    // the same shape for the same reason.
    //
    // The inversion itself happens in the tab that holds the renderer. This class
    // asks for images and draws what it is given, and it is given an answer.
    void setNightInverted(bool inverted);
    bool nightInverted() const { return night; }

    // ---- search ----
    //
    // The matches to wash over the pages, and which one is being looked at.
    // Ranges, exactly like a selection, because that is what a search result is:
    // a range of a page's text. Drawing them is the same code as the selection,
    // in another colour and a lighter wash, so that a match with the selection
    // over it is still readable.
    void setSearchHighlights(const QVector<PageTextRange> &matches, const PageTextRange &current);

    // Scrolls so that a range is on screen, which is what "go to the match"
    // means. A third of the way down rather than at the edge: a line at the very
    // top of the window is a line with nothing above it, and matches are read
    // with the text that came before them.
    void showRange(const PageTextRange &range);

    // What a drag on a page does, and what the pointer looks like while it is
    // over one. Changing it cancels whatever the previous tool had in progress:
    // a box half-dragged and a pan half-done belong to the tool that started
    // them, and carrying either into the next one is a drag nobody asked for.
    void setTool(Tool tool);
    Tool tool() const { return activeTool; }

    QSize minimumSizeHint() const override;

Q_SIGNALS:
    // Whatever is being rendered is now an answer to a question nobody asked.
    //
    // Emitted before the request that replaces it, and that order is the whole
    // point of having the signal at all. The receiver (DocumentTab) bumps the
    // renderer's generation here; if the request went out first it would carry
    // the old generation, come back after the bump, and be dropped as stale --
    // with the page's entry already recorded as "on its way", so nothing would
    // ever ask again. Zoom and rotation were both broken that way, and looked
    // like a page that stays grey forever.
    //
    // Anything added here that changes what an image is an answer to has to
    // emit this before it asks for anything.
    void invalidated();

    // A page -- or a band of one -- worth rendering, and at what size. The
    // receiver puts it through PageRenderer and calls setPageImage when it
    // comes back.
    //
    // The token has no generation: that belongs to the renderer and the tab
    // fills it in, which is what keeps this view from having to know about
    // generations at all.
    void pageImageNeeded(const RenderToken &token);

    void currentPageChanged(int page);
    void zoomChanged(double zoom);
    void zoomModeChanged(ZoomController::Mode mode);
    void rotationChanged(int degrees);

    // The selection changed, whether or not there is one. The window uses it to
    // enable Copy, which must not be offered when there is nothing to copy.
    void selectionChanged();

    // A rectangle the user dragged on a part of a page that has no text of its
    // own -- a scan, a picture, or anywhere else the pointer showed an arrow.
    // The rectangle is in the page's own coordinates (points, unrotated, like
    // everything else that names a place on a page), clamped to the page.
    //
    // A tiny drag is not reported: it is a click that missed, not a region.
    // Received by the tab, which renders the crop and has it recognized.
    void regionCaptured(int page, QRectF points);

    // The tool in force changed, whether the user changed it here or somewhere
    // else did. The window keeps its toolbar in step with this.
    void toolChanged(Tool tool);

    // The reader asked, from the right-click menu over a selection, to copy or
    // translate it. The view has no business with the clipboard or a network, so
    // it says what was asked and the tab does it -- the same division that keeps
    // rendering out of this class.
    void copyRequested();
    void translateRequested();

    // A link the reader clicked, in the reading (View) tool. The view does not
    // open URLs or move the page -- it says which link was hit and the tab acts
    // (internal jump, or an external link behind a confirmation).
    void linkActivated(const Link &link);

    // A selection was just completed -- the drag was released over text, or a
    // word was double-clicked. Emitted only on the finishing gesture, not on
    // every move, so a caller that acts on a finished selection (the "划线即翻"
    // auto-translate mode) is not asked to act while the reader is still dragging.
    void selectionFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // One rendered image, with the token it is an answer to: the token carries
    // which band of the page it is, which is what puts it in the right place
    // when only part of a page was rendered.
    struct PageImage
    {
        RenderToken token;
        QImage image;
    };

    // The part of the content the viewport is showing, in content coordinates.
    QRectF visibleContentRect() const;

    // Recomputes the zoom when a fit mode is in effect. Called whenever the
    // answer could have changed -- a new document, another page, a resize --
    // rather than on a signal from somewhere else, because a fit that is not
    // reapplied is a page that stops fitting.
    void applyFitIfNeeded();

    void updateScrollBars();

    // Whichever page the middle of the window sits on becomes the current one, and the
    // tab is told. The status bar's "N / M", the page box and the six 文档 dialogs'
    // "当前页（第 %1 页）" all read this class's answer, and the answer is a fact about
    // the geometry -- which rectangle the middle of the viewport falls in -- so it is
    // taken from the geometry wherever the geometry changes.
    //
    // That plural is the fix. It used to happen in the scroll handler only, and the
    // changes that re-lay the pages out do not scroll: 适应整页 makes every page a
    // different height, which moves a page boundary across the middle of the window
    // without moving the scrollbar a pixel. No scroll event, nothing re-derived, and the
    // reader was shown a number for a page they could not see -- docs/store-submission.md
    // §13 wrote that down from a real window.
    //
    // goToPage() deliberately does not call this. That path announces the page it is
    // moving to, and for a page shorter than the window the middle of the viewport is
    // inside the *next* one, so re-deriving here would take the announced page back
    // again -- which is the flicker `adjustingScroll` exists to stop.
    void updateCurrentPageFromViewport();

    // Works out what is worth having, lets go of everything else, and asks for
    // what is missing. Called from everywhere the answer can change.
    void requestVisiblePages();

    // Which part of a page is worth rendering at this zoom, in the page's own
    // pixel grid: the whole page when one image of it is affordable, and the
    // band on screen otherwise.
    QRect neededRegion(int page, const QSize &pagePixels) const;

    // Forgets every image and every outstanding request.
    //
    // For the changes that make an image mean something else -- another zoom,
    // another rotation, another screen -- rather than merely unwanted: those
    // keep the images they can still use. requestVisiblePages() is what trims
    // to what is wanted; this is for when what it would trim to is nothing.
    void dropAllImages();

    // The largest a single render may be, in device pixels. Past this a page is
    // rendered in bands. Four viewports, so that a page that fits on screen is
    // always under it: the ordinary case keeps its whole-page image and its one
    // cache entry, and only a zoomed-in page is cut up.
    qint64 maximumRenderedPixels() const;

    static qint64 bytesOf(const QSize &pixels);

    // Causes a page to be asked for again after a render that produced nothing.
    void noteFailedRender(int page);

    void drawPlaceholder(QPainter &painter, int page, const QRectF &rect) const;

    // The colour of the paper itself. A PDF page carries no background: the
    // renderer draws content over transparency and whoever shows it supplies the
    // white -- see the note where it is filled. Night reading has to darken that
    // too, or inverting the content leaves white text on white paper.
    QColor paperColour() const;

    // The selected text, washed over the pages it covers.
    void drawSelection(QPainter &painter, const QPoint &offset, const QRectF &visible) const;

    // The search matches, and the selection, drawn by one piece of code: both
    // are ranges of pages' text, and both are boxes from pageSelection() scaled
    // onto the drawn page.
    void drawRanges(QPainter &painter, const QPoint &offset, const QRectF &visible,
                    const QVector<PageTextRange> &ranges, const QColor &colour, int alpha) const;

    // Where a range sits on screen, in viewport coordinates: a null rectangle
    // for a range whose boxes cannot be had -- a page that is not laid out, or
    // one whose text the backend reports no geometry for.
    QRectF rangeRectInViewport(const PageTextRange &range) const;

    // The box being dragged over a text-free part of a page, drawn over
    // everything.
    void drawRegionDrag(QPainter &painter) const;

    // Turns a finished drag into a rectangle on a page, and says so. The two
    // corners go through the same mapping a click does, which is what makes a
    // box on a rotated page come back in the page's own coordinates.
    void finishRegionDrag();

    // Which page is under a point, in viewport coordinates, or -1 for the gap
    // between pages and the backdrop around them.
    int pageAtPoint(const QPoint &viewportPoint) const;

    // A point on screen turned into a point on the page, in the page's own
    // coordinates -- which is the unrotated ones, whatever the view is doing.
    QPointF pagePointFor(int page, const QPointF &viewportPoint) const;

    // A page's links, fetched from the document once and kept. links() is a
    // PDFium call behind the same global lock as rendering, so it is not
    // something to ask on every mouse move; the cursor and the click both go
    // through this cache, which is emptied when the document changes.
    const QVector<Link> &linksFor(int page) const;
    // The link under a viewport point, or null when there is none. Only asked in
    // the reading tool, where following a link is what a click means.
    const Link *linkAt(const QPoint &viewportPoint) const;

    // Moves the focus end of the selection to a point, if there is text there.
    void extendSelectionTo(const QPoint &viewportPoint);

    // The pointer's shape, from the tool in force and from what is under it:
    // a hand for View, an I-beam over a line of text for Select, a crosshair
    // for Recognize. Called from the move handler, and only changes the cursor
    // when the answer changes -- see the note in the implementation.
    void updateCursorAt(const QPoint &viewportPoint);
    Qt::CursorShape pointerShapeFor(const QPoint &viewportPoint) const;
    // Sets the pointer to `shape`, remembering it so that the next move does
    // not set it again for nothing.
    void applyPointerShape(Qt::CursorShape shape);

    // Scrolling, which is what the keyboard does: PgUp/PgDn move by a screen,
    // and the arrows and space by less.
    void scrollBy(int pixels);

    PageLayout layout;
    ZoomController zoomController;
    // Page sizes as the document reports them, in points and unrotated. The
    // layout is given the rotated ones; rotation belongs to the view rather
    // than to the layout, because it is also what the renderer has to be told.
    QVector<QSizeF> baseSizes;

    // What is being drawn, and what has been asked for. Both are trimmed to the
    // wanted set on every pass, so neither grows with how far somebody has
    // scrolled.
    QHash<int, PageImage> images;
    QHash<int, RenderToken> requested;
    // How many times a render for a page has come back empty since it last
    // worked. A page that cannot be rendered at all must not be retried
    // forever: at 800% a failing page would otherwise be a render every frame.
    QHash<int, int> failedRenders;

    // Borrowed, for text queries only. See setDocument().
    Document *textSource = nullptr;

    // A page's links, kept once fetched (see linksFor). Mutable because the
    // cursor's hit test is a const question that still has to populate it.
    mutable QHash<int, QVector<Link>> linkCache;

    // The two ends of the selection: where the drag started, and where the
    // pointer is. Page -1 means there is no selection. Which one is "before"
    // the other is not recorded, because a selection dragged backwards is the
    // same selection -- textlayer.h sorts them.
    int anchorPage = -1;
    int anchorIndex = 0;
    int focusPage = -1;
    int focusIndex = 0;
    Tool activeTool = Tool::Select;

    // Between the press and the release, so that a move without a button held
    // does not extend the selection -- which is how a selection gets dragged
    // out from under the user by a stray mouse movement.
    bool selecting = false;

    // Between the press and the release of the hand tool's drag, and where the
    // pointer was at the last move: the page is moved by the *difference*
    // between two positions, so the last one is what it is moved against.
    bool panning = false;
    QPoint panOrigin;

    // What the pointer was last set to, so that the cursor is changed when the
    // answer changes rather than on every mouse move.
    Qt::CursorShape pointerShape = Qt::ArrowCursor;

    // The same drag over a part of the page with no text of its own. Which one
    // a drag becomes is decided at the press, from the same question the cursor
    // already answered: an I-beam under the pointer is a text drag, and an
    // arrow is a box. The cursor is the interface here -- nothing else is
    // needed for the user to know which drag they are about to make.
    bool regionSelecting = false;
    // Where the box started, and what it now covers, in viewport coordinates.
    // Cleared the moment the drag ends: the box is a question, not a mark to
    // keep, and the answer arrives in a window of its own.
    QPoint regionOrigin;
    QRect regionDrag;

    int rotationDegrees = 0;
    double lastDevicePixelRatio = 1.0;
    // Night reading. Changes what an image is an answer to (see setNightInverted).
    bool night = false;
    // Wheel movement that has not yet added up to a zoom notch. A mouse
    // delivers a whole notch at a time and never uses this; a touchpad
    // delivers nothing else. See ZoomController::wheelSteps().
    int wheelCarried = 0;
    qint64 prefetchBytes = 0;    // Set while goToPage() moves the scrollbar, so that the scroll handler does
    // not recompute the current page from the new position and announce a
    // different one moments after the jump announced its own.
    bool adjustingScroll = false;

    // The search results, and which of them is being looked at. Empty in the
    // ordinary case, and drawn in both of the two ways a page can be drawn.
    QVector<PageTextRange> searchMatches;
    PageTextRange searchCurrent;

    QColor background = QColor(120, 124, 130);
    QColor placeholder = QColor(232, 234, 237);
    QColor border = QColor(198, 202, 208);
    QColor selection = QColor(57, 130, 207);
    // Amber, and deliberately not from the theme: a highlight is read against
    // white paper, which is the same paper in either theme, and the one colour
    // that has to be recognisable as "this is what you searched for".
    QColor highlight = QColor(255, 200, 60);
};
