#include "bookmarkposition.h"

#include "pagemetrics.h"

#include <QtGlobal>

#include <optional>

namespace {

// The destination's vertical place as a rectangle in the page's own top-left coordinates, or
// nothing when it names none. The rectangle is the line the kind describes, not necessarily a
// point: /FitH and /FitR span the page horizontally, so after a quarter turn they span it
// vertically and there is no one height left to jump to. An /XYZ anchor is deliberately a
// zero-size rect, which is why the answer is optional rather than a null QRectF.
std::optional<QRectF> anchorInPage(const BookmarkDest &dest, QSizeF pagePoints)
{
    const auto number = [&dest](int index) {
        return index < dest.numbers.size() ? dest.numbers.at(index) : std::optional<double>();
    };

    switch (dest.kind) {
    case BookmarkDest::Kind::XYZ: {
        const std::optional<double> top = number(1);
        if (!top)
            return {};
        // The x matters only when the reader's turn makes it vertical; the caller checks that
        // separately. Zero here keeps the rectangle a rectangle for the unrotated and half-turned
        // cases, where only the y is read.
        return QRectF(number(0).value_or(0.0), pagePoints.height() - *top, 0.0, 0.0);
    }
    case BookmarkDest::Kind::FitH:
    case BookmarkDest::Kind::FitBH: {
        const std::optional<double> top = number(0);
        if (!top)
            return {};
        return QRectF(0.0, pagePoints.height() - *top, pagePoints.width(), 0.0);
    }
    case BookmarkDest::Kind::FitR: {
        // /FitR left bottom right top: the top edge is what is visible, so it is what the view
        // lines up with the top of the window.
        const std::optional<double> top = number(3);
        if (!top)
            return {};
        const double left = number(0).value_or(0.0);
        const double right = number(2).value_or(pagePoints.width());
        return QRectF(left, pagePoints.height() - *top, right - left, 0.0);
    }
    case BookmarkDest::Kind::None:
    case BookmarkDest::Kind::Fit:
    case BookmarkDest::Kind::FitV:
    case BookmarkDest::Kind::FitBV:
    case BookmarkDest::Kind::FitB:
        break;
    }
    return {};
}

} // namespace

double bookmarkTopOffset(const BookmarkDest &dest, QSizeF pagePoints, int rotationDegrees)
{
    if (pagePoints.height() <= 0.0)
        return 0.0;

    // A quarter turn turns the page's x into the drawn page's vertical direction. /XYZ allows a
    // null x ("keep the horizontal position"), and there is no vertical answer for it once the
    // page is standing on its side -- page top is the honest fallback, not a guess at x = 0.
    const int turn = ((rotationDegrees % 360) + 360) % 360;
    if (dest.kind == BookmarkDest::Kind::XYZ && turn % 180 != 0
        && (dest.numbers.isEmpty() || !dest.numbers.constFirst()))
        return 0.0;

    const std::optional<QRectF> anchor = anchorInPage(dest, pagePoints);
    if (!anchor)
        return 0.0;

    const QSizeF drawn = PageMetrics::rotated(pagePoints, rotationDegrees);
    const QRectF onScreen = PageMetrics::rotatedRect(*anchor, pagePoints, rotationDegrees);
    // Places outside the paper are pulled to the nearest edge rather than scrolled past it: a
    // destination may name a height beyond the crop box, and a scrollbar told to go there would
    // clamp anyway -- one answer, here, where a test can see it.
    return qBound(0.0, onScreen.top(), drawn.height());
}
