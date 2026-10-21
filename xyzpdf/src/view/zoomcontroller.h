// The zoom factor, and the two things that are easy to get wrong about it.
//
// ---- fit modes are not a stored number ----
//
// FitWidth and FitPage are recomputed from the page and the window every time
// either changes, which is why the mode is stored and not just the resulting
// factor: a window that is resized while showing "fit width" has to stay fitted
// rather than keep the factor that happened to be right a moment ago.
//
// ---- zooming keeps the point under the cursor ----
//
// This is the part QPdfView does not do, and the reason for writing a view.
// Zooming without it moves the document out from under the pointer: the reader
// has found the line they care about, presses Ctrl and scrolls, and is now
// looking at a different part of the page. The arithmetic is in
// scrollForAnchor() below and is checked by zoom_smoke.
#pragma once

#include <QPointF>
#include <QSizeF>
#include <QtGlobal>

class ZoomController
{
public:
    enum class Mode {
        // Fills the viewport's width; scrolling is vertical only.
        FitWidth,
        // The whole page visible; nothing scrolls.
        FitPage,
        // A percentage the user chose.
        Percent
    };

    // 5% is smaller than anyone reads at and large enough that the rounding in
    // PageMetrics still leaves a pixel; 800% is where a PDF's own coordinate
    // precision starts to show as a wobbly line, so past it there is nothing to
    // gain by rendering more pixels.
    static constexpr double Minimum = 0.05;
    static constexpr double Maximum = 8.0;

    // One notch of Ctrl+wheel, as a ratio rather than a percentage: a step is
    // perceived as a proportion, so ten steps up and ten down return to where
    // they started, which adding 10% each time does not.
    static constexpr double Step = 1.25;

    void setMode(Mode mode);
    Mode mode() const { return zoomMode; }

    // The factor to use, given the page being fitted and the viewport to fit it
    // into, both in logical pixels except `pagePoints` which is in points. The
    // margin is what keeps a fitted page from touching the window edges.
    double effectiveZoom(const QSizeF &pagePoints, const QSizeF &viewport, double margin) const;

    // The factor FitWidth or FitPage would produce. Public because the toolbar
    // shows it as a percentage and the status bar reads it out.
    double fitZoom(Mode mode, const QSizeF &pagePoints, const QSizeF &viewport,
                   double margin) const;

    // Only meaningful in Percent mode; the other two ignore it.
    void setZoom(double zoom);
    double zoom() const { return fixedZoom; }

    // One notch, in or out, clamped. Returns the new factor so the caller does
    // not have to ask again.
    double stepped(double from, int steps) const;

    // How many notches an arriving wheel event is worth, carrying the
    // remainder over to the next one.
    //
    // ---- why the remainder has to be kept ----
    //
    // A mouse wheel detent is 120, so `delta / 120` worked for anyone with a
    // mouse and silently did nothing for anyone without one: a Windows
    // precision touchpad sends a stream of small deltas (20, 40) as the fingers
    // move, every one of which divides to zero. Ctrl plus two fingers therefore
    // zoomed by nothing at all, and because the view accepted the event it did
    // not scroll either -- the gesture did nothing whatsoever
    // (docs/review-03.md §2.3).
    //
    // `carried` is the caller's accumulator and is updated here. A change of
    // direction drops what was carried rather than cancelling against it, so
    // that a push the other way answers at once instead of first spending the
    // leftovers of the push before it.
    static int wheelSteps(int &carried, int angleDelta);

    // Where the scroll position has to be for the document point under `anchor`
    // to stay under it after the zoom changes.
    //
    // `scroll` and `anchor` are both in the viewport's space (the anchor is
    // where the pointer is, the scroll is the scrollbar's position, so the
    // document point is scroll + anchor). The answer is that document point at
    // the new zoom, less the anchor.
    static QPointF scrollForAnchor(const QPointF &scroll, const QPointF &anchor,
                                   double fromZoom, double toZoom);

    static double clamp(double zoom);

private:
    Mode zoomMode = Mode::FitWidth;
    // Only used in percent mode, and it survives a trip through a fit mode so
    // that going back to a factor by hand returns to the one that was chosen
    // rather than to whatever the window happened to fit at.
    double fixedZoom = 1.0;
};
