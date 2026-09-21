// The wiring between the view and the tab.
//
// ---- why this test exists ----
//
// Every part of this had a test and the whole of it did not. render_smoke
// checks PageRenderer on its own, layout_smoke and zoom_smoke check arithmetic,
// and none of them noticed that zooming and rotating left the page grey
// forever: PageView asked for a render *before* telling the tab to make the
// work in flight pointless, so every result came back for a generation that had
// already been superseded and was dropped -- and the page was recorded as "on
// its way" for the size that had just been asked for, so nothing ever asked
// again.
//
// The assertion therefore has to be about what ends up on screen, not about
// signals being emitted. "A request went out" is exactly what was true while
// this was broken.
//
// It runs on the offscreen platform: the point is the plumbing, and a window
// appearing on somebody's desktop while their tests run is not.

#include "../documenttab.h"
#include "../src/theme/thememanager.h"
#include "../src/ui/findbar.h"
#include "../src/ui/ocrdialog.h"
#include "../src/view/pageview.h"
#include "../src/view/zoomcontroller.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QString>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cmath>
#include <functional>
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

// Pumps the event loop until `ready` says so, or the time runs out. The
// renders are on other threads and come back through queued signals, so there
// is nothing to wait on but the event loop itself.
bool waitFor(const std::function<bool()> &ready, int timeoutMilliseconds = 30000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready()) {
        if (elapsed.elapsed() > timeoutMilliseconds)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

PageView *viewOf(DocumentTab &tab)
{
    return tab.view();
}

// One mouse event, straight to the viewport.
//
// Sent to the viewport rather than to the view because that is where Qt
// delivers a mouse to a scroll area from: QAbstractScrollArea filters its
// viewport's events and hands them on with the coordinates already in the
// viewport's system, which is the one pageRectInViewport() reports in.
void sendMouse(QWidget *viewport, QEvent::Type type, const QPointF &position,
               bool buttonHeld = false, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const Qt::MouseButton button = Qt::LeftButton;
    const Qt::MouseButtons buttons =
        type == QEvent::MouseMove
            ? (buttonHeld ? Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(Qt::NoButton))
            : Qt::MouseButtons(Qt::LeftButton);
    QMouseEvent event(type, position, viewport->mapToGlobal(position), button, buttons, modifiers);
    QApplication::sendEvent(viewport, &event);
}

} // namespace

