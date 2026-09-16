#include "pagelayout.h"

#include <QtGlobal>

void PageLayout::setMode(Mode mode)
{
    if (layoutMode == mode)
        return;
    layoutMode = mode;
    rebuild();
}

void PageLayout::setPageSizes(const QVector<QSizeF> &points)
{
    sizes = points;
    if (current >= sizes.size())
        current = sizes.isEmpty() ? 0 : sizes.size() - 1;
    rebuild();
}

void PageLayout::setZoom(double zoom)
{
    if (qFuzzyCompare(scale, zoom))
        return;
    scale = zoom;
    rebuild();
}

void PageLayout::setSpacing(double pixels)
{
    if (qFuzzyCompare(gap, pixels))
        return;
    gap = pixels;
    rebuild();
}

void PageLayout::setViewportSize(QSizeF size)
{
    if (viewport == size)
        return;
    viewport = size;
    rebuild();
}

void PageLayout::setCurrentPage(int page)
{
    const int clamped = sizes.isEmpty() ? 0 : qBound(0, page, sizes.size() - 1);
    if (clamped == current)
        return;
    current = clamped;
    // In continuous mode the rectangles do not move when the current page
    // changes -- scrolling does not re-lay anything out -- so the rebuild is
    // only for single-page mode. Asking anyway would throw away the whole
    // vector on every scroll event.
    if (layoutMode == Mode::SinglePage)
        rebuild();
}

QRectF PageLayout::rectOf(int page) const
{
    if (page < 0 || page >= rects.size())
        return {};
    return rects.at(page);
}

void PageLayout::rebuild()
{
    rects.clear();

    if (sizes.isEmpty()) {
        content = viewport;
        return;
    }

    // Every page's drawn size first, because in continuous mode each page is
    // centred against the widest one -- and that is not known until all of them
    // have been measured. Placing pages as they were measured instead centres
    // each against the widest page *so far*, which puts page 1 and page 2 on
    // different axes in a document with one landscape page near the end.
    QVector<QSizeF> drawn;
    drawn.reserve(sizes.size());
    double widest = 0;
    for (const QSizeF &points : std::as_const(sizes)) {
        const QSizeF size(points.width() * scale, points.height() * scale);
        drawn.append(size);
        widest = qMax(widest, size.width());
    }

    if (layoutMode == Mode::SinglePage) {
        // A null rectangle for every page that is not shown: the scroll area is
        // told the content is one page, and rectangles for the others would let
        // it scroll into empty space. pagesIntersecting() skips the null ones.
        rects.fill(QRectF(), sizes.size());
        const QSizeF size = drawn.at(current);
        content = QSizeF(qMax(size.width(), viewport.width()),
                         qMax(size.height(), viewport.height()));
        rects[current] = QRectF((content.width() - size.width()) / 2.0,
                                (content.height() - size.height()) / 2.0, size.width(),
                                size.height());
        return;
    }

    // Centred on the wider of the viewport and the pages: a page wider than the
    // window keeps its left margin and the scrollbar is how the rest of it is
    // reached.
    const double width = qMax(widest + 2 * gap, viewport.width());
    double y = gap;
    for (int page = 0; page < drawn.size(); ++page) {
        const QSizeF size = drawn.at(page);
        rects.append(QRectF((width - size.width()) / 2.0, y, size.width(), size.height()));
        y += size.height() + gap;
    }
    content = QSizeF(width, qMax(y, viewport.height()));
}

QSizeF PageLayout::contentSize() const
{
    return content;
}

QVector<int> PageLayout::pagesIntersecting(const QRectF &visible, double prefetch) const
{
    // Grown by whole viewports rather than by a pixel count, so that the answer
    // does not depend on the window size: the plan asks for the pages in view
    // plus one screen either side, and on a short window "one screen" is
    // rightly a shorter distance.
    const double margin = visible.height() * prefetch;
    const QRectF wanted = visible.adjusted(0, -margin, 0, margin);

    QVector<int> pages;
    for (int page = 0; page < rects.size(); ++page) {
        const QRectF &rect = rects.at(page);
        if (rect.isNull())
            continue;
        if (rect.intersects(wanted))
            pages.append(page);
    }
    return pages;
}

int PageLayout::pageAtViewportCenter(const QRectF &visible) const
{
    if (rects.isEmpty())
        return 0;
    const double centre = visible.center().y();
    for (int page = 0; page < rects.size(); ++page) {
        const QRectF &rect = rects.at(page);
        if (!rect.isNull() && centre < rect.bottom())
            return page;
    }
    return rects.size() - 1;
}
