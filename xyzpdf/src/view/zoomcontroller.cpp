#include "zoomcontroller.h"

#include <QtGlobal>

#include <cmath>

double ZoomController::clamp(double zoom)
{
    if (!std::isfinite(zoom))
        return 1.0;
    return qBound(Minimum, zoom, Maximum);
}

void ZoomController::setMode(Mode mode)
{
    zoomMode = mode;
}

void ZoomController::setZoom(double zoom)
{
    fixedZoom = clamp(zoom);
    zoomMode = Mode::Percent;
}

double ZoomController::fitZoom(Mode mode, const QSizeF &pagePoints, const QSizeF &viewport,
                               double margin) const
{
    if (pagePoints.isEmpty() || viewport.isEmpty())
        return fixedZoom;

    const double availableWidth = qMax(1.0, viewport.width() - 2 * margin);
    const double availableHeight = qMax(1.0, viewport.height() - 2 * margin);
    const double widthFit = availableWidth / pagePoints.width();

    if (mode == Mode::FitPage) {
        const double heightFit = availableHeight / pagePoints.height();
        // The smaller of the two, so the whole page is inside the viewport in
        // both directions. Taking the larger would fill the width and push the
        // bottom of the page off the screen, which is what "fit page" is
        // understood not to mean.
        return clamp(qMin(widthFit, heightFit));
    }
    return clamp(widthFit);
}

double ZoomController::effectiveZoom(const QSizeF &pagePoints, const QSizeF &viewport,
                                     double margin) const
{
    switch (zoomMode) {
    case Mode::FitWidth:
    case Mode::FitPage:
        return fitZoom(zoomMode, pagePoints, viewport, margin);
    case Mode::Percent:
        break;
    }
    return fixedZoom;
}

double ZoomController::stepped(double from, int steps) const
{
    if (steps == 0)
        return from;
    // Multiplied rather than added, so that a step out undoes a step in. A
    // fitted page is the starting point when the user has not chosen a factor
    // yet, which is why `from` is a parameter rather than read from fixedZoom.
    return clamp(from * std::pow(Step, steps));
}

QPointF ZoomController::scrollForAnchor(const QPointF &scroll, const QPointF &anchor,
                                        double fromZoom, double toZoom)
{
    if (fromZoom <= 0.0 || toZoom <= 0.0 || qFuzzyCompare(fromZoom, toZoom))
        return scroll;

    // The document point under the anchor, then where that point has to be for
    // the anchor to still be over it. The ratio is what makes this exact rather
    // than nearly right: converting through the document point keeps the answer
    // to one rounding, and scaling the scroll by toZoom/fromZoom does not,
    // because the anchor is not at the origin.
    const QPointF documentPoint((scroll.x() + anchor.x()) / fromZoom,
                                (scroll.y() + anchor.y()) / fromZoom);
    return QPointF(documentPoint.x() * toZoom - anchor.x(),
                   documentPoint.y() * toZoom - anchor.y());
}