int main(int argc, char **argv)
{
    // Before QApplication: the platform has to be chosen before it starts.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    const QString path = directory + QStringLiteral("/text.pdf");

    DocumentTab tab;
    tab.resize(900, 700);
    tab.show();

    if (tab.open(path) != Document::Status::Ready) {
        std::cerr << "FAIL: could not open the fixture " << path.toStdString() << ": "
                  << tab.lastError().toStdString() << '\n';
        return 1;
    }

    PageView *view = viewOf(tab);

    // ---- the first page appears ----
    {
        check(waitFor([view] { return view->hasImageFor(0); }),
              "the page on screen is rendered and drawn");
        check(tab.pageCount() == 2, "the document's pages are known to the tab");
    }

    // ---- zooming still shows a page ----
    //
    // The regression. Changing the zoom drops the images, so the check is that
    // a page comes back afterwards -- and it is checked from nothing, because
    // the "has an image" answer is what a user sees as "not a grey rectangle".
    {
        const double before = view->zoom();
        tab.setZoom(before * 2.0);

        check(!view->hasImageFor(0),
              "changing the zoom drops the image that no longer fits the page");
        check(waitFor([view] { return view->hasImageFor(0); }),
              "and a page is rendered again at the new size -- this is what was broken");
        check(std::fabs(view->zoom() - before * 2.0) < 0.001,
              "at the zoom that was asked for");
    }

    // ---- rotating still shows a page ----
    {
        const int before = view->rotation();
        tab.rotateBy(90);
        check(view->rotation() == (before + 90) % 360, "the rotation was taken");

        check(!view->hasImageFor(0), "the old image is dropped, because it is the wrong way up");
        check(waitFor([view] { return view->hasImageFor(0); }),
              "and a page is rendered again the right way up");
    }

    // ---- a factor chosen by hand survives a resize ----
    //
    // The second regression: setZoom() used to leave the controller in "fit
    // width", so the next resize (or one notch of the wheel) replaced the
    // factor the user had typed with whatever fitted.
    {
        view->setZoom(2.0);
        check(view->zoomMode() == ZoomController::Mode::Percent,
              "setting a factor by hand takes the view out of a fit mode");
        check(waitFor([view] { return view->hasImageFor(0); }), "and the page is rendered");

        tab.resize(700, 520);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(std::fabs(view->zoom() - 2.0) < 0.001,
              "and a resize does not throw it away");

        // While a fit mode is the one in force, a resize does change the zoom --
        // that is the whole point of a fit mode, and the check above would pass
        // just as well if the fit had stopped working altogether.
        view->setZoomMode(ZoomController::Mode::FitWidth);
        const double fitted = view->zoom();
        check(view->zoomMode() == ZoomController::Mode::FitWidth, "fit width is in force");
        tab.resize(520, 700);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(std::fabs(view->zoom() - fitted) > 0.001,
              "and a narrower window fits a narrower page");
    }

    // ---- a jump announces one page, not two ----
    //
    // goToPage() moves the scrollbar, which used to run the scroll handler,
    // which recomputed the current page from the middle of the viewport and
    // announced that as well -- so the page box and the status bar flickered
    // through two numbers, the second of which was often the page after the one
    // that was asked for.
    {
        int announcements = 0;
        int last = -1;
        const QMetaObject::Connection listening =
            QObject::connect(view, &PageView::currentPageChanged, view, [&](int page) {
                ++announcements;
                last = page;
            });

        tab.setCurrentPage(1);
        check(announcements == 1,
              QStringLiteral("jumping announces the page once, not once per scroll event (got %1)")
                  .arg(announcements));
        check(last == 1, "and announces the page that was asked for");

        // Off again before the locals go out of scope: the connection lives as
        // long as the view does, and a lambda that captures locals by reference
        // is a use-after-free the moment anything else scrolls.
        QObject::disconnect(listening);
    }

    // ---- a page larger than the screen is rendered as a band ----
    //
    // At 800% a page is 7140 by 10000 device pixels -- 288 MB for the one page,
    // more than the entire render budget, on a machine where "fit width" is
    // already 33 MB a page. A page bigger than a few viewports is therefore
    // rendered as the band that is on screen, and this is the assertion that it
    // happens rather than being described in a comment.
    {
        QVector<RenderToken> asked;
        const QMetaObject::Connection listening = QObject::connect(
            view, &PageView::pageImageNeeded, view,
            [&asked](const RenderToken &token) { asked.append(token); });

        view->setZoom(4.0);
        check(waitFor([view] { return view->hasImageFor(0); }), "a banded page arrives");

        check(!asked.isEmpty(), "the zoomed page was asked for");
        const RenderToken band = asked.last();
        check(!band.clip.isNull(), "as a band rather than as a whole page");
        check(band.imageSize() != band.pixels,
              "so the image is smaller than the page it came from");

        const qint64 viewportPixels = qint64(view->viewport()->width())
                                      * view->viewport()->height();
        check(qint64(band.imageSize().width()) * band.imageSize().height()
                  <= 4 * viewportPixels,
              QStringLiteral("and no bigger than a few screens (%1 px against a %2 px screen)")
                  .arg(band.imageSize().width() * band.imageSize().height())
                  .arg(viewportPixels));

        // And a small scroll stays inside the band that was already rendered. A
        // band that changed on every pixel of scrolling would be a render per
        // frame, which is worse than the memory banding saves.
        const auto requestsForFrontPage = [&asked] {
            int count = 0;
            for (const RenderToken &token : asked) {
                if (token.page == 0)
                    ++count;
            }
            return count;
        };
        const int before = requestsForFrontPage();
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + 40);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        check(requestsForFrontPage() == before, "a small scroll does not ask for it again");

        // See the note above the jump test: the connection outlives this block.
        QObject::disconnect(listening);
    }

    // ---- the images held do not grow with how far somebody has scrolled ----
    //
    // This is the one that mattered: the view used to keep every page it had
    // ever been given, so scrolling grew memory without limit while the target
    // file's pages were 33 to 52 MB each. Forty pages, scrolled to the end, has
    // to leave a handful held rather than forty.
    {
        const QString manyPages = directory + QStringLiteral("/many-pages.pdf");
        if (QFileInfo::exists(manyPages)) {
            check(tab.open(manyPages) == Document::Status::Ready, "the long fixture opens");
            check(waitFor([view] { return view->hasImageFor(0); }), "its first page is drawn");

            for (int page = 1; page < tab.pageCount(); ++page) {
                view->goToPage(page);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            check(waitFor([view, &tab] { return view->hasImageFor(tab.pageCount() - 1); }),
                  "and so is the last one");

            // One page, the neighbour above and the neighbour below: three, and
            // at most a couple more if the prefetch allowance is generous. Forty
            // would be the unbounded version.
            check(view->heldImageCount() <= 5,
                  QStringLiteral("only the pages in view and nearby are held (holding %1 of %2)")
                      .arg(view->heldImageCount())
                      .arg(tab.pageCount()));
        } else {
            std::cerr << "FAIL: " << manyPages.toStdString() << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        }
    }

    // ---- a render that produced nothing does not leave a page grey ----
    //
    // An out-of-memory render comes back as a null image, and the view used to
    // treat that as no news: the page stayed recorded as "asked for", so nothing
    // ever asked again and the placeholder was permanent. Every page that failed
    // once stayed grey for the rest of the session.
    {
        const QString path2 = directory + QStringLiteral("/text.pdf");
        check(tab.open(path2) == Document::Status::Ready, "the small fixture reopens");

        RenderToken outstanding;
        int requestsForPage0 = 0;
        const QMetaObject::Connection listening = QObject::connect(
            view, &PageView::pageImageNeeded, view,
            [&outstanding, &requestsForPage0](const RenderToken &token) {
                if (token.page == 0) {
                    ++requestsForPage0;
                    outstanding = token;
                }
            });

        // A fresh request, and then failure for it before the event loop can
        // deliver the real one.
        view->setZoom(2.0);
        check(!outstanding.isValid(), "the token captured from the signal has no generation yet");

        const int before = requestsForPage0;
        view->setPageImage(outstanding, QImage());
        check(waitFor([&requestsForPage0, before] { return requestsForPage0 > before; }),
              "the page is asked for again after a render that produced nothing");
        check(waitFor([view] { return view->hasImageFor(0); }), "and ends up with an image");

        // Off again before `outstanding` and the counter go out of scope: the
        // next block scrolls and renders, and a live connection into locals
        // that no longer exist is a crash rather than a test failure.
        QObject::disconnect(listening);
    }

    // ---- the page itself is white, not whatever is behind it ----
    //
    // A PDF page has no background: the renderer draws the content, and the
    // white paper is supplied by whoever shows the result. Draw the content
    // straight onto the surround and the page comes out the surround's colour
    // -- a grey page under perfectly correct text, which is exactly what the
    // report was. Asserted off the widget rather than out of the code, because
    // the colour is the thing the user is looking at.
    {
        view->goToPage(0);
        // Rendered, not merely held: a band from the other end of the page is
        // an image the view is about to replace, and sampling before it lands
        // is sampling the placeholder.
        check(waitFor([view] { return view->isRendered(0); }),
              "the page on screen is fully rendered");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "and it has somewhere to be");

        const QImage shot = view->grab().toImage();
        check(!shot.isNull(), "the view can be sampled");

        // Inside the page, near its top-left corner, where an ordinary document
        // has margin: paper, and nothing else. The failure message carries
        // everything that decides what that pixel can be, because "not white"
        // is the symptom of five different things.
        const QPoint inside(int(page.left()) + 8, int(page.top()) + 8);
        const QColor onPage = shot.pixelColor(inside);
        check(onPage == QColor(255, 255, 255),
              QStringLiteral("the page is paper-coloured, not the backdrop (%1,%2,%3 at %4,%5; "
                             "page %6,%7 %8x%9; viewport %10x%11; zoom %12; image %13)")
                  .arg(onPage.red())
                  .arg(onPage.green())
                  .arg(onPage.blue())
                  .arg(inside.x())
                  .arg(inside.y())
                  .arg(page.left())
                  .arg(page.top())
                  .arg(page.width())
                  .arg(page.height())
                  .arg(view->viewport()->width())
                  .arg(view->viewport()->height())
                  .arg(view->zoom())
                  .arg(view->hasImageFor(0) ? QStringLiteral("held")
                                            : QStringLiteral("missing")));

        // Beside the page, which is where the surround is supposed to show --
        // the same grey it has always been, now that the page is not drawn on
        // top of it.
        const QColor beside = shot.pixelColor(qMax(0, int(page.left()) - 4),
                                             int(page.top()) + 8);
        const QColor expected = ThemeManager::currentColors().pageBackdrop;
        check(beside == expected,
              QStringLiteral("and beside it is still the backdrop (%1,%2,%3, expected %4,%5,%6)")
                  .arg(beside.red())
                  .arg(beside.green())
                  .arg(beside.blue())
                  .arg(expected.red())
                  .arg(expected.green())
                  .arg(expected.blue()));
    }

    // ---- dragging over the text selects it ----
    //
    // The end-to-end version of textlayer_smoke: real mouse events, on a real
    // page, turned into a real selection. What it checks is not the exact
    // characters -- those depend on the font's metrics, which are not this
    // program's business -- but that a drag selects part of the document's own
    // text, that the same drag backwards selects the same thing, and that a
    // click selects nothing.
    {
        // The tool this block is about. It is also the default, and saying so
        // here is the point: what a drag does is now a mode, and a test that
        // does not name the mode it is exercising is a test that reads as
        // though the old implicit rule were still in force.
        view->setTool(PageView::Tool::Select);

        const QString path3 = directory + QStringLiteral("/text.pdf");
        check(tab.open(path3) == Document::Status::Ready, "the fixture reopens for selecting");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "the page is drawn");

        // The document's own text, to compare a drag against. Select-all is
        // also the one case where the selection covers every page, so it is
        // worth checking on its own.
        tab.selectAll();
        const QString whole = tab.selectedText();
        check(whole.contains(QStringLiteral("XYZPDF one")), "select all reaches the first page");
        check(whole.contains(QLatin1Char('\n')), "and separates the second page from it");
        check(tab.hasSelection(), "select all leaves a selection");
        tab.clearSelection();
        check(!tab.hasSelection(), "and clearing it leaves none");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "the page is on screen");

        // Across the first line. The fixture draws its text near the top left
        // of the page; these fractions of it are inside that line, so the
        // assertions below do not depend on the font being any set size.
        const QPointF left(page.left() + page.width() * 0.12, page.top() + page.height() * 0.09);
        const QPointF right(page.left() + page.width() * 0.30, page.top() + page.height() * 0.09);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, left, true);
        sendMouse(view->viewport(), QEvent::MouseMove, right, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, right);

        const QString dragged = tab.selectedText();
        check(!dragged.isEmpty(),
              QStringLiteral("dragging across the text selects some of it (got \"%1\")")
                  .arg(dragged));
        check(whole.contains(dragged), "and what it selected is part of the document's text");
        check(dragged != whole, "a drag along one line is not the whole document");

        // Backwards over the same span: the same text. This is the case that
        // gets written wrong, and the one a user hits the moment they overshoot
        // a word and drag back over it.
        view->clearSelection();
        sendMouse(view->viewport(), QEvent::MouseButtonPress, right, true);
        sendMouse(view->viewport(), QEvent::MouseMove, left, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, left);
        check(tab.selectedText() == dragged, "and the same text dragged backwards");

        // A click, which is not a selection.
        view->clearSelection();
        sendMouse(view->viewport(), QEvent::MouseButtonPress, left);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, left);
        check(!tab.hasSelection(), "a click selects nothing");

        // ---- the pointer says what is under it ----
        //
        // An I-beam over a line of text and an arrow everywhere else, which is
        // how every other program says "this can be selected". The tracking
        // flag is checked as well: without it the handler is never called in
        // the real program, and this test -- which sends its events by hand --
        // would pass anyway.
        check(view->viewport()->hasMouseTracking(),
              "the view is told about moves with no button held");

        sendMouse(view->viewport(), QEvent::MouseMove, left);
        check(view->viewport()->cursor().shape() == Qt::IBeamCursor,
              QStringLiteral("the pointer is an I-beam over text (shape %1 at %2,%3 of a page "
                             "at %4,%5 %6x%7)")
                  .arg(int(view->viewport()->cursor().shape()))
                  .arg(left.x())
                  .arg(left.y())
                  .arg(page.left())
                  .arg(page.top())
                  .arg(page.width())
                  .arg(page.height()));

        sendMouse(view->viewport(), QEvent::MouseMove, QPointF(4, 2));
        check(view->viewport()->cursor().shape() == Qt::ArrowCursor,
              "and an arrow away from it");

        // And it is visible. The wash is the only part of this a user can see,
        // and a selection that is correct in the clipboard but invisible on the
        // page is a selection nobody would believe in.
        tab.selectAll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QImage shot = view->grab().toImage();
        const QColor washed = shot.pixelColor(left.toPoint());
        check(washed != QColor(255, 255, 255),
              QStringLiteral("the selection is drawn on the page (%1,%2,%3)")
                  .arg(washed.red())
                  .arg(washed.green())
                  .arg(washed.blue()));
        check(washed.blue() > washed.red(), "in the selection colour rather than the page's");
    }

    // ---- the two drag tools do not answer to each other's gestures ----
    //
    // What this milestone was about. Until the tools existed, a drag meant
    // whichever of the two things the pointer was pointing at, and the two
    // features fought over the same gesture: selecting text on a scan was a
    // box, and a box over a line was a selection. Now the mode decides, and
    // this is the pair of assertions that says so -- the same drag, over the
    // same page, doing one thing and then the other.
    //
    // blank.pdf has no text layer, so a selection drag there has nothing to
    // select: in Select mode it must do nothing at all, and in Recognize mode
    // it must draw a box.
    {
        const QString blankPath = directory + QStringLiteral("/blank.pdf");
        if (!QFileInfo::exists(blankPath)) {
            std::cerr << "FAIL: " << blankPath.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            check(tab.open(blankPath) == Document::Status::Ready, "the blank fixture opens");
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

            int captures = 0;
            int capturedPage = -1;
            QRectF captured;
            const QMetaObject::Connection listening = QObject::connect(
                view, &PageView::regionCaptured, view,
                [&](int page, const QRectF &points) {
                    ++captures;
                    capturedPage = page;
                    captured = points;
                });

            const QRectF page = view->pageRectInViewport(0);
            const QPointF from(page.left() + page.width() * 0.3,
                               page.top() + page.height() * 0.3);
            const QPointF to(page.left() + page.width() * 0.6,
                             page.top() + page.height() * 0.6);

            // ---- and first: the selection tool, where the same drag does nothing ----
            view->setTool(PageView::Tool::Select);
            sendMouse(view->viewport(), QEvent::MouseMove, QPointF(4, 2));
            check(view->viewport()->cursor().shape() == Qt::ArrowCursor,
                  "the selection tool's pointer is not a hand or a crosshair");

            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);
            check(captures == 0, "a selection drag on a page of scans draws no box");
            check(!tab.hasSelection(), "and selects nothing, there being no text to select");

            // ---- then the recognition tool, where it draws the box ----
            view->setTool(PageView::Tool::Recognize);
            sendMouse(view->viewport(), QEvent::MouseMove, from);
            check(view->viewport()->cursor().shape() == Qt::CrossCursor,
                  "the recognition tool's pointer is a crosshair");

            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

            check(captures == 1, "the drag reports exactly one region");
            check(capturedPage == 0, "from the page it was dragged on");
            // The page's own points, not the screen's: a 595x842 page dragged
            // from 30% to 60% is those fractions of 595 and 842, whatever the
            // zoom and the window did to the pixels in between.
            const QSizeF points(595, 842);
            const QRectF expected(0.3 * points.width(), 0.3 * points.height(),
                                  0.3 * points.width(), 0.3 * points.height());
            check(std::fabs(captured.left() - expected.left()) < 1.5
                      && std::fabs(captured.top() - expected.top()) < 1.5
                      && std::fabs(captured.width() - expected.width()) < 1.5
                      && std::fabs(captured.height() - expected.height()) < 1.5,
                  QStringLiteral("in the page's own coordinates (got %1,%2 %3x%4, wanted %5,%6 "
                                 "%7x%8)")
                      .arg(captured.left())
                      .arg(captured.top())
                      .arg(captured.width())
                      .arg(captured.height())
                      .arg(expected.left())
                      .arg(expected.top())
                      .arg(expected.width())
                      .arg(expected.height()));

            // A click is not a region: it is how a box drag is cancelled, and
            // it must not open a window asking to be recognized.
            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, from);
            check(captures == 1, "a click reports no region");

            // ---- and a box over text, which is the case the tool exists for ----
            //
            // A picture inside a page of text, a table read as a picture, a
            // heading in a scan: the box is drawn over whatever is there and
            // nothing is selected. This is what the mode buys over the old
            // "the pointer decides" rule, where a drag here was a selection.
            check(tab.open(directory + QStringLiteral("/text.pdf"))
                      == Document::Status::Ready,
                  "the text fixture reopens");
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

            const QRectF textPage = view->pageRectInViewport(0);
            const QPointF onLine(textPage.left() + textPage.width() * 0.12,
                                 textPage.top() + textPage.height() * 0.09);
            const QPointF across(textPage.left() + textPage.width() * 0.30,
                                 textPage.top() + textPage.height() * 0.09);

            // A box has two dimensions: these two corners are above and below
            // the line, not just either side of it. A drag along one line is a
            // selection gesture, and in this tool it is a box with no height,
            // which the minimum size throws away.
            const QPointF boxFrom(textPage.left() + textPage.width() * 0.08,
                                  textPage.top() + textPage.height() * 0.05);
            const QPointF boxTo(textPage.left() + textPage.width() * 0.40,
                                textPage.top() + textPage.height() * 0.13);

            sendMouse(view->viewport(), QEvent::MouseButtonPress, boxFrom, true);
            sendMouse(view->viewport(), QEvent::MouseMove, boxTo, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, boxTo);
            check(captures == 2, "a box over a line of text is still a box");
            check(!tab.hasSelection(), "and selects nothing");

            // ---- and the selection tool selects the very same line ----
            view->setTool(PageView::Tool::Select);
            sendMouse(view->viewport(), QEvent::MouseButtonPress, onLine, true);
            sendMouse(view->viewport(), QEvent::MouseMove, across, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, across);
            check(captures == 2, "while the selection tool draws no box");
            check(tab.hasSelection(), "and selects the text instead");

            // Off again before the locals go out of scope: the connection
            // outlives this block, and it captures them by reference.
            QObject::disconnect(listening);
        }
    }

    // ---- the hand drags the page and touches nothing on it ----
    //
    // The third tool, and the one with the least to get wrong -- which is why
    // it is worth asserting rather than assuming: a pan that also selected
    // something, or a pan in the wrong direction, is a tool nobody would use
    // twice.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "a document is open for the hand");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for the hand");

        view->setTool(PageView::Tool::View);
        // Scrolled somewhere with room in both directions, so that a pan that
        // ran off the end of the document would still be visible as a value
        // that did not move.
        view->horizontalScrollBar()->setValue(100);
        view->verticalScrollBar()->setValue(200);
        const int beforeX = view->horizontalScrollBar()->value();
        const int beforeY = view->verticalScrollBar()->value();
        const QRectF beforeRect = view->pageRectInViewport(0);

        const QRectF page = view->pageRectInViewport(0);

        const QPointF from(page.left() + page.width() * 0.5, page.top() + page.height() * 0.5);
        const QPointF to(from.x() + 40, from.y() + 30);

        sendMouse(view->viewport(), QEvent::MouseMove, from);
        check(view->viewport()->cursor().shape() == Qt::OpenHandCursor,
              "the hand tool's pointer is an open hand");

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        check(view->viewport()->cursor().shape() == Qt::ClosedHandCursor,
              "which closes while the page is being dragged");
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);
        check(view->viewport()->cursor().shape() == Qt::OpenHandCursor, "and opens again");

        // The paper follows the pointer: dragging right and down shows what is
        // to the left and above, so the scroll position moves against the drag
        // by exactly its own amount.
        check(view->horizontalScrollBar()->value() == beforeX - 40
                  && view->verticalScrollBar()->value() == beforeY - 30,
              QStringLiteral("the page followed the pointer (%1,%2 from %3,%4)")
                  .arg(view->horizontalScrollBar()->value())
                  .arg(view->verticalScrollBar()->value())
                  .arg(beforeX)
                  .arg(beforeY));
        check(qRound(view->pageRectInViewport(0).left() - beforeRect.left()) == 40
                  && qRound(view->pageRectInViewport(0).top() - beforeRect.top()) == 30,
              "which moved the page on screen by the same amount");
        check(!tab.hasSelection(), "and selected nothing");
    }

    // ---- a drag over the second line of a page selects that line ----
    //
    // The reported bug, end to end. On a document with more than one line per
    // page, dragging over any line but the first used to select nothing a reader
    // could see: the index it anchored at was found by bisecting on a predicate
    // a line break makes non-monotone, and came back from somewhere else in the
    // document. It was reported as "selection works in English and not in
    // Chinese" -- the document was Chinese, and every fixture here had exactly
    // one line per page, so the line break was in none of them.
    //
    // The second line of this fixture is at y between 93 and 110 points of a
    // 595x842 page, which is where the drag below is aimed.
    {
        view->setTool(PageView::Tool::Select);
        check(tab.open(directory + QStringLiteral("/lines.pdf")) == Document::Status::Ready,
              "the multi-line fixture opens");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

        const QRectF page = view->pageRectInViewport(0);
        const QPointF from(page.left() + page.width() * 0.15, page.top() + page.height() * 0.115);
        const QPointF to(page.left() + page.width() * 0.35, page.top() + page.height() * 0.125);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        const QString selected = tab.selectedText();
        check(!selected.isEmpty(),
              QStringLiteral("dragging over the second line selects something (got \"%1\")")
                  .arg(selected));
        check(QStringLiteral("second line here").contains(selected),
              QStringLiteral("and it is that line's own text (got \"%1\")").arg(selected));
    }

    // ---- a box over the text: read, not selected ----
    //
    // The case the whole feature is for, and the one its own test did not
    // cover: the drag on the blank fixture proves a window opens and says
    // nothing about what is in it. Here the box is drawn around the fixture's
    // line of text, and what has to come back is the words that are in it.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a box over its text");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for the box");

        view->setTool(PageView::Tool::Recognize);
        view->clearSelection();
        const int dialogsBefore = tab.findChildren<OcrDialog *>().size();

        const QRectF page = view->pageRectInViewport(0);
        // Around the line of text, with room to spare: the fixture draws it
        // near the top left of a 595x842 page, at y between 63 and 80 points.
        const QPointF from(page.left() + page.width() * 0.05,
                           page.top() + page.height() * 0.05);
        const QPointF to(page.left() + page.width() * 0.45, page.top() + page.height() * 0.13);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        check(!tab.hasSelection(), "a box over text does not select it");

        OcrDialog *dialog = nullptr;
        check(waitFor([&tab, dialogsBefore, &dialog] {
                  const QList<OcrDialog *> dialogs = tab.findChildren<OcrDialog *>();
                  if (dialogs.size() <= dialogsBefore)
                      return false;
                  dialog = dialogs.last();
                  return true;
              }),
              "the box opens a recognition window");

        QPlainTextEdit *recognized = dialog ? dialog->findChild<QPlainTextEdit *>() : nullptr;
        check(recognized != nullptr, "which has somewhere to show the text");
        if (recognized) {
            check(waitFor([recognized] { return recognized->isEnabled(); }, 30000),
                  "and the recognition finishes");
            const QString text = recognized->toPlainText().simplified();
            check(text.contains(QStringLiteral("XYZPDF")),
                  QStringLiteral("with the text that was inside the box (got \"%1\")").arg(text));
        }
    }

    // ---- searching the document ----
    //
    // The whole way round: a needle typed into the bar, a scan that runs off the
    // UI thread, the count that comes back, stepping through the hits (wrapping
    // at both ends), the highlight on the page, and the search being forgotten
    // when the document it belonged to is closed.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the two-page fixture opens for searching");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn for the search");

        // ---- the bar is shown where the search belongs ----
        tab.showFindBar();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        check(tab.findBarWidget() && tab.findBarWidget()->isVisible(),
              "ask for the find bar and it appears");
        if (tab.findBarWidget()) {
            auto *field = tab.findBarWidget()->findChild<QLineEdit *>(QStringLiteral("findField"));
            // The bar's own focus widget rather than hasFocus(), which is false
            // unless the window is active -- and on the offscreen platform it is
            // not. What is being checked is that the caret has been given to the
            // field, which is what Ctrl+F twice is for.
            check(field && tab.findBarWidget()->focusWidget() == field,
                  "with the caret in the field, ready to be typed in");
        }

        // ---- a needle in two pages ----
        //
        // The fixture says "XYZPDF one" on page 1 and "XYZPDF two" on page 2, so
        // this is a search that has to cross a page boundary to be right.
        tab.startSearch(QStringLiteral("XYZPDF"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
              QStringLiteral("two hits in the document (found %1)").arg(tab.searchMatchCount()));
        check(tab.currentSearchMatch() == 0, "and the first of them is in front");
        check(view->currentPage() == 0, "which is on the page that was being read");

        // ---- and the bar says so ----
        //
        // The label was in the bar from the day it was written and nothing ever
        // filled it in: the count was only reachable through this tab's API,
        // which is not where a reader looks for it. Asserted on the text rather
        // than on the API for that reason, and loosely -- the numbers are the
        // requirement, the wording is the bar's business.
        if (tab.findBarWidget()) {
            auto *count = tab.findBarWidget()->findChild<QLabel *>(QStringLiteral("findCount"));
            check(count != nullptr, "the bar has somewhere to show the count");
            if (count) {
                const QString shown = count->text();
                check(shown.contains(QStringLiteral("1")) && shown.contains(QStringLiteral("2")),
                      QStringLiteral("and it says which of how many (got \"%1\")").arg(shown));
            }
        }

        // ---- it is on the page, and visible ----
        {
            const QRectF page = view->pageRectInViewport(0);
            // The fixture draws its text near the top left of a 595x842 page; the
            // view has just been scrolled to the match, so the box is sampled
            // where the match is rather than where it was.
            const QPointF inside(page.left() + page.width() * 0.12,
                                 page.top() + page.height() * 0.09);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QImage shot = view->grab().toImage();
            const QColor marked = shot.pixelColor(inside.toPoint());
            check(marked != QColor(255, 255, 255),
                  QStringLiteral("the hit is washed on the page (%1,%2,%3 at %4,%5)")
                      .arg(marked.red())
                      .arg(marked.green())
                      .arg(marked.blue())
                      .arg(inside.x())
                      .arg(inside.y()));
            check(marked.red() > marked.blue(), "in the highlight colour, not the page's");
        }

        // ---- stepping, and wrapping at both ends ----
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == 1, "the next hit is the one on page 2");
        check(view->currentPage() == 1, "and the view went to page 2 with it");
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == 0, "stepping past the last hit wraps to the first");
        tab.stepSearch(-1);
        check(tab.currentSearchMatch() == 1, "and stepping back from the first wraps to the last");

        // ---- case, both ways ----
        tab.startSearch(QStringLiteral("xyzpdf"), true);
        check(waitFor([&tab] { return !tab.isSearching(); }), "a case-sensitive search finishes");
        check(tab.searchMatchCount() == 0,
              QStringLiteral("and finds nothing in a document that has it in capitals (found %1)")
                  .arg(tab.searchMatchCount()));
        tab.startSearch(QStringLiteral("xyzpdf"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
              "while the same needle without case finds both");

        // ---- nothing to find ----
        tab.startSearch(QStringLiteral("nothing like this is in the document"), false);
        check(waitFor([&tab] { return !tab.isSearching(); }), "a needle that is not there finishes");
        check(tab.searchMatchCount() == 0, "with no hits");
        check(tab.currentSearchMatch() == -1, "and nothing in front");
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == -1, "and stepping does nothing rather than crashing");

        tab.startSearch(QString(), false);
        check(waitFor([&tab] { return !tab.isSearching(); }), "an empty needle finishes");
        check(tab.searchMatchCount() == 0, "and matches nothing rather than everything");

        // ---- the same thing on a document with more than one line per page ----
        //
        // Which is where selection used to break, and a search that highlights
        // the wrong box is the same bug wearing a hat.
        check(tab.open(directory + QStringLiteral("/lines.pdf")) == Document::Status::Ready,
              "the multi-line fixture opens");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

        tab.startSearch(QStringLiteral("second"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 1; }),
              QStringLiteral("one hit for the word on the second line (found %1)")
                  .arg(tab.searchMatchCount()));

        // ---- and a search does not outlive its document ----
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "another document opens");
        check(tab.searchMatchCount() == 0,
              QStringLiteral("and the search of the previous one is gone (found %1)")
                  .arg(tab.searchMatchCount()));
    }

    // ---- a document is replaced while its pages are still rendering ----
    //
    // The race, made reproducible rather than left to luck: open a document,
    // ask for pages, and replace the document before those renders come back --
    // five times over. A render that is still running is inside the document
    // that QPdfDocument::load() is about to replace, and Qt's global PDFium lock
    // does not cover the reload itself: this used to end in a fail-fast inside
    // open(), which the test saw as "a page is drawn" timing out and then the
    // process dying.
    //
    // A reader hits it by opening a second document while the first is still
    // painting -- rare on a two-page file, ordinary on a five-hundred-page scan.
    // DocumentTab::open() now waits for the renders in flight before it reloads.
    {
        const QString manyPages = directory + QStringLiteral("/many-pages.pdf");
        const QString small = directory + QStringLiteral("/text.pdf");
        if (QFileInfo::exists(manyPages)) {
            for (int attempt = 0; attempt < 5; ++attempt) {
                check(tab.open(manyPages) == Document::Status::Ready,
                      QStringLiteral("round %1: the long fixture opens").arg(attempt + 1));
                // Deliberately not waited for: the renders are what the next
                // open has to survive.
                view->goToPage(attempt + 3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

                check(tab.open(small) == Document::Status::Ready,
                      QStringLiteral("round %1: and the next document opens over the renders "
                                     "still in flight")
                          .arg(attempt + 1));
            }
            // Back to the first page before waiting: the loops above left the
            // view scrolled wherever the last document's page 4 was, and a page
            // that is not on screen is never rendered -- which is not the
            // question being asked here.
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }),
                  "and a page is drawn afterwards, so nothing was left wedged");
        }
    }

    // The recognition a box drag kicks off runs on the global pool. Whatever
    // it decided to do, the test does not leave work behind at exit: a process
    // teardown under a running WinRT call is a hang, not a failure.
    QThreadPool::globalInstance()->waitForDone(30000);

    // ---- 导出为 Word：the tab's own half of it ----
    //
    // The conversion itself is wordexport_smoke's business. What is this test's
    // is the wiring around it: that the work runs off the UI thread, that the
    // answer comes back as a signal, and that the tab knows it is busy -- the
    // last of which is what keeps a document from being reloaded under it.
    {
        const QString path = directory + QStringLiteral("/text.pdf");
        check(tab.open(path) == Document::Status::Ready, "the fixture opens for export");

        QTemporaryDir output;
        check(output.isValid(), "there is somewhere to export to");
        const QString file = output.filePath(QStringLiteral("tab.docx"));

        bool answered = false;
        WordExportReport answer;
        int progressReports = 0;
        int lastReported = 0;
        const QMetaObject::Connection listening = QObject::connect(
            &tab, &DocumentTab::wordExportFinished, &tab, [&](const WordExportReport &report) {
                answered = true;
                answer = report;
            });
        // The progress the window puts in the status bar, which is the only sign
        // of life a long export has -- and the only part of this that has to
        // cross from the worker thread to this one.
        const QMetaObject::Connection progress = QObject::connect(
            &tab, &DocumentTab::wordExportProgress, &tab, [&](int done, int total) {
                ++progressReports;
                lastReported = total > 0 ? done : lastReported;
            });

        tab.exportToWord(file);
        check(waitFor([&answered] { return answered; }, 30000), "the export answers");
        check(progressReports > 0, QStringLiteral("having reported its progress (%1 reports)")
                                       .arg(progressReports));
        check(progressReports == 0 || lastReported == 2,
              QStringLiteral("up to the last page (%1)").arg(lastReported));
        QObject::disconnect(progress);

        check(answer.ok, QStringLiteral("with a file written (%1)").arg(answer.error));
        check(answer.path == file, "at the path it was given");
        check(answer.pages == 2, QStringLiteral("of both pages (%1)").arg(answer.pages));
        check(QFileInfo::exists(file) && QFileInfo(file).size() > 0, "and it is on disk");
        check(!tab.isExporting(), "and the tab is no longer busy");

        QObject::disconnect(listening);
    }

    // ---- night reading ----
    //
    // Asserted off the pixels because the pixels are the feature. The setting
    // existed for milestones with nothing reading it -- a stored preference that
    // changed nothing on screen -- and that is precisely the defect that reading
    // the code cannot catch and sampling the widget can.
    {
        tab.setNightInverted(false);
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }),
              "the first page is drawn for the day");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "and it is somewhere that can be sampled");

        // The viewport's own centre, not a corner of the page: this fixture is
        // laid out far larger than the test viewport, so the page's top-left
        // corner sits at a negative x and a point taken from it falls outside the
        // grabbed image -- where pixelColor answers with an invalid colour whose
        // channels all read 0, which looks exactly like a black page.
        const QPoint paper = view->viewport()->rect().center();
        check(page.contains(paper),
              "the point sampled is on the page, not the surround");
        const QImage dayShot = view->grab().toImage();
        const QColor day = dayShot.pixelColor(paper);
        check(day.isValid(), QStringLiteral("and the sample is a real pixel (%1)")
                                 .arg(day.isValid() ? day.name() : QStringLiteral("invalid")));

        tab.setNightInverted(true);
        check(view->nightInverted(), "the view reports itself inverted");
        check(waitFor([view] { return view->isRendered(0); }),
              "the page is re-rendered rather than left as a placeholder");
        const QColor nightColour = view->grab().toImage().pixelColor(paper);

        // Asserted as a relationship between the two samples rather than against
        // a colour, because by this point in the file the tab holds whichever
        // fixture the last block opened, and what that pixel is -- paper, ink, or
        // a black bar in a graphic -- is that fixture's business. What must hold
        // for any pixel is that inversion takes each channel to its complement.
        check(day != nightColour,
              QStringLiteral("the toggle changed what is on screen (%1,%2,%3 became %4,%5,%6)")
                  .arg(day.red()).arg(day.green()).arg(day.blue())
                  .arg(nightColour.red()).arg(nightColour.green()).arg(nightColour.blue()));
        check(day.red() + nightColour.red() == 255
                  && day.green() + nightColour.green() == 255
                  && day.blue() + nightColour.blue() == 255,
              QStringLiteral("and every channel went to its complement (%1,%2,%3 / %4,%5,%6)")
                  .arg(day.red()).arg(day.green()).arg(day.blue())
                  .arg(nightColour.red()).arg(nightColour.green()).arg(nightColour.blue()));

        // Back the way it was, so that anything added after this sees a day page.
        tab.setNightInverted(false);
        check(waitFor([view] { return view->isRendered(0); }), "and it comes back");
    }

    if (failures == 0) {
        std::cout << "the view and the tab hold together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
