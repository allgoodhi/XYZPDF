// The zoom arithmetic.
//
// Two things are worth checking and neither is visible on screen when it is
// wrong by a little: what "fit width" and "fit page" work out to, and where the
// scroll position has to be for the point under the cursor to stay under it
// while zooming. The second one is the reason for writing a view at all -- QPdfView
// does not do it -- and a version that is nearly right is one where the line the
// reader was looking at has crept away by a few pixels per notch.

#include "../src/document/pagemetrics.h"
#include "../src/view/zoomcontroller.h"

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

void checkClose(double actual, double expected, const QString &what)
{
    if (std::fabs(actual - expected) < 0.0001)
        return;
    std::cerr << "FAIL: " << what.toStdString() << " -- expected " << expected << ", got "
              << actual << '\n';
    ++failures;
}

} // namespace

int main()
{
    const QSizeF a4(595, 842);
    const QSizeF viewport(1000, 800);
    const double margin = 12.0;

    ZoomController controller;

    // ---- fit width ----
    // (1000 - 24) / 595 = 1.640336...
    {
        controller.setMode(ZoomController::Mode::FitWidth);
        checkClose(controller.effectiveZoom(a4, viewport, margin), (1000 - 24) / 595.0,
                   "fit width divides the available width by the page width");
    }

    // ---- fit page ----
    // Width would allow 1.640336; the height allows (800 - 24) / 842 = 0.921615.
    // The smaller wins, or the page would be wider than the window and lose its
    // bottom edge -- which is not what "fit page" is understood to mean.
    {
        controller.setMode(ZoomController::Mode::FitPage);
        checkClose(controller.effectiveZoom(a4, viewport, margin), (800 - 24) / 842.0,
                   "fit page is the smaller of the two fits");
    }

    // ---- percent ----
    {
        controller.setZoom(1.5);
        check(controller.mode() == ZoomController::Mode::Percent,
              "setting a factor by hand leaves the fit modes");
        checkClose(controller.effectiveZoom(a4, viewport, margin), 1.5,
                   "and it is what is used");
    }

    // ---- the steps ----
    //
    // A step is a ratio, so a step out undoes a step in. Adding 25% each time
    // does not: three steps up and three down from 100% would land on 25%.
    {
        checkClose(controller.stepped(1.0, 1), 1.25, "one step up");
        checkClose(controller.stepped(1.25, -1), 1.0, "and it comes back");
        checkClose(controller.stepped(1.0, 0), 1.0, "no steps is no change");
        const double up = controller.stepped(controller.stepped(1.0, 1), 1);
        checkClose(up, 1.5625, "two steps are multiplicative");
    }

    // ---- the clamps ----
    {
        checkClose(controller.stepped(1.0, 100), ZoomController::Maximum,
                   "stepping past the maximum stops at it");
        checkClose(controller.stepped(1.0, -100), ZoomController::Minimum,
                   "and past the minimum at that");
        checkClose(ZoomController::clamp(0.0), ZoomController::Minimum,
                   "a zero zoom is clamped rather than becoming a null image");
        checkClose(ZoomController::clamp(-1.0), ZoomController::Minimum, "and so is a negative");
        checkClose(ZoomController::clamp(1e9), ZoomController::Maximum, "and a silly large one");
    }

    // ---- the point under the cursor ----
    //
    // The document point under the anchor is (scroll + anchor) / zoom, and the
    // new scroll is that point at the new zoom, less the anchor.
    {
        // At the top left, zooming in on the origin: the origin stays put, so
        // the scroll does not move either.
        const QPointF still = ZoomController::scrollForAnchor(QPointF(0, 0), QPointF(0, 0), 1.0, 2.0);
        checkClose(still.x(), 0, "the origin stays at the origin");
        checkClose(still.y(), 0, "in both directions");

        // Scrolled to (200, 100) at 100%, the pointer at (50, 25) is over the
        // document point (250, 125). At 200% that point is at (500, 250), so
        // the scroll has to be (500 - 50, 250 - 25) = (450, 225).
        const QPointF moved =
            ZoomController::scrollForAnchor(QPointF(200, 100), QPointF(50, 25), 1.0, 2.0);
        checkClose(moved.x(), 450, "zooming in keeps the point under the cursor");
        checkClose(moved.y(), 225, "in both directions");

        // And back out: at 200% scrolled to (450, 225) with the pointer at
        // (50, 25), the document point is (250, 125), and at 100% it is at
        // (250, 125), so the scroll returns to (200, 100).
        const QPointF back =
            ZoomController::scrollForAnchor(QPointF(450, 225), QPointF(50, 25), 2.0, 1.0);
        checkClose(back.x(), 200, "zooming out returns to where it was");
        checkClose(back.y(), 100, "in both directions");

        // No change in zoom, no change in scroll -- and no division by zero.
        const QPointF unchanged =
            ZoomController::scrollForAnchor(QPointF(37, 42), QPointF(5, 5), 1.0, 1.0);
        checkClose(unchanged.x(), 37, "the same zoom moves nothing");
        checkClose(unchanged.y(), 42, "in both directions");
        const QPointF guarded =
            ZoomController::scrollForAnchor(QPointF(37, 42), QPointF(5, 5), 0.0, 2.0);
        checkClose(guarded.x(), 37, "a zero zoom is refused rather than divided by");
    }

    // ---- points, pixels and rotation ----
    {
        checkClose(PageMetrics::pixelsFor(QSizeF(595, 842), 1.0, 2.0).width(), 1190,
                   "a 200% display renders twice the pixels");
        checkClose(PageMetrics::pixelsFor(QSizeF(595, 842), 0.5, 1.0).height(), 421,
                   "and a half-size zoom half the pixels");

        const QSize tiny = PageMetrics::pixelsFor(QSizeF(1, 1), 0.001, 1.0);
        check(tiny.width() >= 1 && tiny.height() >= 1,
              "a page that rounds to nothing still gets one pixel");

        const QSizeF turned = PageMetrics::rotated(QSizeF(300, 400), 90);
        checkClose(turned.width(), 400, "a quarter turn swaps the sides");
        checkClose(turned.height(), 300, "in both directions");
        checkClose(PageMetrics::rotated(QSizeF(300, 400), 180).width(), 300, "a half turn does not");
        checkClose(PageMetrics::rotated(QSizeF(300, 400), 360).width(), 300,
                   "and a full turn is no turn");
        checkClose(PageMetrics::rotated(QSizeF(300, 400), -90).width(), 400,
                   "a turn the other way counts as the same quarter turn");
    }

    if (failures == 0) {
        std::cout << "the zoom arithmetic holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
