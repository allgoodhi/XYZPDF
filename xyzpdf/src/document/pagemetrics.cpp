#include "pagemetrics.h"

#include <QtGlobal>

#include <cmath>

namespace PageMetrics {

QSizeF rotated(QSizeF size, int rotationDegrees)
{
    // Normalized before the test so that -90 and 450 mean what they say; a
    // caller that accumulates rotation by adding would otherwise land on 360
    // and get an unrotated page, which is right, or on 270 and get a width and
    // height that are the wrong way round, which is not.
    int normalized = rotationDegrees % 360;
    if (normalized < 0)
        normalized += 360;
    if (normalized == 90 || normalized == 270)
        return QSizeF(size.height(), size.width());
    return size;
}

QRectF rotatedRect(QRectF box, QSizeF pagePoints, int rotationDegrees)
{
    // The same four cases as a point's journey back through the rotation in
    // PageView::pagePointFor(): across the drawn page is down the page at 90,
    // and so on. Kept here rather than in the view so that the tab -- which
    // never sees the view's private arithmetic -- can name a region in the
    // drawn page too, which is what an OCR crop has to be in to match the
    // image the renderer produces.
    int normalized = rotationDegrees % 360;
    if (normalized < 0)
        normalized += 360;
    switch (normalized) {
    case 90:
        return QRectF(pagePoints.height() - box.bottom(), box.left(), box.height(), box.width());
    case 180:
        return QRectF(pagePoints.width() - box.right(), pagePoints.height() - box.bottom(),
                      box.width(), box.height());
    case 270:
        return QRectF(box.top(), pagePoints.width() - box.right(), box.height(), box.width());
    default:
        return box;
    }
}

QSize pixelsFor(QSizeF points, double zoom, double devicePixelRatio)
{
    // At least one pixel in each direction. A page rendered at a zoom so small
    // that it rounds to nothing produces a null QImage, and every caller then
    // has to ask whether it got one -- which is the kind of check that gets
    // forgotten in one place and shows up as a blank page in a corner case.
    const double scale = zoom * devicePixelRatio;
    const int width = qMax(1, static_cast<int>(std::lround(points.width() * scale)));
    const int height = qMax(1, static_cast<int>(std::lround(points.height() * scale)));
    return QSize(width, height);
}

} // namespace PageMetrics
