#include "pageview.h"

#include "../document/pagemetrics.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

// The gap between pages, and the least gap at the sides. One number: a document
// whose pages are separated by a different distance from the one at its edge
// looks like a mistake.
constexpr double kSpacing = 12.0;

// How far the keyboard moves, in logical pixels. A line is what a text editor
// calls it; this is a page reader, so the arrow keys move by something a reader
// can follow and space goes most of the way down.
constexpr int kArrowStep = 64;

// How far past the viewport a page has to come to be worth prefetching, in
// viewport heights. Half rather than a whole: a screenful of prefetch costs a
// screenful of memory, and what it buys is the page just off the edge being
// there when it is scrolled to. A whole screen was spending three times the
// memory of the pages actually in view.
constexpr double kPrefetchViewports = 0.5;

// The default prefetch allowance when the tab has not said. The tab sets it
// from the render cache's budget, which is the number the user can change.
constexpr qint64 kDefaultPrefetchBytes = 32LL * 1024 * 1024;

// The grid a band of an oversized page is rounded outward to, in device pixels.
// Without it the band would change on every pixel of scrolling, and a render
// per frame is a worse problem than the memory that banding is there to save.
constexpr int kBandGrid = 512;

// The largest a single render may be, in viewport areas. Four is well above
// what a page that fits on screen ever needs -- so the ordinary case is never
// banded -- and far below the 288 MB a page at 800% would otherwise cost.
constexpr int kMaximumViewportMultiple = 4;

// How many renders that produced nothing before a page is left alone. A page
// that cannot be rendered must not be retried forever: at 800% that would be a
// render per frame, each one failing.
constexpr int kMaximumRenderAttempts = 3;

// How big a dragged box has to be, in viewport pixels, before it counts as one.
// Smaller than this it is a click that missed by a few pixels, and turning that
// into "please recognize this" would be a dialog answering a question nobody
// asked.
constexpr int kMinimumRegionPixels = 8;

int normalizedRotation(int degrees)
{
    int normalized = degrees % 360;
    if (normalized < 0)
        normalized += 360;
    return normalized == 90 || normalized == 180 || normalized == 270 ? normalized : 0;
}

// The same rectangle moved onto whole device pixels.
//
// Pages are centred, so a page's left edge is routinely at x.5 -- and an image
// drawn at a fractional offset with no smoothing loses or doubles a row and a
// column of pixels, which is exactly where thin rules and small text are. The
// image itself is the region's size in device pixels, so moving the target by
// less than one device pixel is invisible and makes the copy exact.
QRectF alignToDevicePixels(const QRectF &rect, double ratio)
{
    if (ratio <= 0.0 || rect.isNull())
        return rect;
    return QRectF(std::round(rect.left() * ratio) / ratio,
                  std::round(rect.top() * ratio) / ratio,
                  std::round(rect.width() * ratio) / ratio,
                  std::round(rect.height() * ratio) / ratio);
}

// A box on the page, in the coordinates of the page as it is drawn.
//
// The same four cases as the point mapping in pagePointFor(), and the same
// reason: the text is in the page's own coordinates, and what is on screen is
// that page turned by a quarter, a half or three quarters. The arithmetic is
// exact -- no transform, no rounding of a rotated rectangle's four corners into
// a bounding box, which is what makes a highlight fit a glyph.
QRectF rotateBox(const QRectF &box, const QSizeF &pagePoints, int rotationDegrees)
{
    // Lived here once, and moved to PageMetrics when the tab needed the same
    // turn for the OCR crop: one formula, and not a copy of it that could drift.
    return PageMetrics::rotatedRect(box, pagePoints, rotationDegrees);
}

} // namespace

PageView::PageView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);

    // The vertical scrollbar never goes away, and that is not cosmetic.
    //
    // It changes the width of the viewport when it appears, and the width of
    // the viewport is what "fit width" fits to. A document that fits exactly
    // when there is no scrollbar does not fit once one appears, so the scrollbar
    // comes back, and a document that fits exactly when there is one does not
    // need it, so it goes -- a layout that oscillates between two states, each
    // one causing every visible page to be rendered again. Leaving it in place
    // costs thirteen pixels and removes the whole class of problem. This is the
    // flicker XYZip's preview had to fix the same way.
    //
    // The horizontal one is left as-needed: nothing about it feeds back --
    // "fit width" does not depend on the viewport's height, and "fit page" is
    // only ever chosen when the whole page is visible, in which case there is
    // nothing to scroll sideways to. It is also the one a reader never wants to
    // see, so it appears only when a page is genuinely wider than the window.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    viewport()->setAutoFillBackground(false);

    // Moves with no button held have to arrive here: they are what changes the
    // pointer's shape over a line of text.
    //
    // Two flags, not one, and both are needed. The platform decides whether to
    // send a button-less move at all by looking at the widget under the
    // pointer, which is the viewport -- so without the viewport's flag the
    // event is never generated. QAbstractScrollArea then decides whether to
    // hand its viewport's events on to this widget by looking at its own flag,
    // so without this one the event is generated and dropped. Having set only
    // the first, the cursor stayed an arrow and the events stopped here.
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    layout.setSpacing(kSpacing);
    prefetchBytes = kDefaultPrefetchBytes;
}

void PageView::setPageSizes(const QVector<QSizeF> &points)
{
    baseSizes = points;
    // Every image on screen was rendered for a different document. They are
    // dropped here rather than left to be overwritten: an image kept by page
    // number is the same number in the next document and would be drawn as a
    // page from the file that was open before.
    dropAllImages();
    // And so is the selection: it is a pair of character indices, and the
    // characters they point at belong to the document that has just gone.
    clearSelection();
    // And so are the search results: they are character indices into a text
    // that has just been replaced.
    searchMatches.clear();
    searchCurrent = PageTextRange();

    // A box being dragged, and a pan, belong to the document that was there
    // when they started.
    regionSelecting = false;
    regionDrag = QRect();
    if (panning) {
        panning = false;
        viewport()->releaseMouse();
        applyPointerShape(pointerShapeFor(QPoint()));
    }

    QVector<QSizeF> rotatedSizes;
    rotatedSizes.reserve(points.size());
    for (const QSizeF &size : std::as_const(points))
        rotatedSizes.append(PageMetrics::rotated(size, rotationDegrees));
    layout.setPageSizes(rotatedSizes);
    layout.setCurrentPage(0);

    // Fitted before anything is asked for, so that a document opened in "fit
    // width" requests its first page at the size it will be drawn at rather
    // than at 100% and then again at the right size.
    applyFitIfNeeded();

    updateScrollBars();
    requestVisiblePages();
    // The page number above is an intention ("a new document starts at its first
    // page"), not an observation, and the scroll position is not reset for a new
    // document -- so when the old position still falls inside the new document's
    // range, the first page is not what is on screen and the intention is a lie.
    // Measured: a 40-page document at page 2, then a two-page one whose scroll
    // maximum is larger, leaves the reader looking at page 2 with the status bar
    // reading "1 / 2".
    //
    // Asking the same question the other paths ask makes the number match the
    // screen. Where a new document *should* start is a different question, and
    // is not answered here -- see docs/store-submission.md §25.
    updateCurrentPageFromViewport();
    viewport()->update();
}

void PageView::clearDocument()
{
    setPageSizes({});
}

void PageView::setLayoutMode(PageLayout::Mode mode)
{
    layout.setMode(mode);
    // The images are still valid -- the same pages at the same size -- but the
    // rectangles they go in are not, so what is on screen is redrawn from the
    // new layout and the pages now in view are asked for.
    updateScrollBars();
    requestVisiblePages();
    // And one of those new rectangles is the one the middle of the window is in,
    // which is a different page more often than not: two pages per row halves how
    // deep into the document a given scroll position reaches.
    updateCurrentPageFromViewport();
    viewport()->update();
}

void PageView::setZoom(double zoom)
{
    const double clamped = ZoomController::clamp(zoom);
    if (qFuzzyCompare(layout.zoom(), clamped))
        return;

    // Invalidated first, requested after. The other order is not a detail: see
    // the note on the invalidated() signal in the header for what it costs.
    Q_EMIT invalidated();

    layout.setZoom(clamped);
    // The controller is told as well, which is what makes this a factor the
    // user chose rather than one a fit mode happened to produce. Without it the
    // mode stayed "fit width" and the next resize or wheel notch threw the
    // number away -- typing 200% and then dragging the window put it back to
    // fit width, and one notch of the wheel jumped to fit x 1.25.
    const ZoomController::Mode before = zoomController.mode();
    zoomController.setZoom(clamped);
    const ZoomController::Mode after = zoomController.mode();

    // Every image is the wrong size now, and its placement is worked out from
    // the size it was rendered at -- so they are not merely stale, they would
    // be drawn at the wrong scale.
    dropAllImages();
    updateScrollBars();
    requestVisiblePages();
    // Pages are taller or shorter now, so the one the middle of the window sits on
    // changed without anything scrolling -- the case this used to miss.
    updateCurrentPageFromViewport();
    Q_EMIT zoomChanged(clamped);
    if (before != after)
        Q_EMIT zoomModeChanged(after);
    viewport()->update();
}

