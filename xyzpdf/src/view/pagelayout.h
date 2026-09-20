// Where each page sits, in logical pixels.
//
// Kept apart from PageView and free of Qt widgets on purpose: this is the part
// of the view that is arithmetic, and arithmetic is what a test can check.
// layout_smoke asserts on page positions and content height, which is the only
// way to notice that a page gained or lost a gap -- on screen that looks like a
// page drawn one pixel off, and nobody ever reports it.
//
// Coordinates are logical pixels of the viewport's own space (points times
// zoom), with the origin at the top-left of the content. A page is placed where
// it is, and the scroll area is told how big the whole thing is; nobody
// converts between the two outside this file.
//
// 双页 (two-page) and 书籍 (book) are both spreads: pages paired side by side,
// the spreads stacked and scrolled. They differ only in the pairing -- two-page
// starts (1,2)(3,4) with page 1 on the left, book starts (·,1)(2,3)(4,5) with
// page 1 on the right facing a blank, the way a real book opens. The pairing
// lives in one place (spreadList) so the layout and the fit-to-spread zoom cannot
// drift apart.
#pragma once

#include <QPair>
#include <QRectF>
#include <QSizeF>
#include <QVector>

class PageLayout
{
public:
    enum class Mode {
        // Pages stacked top to bottom, scrolled through.
        Continuous,
        // One page at a time.
        SinglePage,
        // Two pages side by side per spread (page 1 on the left), the spreads
        // stacked and scrolled like continuous mode.
        TwoPage,
        // Like TwoPage but a book: page 1 sits on the right facing a blank, then
        // the pages pair (2,3)(4,5)... the way a real book opens.
        Book
    };

    void setMode(Mode mode);
    Mode mode() const { return layoutMode; }

    // Page sizes in points, in page order. Setting these rebuilds.
    void setPageSizes(const QVector<QSizeF> &points);

    // The same sizes back, as they were given. The view needs them to work out
    // what "fit width" means for the page it is fitting, and asking the tab
    // again would mean two copies of the same list that can disagree.
    const QVector<QSizeF> &pagePoints() const { return sizes; }

    void setZoom(double zoom);
    double zoom() const { return scale; }

    // Gap between pages, and the minimum gap at the sides, in logical pixels.
    void setSpacing(double pixels);
    double spacing() const { return gap; }

    // The viewport's size in logical pixels. Needed because pages are centred
    // horizontally, so the layout changes when the window does even though no
    // page did.
    void setViewportSize(QSizeF size);

    // Which page SinglePage shows. Ignored in continuous mode.
    void setCurrentPage(int page);
    int currentPage() const { return current; }

    int pageCount() const { return sizes.size(); }

    // The rectangle each page occupies, in content coordinates.
    const QVector<QRectF> &pageRects() const { return rects; }

    QRectF rectOf(int page) const;

    // What "fit width" / "fit page" is fitted *to* for a given page. In the
    // spread modes that is the whole spread -- the two facing pages summed (a
    // book's leading blank mirrors its partner, so page 1 sits on the right at a
    // width that keeps both halves visible) -- not the single page, which would
    // leave the facing page off-screen and force a sideways scroll. For the other
    // modes it is just the page's own size.
    QSizeF fitTarget(int page) const;

    // How big the whole content is, which is what the scroll area is told.
    QSizeF contentSize() const;

    // The pages whose rectangles meet `visible` grown by `prefetch` viewports
    // in each direction: the ones worth rendering, and the reason a page just
    // off the bottom edge is already there when it is scrolled into view.
    QVector<int> pagesIntersecting(const QRectF &visible, double prefetch = 1.0) const;

    // The page covering the middle of `visible` -- what the status bar says and
    // what a bookmark jump is measured against.
    int pageAtViewportCenter(const QRectF &visible) const;

private:
    void rebuild();

    // The spreads as (left, right) page indices, -1 for a blank half. Empty for
    // the modes that are not spreads. The single source of the pairing, shared by
    // rebuild() and fitTarget() so the layout and the fit-to-spread zoom agree.
    QVector<QPair<int, int>> spreadList() const;

    Mode layoutMode = Mode::Continuous;
    QVector<QSizeF> sizes;
    QVector<QRectF> rects;
    double scale = 1.0;
    double gap = 12.0;
    QSizeF viewport;
    int current = 0;
    QSizeF content;
};
