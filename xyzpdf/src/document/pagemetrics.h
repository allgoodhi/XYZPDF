// Points, pixels and rotation -- the three-way conversion every layout
// calculation needs, in one place.
//
// Small on purpose. The arithmetic here is a line each, and the reason it is a
// file rather than a line at each call site is that getting it wrong is
// invisible until a page is drawn slightly the wrong size on one machine: a
// high-DPI display renders at twice the pixels for the same points, and a
// zoom factor applied after that rather than before is a document that is
// sharp on one screen and blurry on another.
#pragma once

#include <QRectF>
#include <QSize>
#include <QSizeF>

namespace PageMetrics {

// The page's size as the reader sees it, with rotation applied: 90 and 270
// swap width and height. Anything else is 0, so an unrotated page is the
// identity.
QSizeF rotated(QSizeF size, int rotationDegrees);

// A rectangle in the page's own coordinates turned into the coordinates of the
// page as it is drawn -- the same three-way turn that `rotated` does to a size,
// applied to a box. Exact on purpose: no transform, and no rounding of four
// corners into a bounding box, which is what makes a highlight fit a glyph and
// a cropped region contain the picture it was drawn around.
//
// The caller says which way the page is drawn (`rotationDegrees`) and gives
// `pagePoints` in the page's OWN, unrotated coordinates -- the same space the
// boxes in PageSelection report.
QRectF rotatedRect(QRectF box, QSizeF pagePoints, int rotationDegrees);

// The pixel size to render a page at.
//
// `zoom` is 1.0 for 100%, and `devicePixelRatio` is the screen's -- 2.0 on a
// typical 200% display. Both multiply, and the ratio is not a scaling of the
// zoom: it says how many device pixels a logical one is worth, so a page
// rendered without it comes back at half resolution and is stretched to fit.
QSize pixelsFor(QSizeF points, double zoom, double devicePixelRatio);

} // namespace PageMetrics