void PageView::setZoomMode(ZoomController::Mode mode)
{
    if (zoomController.mode() == mode)
        return;
    if (mode == ZoomController::Mode::Percent) {
        // Entering percent mode through this door leaves the controller holding
        // whatever factor was typed last, while the view is at the one it is
        // drawing. Nothing calls it this way today -- setZoom() is how a factor
        // is chosen -- and the two disagreeing is the kind of thing that is
        // found later by someone who does. Cheap to make impossible.
        zoomController.setZoom(layout.zoom());
    }
    zoomController.setMode(mode);
    // Choosing "fit width" from the toolbar has to actually fit, and the page
    // it fits is the one on screen rather than the one the document opens on.
    applyFitIfNeeded();
    updateScrollBars();
    requestVisiblePages();
    // The change this is here for: a fit mode rescales every page, so a page boundary
    // crosses the middle of the window and no scroll event happens to notice. A fit is
    // also recomputed once more a moment later whenever the new page widths make a
    // sideways scrollbar come or go -- that second pass arrives as a resize event, which
    // asks the same question again for the same reason.
    updateCurrentPageFromViewport();
    Q_EMIT zoomModeChanged(mode);
}

double PageView::fittingZoom() const
{
    // What "fit" is fitted *to*. The layout decides: a page in the normal
    // modes, the whole spread in the two-page and book modes -- fitting one page
    // there would leave the facing page twice as wide as the window and force a
    // sideways scroll, the opposite of what "fit width" promises.
    //
    // The *largest* target in the document, not the one being read. That is not
    // a detail: fitting the current page is a feedback loop through the
    // horizontal scrollbar on any document that mixes page sizes, and it spins
    // at 100% of a core for as long as the document is open. The measurement
    // and the cost of this answer are in PageLayout::largestFitTarget().
    const QSizeF fitAgainst = layout.largestFitTarget();
    if (fitAgainst.isEmpty())
        return layout.zoom();
    const QSizeF viewportSize(viewport()->width(), viewport()->height());
    // The controller owns this decision for all three modes, including "the
    // factor the user chose, whatever the window is doing". Asking it rather
    // than repeating the rule here is what keeps the two from drifting.
    return zoomController.effectiveZoom(fitAgainst, viewportSize, kSpacing);
}

void PageView::applyFitIfNeeded()
{
    if (zoomController.mode() == ZoomController::Mode::Percent)
        return;
    const double wanted = fittingZoom();
    if (qFuzzyCompare(layout.zoom(), wanted))
        return;
    // The same order as setZoom, and for the same reason: this runs when a
    // fitted page changes size, and everything in flight is then for the old
    // one.
    Q_EMIT invalidated();
    layout.setZoom(wanted);
    dropAllImages();
    Q_EMIT zoomChanged(wanted);
}

void PageView::setRotation(int degrees)
{
    const int normalized = normalizedRotation(degrees);
    if (normalized == rotationDegrees)
        return;
    rotationDegrees = normalized;

    // Before the sizes and before the requests: the renderer has to be told
    // which way up to draw before it is asked to draw anything.
    Q_EMIT invalidated();

    QVector<QSizeF> rotatedSizes;
    rotatedSizes.reserve(baseSizes.size());
    for (const QSizeF &size : std::as_const(baseSizes))
        rotatedSizes.append(PageMetrics::rotated(size, rotationDegrees));
    layout.setPageSizes(rotatedSizes);

    dropAllImages();
    updateScrollBars();
    requestVisiblePages();
    // A quarter turn makes every page above this one shorter or taller, which moves the
    // middle of the window to another page without moving the scrollbar.
    updateCurrentPageFromViewport();
    Q_EMIT rotationChanged(rotationDegrees);
    viewport()->update();
}

void PageView::setPrefetchBudget(qint64 bytes)
{
    const qint64 clamped = qMax<qint64>(0, bytes);
    if (clamped == prefetchBytes)
        return;
    prefetchBytes = clamped;
    // Taking effect now rather than at the next scroll: lowering the limit has
    // to give the memory back, which is the reason somebody lowers it.
    requestVisiblePages();
}

