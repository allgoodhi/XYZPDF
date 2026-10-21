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

QVector<QPair<int, int>> PageLayout::spreadList() const
{
    QVector<QPair<int, int>> spreads;
    const int n = sizes.size();
    if (n == 0)
        return spreads;
    if (layoutMode == Mode::Book) {
        // Page 1 (index 0) on the right, facing a blank; then (2,3)(4,5)...
        spreads.append({-1, 0});
        for (int p = 1; p < n; p += 2)
            spreads.append({p, p + 1 < n ? p + 1 : -1});
    } else if (layoutMode == Mode::TwoPage) {
        // Page 1 on the left: (1,2)(3,4)...
        for (int p = 0; p < n; p += 2)
            spreads.append({p, p + 1 < n ? p + 1 : -1});
    }
    return spreads;
}

QSizeF PageLayout::fitTarget(int page) const
{
    if (page < 0 || page >= sizes.size())
        return {};
    if (layoutMode != Mode::TwoPage && layoutMode != Mode::Book)
        return sizes.at(page);
    for (const QPair<int, int> &spread : spreadList()) {
        if (spread.first != page && spread.second != page)
            continue;
        const int left = spread.first;
        const int right = spread.second;
        const QSizeF leftSize =
            left >= 0 ? sizes.at(left) : (right >= 0 ? sizes.at(right) : QSizeF());
        const QSizeF rightSize = right >= 0 ? sizes.at(right) : QSizeF();
        // The gap is left out: it is added in pixels by the layout and already
        // allowed for by the fit margin, so the target is the pages' own widths.
        return QSizeF(leftSize.width() + (right >= 0 ? rightSize.width() : 0.0),
                      qMax(leftSize.height(), rightSize.height()));
    }
    return sizes.at(page);
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

    if (layoutMode == Mode::TwoPage || layoutMode == Mode::Book) {
        // Both are spreads; spreadList() is the single source of how pages pair.
        // A spread is as wide as its two pages plus the gap between them and as
        // tall as the taller; a book's leading blank mirrors its partner so page
        // 1 sits on the right. The spreads then centre and stack as continuous
        // mode does, one spread being the unit instead of one page.
        const QVector<QPair<int, int>> spreads = spreadList();
        struct Box
        {
            double width;
            double height;
        };
        QVector<Box> boxes;
        boxes.reserve(spreads.size());
        double widestSpread = 0;
        for (const QPair<int, int> &spread : std::as_const(spreads)) {
            const int left = spread.first;
            const int right = spread.second;
            const QSizeF leftSize =
                left >= 0 ? drawn.at(left) : (right >= 0 ? drawn.at(right) : QSizeF());
            const QSizeF rightSize = right >= 0 ? drawn.at(right) : QSizeF();
            Box box;
            box.width = leftSize.width() + (right >= 0 ? gap + rightSize.width() : 0.0);
            box.height = qMax(leftSize.height(), rightSize.height());
            widestSpread = qMax(widestSpread, box.width);
            boxes.append(box);
        }

        const double width = qMax(widestSpread + 2 * gap, viewport.width());
        rects.fill(QRectF(), sizes.size());
        double y = gap;
        for (int s = 0; s < spreads.size(); ++s) {
            const int left = spreads.at(s).first;
            const int right = spreads.at(s).second;
            const QSizeF leftSize =
                left >= 0 ? drawn.at(left) : (right >= 0 ? drawn.at(right) : QSizeF());
            const double x = (width - boxes.at(s).width) / 2.0;
            if (left >= 0)
                rects[left] = QRectF(x, y, drawn.at(left).width(), drawn.at(left).height());
            if (right >= 0)
                rects[right] = QRectF(x + leftSize.width() + gap, y, drawn.at(right).width(),
                                      drawn.at(right).height());
            y += boxes.at(s).height + gap;
        }
        content = QSizeF(width, qMax(y, viewport.height()));
        return;
    }

    // Continuous: one page per row, centred.
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

QSizeF PageLayout::largestFitTarget() const
{
    QSizeF largest;
    const auto grow = [&largest](const QSizeF &candidate) {
        largest.setWidth(qMax(largest.width(), candidate.width()));
        largest.setHeight(qMax(largest.height(), candidate.height()));
    };

    // Walked directly rather than through fitTarget() for every page: in the
    // spread modes that function searches the spread list, so asking it once
    // per page would be quadratic on a long book for an answer that is the
    // same either way.
    if (layoutMode != Mode::TwoPage && layoutMode != Mode::Book) {
        for (const QSizeF &size : sizes)
            grow(size);
        return largest;
    }
    for (const QPair<int, int> &spread : spreadList()) {
        const int left = spread.first;
        const int right = spread.second;
        const QSizeF leftSize =
            left >= 0 ? sizes.at(left) : (right >= 0 ? sizes.at(right) : QSizeF());
        const QSizeF rightSize = right >= 0 ? sizes.at(right) : QSizeF();
        grow(QSizeF(leftSize.width() + (right >= 0 ? rightSize.width() : 0.0),
                    qMax(leftSize.height(), rightSize.height())));
    }
    return largest;
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
