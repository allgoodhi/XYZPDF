// Where pages land.
//
// This is the arithmetic of the view, and the only way to check it: a page a
// few pixels out of place looks like nothing at all on screen, and a gap that
// is applied once too often is invisible until somebody counts the pixels. The
// numbers below are worked out by hand in the comments, which is the point.

#include "../src/view/pagelayout.h"

#include <QString>

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

bool close(double left, double right)
{
    return std::fabs(left - right) < 0.01;
}

void checkClose(double actual, double expected, const QString &what)
{
    if (close(actual, expected))
        return;
    std::cerr << "FAIL: " << what.toStdString() << " -- expected " << expected << ", got "
              << actual << '\n';
    ++failures;
}

// Two pages of different sizes, which is the case that catches a layout that
// centres each page against the widest one seen so far.
PageLayout twoPages()
{
    PageLayout layout;
    layout.setSpacing(10);
    layout.setViewportSize(QSizeF(400, 600));
    layout.setZoom(1.0);
    layout.setPageSizes({QSizeF(100, 200), QSizeF(50, 300)});
    return layout;
}

} // namespace

int main()
{
    // ---- continuous ----
    //
    // Content width is the widest page plus the two margins, or the viewport
    // when that is wider: max(100 + 20, 400) = 400. So page 1 is centred at
    // x = (400 - 100) / 2 = 150 and starts at the top margin y = 10, and page 2
    // at y = 10 + 200 + 10 = 220, x = (400 - 50) / 2 = 175.
    {
        PageLayout layout = twoPages();
        check(layout.mode() == PageLayout::Mode::Continuous, "continuous mode by default");
        check(layout.pageRects().size() == 2, "one rectangle per page");

        const QRectF first = layout.rectOf(0);
        checkClose(first.left(), 150, "page 1 is centred");
        checkClose(first.top(), 10, "page 1 starts at the margin");
        checkClose(first.width(), 100, "page 1 keeps its width");
        checkClose(first.height(), 200, "page 1 keeps its height");

        const QRectF second = layout.rectOf(1);
        checkClose(second.left(), 175,
                   "page 2 is centred on the widest page, not on itself");
        checkClose(second.top(), 220, "page 2 follows the gap after page 1");

        const QSizeF content = layout.contentSize();
        checkClose(content.width(), 400, "the content is as wide as the viewport");
        // 10 + 200 + 10 + 300 + 10 = 530, and the viewport is taller than that.
        checkClose(content.height(), 600, "the content is at least as tall as the viewport");
    }

    // A page wider than the window: the content grows, the sides keep their
    // margin, and the scrollbar is how the rest is reached.
    {
        PageLayout layout;
        layout.setSpacing(10);
        layout.setViewportSize(QSizeF(400, 600));
        layout.setZoom(1.0);
        layout.setPageSizes({QSizeF(800, 200)});
        // 800 + 2 * 10 = 820, which is wider than the 400-pixel viewport.
        checkClose(layout.contentSize().width(), 820, "a wide page widens the content");
        checkClose(layout.rectOf(0).left(), 10, "and keeps its left margin");
    }

    // Zoom scales the pages, not the margins: a gap that grew with the zoom
    // would leave a document at 400% with a corridor down the middle of it.
    {
        PageLayout layout = twoPages();
        layout.setZoom(2.0);
        checkClose(layout.rectOf(0).width(), 200, "the page is twice as wide");
        checkClose(layout.rectOf(1).top(), 10 + 400 + 10, "the gap stayed 10");
    }

    // ---- which pages are worth rendering ----
    {
        PageLayout layout = twoPages();
        // Scrolled to the top: page 1 is visible, and page 2 is not -- but one
        // viewport of prefetch reaches it, so both are asked for.
        const QVector<int> atTop = layout.pagesIntersecting(QRectF(0, 0, 400, 200), 1.0);
        check(atTop.contains(0) && atTop.contains(1), "the page below the fold is prefetched");

        // With no prefetch, only the page actually on screen.
        const QVector<int> noPrefetch = layout.pagesIntersecting(QRectF(0, 0, 400, 200), 0.0);
        check(noPrefetch.size() == 1 && noPrefetch.first() == 0,
              "without prefetch only the visible page is wanted");

        // The middle of the viewport decides which page the status bar names.
        check(layout.pageAtViewportCenter(QRectF(0, 0, 400, 200)) == 0,
              "the top of the document is page 1");
        check(layout.pageAtViewportCenter(QRectF(0, 300, 400, 200)) == 1,
              "scrolled down it is page 2");
        check(layout.pageAtViewportCenter(QRectF(0, 100000, 400, 200)) == 1,
              "past the end it is the last page");
    }

    // ---- single page ----
    {
        PageLayout layout;
        layout.setSpacing(10);
        layout.setViewportSize(QSizeF(400, 600));
        layout.setZoom(1.0);
        layout.setMode(PageLayout::Mode::SinglePage);
        layout.setPageSizes({QSizeF(100, 200), QSizeF(50, 300)});

        check(layout.rectOf(1).isNull(), "the page that is not shown has no rectangle");
        check(!layout.rectOf(0).isNull(), "the first page is shown");

        layout.setCurrentPage(1);
        check(layout.rectOf(0).isNull(), "and switching pages swaps which one that is");
        const QRectF shown = layout.rectOf(1);
        checkClose(shown.width(), 50, "the shown page keeps its size");
        // Centred in the viewport, which is taller than the page.
        checkClose(shown.top(), (600 - 300) / 2.0, "and is centred vertically");
        checkClose(layout.contentSize().width(), 400, "the content is the viewport");
        checkClose(layout.contentSize().height(), 600, "in both directions");
    }

    // ---- nothing open ----
    {
        PageLayout layout;
        layout.setViewportSize(QSizeF(400, 600));
        check(layout.pageCount() == 0, "no pages");
        check(layout.pageRects().isEmpty(), "and no rectangles");
        check(layout.pageAtViewportCenter(QRectF(0, 0, 400, 600)) == 0,
              "asking which page is in view says the first, not a crash");
        check(layout.rectOf(0).isNull(), "and asking for a rectangle says none");
    }

    if (failures == 0) {
        std::cout << "the page layout holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