void PageView::setPageImage(const RenderToken &token, const QImage &image)
{
    const int page = token.page;
    if (page < 0 || page >= pageCount())
        return;

    const auto outstanding = requested.constFind(page);
    if (outstanding == requested.constEnd())
        return; // nothing is waiting for this page: an answer to an old question

    if (image.isNull() || image.size() != token.imageSize()) {
        // No image came back, or one of the wrong size. Either way there is
        // nothing to draw, and the record of having asked has to go: the next
        // pass skips any page it believes is on its way, so leaving it would
        // keep this page a placeholder for good. That is what turned a single
        // failed render into a page that never came back -- and once memory ran
        // out, every page it touched stayed grey while the user scrolled on.
        requested.remove(page);
        images.remove(page);
        noteFailedRender(page);
        return;
    }

    if (!(outstanding.value() == token))
        return; // an answer to a different question about the same page

    images.insert(page, PageImage{token, image});
    failedRenders.remove(page);
    // The picture that was out of date is the one that just arrived, so the page is
    // current again -- and an answer that did not come back (the branch above) leaves
    // it stale, which is what keeps the retry going.
    stalePages.remove(page);

    const QPoint offset(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const QRectF pageRect = layout.rectOf(page);
    if (!pageRect.isNull())
        viewport()->update(pageRect.translated(-offset.x(), -offset.y())
                               .toAlignedRect()
                               .adjusted(-1, -1, 1, 1));
}

void PageView::noteFailedRender(int page)
{
    const int attempts = failedRenders.value(page) + 1;
    failedRenders.insert(page, attempts);
    if (attempts >= kMaximumRenderAttempts)
        return; // this page is not going to render; stop asking

    // Once more, on the next turn of the event loop rather than now: this runs
    // while a signal from the renderer is being delivered, and asking from
    // inside that would re-enter the request path.
    QTimer::singleShot(0, this, [this] { requestVisiblePages(); });
}

void PageView::dropAllImages()
{
    images.clear();
    requested.clear();
    failedRenders.clear();
    stalePages.clear();
}

void PageView::goToPage(int page)
{
    goToLocation(page, 0.0);
}

void PageView::goToLocation(int page, double offsetPoints)
{
    if (pageCount() == 0)
        return;
    const int clamped = qBound(0, page, pageCount() - 1);
    layout.setCurrentPage(clamped);
    updateScrollBars();

    const QRectF rect = layout.rectOf(clamped);
    if (!rect.isNull()) {
        // The page's top edge -- displaced by the destination, when a bookmark named a place
        // inside the page -- and the horizontal position left where it was: the layout centres
        // pages, so the horizontal scrollbar is only ever where a zoomed-in reader put it, and
        // moving it here would undo that on every page change.
        //
        // The offset is in points of the drawn page while the layout is in logical pixels, and
        // the page it may name is the laid-out one: that size already has the reader's rotation
        // in it, which is why the number the caller computed from the destination is a distance
        // from the top edge on screen. Zero stays exactly the old goToPage() arithmetic.
        //
        // The flag is not about the scrollbar, it is about who gets to say
        // which page is current. Moving the scrollbar runs the scroll handler,
        // which recomputes the current page from the middle of the viewport --
        // and for a page shorter than the viewport, that middle is inside the
        // *next* page. So without this the window announced the page that was
        // asked for and then, a moment later, the one after it, and the status
        // bar and the page box flickered through both.
        const double pageHeightPoints = layout.pagePoints().at(clamped).height();
        const double bounded = qBound(0.0, offsetPoints, pageHeightPoints);
        adjustingScroll = true;
        verticalScrollBar()->setValue(qRound(rect.top() + bounded * layout.zoom()));
        adjustingScroll = false;
    }
    requestVisiblePages();
    viewport()->update();
    Q_EMIT currentPageChanged(clamped);
}

void PageView::setColors(const QColor &backgroundColour, const QColor &placeholderColour,
                         const QColor &borderColour, const QColor &selectionColour)
{
    background = backgroundColour;
    placeholder = placeholderColour;
    border = borderColour;
    selection = selectionColour;
    viewport()->update();
}

void PageView::setNightInverted(bool inverted)
{
    if (night == inverted)
        return;
    night = inverted;

    // Before the requests, exactly as a rotation does it: an inverted page is a
    // different image from the same page, so whatever is in flight has to be told
    // it is no longer wanted before anything asks again.
    Q_EMIT invalidated();

    dropAllImages();
    requestVisiblePages();
}

QColor PageView::paperColour() const
{
    return night ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

// ---- text ----

void PageView::setDocument(Document *document)
{
    textSource = document;
    // Indices into text belong to the text they came from: kept across a
    // document change they would select a range of whatever is open now.
    clearSelection();
    // And the links are per-page into the document too -- a cached page of links
    // from the old file would point at pages of this one.
    linkCache.clear();
    // A field's focus belongs to the document that had it. Leaving it set is worse
    // than leaving a selection behind: the next keystroke would go into the form
    // layer of a file that is no longer here.
    if (form && form->hasFocusedField())
        form->releaseFocus();
}

void PageView::setFormSurface(FormFill *surface)
{
    // Whatever the last document's fields were doing, this view stops routing input
    // to them now -- including the drag, which would otherwise release into a form
    // layer that is not the one that took the press.
    inFormDrag = false;
    formDragPage = -1;
    form = surface;
    // The pointer's shape was chosen by the surface that is going away, so it is
    // recomputed on the next move rather than kept.
    applyPointerShape(activeTool == Tool::View ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

bool PageView::fieldHasFocus() const
{
    return form && form->hasFocusedField();
}

bool PageView::canUndoInField() const
{
    return form && form->canUndoFocused();
}

bool PageView::canRedoInField() const
{
    return form && form->canRedoFocused();
}

void PageView::undoInField()
{
    if (!form)
        return;
    form->undoFocused();
    applyFormCallbacks();
    Q_EMIT selectionChanged();
}

void PageView::redoInField()
{
    if (!form)
        return;
    form->redoFocused();
    applyFormCallbacks();
    Q_EMIT selectionChanged();
}

FormFill::Frame PageView::formFrameFor(int page) const
{
    FormFill::Frame frame;
    const QVector<QSizeF> &sizes = layout.pagePoints();
    if (page < 0 || page >= sizes.size())
        return frame;
    // The whole page at this zoom and this screen, and the reader's quarter turns --
    // the two the render was asked for. PageMetrics::pixelsFor is the same call
    // requestVisiblePages() makes, and it has to stay the same call: if the frame
    // this returns and the size the image was drawn at ever disagree, a click lands
    // on the wrong point of the page and the disagreement is invisible in both.
    frame.pixels = PageMetrics::pixelsFor(sizes.at(page), layout.zoom(), devicePixelRatioF());
    frame.quarterTurns = rotationDegrees == 90   ? 1
        : rotationDegrees == 180 ? 2
        : rotationDegrees == 270 ? 3
                                 : 0;
    return frame;
}

QPointF PageView::formDevicePoint(int page, const QPointF &viewportPoint) const
{
    const QVector<QSizeF> &sizes = layout.pagePoints();
    if (page < 0 || page >= sizes.size())
        return {};
    const QRectF rect = layout.rectOf(page);
    const QSize pagePixels = PageMetrics::pixelsFor(sizes.at(page), layout.zoom(),
                                                    devicePixelRatioF());
    if (rect.isEmpty() || pagePixels.isEmpty())
        return {};
    // Viewport to content coordinates (the scrollbars are what separates them), then
    // the page's own box within the content, then the same scale paintEvent() draws
    // the image with -- in logical pixels on one side and device pixels on the other.
    const QPointF inContent = viewportPoint + QPointF(horizontalScrollBar()->value(),
                                                      verticalScrollBar()->value());
    return QPointF((inContent.x() - rect.left()) * pagePixels.width() / rect.width(),
                   (inContent.y() - rect.top()) * pagePixels.height() / rect.height());
}

FormFill::Field PageView::fieldUnder(const QPoint &viewportPoint) const
{
    if (!form || !form->fillable())
        return FormFill::Field::None;
    const int page = pageAtPoint(viewportPoint);
    if (page < 0)
        return FormFill::Field::None;
    return form->fieldAt(page, formFrameFor(page), formDevicePoint(page, viewportPoint));
}

void PageView::applyFormCallbacks()
{
    if (!form)
        return;
    // The shape PDFium asked for, if it asked for one. Measured on this build:
    // FORM_OnMouseMove asks for nothing and the shape arrives while a field is
    // taking focus, so this is a nudge rather than the pointer's main answer --
    // fieldUnder() is that. It still has to be drained, because an unanswered
    // request stays in the queue and applies to the next event too.
    const FormFill::Pointer askedFor = form->takeRequestedPointer();
    if (askedFor != FormFill::Pointer::Arrow) {
        applyPointerShape(askedFor == FormFill::Pointer::IBeam ? Qt::IBeamCursor
                      : askedFor == FormFill::Pointer::Hand ? Qt::PointingHandCursor
                      : askedFor == FormFill::Pointer::Cross ? Qt::CrossCursor
                      : askedFor == FormFill::Pointer::Wait ? Qt::WaitCursor
                      : askedFor == FormFill::Pointer::UpDown ? Qt::SizeVerCursor
                      : askedFor == FormFill::Pointer::SizeAll ? Qt::SizeAllCursor
                      : askedFor == FormFill::Pointer::No ? Qt::ForbiddenCursor
                                                           : Qt::ArrowCursor);
    }

    const QVector<FormFill::Repaint> repaints = form->takeRepaints();
    if (repaints.isEmpty())
        return;

    // PDFium's rectangle is the answer to "what did you just change"; the answer to
    // "what is on screen that has to be drawn again" is the band this view already
    // asks for. So the rect is what makes the page stale, and neededRegion() is what
    // keeps the re-render to the part of the page the reader can see -- at 800% that
    // is a band, not the page, which is the case handoff-04 G1 3 asks about.
    QSet<int> pages;
    for (const FormFill::Repaint &repaint : repaints)
        pages.insert(repaint.page);
    for (const int page : std::as_const(pages)) {
        if (page < 0 || page >= pageCount())
            continue;
        // The cache first, then the generation, then the request: token equality
        // ignores the generation, so a page the cache still holds would come back
        // with the keystroke missing. See the note on formPageStale().
        Q_EMIT formPageStale(page);
        stalePages.insert(page);
        requested.remove(page); // so the page is asked for again, not left "on its way"
    }
    Q_EMIT invalidated();
    requestVisiblePages();
    // The reader changed the document, not just the screen. The tab's star, its 保存
    // rows and the close question all hang off this, and none of them can ask the
    // question themselves without a signal to ask it on.
    Q_EMIT formEdited();
}

void PageView::clearSelection()
{
    if (anchorPage < 0 && focusPage < 0)
        return;
    anchorPage = -1;
    anchorIndex = 0;
    focusPage = -1;
    focusIndex = 0;
    selecting = false;
    viewport()->update();
    Q_EMIT selectionChanged();
}

bool PageView::hasSelection() const
{
    return !selectedRanges().isEmpty();
}

QVector<PageTextRange> PageView::selectedRanges() const
{
    if (!textSource)
        return {};
    return selectionRanges(anchorPage, anchorIndex, focusPage, focusIndex, [this](int page) {
        return textSource->text(page).length();
    });
}

QString PageView::selectedText() const
{
    if (!textSource)
        return {};
    const QVector<PageTextRange> ranges = selectedRanges();
    if (ranges.isEmpty())
        return {};
    return selectionText(ranges, [this](int page) { return textSource->text(page); });
}

void PageView::selectAll()
{
    if (!textSource || pageCount() == 0) {
        clearSelection();
        return;
    }
    anchorPage = 0;
    anchorIndex = 0;
    focusPage = pageCount() - 1;
    // The last page's length, not "a big number": selectionRanges() clamps, but
    // the answer to "where does the text end" is the document's to give.
    //
    // This walks every page's text, because that is where the lengths are. On a
    // five-hundred-page document that is five hundred text pages loaded, which
    // is the same work M2's search has to move off this thread -- and it is why
    // the M2 note in document.h exists.
    focusIndex = textSource->text(focusPage).length();
    selecting = false;
    viewport()->update();
    Q_EMIT selectionChanged();
}

int PageView::pageAtPoint(const QPoint &viewportPoint) const
{
    const QPointF at(viewportPoint);
    for (int page = 0; page < pageCount(); ++page) {
        const QRectF rect = pageRectInViewport(page);
        if (!rect.isNull() && rect.contains(at))
            return page;
    }
    return -1;
}

QPointF PageView::pagePointFor(int page, const QPointF &viewportPoint) const
{
    const QVector<QSizeF> &sizes = layout.pagePoints();
    if (page < 0 || page >= sizes.size() || page >= baseSizes.size())
        return {};
    const QRectF rect = pageRectInViewport(page);
    if (rect.isEmpty())
        return {};

    // Where in the drawn page the pointer is, as a fraction of it. The drawn
    // page is the rotated one, and the text is in the page's own coordinates,
    // so the fraction has to be turned back through the rotation.
    const double u = qBound(0.0, (viewportPoint.x() - rect.left()) / rect.width(), 1.0);
    const double v = qBound(0.0, (viewportPoint.y() - rect.top()) / rect.height(), 1.0);
    const QSizeF points = baseSizes.at(page);
    switch (rotationDegrees) {
    case 90:
        // A quarter turn clockwise puts the page's top-left at the top-right:
        // across the screen is down the page, and down the screen is back along
        // the page's top edge.
        return QPointF(v * points.width(), (1.0 - u) * points.height());
    case 180:
        return QPointF((1.0 - u) * points.width(), (1.0 - v) * points.height());
    case 270:
        return QPointF((1.0 - v) * points.width(), u * points.height());
    default:
        return QPointF(u * points.width(), v * points.height());
    }
}

const QVector<Link> &PageView::linksFor(int page) const
{
    static const QVector<Link> none;
    if (!textSource || page < 0 || page >= pageCount())
        return none;
    auto found = linkCache.constFind(page);
    if (found != linkCache.constEnd())
        return found.value();
    // Fetched once per page. links() is a PDFium call, and the cursor asks on
    // every move, so the cache is what keeps that off the hot path.
    linkCache.insert(page, textSource->links(page));
    return linkCache.constFind(page).value();
}

const Link *PageView::linkAt(const QPoint &viewportPoint) const
{
    const int page = pageAtPoint(viewportPoint);
    if (page < 0)
        return nullptr;
    const QPointF at = pagePointFor(page, viewportPoint);
    const QVector<Link> &links = linksFor(page);
    for (const Link &link : links) {
        if (link.box.contains(at))
            return &link;
    }
    return nullptr;
}

void PageView::extendSelectionTo(const QPoint &viewportPoint)
{
    if (!textSource)
        return;
    const int page = pageAtPoint(viewportPoint);
    if (page < 0)
        return; // the gap between pages: keep the last position rather than jump
    const int index = textSource->characterIndexAt(page, pagePointFor(page, viewportPoint));
    if (index < 0)
        return; // not over any text; same

    if (page == focusPage && index == focusIndex)
        return;
    focusPage = page;
    focusIndex = index;
    viewport()->update();
    Q_EMIT selectionChanged();
}

void PageView::setTool(Tool tool)
{
    if (activeTool == tool)
        return;

    // Whatever the old tool had in progress ends with it. A box half-dragged or
    // a page half-panned belongs to the tool that started it -- carrying either
    // into the next one is a drag the user never asked for, and the release
    // would arrive at a handler that no longer has a press to go with it.
    if (regionSelecting) {
        regionSelecting = false;
        regionDrag = QRect();
    }
    if (panning) {
        panning = false;
        viewport()->releaseMouse();
    }
    selecting = false;

    activeTool = tool;

    // The pointer is the only thing on screen that says which tool is in force
    // before anything is dragged, so it changes with the tool. In Select mode
    // the next move refines it to an I-beam over a line of text; the other two
    // are the same everywhere on the page.
    applyPointerShape(tool == Tool::View ? Qt::OpenHandCursor
                      : (tool == Tool::Recognize || tool == Tool::Snapshot) ? Qt::CrossCursor
                                                                            : Qt::ArrowCursor);

    viewport()->update();
    Q_EMIT toolChanged(tool);
}

void PageView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const QPoint at = event->position().toPoint();

    // ---- the field ----
    //
    // A press on a field the reader can fill is not a pan and not a selection: it
    // goes to the form layer, which decides what that field does with it -- a text
    // field takes the caret, a checkbox toggles, a list box moves its selection. The
    // answer comes back from the call itself, so a push button or a signature (shown,
    // not answered: handoff-04 G1 5) falls through to exactly the behaviour this
    // function had before the form layer existed.
    if (form && activeTool != Tool::Recognize && activeTool != Tool::Snapshot
        && activeTool != Tool::Translate && form->fillable()) {
        const int page = pageAtPoint(at);
        if (page >= 0
            && form->mousePressed(page, formFrameFor(page), formDevicePoint(page, at),
                                  event->modifiers())) {
            inFormDrag = true;
            formDragPage = page;
            // One drag, one meaning -- the same rule the box and the selection follow.
            clearSelection();
            applyFormCallbacks();
            Q_EMIT formFocusChanged(form->hasFocusedField());
            event->accept();
            return;
        }
        if (form->hasFocusedField()) {
            // A click on paper, on a link, on nothing: the field lets go and commits.
            // The click is taken, because a reader who clicks away from a field they
            // were typing in asked for one thing -- to stop typing in it.
            form->releaseFocus();
            applyFormCallbacks();
            Q_EMIT formFocusChanged(false);
            event->accept();
            return;
        }
    }

    // ---- the hand ----
    //
    // A click on a link is not the start of a pan: in the reading tool, a link
    // under the pointer is what a click means, so it is followed and the press
    // stops there. Anything else the hand does is pick the page up and move it.
    if (activeTool == Tool::View) {
        if (const Link *link = linkAt(at)) {
            Q_EMIT linkActivated(*link);
            event->accept();
            return;
        }
        panning = true;
        panOrigin = at;
        viewport()->grabMouse();
        applyPointerShape(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    const int page = pageAtPoint(at);

    // ---- the box, or the selection ----
    //
    // Recognize always draws a box. Translate is the one tool that does both
    // jobs and decides which from where the pointer landed: over a line of text
    // it selects and translates the words, everywhere else it boxes, reads, and
    // translates -- which is the only way to translate a scan.
    const bool overText =
        page >= 0 && textSource && textSource->hasTextAt(page, pagePointFor(page, at));
    const bool wantsBox = activeTool == Tool::Recognize || activeTool == Tool::Snapshot
        || (activeTool == Tool::Translate && !overText);
    if (wantsBox) {
        if (page < 0)
            return; // the backdrop, or the gap between two pages
        // A previous selection goes with it: one drag, one meaning, and leaving
        // the old selection lit while a box is drawn would make both look like
        // they are about to do something.
        clearSelection();
        regionSelecting = true;
        regionOrigin = at;
        regionDrag = QRect();
        viewport()->update();
        return;
    }

    // ---- the selection ----
    if (!textSource) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    if (page < 0) {
        // The backdrop, or the gap between two pages: clicking there is how
        // everybody clears a selection.
        clearSelection();
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    // Anywhere on a page that has text anchors the selection at the nearest
    // character -- including the margin, which is where a drag that selects from
    // the start of a line begins. The pointer is stricter than this (an I-beam
    // only over the ink, an arrow over the margin), and deliberately: the
    // pointer answers "is there a line right here", the press answers "where
    // would a selection start". Making the press as strict as the pointer is how
    // a drag from just left of a line stops working.
    const int index = textSource->characterIndexAt(page, pagePointFor(page, at));
    if (index < 0) {
        clearSelection(); // a page of scans, or a picture: no text to select
        return;
    }

    // Shift extends from the existing anchor, which is what every text control
    // does and what makes a selection reachable without dragging.
    if (!event->modifiers().testFlag(Qt::ShiftModifier) || anchorPage < 0) {
        anchorPage = page;
        anchorIndex = index;
    }
    focusPage = page;
    focusIndex = index;
    selecting = true;
    viewport()->update();
    Q_EMIT selectionChanged();
}

void PageView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();

    if (inFormDrag && form) {
        // Between a press that went into a field and its release, the pointer is
        // selecting text inside that field, not on the page. Same mapping as the
        // press, so a drag that leaves the field's box stops where the box does.
        const int page = formDragPage;
        if (page >= 0) {
            form->mouseMoved(page, formFrameFor(page), formDevicePoint(page, at));
            applyFormCallbacks();
        }
        event->accept();
        return;
    }

    if (panning) {
        // The paper follows the pointer, so the scroll position moves against
        // it: dragging right shows what is to the left, dragging down shows
        // what is above. This is the whole of the hand tool, and the reason it
        // is not "scroll by the drag" is that a page picked up and moved is
        // what the hand means.
        const QPoint delta = at - panOrigin;
        panOrigin = at;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return;
    }

    if (regionSelecting) {
        regionDrag = QRect(regionOrigin, at).normalized();
        viewport()->update();
    } else if (selecting) {
        // Only while the button is held. Extending on plain movement is how a
        // selection gets dragged out from under somebody by a stray hand.
        extendSelectionTo(at);
    } else {
        updateCursorAt(at);
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

Qt::CursorShape PageView::pointerShapeFor(const QPoint &viewportPoint) const
{
    // A field the reader can fill says so with the pointer, in the two tools that
    // are not about marking the page up. This is the reading handoff-04 G1 1 asks for
    // (`FORM_OnMouseMove` + `FFI_SetCursor`), and measured on this build the
    // FORM_OnMouseMove half of that asks for no cursor at all -- moving over a text
    // field and over blank paper both produced no FFI_SetCursor call -- so the shape
    // comes from the hit test PDFium also offers, FPDFPage_HasFormFieldAtPoint, and
    // any shape a callback does request is applied in applyFormCallbacks().
    //
    // Only 选择 and 手形: in the four marking tools the pointer is the answer to a
    // different question ("what will this drag draw") and a field must not steal it.
    if (form && (activeTool == Tool::Select || activeTool == Tool::View)
        && form->fillable()) {
        switch (fieldUnder(viewportPoint)) {
        case FormFill::Field::TextField:
            return Qt::IBeamCursor;
        case FormFill::Field::CheckBox:
        case FormFill::Field::RadioButton:
        case FormFill::Field::ComboBox:
        case FormFill::Field::ListBox:
            return Qt::PointingHandCursor;
        case FormFill::Field::None:
        case FormFill::Field::PushButton:
        case FormFill::Field::Signature:
            break; // shown, not answered: the tool's own pointer stands
        }
    }

    switch (activeTool) {
    case Tool::View:
        // A hand everywhere, except over a link -- where it becomes the pointing
        // hand that says "this click goes somewhere".
        if (!panning && linkAt(viewportPoint))
            return Qt::PointingHandCursor;
        return panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    case Tool::Recognize:
    case Tool::Snapshot:
        // A crosshair, which is what "draw a box" looks like everywhere else,
        // and is not confusable with the others.
        return Qt::CrossCursor;
    case Tool::Translate:
        // The translate tool does both jobs, so it shows which one the next drag
        // will be: an I-beam over a line of text (select and translate), a
        // crosshair everywhere else (box, recognize, translate).
        if (textSource) {
            const int page = pageAtPoint(viewportPoint);
            if (page >= 0 && textSource->hasTextAt(page, pagePointFor(page, viewportPoint)))
                return Qt::IBeamCursor;
        }
        return Qt::CrossCursor;
    case Tool::Select:
        break;
    }

    // Selection: an I-beam over a line of text and an arrow everywhere else --
    // the pointer is the only thing that says "this can be selected", and it
    // has to be right about it, so it asks the strict question. What a *press*
    // does is more forgiving; see the note in mousePressEvent().
    if (!textSource)
        return Qt::ArrowCursor;
    const int page = pageAtPoint(viewportPoint);
    if (page < 0 || !textSource->hasTextAt(page, pagePointFor(page, viewportPoint)))
        return Qt::ArrowCursor;
    return Qt::IBeamCursor;
}

void PageView::updateCursorAt(const QPoint &viewportPoint)
{
    // Which question to ask depends on the tool and on the document, and asking
    // it on every move is the point: the pointer crosses lines, margins and
    // background as it goes, and it has to say which one it is over.
    applyPointerShape(pointerShapeFor(viewportPoint));
}

void PageView::applyPointerShape(Qt::CursorShape shape)
{
    // Only when the answer changes. setCursor() on every mouse move is a
    // window-system call per event for a shape that is almost always the one
    // already in force, and on a page of scans it would be that call for
    // nothing, thousands of times a second.
    if (shape == pointerShape)
        return;
    pointerShape = shape;
    viewport()->setCursor(shape);
}

void PageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && inFormDrag && form) {
        inFormDrag = false;
        const int page = formDragPage;
        formDragPage = -1;
        if (page >= 0) {
            form->mouseReleased(page, formFrameFor(page),
                                 formDevicePoint(page, event->position().toPoint()),
                                 event->modifiers());
            applyFormCallbacks();
            Q_EMIT formFocusChanged(form->hasFocusedField());
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && panning) {
        panning = false;
        viewport()->releaseMouse();
        // Back to the open hand: the tool has not changed, the drag has ended.
        applyPointerShape(Qt::OpenHandCursor);
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && regionSelecting) {
        regionSelecting = false;
        finishRegionDrag();
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && selecting) {
        extendSelectionTo(event->position().toPoint());
        selecting = false;
        // A press and a release in the same place is not a selection: it is a
        // click, and what it means is "put the cursor here", which for this
        // program is the same as having selected nothing.
        if (isEmptySelection(anchorPage, anchorIndex, focusPage, focusIndex))
            clearSelection();
        else if (activeTool == Tool::Translate)
            Q_EMIT translateRequested(); // the tool's whole purpose is to translate
        else
            Q_EMIT selectionFinished();
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void PageView::finishRegionDrag()
{
    const QRect dragged = regionDrag;
    regionDrag = QRect();
    viewport()->update();

    // A click, or almost one, was never a region.
    if (dragged.width() < kMinimumRegionPixels || dragged.height() < kMinimumRegionPixels)
        return;

    // Decided by where the box ended up, not where it started: a drag that
    // wanders across the gap between two pages is two half-boxes, and the one
    // in the majority is the one being asked about.
    const int page = pageAtPoint(dragged.center());
    if (page < 0)
        return;

    // Both corners through the same mapping a click takes. pagePointFor clamps
    // to the page, which is what a box hanging off the page's edge should do --
    // recognition cannot read what is not there -- and normalized() because a
    // quarter turn swaps the axes, which puts the mapped "top left" on the
    // wrong corner.
    const QRectF points = QRectF(pagePointFor(page, dragged.topLeft()),
                                 pagePointFor(page, dragged.bottomRight()))
                              .normalized();
    Q_EMIT regionCaptured(page, points);
}

void PageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // In a text field a double-click selects the word under the pointer, which is
    // the fastest way to replace one answer with another. Only when the field took
    // the press that came with it -- the check is the same one the press uses, so a
    // push button or a signature gets the gesture it got before this existed.
    if (form && event->button() == Qt::LeftButton && activeTool != Tool::Recognize
        && activeTool != Tool::Snapshot && activeTool != Tool::Translate
        && form->fillable() && FormFill::isInteractive(fieldUnder(event->position().toPoint()))) {
        const QPoint at = event->position().toPoint();
        const int page = pageAtPoint(at);
        if (page >= 0 && form->mouseDoubleClicked(page, formFrameFor(page),
                                                 formDevicePoint(page, at))) {
            applyFormCallbacks();
            event->accept();
            return;
        }
    }

    // Selecting a word is a thing the selection tool does, and the translate tool
    // does it too -- a double-clicked word is the fastest way to ask after one
    // term. In the other two a double-click is two clicks of the tool in force:
    // two pans, or two boxes the second of which is the click that cancels the
    // first.
    if ((activeTool != Tool::Select && activeTool != Tool::Translate) || !textSource
        || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }

    const QPoint at = event->position().toPoint();
    const int page = pageAtPoint(at);
    if (page < 0) {
        clearSelection();
        return;
    }
    const int index = textSource->characterIndexAt(page, pagePointFor(page, at));
    if (index < 0) {
        clearSelection();
        return;
    }

    // The word around the index: everything up to the nearest character that is
    // not part of a word. Chinese has no spaces, so a run of ideographs counts
    // as one word -- which is what every other reader does with it, and the
    // alternative is picking one character at a time by hand.
    const QString text = textSource->text(page);
    const auto isWordCharacter = [](QChar character) {
        return character.isLetterOrNumber() || character == QLatin1Char('_');
    };
    int from = qBound(0, index, text.length());
    int to = from;
    while (from > 0 && isWordCharacter(text.at(from - 1)))
        --from;
    while (to < text.length() && isWordCharacter(text.at(to)))
        ++to;

    if (to <= from) {
        // Punctuation or a space: nothing to select beyond the position itself.
        clearSelection();
        return;
    }

    anchorPage = page;
    anchorIndex = from;
    focusPage = page;
    focusIndex = to;
    selecting = false;
    viewport()->update();
    Q_EMIT selectionChanged();
    // A double-clicked word in the translate tool translates at once; in the
    // selection tool it is a finished selection the auto-translate mode may act
    // on.
    if (activeTool == Tool::Translate)
        Q_EMIT translateRequested();
    else
        Q_EMIT selectionFinished();
}

void PageView::contextMenuEvent(QContextMenuEvent *event)
{
    // The menu is only worth offering over a selection: right-clicking blank
    // paper, or a page of scans with no text to take, would show a menu of
    // actions that all do nothing -- and a menu of dead items reads as a bug
    // rather than as "there is nothing here". The tab's own actions stay
    // disabled in that case too, so the two agree.
    if (!hasSelection()) {
        QAbstractScrollArea::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction *copy = menu.addAction(tr("复制(&C)"));
    QAction *translate = menu.addAction(tr("翻译(&T)"));
    // The action the menu takes is the action the toolbar takes -- emitted out,
    // done by the tab, which is the one that can reach the clipboard and the
    // translation service.
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copy)
        Q_EMIT copyRequested();
    else if (chosen == translate)
        Q_EMIT translateRequested();
    event->accept();
}

void PageView::drawRanges(QPainter &painter, const QPoint &offset, const QRectF &visible,
                          const QVector<PageTextRange> &ranges, const QColor &colour,
                          int alpha) const
{
    if (!textSource || ranges.isEmpty())
        return;

    const QVector<QSizeF> &sizes = layout.pagePoints();
    QColor wash = colour;
    // Washed rather than filled: the text underneath has to stay readable, and
    // a highlight that hides what it highlights is the one thing neither a
    // selection nor a search result may do.
    wash.setAlpha(alpha);
    painter.setPen(Qt::NoPen);

    for (const PageTextRange &range : ranges) {
        if (range.isEmpty() || range.page >= sizes.size() || range.page >= baseSizes.size())
            continue;
        const QRectF rect = layout.rectOf(range.page);
        if (rect.isNull())
            continue;
        if (!rect.translated(-offset.x(), -offset.y()).intersects(visible))
            continue;

        const QSizeF drawn = sizes.at(range.page); // the rotated size, in points
        if (drawn.isEmpty())
            continue;

        const PageSelection part = textSource->pageSelection(range.page, range.from, range.to);
        const double scaleX = rect.width() / drawn.width();
        const double scaleY = rect.height() / drawn.height();
        const QPointF origin = rect.topLeft() - QPointF(offset.x(), offset.y());
        for (const QRectF &box : part.boxes) {
            // The box is in the page's own coordinates; the rectangle on screen
            // is the rotated frame of it. A rectangle turns by a quarter, a
            // half or three quarters exactly, so this is arithmetic rather than
            // a transform -- and exact, which matters because a highlight half
            // a pixel off a glyph looks broken.
            const QRectF turned = rotateBox(box, baseSizes.at(range.page), rotationDegrees);
            const QRectF washRect(origin.x() + turned.left() * scaleX,
                                  origin.y() + turned.top() * scaleY, turned.width() * scaleX,
                                  turned.height() * scaleY);
            painter.fillRect(washRect, wash);
        }
    }
}

void PageView::drawSelection(QPainter &painter, const QPoint &offset, const QRectF &visible) const
{
    // The selection is the user's own doing and is drawn over the search
    // results: when the two are on the same words, the one they just did with
    // the mouse is the one that has to be recognisable.
    drawRanges(painter, offset, visible, selectedRanges(), selection, 96);
}

void PageView::setSearchHighlights(const QVector<PageTextRange> &matches,
                                   const PageTextRange &current)
{
    if (matches == searchMatches && current == searchCurrent)
        return;
    searchMatches = matches;
    searchCurrent = current;
    viewport()->update();
}

QRectF PageView::rangeRectInViewport(const PageTextRange &range) const
{
    if (!textSource || range.isEmpty())
        return {};
    const QVector<QSizeF> &sizes = layout.pagePoints();
    if (range.page >= sizes.size() || range.page >= baseSizes.size())
        return {};
    const QRectF rect = layout.rectOf(range.page);
    if (rect.isNull())
        return {};

    // Where it is on the page, which is what the backend can say, and then
    // where the page is on screen, which is what the layout can say.
    const PageSelection part = textSource->pageSelection(range.page, range.from, range.to);
    if (part.boxes.isEmpty())
        return {};
    QRectF box = part.boxes.first();
    for (const QRectF &one : part.boxes)
        box = box.united(one);

    const QSizeF drawn = sizes.at(range.page);
    if (drawn.isEmpty())
        return {};
    const QRectF turned = rotateBox(box, baseSizes.at(range.page), rotationDegrees);
    const double scaleX = rect.width() / drawn.width();
    const double scaleY = rect.height() / drawn.height();
    const QRectF onPage(rect.left() + turned.left() * scaleX, rect.top() + turned.top() * scaleY,
                        turned.width() * scaleX, turned.height() * scaleY);
    return onPage.translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
}

void PageView::showRange(const PageTextRange &range)
{
    const QRectF onScreen = rangeRectInViewport(range);
    if (onScreen.isNull())
        return;

    const QRectF viewportRect(viewport()->rect());
    const QRectF content = visibleContentRect();

    // ----- horizontally: centred when it fits, its left edge otherwise -----
    double left = content.left();
    if (onScreen.width() >= viewportRect.width()) {
        // Wider than the window: its beginning is what is worth showing, since
        // that is where reading starts.
        left = onScreen.left() - kSpacing;
    } else if (onScreen.left() < viewportRect.left() || onScreen.right() > viewportRect.right()) {
        left = onScreen.center().x() - viewportRect.width() / 2.0;
    } else {
        left = content.left(); // already visible: leave the scrollbar where it is
    }

    // ----- vertically: a third of the way down -----
    //
    // A third rather than the top, because a match at the very top of the window
    // has none of its context above it -- and rather than the middle, which
    // leaves the text that leads up to it off the screen.
    double top = content.top();
    if (onScreen.top() < viewportRect.top() || onScreen.bottom() > viewportRect.bottom())
        top = onScreen.top() - viewportRect.height() / 3.0;
    else
        top = content.top();

    horizontalScrollBar()->setValue(qRound(left));
    verticalScrollBar()->setValue(qRound(top));

    // And the page it is on becomes the current one, which is what the page box
    // and the status bar show.
    if (range.page != layout.currentPage()) {
        layout.setCurrentPage(range.page);
        Q_EMIT currentPageChanged(range.page);
    }
}

QRectF PageView::pageRectInViewport(int page) const
{
    const QRectF rect = layout.rectOf(page);
    if (rect.isNull())
        return rect;
    return rect.translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
}

QSize PageView::minimumSizeHint() const
{
    return QSize(240, 180);
}

QRectF PageView::visibleContentRect() const
{
    return QRectF(horizontalScrollBar()->value(), verticalScrollBar()->value(),
                  viewport()->width(), viewport()->height());
}

void PageView::updateCurrentPageFromViewport()
{
    // While a jump is moving the scrollbar to the page it has already announced, that
    // jump owns the answer. See goToPage().
    if (adjustingScroll)
        return;
    const int page = layout.pageAtViewportCenter(visibleContentRect());
    if (page == layout.currentPage())
        return;
    layout.setCurrentPage(page);
    Q_EMIT currentPageChanged(page);
}

void PageView::updateScrollBars()
{
    const QSizeF content = layout.contentSize();
    const QSize area = viewport()->size();

    horizontalScrollBar()->setRange(0, qMax(0, qRound(content.width() - area.width())));
    horizontalScrollBar()->setPageStep(area.width());
    horizontalScrollBar()->setSingleStep(qMax(1, area.width() / 20));

    verticalScrollBar()->setRange(0, qMax(0, qRound(content.height() - area.height())));
    verticalScrollBar()->setPageStep(area.height());
    verticalScrollBar()->setSingleStep(qMax(1, kArrowStep / 2));
}

qint64 PageView::maximumRenderedPixels() const
{
    const double ratio = devicePixelRatioF();
    const qint64 width = qint64(viewport()->width() * ratio);
    const qint64 height = qint64(viewport()->height() * ratio);
    return qMax<qint64>(1, width * height * kMaximumViewportMultiple);
}

qint64 PageView::bytesOf(const QSize &pixels)
{
    // Four bytes a pixel, which is what a rendered page comes back as
    // (ARGB32). Only ever used to decide what to prefetch, so being right about
    // the format Qt uses is enough -- and it is the format.
    return qint64(pixels.width()) * qint64(pixels.height()) * 4;
}

QRect PageView::neededRegion(int page, const QSize &pagePixels) const
{
    const QRect whole(QPoint(0, 0), pagePixels);
    if (whole.isEmpty())
        return {};

    // The ordinary case: a page that fits on screen is rendered in one piece,
    // cached as one entry, and never re-rendered for a scroll. Banding it would
    // be work for nothing, and it is the case that has to stay fast.
    if (qint64(pagePixels.width()) * pagePixels.height() <= maximumRenderedPixels())
        return whole;

    const QRectF rect = layout.rectOf(page);
    if (rect.isNull() || rect.isEmpty())
        return whole;

    const double scaleX = double(pagePixels.width()) / rect.width();
    const double scaleY = double(pagePixels.height()) / rect.height();
    const QRectF visible = visibleContentRect();
    const QRectF onScreen((visible.left() - rect.left()) * scaleX,
                          (visible.top() - rect.top()) * scaleY, visible.width() * scaleX,
                          visible.height() * scaleY);
    const QRect wanted = onScreen.toAlignedRect();
    if (wanted.isEmpty())
        return whole;

    // Rounded outward to a grid, so that a small scroll stays inside the band
    // already rendered. This is what makes banding cost one render per screen
    // rather than one per pixel.
    const auto down = [](int value) { return (value / kBandGrid) * kBandGrid; };
    const auto up = [](int value) { return ((value + kBandGrid - 1) / kBandGrid) * kBandGrid; };
    const int left = qBound(0, down(wanted.left()), pagePixels.width() - 1);
    const int top = qBound(0, down(wanted.top()), pagePixels.height() - 1);
    const int right = qBound(left + 1, up(wanted.right() + 1), pagePixels.width());
    const int bottom = qBound(top + 1, up(wanted.bottom() + 1), pagePixels.height());
    return QRect(left, top, right - left, bottom - top);
}

bool PageView::isRendered(int page) const
{
    const QVector<QSizeF> &sizes = layout.pagePoints();
    if (page < 0 || page >= sizes.size())
        return false;
    // A field changed on this page. What is held is a picture of the page as it was
    // before the keystroke, and paintEvent() still draws it -- so the reader sees the
    // old page for the length of one render instead of a grey box, while this says
    // "not rendered" often enough for requestVisiblePages() to ask again.
    if (stalePages.contains(page))
        return false;
    const auto have = images.constFind(page);
    if (have == images.constEnd())
        return false;
    const QSize pagePixels =
        PageMetrics::pixelsFor(sizes.at(page), layout.zoom(), devicePixelRatioF());
    return have->token.pixels == pagePixels && have->token.rotationDegrees == rotationDegrees
           && have->token.covers(neededRegion(page, pagePixels));
}

void PageView::requestVisiblePages()
{
    const QVector<QSizeF> &sizes = layout.pagePoints();
    if (pageCount() == 0 || sizes.isEmpty()) {
        dropAllImages();
        return;
    }

    const double ratio = devicePixelRatioF();
    const QRectF visible = visibleContentRect();

    // What is on screen, and then as much of what is just off it as the
    // allowance pays for. Bytes rather than pages, because the whole trouble is
    // that a page can be fifty megabytes and "one screen of prefetch" is a
    // different amount of memory on every document.
    QVector<int> wanted = layout.pagesIntersecting(visible, 0.0);
    {
        QVector<QPair<qreal, int>> candidates;
        const QPointF centre = visible.center();
        for (const int page : layout.pagesIntersecting(visible, kPrefetchViewports)) {
            if (wanted.contains(page))
                continue;
            const QRectF rect = layout.rectOf(page);
            if (rect.isNull())
                continue; // single-page mode: nothing else is shown at all
            candidates.append({std::abs(rect.center().y() - centre.y()), page});
        }
        // Nearest first, so the allowance goes on the page just below the fold
        // rather than on one three screens away.
        std::sort(candidates.begin(), candidates.end(),
                  [](const QPair<qreal, int> &left, const QPair<qreal, int> &right) {
                      return left.first < right.first;
                  });

        qint64 allowance = prefetchBytes;
        for (const auto &candidate : std::as_const(candidates)) {
            const QSize pagePixels =
                PageMetrics::pixelsFor(sizes.at(candidate.second), layout.zoom(), ratio);
            const qint64 cost = bytesOf(neededRegion(candidate.second, pagePixels).size());
            if (cost > allowance)
                continue;
            allowance -= cost;
            wanted.append(candidate.second);
        }
    }

    // Let go of everything else. This is what bounds the memory: without it the
    // table grew by a page's worth every time somebody scrolled, and the tab's
    // budget constrained the other copy rather than the one being drawn.
    for (auto entry = images.begin(); entry != images.end();) {
        if (wanted.contains(entry.key()))
            ++entry;
        else
            entry = images.erase(entry);
    }
    for (auto entry = requested.begin(); entry != requested.end();) {
        if (wanted.contains(entry.key()))
            ++entry;
        else
            entry = requested.erase(entry);
    }
    // A page that left the view is not held any more, so it has nothing left to be
    // out of date. The next pass over it asks for a fresh render on its own.
    for (auto entry = stalePages.begin(); entry != stalePages.end();) {
        if (wanted.contains(*entry))
            ++entry;
        else
            entry = stalePages.erase(entry);
    }

    for (const int page : std::as_const(wanted)) {
        if (page < 0 || page >= sizes.size())
            continue;
        const QSize pagePixels = PageMetrics::pixelsFor(sizes.at(page), layout.zoom(), ratio);
        const QRect needed = neededRegion(page, pagePixels);
        if (needed.isEmpty())
            continue;
        const RenderToken token =
            makeRenderToken(0, page, pagePixels, needed, rotationDegrees, true);

        // Already waiting for exactly this. Asking again would be a second
        // render of the same picture.
        const auto outstanding = requested.constFind(page);
        if (outstanding != requested.constEnd() && outstanding.value() == token)
            continue;

        // Or already holding a picture that covers it -- which for a zoomed-in
        // page is the band rendered a moment ago, still covering the screen
        // after a small scroll. A picture larger than what is needed is kept
        // rather than replaced by a smaller one.
        const auto have = images.constFind(page);
        if (have != images.constEnd() && isRendered(page))
            continue;

        requested.insert(page, token);
        Q_EMIT pageImageNeeded(token);
    }
}

void PageView::drawPlaceholder(QPainter &painter, int page, const QRectF &rect) const
{
    painter.fillRect(rect, placeholder);
    painter.setPen(border);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    // The page number, so that a page still being rendered is identifiable
    // rather than an anonymous grey rectangle -- which matters most on the
    // scanned documents that take the longest to render.
    painter.setPen(placeholder.darker(180));
    QFont font = painter.font();
    font.setPointSizeF(qMax(9.0, font.pointSizeF()));
    painter.setFont(font);
    painter.drawText(rect, Qt::AlignCenter, QString::number(page + 1));
}

void PageView::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), background);

    if (pageCount() == 0)
        return;

    // Drawn at exactly the size they were rendered at: the request went out
    // with the device pixel ratio folded in, so the image is the region's size
    // in device pixels and drawing it into a device-pixel-aligned rectangle is
    // a 1:1 copy. Smoothing is therefore off -- it would resample an image that
    // is already pixel-for-pixel and soften it for nothing. The alignment is
    // what makes that 1:1 rather than nearly: see alignToDevicePixels().
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const double ratio = devicePixelRatioF();
    const QPoint offset(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const QRectF visible = visibleContentRect();

    for (int page = 0; page < pageCount(); ++page) {
        const QRectF rect = layout.rectOf(page);
        if (rect.isNull() || !rect.intersects(visible))
            continue;
        const QRectF pageOnScreen =
            alignToDevicePixels(rect.translated(-offset.x(), -offset.y()), ratio);

        const auto entry = images.constFind(page);
        if (entry == images.constEnd() || entry->image.isNull()) {
            drawPlaceholder(painter, page, pageOnScreen);
            continue;
        }

        const RenderToken &token = entry->token;
        const QRect region = token.region();
        const QSize pagePixels = token.pixels;
        if (pagePixels.isEmpty()) {
            drawPlaceholder(painter, page, pageOnScreen);
            continue;
        }

        // Where the rendered region sits inside the page, in logical pixels.
        const double scaleX = pageOnScreen.width() / pagePixels.width();
        const double scaleY = pageOnScreen.height() / pagePixels.height();
        const QRectF target = alignToDevicePixels(
            QRectF(pageOnScreen.left() + region.left() * scaleX,
                   pageOnScreen.top() + region.top() * scaleY, region.width() * scaleX,
                   region.height() * scaleY),
            ratio);

        if (region.size() == pagePixels) {
            // The paper. A PDF page has no background of its own: the renderer
            // draws the content, and the white is supplied by whoever shows the
            // result -- which is what every other reader does. Draw the content
            // straight onto the surround and the page comes out whatever colour
            // the surround is: measured at (214, 218, 222) on this machine,
            // which is the report that found it.
            painter.fillRect(target, paperColour());
            painter.drawImage(target, entry->image);
            painter.setPen(border);
            painter.drawRect(target.adjusted(0, 0, -1, -1));
            continue;
        }

        // A band of a page larger than the screen: what it does not cover is
        // still on its way, so the placeholder goes underneath it rather than
        // instead of it -- and it draws the page's own outline, which the band
        // would otherwise draw as a frame around itself.
        drawPlaceholder(painter, page, pageOnScreen);
        painter.fillRect(target, paperColour());
        painter.drawImage(target, entry->image);
    }

    // The search results, then the selection, over the finished page. Both are
    // overlays on the page and not part of it: drawing them with the page would
    // put the wash under the text of a page that renders a moment later.
    drawRanges(painter, offset, visible, searchMatches, highlight, 112);
    // The one being looked at, again: the same wash twice is how a match stands
    // out from the others without a second colour to explain.
    if (!searchCurrent.isEmpty())
        drawRanges(painter, offset, visible, {searchCurrent}, highlight, 150);
    drawSelection(painter, offset, visible);

    // And the box being dragged over that.
    drawRegionDrag(painter);
}

void PageView::drawRegionDrag(QPainter &painter) const
{
    if (regionDrag.isNull())
        return;

    // A thin wash of the accent and a solid edge: enough to see what is being
    // asked for, and no more than that. It is the selection's own colour,
    // because a box drag and a text drag are the same gesture answered two
    // ways, and a program that paints them in two colours is implying a
    // distinction nobody asked it to make.
    QColor fill = selection;
    fill.setAlpha(48);
    painter.fillRect(regionDrag, fill);
    painter.setPen(selection);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(regionDrag.adjusted(0, 0, -1, -1));
}

void PageView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    layout.setViewportSize(QSizeF(viewport()->size()));

    // A window dragged to a display with a different device pixel ratio needs
    // every page rendered again: the images on hand were rendered for the
    // pixels of the old screen, and stretching them is the blurry result this
    // whole arrangement exists to avoid.
    const double ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, lastDevicePixelRatio)) {
        lastDevicePixelRatio = ratio;
        dropAllImages();
    }

    // After the viewport size, because what fits depends on it, and before the
    // scrollbars, because the zoom decides how much there is to scroll.
    applyFitIfNeeded();
    updateScrollBars();
    requestVisiblePages();
    // A taller window reaches further down the document from the same scroll position,
    // and this event is also how the view hears that a scrollbar coming or going changed
    // the viewport's height underneath it -- neither moves the scrollbar, so neither used
    // to move the page number.
    updateCurrentPageFromViewport();
}

void PageView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    requestVisiblePages();

    // Scrolling is one of the ways which page is current gets decided; the others are the
    // changes that re-lay the pages out, and they are the ones that were missed. See
    // updateCurrentPageFromViewport().
    updateCurrentPageFromViewport();
    viewport()->update();
}

void PageView::scrollBy(int pixels)
{
    verticalScrollBar()->setValue(verticalScrollBar()->value() + pixels);
}

void PageView::wheelEvent(QWheelEvent *event)
{
    // Ctrl and the wheel zooms; anything else scrolls, which is what the base
    // class does and what every other program does.
    if (!event->modifiers().testFlag(Qt::ControlModifier)) {
        // What was carried belongs to a zoom gesture that has ended.
        wheelCarried = 0;

        // A list box that has the keyboard scrolls itself, and the page stays where it
        // is. That is the whole of the routing rule, and it is the engine's answer
        // rather than a guess about where the pointer is: measured on this build,
        // FORM_OnMouseWheel says yes only when the point is inside the widget that has
        // focus and that widget is a list -- it said no over blank paper, over a list
        // that had just lost its focus, and over a combo box with its own dropdown
        // open. So the call goes out first and the page scrolls only on a no.
        //
        // The tools are the press's own list, because a field can only have the keyboard
        // if a press put it there: two conditions that disagreed would let a tool keep
        // scrolling a list it could no longer click.
        if (form && activeTool != Tool::Recognize && activeTool != Tool::Snapshot
            && activeTool != Tool::Translate && form->fillable()) {
            const QPoint at = event->position().toPoint();
            const int page = pageAtPoint(at);
            if (page >= 0) {
                // Detents, not the raw delta: one call to the engine is one row however
                // large the number handed to it is, so a touchpad's flick would scroll
                // a twenty-row list by one row and each of its small deltas would scroll
                // it by one row too. ZoomController::wheelSteps() is the same counting
                // the zoom gesture already does.
                const int detents
                    = ZoomController::wheelSteps(formWheelCarried, event->angleDelta().y());
                if (detents != 0
                    && form->mouseWheeled(page, formFrameFor(page), formDevicePoint(page, at),
                                          detents, event->modifiers())) {
                    applyFormCallbacks();
                    event->accept();
                    return;
                }
            }
        }
        // The page scrolls on the event itself, exactly as it did before the form layer
        // existed, so what was half-way to a detent is thrown away rather than carried
        // into the next gesture and counted twice.
        formWheelCarried = 0;
        QAbstractScrollArea::wheelEvent(event);
        return;
    }

    // Through the accumulator rather than straight into a division: a touchpad
    // sends deltas far smaller than a detent and `delta / 120` threw every one
    // of them away. See ZoomController::wheelSteps().
    // What the form's half-detent belongs to is a scroll gesture, and this is a zoom.
    formWheelCarried = 0;
    const int steps = ZoomController::wheelSteps(wheelCarried, event->angleDelta().y());
    if (steps == 0) {
        // Accepted even though nothing happened: the gesture is a zoom, and
        // letting it through to the base class would scroll the page while the
        // reader is pinching it.
        event->accept();
        return;
    }

    const double from = layout.zoom();
    // One notch is always one step from where the view actually is. It is not
    // taken from the controller's stored factor: in a fit mode that number is
    // whatever was typed last, and the first notch would then jump the page to
    // a size from some earlier window.
    const double to = zoomController.stepped(from, steps);
    if (qFuzzyCompare(from, to)) {
        event->accept();
        return;
    }

    const QPointF anchor = event->position();
    const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());

    // Zoom first, then scroll: the new zoom changes how far the scrollbars can
    // go, and setting the position before that clamps it to the old range --
    // so zooming in on a page near its bottom edge would lose the anchor by
    // however far the range had grown.
    //
    // setZoom() is also what turns this into a factor the user chose rather
    // than one a fit mode produced, so there is nothing to do about the mode
    // here.
    setZoom(to);
    const QPointF target = ZoomController::scrollForAnchor(scroll, anchor, from, to);
    horizontalScrollBar()->setValue(qRound(target.x()));
    verticalScrollBar()->setValue(qRound(target.y()));

    event->accept();
}

