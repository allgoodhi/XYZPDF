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