void PageView::keyPressEvent(QKeyEvent *event)
{
    // ---- the field ----
    //
    // While one of this document's fields has the keyboard, the keys are the field's:
    // the arrows move its caret, Tab and Shift+Tab walk its own field order (across
    // pages, through FFI_GetPage), Backspace and Delete take its characters away and
    // Ctrl+C/V/X/Z/A are its clipboard and its undo. All of that is PDFium's answer to
    // FORM_OnKeyDown/OnChar, so the question goes there first and only a key the field
    // does not want comes back to the page -- which is the rule handoff-04 G1 1 ends
    // with: "字段没有焦点时，键盘快捷键照旧归应用".
    if (form && form->hasFocusedField()) {
        if (event->key() == Qt::Key_Escape) {
            // Esc is the reader asking to be out of the field, not a key the field
            // answers: it commits and lets go. Measured, FORM_ForceToKillFocus is also
            // the commit -- the value the reader typed reaches the field here, not
            // before -- which is what makes 保存 able to write a typed answer at all.
            form->releaseFocus();
            applyFormCallbacks();
            Q_EMIT formFocusChanged(false);
            event->accept();
            return;
        }
        const Qt::KeyboardModifiers modifiers = event->modifiers();
        bool handled = form->keyPressed(event->key(), modifiers);
        if (!handled && !event->text().isEmpty() && event->text().at(0).isPrint()) {
            // A key that is not a name and is not a control: the character itself.
            // PDFium's edit widget takes it through OnChar, one code unit at a time --
            // which is exactly the route an input method must NOT take, see
            // inputMethodEvent().
            for (const QChar character : event->text()) {
                if (form->characterTyped(character, modifiers))
                    handled = true;
            }
        }
        if (handled) {
            applyFormCallbacks();
            event->accept();
            return;
        }
        // Not the field's key. Everything the page does with a key -- PgUp, space,
        // the arrows -- stays available, which is what "a field that has focus does
        // not swallow the reader's navigation" means in practice; the ones a field
        // does want never reach here.
    }

    switch (event->key()) {
    case Qt::Key_PageDown:
        scrollBy(viewport()->height() - kArrowStep);
        return;
    case Qt::Key_PageUp:
        scrollBy(kArrowStep - viewport()->height());
        return;
    case Qt::Key_Space:
        // Space goes forward and Shift+Space back, which is what every reader
        // does and what the mouse wheel does not need because it has direction.
        scrollBy(event->modifiers().testFlag(Qt::ShiftModifier)
                     ? kArrowStep - viewport()->height()
                     : viewport()->height() - kArrowStep);
        return;
    case Qt::Key_Home:
        verticalScrollBar()->setValue(0);
        return;
    case Qt::Key_End:
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        return;
    case Qt::Key_Down:
        scrollBy(kArrowStep);
        return;
    case Qt::Key_Up:
        scrollBy(-kArrowStep);
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PageView::inputMethodEvent(QInputMethodEvent *event)
{
    // A composed string arrives here, not as keys: an input method that was asked to
    // type 中文 sends its candidate string in one commit, and sending the same text as
    // a run of FORM_OnChar calls is what breaks it -- the widget would take each
    // character as a separate keystroke, so the reader's one edit becomes several and
    // the composition the input method just resolved is pasted one glyph at a time
    // (handoff-04 G1 2).
    if (!form || !event->commitString().isEmpty()) {
        // The preedit string is deliberately ignored: this version draws no underlined
        // composition inside the field, so what the reader sees before they press space
        // is the input method's own window, not the page. Said out loud because it is
        // the one visible thing this route does not do.
        if (form) {
            form->commitText(event->commitString());
            applyFormCallbacks();
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::inputMethodEvent(event);
}

QVariant PageView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (!form || !form->hasFocusedField())
        return QAbstractScrollArea::inputMethodQuery(query);

    if (query == Qt::ImEnabled)
        return true;
    if (query == Qt::ImCursorRectangle) {
        // Where the candidate window goes. PDFium reports no caret position inside a
        // widget on this build -- nothing in fpdf_formfill.h asks the embedder to draw
        // one, and FFI_DisplayCaret is the XFA-only entry the header says to leave
        // null -- so the field's own rectangle is what a candidate window can be
        // placed against. The caret is inside it, so the window is never further from
        // what the reader is typing than the box they typed it into.
        int page = -1;
        QRectF userSpace;
        if (!form->focusedFieldRect(&page, &userSpace))
            return QAbstractScrollArea::inputMethodQuery(query);
        const QRectF rect = pageRectInViewport(page);
        const QSizeF points = [this, page] {
            const QVector<QSizeF> &sizes = layout.pagePoints();
            return page < sizes.size() ? sizes.at(page) : QSizeF();
        }();
        if (rect.isEmpty() || points.isEmpty())
            return QVariant();
        // User space has its origin at the page's bottom-left; the frame is the
        // displayed page, so the flip is the one a highlight already uses. Rotation is
        // not folded in here: a field's box on a page turned by the reader is drawn
        // inside the page's own frame, and the window only needs a point near it.
        const double scaleX = rect.width() / points.width();
        const double scaleY = rect.height() / points.height();
        return QRectF(rect.left() + userSpace.left() * scaleX,
                      rect.top() + (points.height() - userSpace.bottom()) * scaleY,
                      userSpace.width() * scaleX, userSpace.height() * scaleY);
    }
    return QAbstractScrollArea::inputMethodQuery(query);
}

void PageView::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    // The reader asked to type into the page. Whether that means a field or the
    // viewer's own keys is answered per keystroke; this only says that an input method
    // has somewhere to put a candidate window.
    setAttribute(Qt::WA_InputMethodEnabled, form && form->fillable());
}

void PageView::focusOutEvent(QFocusEvent *event)
{
    // Leaving the page is leaving the field: the value the reader typed is committed
    // and drawn, so that a 保存 or a 关闭 asked about afterwards sees the same text the
    // reader does. Without it, a field that still holds focus inside PDFium keeps its
    // edit in the widget, and the measurement in formfill.h's header says that value
    // does not reach the field -- and so does not reach the file.
    if (form && form->hasFocusedField()) {
        form->releaseFocus();
        applyFormCallbacks();
        Q_EMIT formFocusChanged(false);
    }
    QAbstractScrollArea::focusOutEvent(event);
}
