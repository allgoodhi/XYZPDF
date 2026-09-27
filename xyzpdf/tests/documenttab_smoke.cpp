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
#include "../src/edit/documenteditor.h"
#include "../src/theme/thememanager.h"
#include "../src/ui/deletepagesdialog.h"
#include "../src/ui/findbar.h"
#include "../src/ui/mergepagesdialog.h"
#include "../src/ui/movepagesdialog.h"
#include "../src/ui/ocrdialog.h"
#include "../src/ui/protectpagesdialog.h"
#include "../src/ui/splitpagesdialog.h"
#include "../src/ui/watermarkdialog.h"
#include "../src/view/pageview.h"
#include "../src/view/zoomcontroller.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QString>
#include <QTableWidget>
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

    // ---- the region snapshot ----
    //
    // The first time this suite touches the clipboard at all, so it is worth
    // stating plainly: if the offscreen platform has no clipboard, the readback
    // below comes back empty and this fails for a reason that is not the
    // feature's fault. It stays because "a button that silently copies nothing"
    // is exactly what the row exists to catch, and the only way to know which of
    // the two happened is to have looked.
    {
        OcrDialog dialog;
        auto *copyImage = dialog.findChild<QPushButton *>(QStringLiteral("copyImageButton"));
        check(copyImage != nullptr, "the recognition window offers a picture to copy");
        if (copyImage) {
            check(!copyImage->isEnabled(),
                  "and it is off before there are any pixels to copy");

            // A crop as the renderer actually returns it: ink over transparency,
            // because the white of a page is put down by whoever shows the image.
            QImage crop(120, 60, QImage::Format_ARGB32_Premultiplied);
            crop.fill(Qt::transparent);
            {
                QPainter ink(&crop);
                ink.fillRect(10, 10, 20, 20, Qt::black);
            }

            // By day: the paper has to be in the picture, or pasting gives a
            // cut-out that borrows whatever background it lands on.
            dialog.setCropImage(crop, false);
            check(copyImage->isEnabled(), "handing it the crop enables it");
            copyImage->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const QImage pasted = QGuiApplication::clipboard()->image();
            check(!pasted.isNull(), "clicking it puts an image on the clipboard");
            check(pasted.size() == crop.size(),
                  QStringLiteral("at the size that was cropped (%1x%2 against %3x%4)")
                      .arg(pasted.size().width())
                      .arg(pasted.size().height())
                      .arg(crop.size().width())
                      .arg(crop.size().height()));
            check(pasted.pixelColor(0, 0) == QColor(Qt::white),
                  QStringLiteral("with paper under the ink, not transparency (%1)")
                      .arg(pasted.pixelColor(0, 0).name()));
            check(pasted.pixelColor(20, 20) == QColor(Qt::black),
                  QStringLiteral("and the ink still where it was (%1)")
                      .arg(pasted.pixelColor(20, 20).name()));

            // At night: what the reader is looking at is the inverted page, and a
            // snapshot that comes back white-on-black is the complement of the
            // thing they chose to copy.
            dialog.setCropImage(crop, true);
            copyImage->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const QImage nightPasted = QGuiApplication::clipboard()->image();
            check(nightPasted.pixelColor(0, 0) == QColor(Qt::black),
                  QStringLiteral("at night the paper pastes black (%1)")
                      .arg(nightPasted.pixelColor(0, 0).name()));
            check(nightPasted.pixelColor(20, 20) == QColor(Qt::white),
                  QStringLiteral("and the ink white (%1)")
                      .arg(nightPasted.pixelColor(20, 20).name()));
        }
    }

    // ---- the merge dialog's order promise ----
    //
    // 合并文件进来 is the first dialog here whose answer is a sequence rather than a
    // number, and the sequence is worked out inside the dialog: a merge lands beside
    // the target the reader named, not beside the block the row before it added, so
    // for 最前面 and 指定页之后 the rows have to be recorded in reverse for the list to
    // be the order the file gets (MergePagesDialog::mergesToRecord). The editor cannot
    // catch that from the other side -- it is handed one file at a time -- so it is
    // checked here, with the file picker replaced by the half of 添加文件… that is not
    // a window.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: the editor opened %1: %2").arg(path, error));
        const int onScreen = editor.pageCount();

        MergePagesDialog dialog(&tab, &editor);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QPushButton *okay = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        auto *table = dialog.findChild<QTableWidget *>();
        check(okay && table, "the merge window has its list and its button");

        auto pickTarget = [&dialog](const QString &words) {
            const auto radios = dialog.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };
        auto summary = [&dialog] {
            const auto labels = dialog.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        auto baseName = [](const QString &file) { return QFileInfo(file).fileName(); };

        check(okay && !okay->isEnabled(),
              "with an empty list the button is off, and says why");
        check(summary().contains(QStringLiteral("还没有加上要合并进来的文件")),
              QStringLiteral("the empty list is described, not just greyed: %1").arg(summary()));

        const QStringList donors = {directory + QStringLiteral("/many-pages.pdf"),
                                    directory + QStringLiteral("/dense.pdf"),
                                    directory + QStringLiteral("/mixed.pdf")};
        const QStringList refused = dialog.addPaths(donors);
        check(refused.isEmpty(),
              QStringLiteral("three ordinary files are taken: %1").arg(refused.join(QStringLiteral("; "))));
        const QVector<MergePagesDialog::Entry> listed = dialog.mergesToRecord();
        check(listed.size() == donors.size(), "and one row per file");
        for (int row = 0; row < listed.size(); ++row) {
            check(baseName(listed.at(row).path) == baseName(donors.at(row)),
                  QStringLiteral("row %1 is the file picked at that place: got %2")
                      .arg(row + 1)
                      .arg(baseName(listed.at(row).path)));
            check(listed.at(row).pageCount > 0
                      && listed.at(row).pages.size() == listed.at(row).pageCount,
                  QStringLiteral("and a new row says the whole of its own file (%1 of %2)")
                      .arg(listed.at(row).pages.size())
                      .arg(listed.at(row).pageCount));
        }

        check(pickTarget(QStringLiteral("最前面")) && pickTarget(QStringLiteral("指定页之后"))
                  && pickTarget(QStringLiteral("最后面")),
              "the three places the reader can aim at are on the dialog");

        // The reversal itself, and what it is for: recorded in list order at 最前面 the
        // three land 3,2,1, because each one goes in front of the two already there.
        pickTarget(QStringLiteral("最前面"));
        QStringList recordOrder;
        for (const MergePagesDialog::Entry &entry : dialog.mergesToRecord())
            recordOrder << baseName(entry.path);
        check(recordOrder == QStringList({baseName(donors.at(2)), baseName(donors.at(1)),
                                          baseName(donors.at(0))}),
              QStringLiteral("pointed at 最前面 the rows are recorded so the file gets them "
                             "in list order: got %1")
                  .arg(recordOrder.join(QStringLiteral(","))));

        DocumentEditor landing;
        check(landing.open(path, QString(), &error), "setup: a second editor on the same file");
        for (const MergePagesDialog::Entry &entry : dialog.mergesToRecord()) {
            check(landing.mergeFile(entry.path, entry.pages, DocumentEditor::MoveTarget::Front,
                                    -1, &error),
                  QStringLiteral("%1 comes in at the front: %2").arg(baseName(entry.path), error));
        }
        const QVector<DocumentEditor::PageRef> front = landing.pageOrder();
        check(front.size() == onScreen + listed.at(0).pages.size() + listed.at(1).pages.size()
                    + listed.at(2).pages.size(),
              "and the document is every one of those pages longer");
        // Each merge carries its own identity, and the last one recorded is the first
        // of them -- so the front of the file is row 1, then row 2, then row 3.
        int walking = 0;
        for (int row = 0; row < listed.size(); ++row) {
            const int identity = listed.size() - row;
            for (int page = 0; page < listed.at(row).pages.size(); ++page, ++walking) {
                check(front.at(walking).source == identity
                          && front.at(walking).page == page,
                      QStringLiteral("page %1 of the file is merge %2's page %3, which is row %4 "
                                     "of the list")
                          .arg(walking + 1)
                          .arg(identity)
                          .arg(page + 1)
                          .arg(row + 1));
            }
        }

        pickTarget(QStringLiteral("最后面"));
        table->item(0, 2)->setText(QStringLiteral("2"));
        check(dialog.mergesToRecord().at(0).pages == QVector<int>({1}),
              "a row's range cell is read as that file's own page numbers");
        table->item(1, 2)->setText(QStringLiteral(" "));
        check(okay && !okay->isEnabled(),
              "and a row left blank stops the button -- blank is nothing said, not everything");
        check(summary().contains(baseName(donors.at(1))),
              QStringLiteral("naming which row: %1").arg(summary()));
        table->item(1, 2)->setText(QStringLiteral("1"));

        pickTarget(QStringLiteral("指定页之后"));
        check(okay && !okay->isEnabled(),
              "an 指定页之后 with no number in the box is not an answer yet");
        check(summary().contains(QStringLiteral("要说合并到第几页之后")),
              QStringLiteral("and it says that: %1").arg(summary()));
        auto *anchorBox = dialog.findChild<QLineEdit *>();
        check(anchorBox != nullptr, "the box beside 指定页之后");
        if (anchorBox) {
            anchorBox->setText(QString::number(onScreen + 5));
            check(okay && !okay->isEnabled(), "a number past the last page of the screen is refused");
            check(summary().contains(QStringLiteral("没有第 %1 页").arg(onScreen + 5)),
                  QStringLiteral("and named as the number that was typed: %1").arg(summary()));
            anchorBox->setText(QStringLiteral("2"));
            check(okay && okay->isEnabled(), "a page that is there lets the button go back");
        }

        // The sentence 删除页面 needs too, said where the pages come from: an imported
        // page has no number on the screen, so a reader has to be told before merging.
        check(editor.mergeFile(donors.at(0), {0}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("setup: one page recorded into the editor the dialog reads: %1")
                  .arg(error));
        pickTarget(QStringLiteral("最后面"));
        check(summary().contains(QStringLiteral("已经有 %1 页是合并进来的").arg(1)),
              QStringLiteral("the summary counts what the screen never showed: %1").arg(summary()));
        check(summary().contains(QStringLiteral("只能撤销那次合并")),
              QStringLiteral("and says the only way back out: %1").arg(summary()));
    }

    // ---- the merge box refuses a pending split before the click ----
    //
    // §20 caught this dialog lighting 合并这些文件 and printing an arithmetic line while
    // DocumentEditor::mergeFile() was quietly going to refuse whatever it promised, with
    // 拆分文件 having done it the other way round (greyed button, reason in the summary)
    // since §16. So both ends of that mutual exclusion now answer the same two questions
    // of the box itself: does the button stay off, and is the reason said in the model's
    // own words rather than a paraphrase that can drift. A fresh box per click, the way
    // the menu builds one, so what it says is read off the list as it stands now -- which
    // is also what makes the request pass again once the split has been undone.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        const auto saidByMergeBox = [](MergePagesDialog &box, const QString &donor) {
            check(box.addPaths({donor}).isEmpty(), "setup: the donor file is taken");
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto okayOfMergeBox = [](MergePagesDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const QString donor = directory + QStringLiteral("/dense.pdf");

        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("setup: with no split pending, this same request is one the "
                                 "button lights for: %1")
                      .arg(said));
        }

        check(editor.splitInto(15, &error),
              QStringLiteral("setup: a split of every 15 pages is pending: %1").arg(error));
        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && !okay->isEnabled(),
                  "with a split pending the merge box's own button is off before the click");
            check(said.contains(QStringLiteral("已经说了要拆成每 15 页一份")),
                  QStringLiteral("and the summary names the pending split, in the model's "
                                 "sentence: %1")
                      .arg(said));
            // The number the refusal replaces: where a batch would land is an answer
            // about a file that will not be written, which is the same kind of blank as
            // §20's "一个没有指称的数".
            check(!said.contains(QStringLiteral("连着排在第")),
                  QStringLiteral("and no page numbers are promised for a merge that cannot "
                                 "be recorded: %1")
                      .arg(said));
            // The box and the editor refuse the same request -- the rule §21 states,
            // asked from this end.
            check(!editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error)
                      && error.contains(QStringLiteral("先撤销那一步")),
                  QStringLiteral("and the editor refuses it too: %1").arg(error));
        }

        check(editor.undo(), "setup: the split is taken back out of the list");
        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("undo that split and the very same request is lit again: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("已经说了要拆成")),
                  QStringLiteral("and the refusal is gone with it: %1").arg(said));
        }
    }

    // ---- one page is not a range: the two landing sentences ----
    //
    // §20 fixed this for 拆分's file names and pinned it there ("a one-page piece says 21,
    // not 21-21"), while 移动页面 and 合并文件进来 kept printing 5-5 and 40-40 -- and kept
    // saying 它们连着 / "they end up together" about a single page, which is the same thing:
    // a number deciding the words around it. Both dialogs now read the number from
    // DocumentEditor::pageSpan() and pick the wording by the count, so this asks each box the
    // one-page question and the several-page question, and looks for the dash on the right
    // side of that line only.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document to move pages around in: %1").arg(error));
        const int onScreen = editor.pageCount();

        const auto saidBy = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto pickTarget = [](QDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };

        // 合并文件进来 first: a one-page donor needs no range typed, because a fresh row takes
        // the whole of its own file, and rotated.pdf is exactly one page.
        const auto rangeCell = [](QDialog &box) {
            auto *cells = box.findChild<QTableWidget *>();
            return cells && cells->item(0, 2) ? cells->item(0, 2)->text() : QString();
        };
        {
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({directory + QStringLiteral("/rotated.pdf")}).isEmpty(),
                  "setup: a one-page donor is taken");
            check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("一共取 1 页")),
                  QStringLiteral("the count says one page: %1").arg(said));
            check(said.contains(QStringLiteral("排在第 %1 页").arg(onScreen + 1)),
                  QStringLiteral("and it lands on one number, not a range: %1").arg(said));
            check(!said.contains(QStringLiteral("连着")),
                  QStringLiteral("one page is not 连着 either, which is the half the English "
                                 "reader sees as \"they end up together\": %1").arg(said));
            // The fifth exit, and the only one that was an *input*: the cell a fresh row seeds
            // with its own file's range held "1-1" for a one-page file. It parses the same, so
            // nothing downstream noticed -- it is only ever a reader reading the number back.
            check(rangeCell(box) == QStringLiteral("1"),
                  QStringLiteral("a one-page donor seeds the range cell with 1, not 1-1: [%1]")
                      .arg(rangeCell(box)));
        }
        {
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({directory + QStringLiteral("/dense.pdf")}).isEmpty(),
                  "setup: a two-page donor is taken");
            check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("连着排在第 %1-%2 页")
                                     .arg(onScreen + 1)
                                     .arg(onScreen + 2)),
                  QStringLiteral("two pages keep the run and the wording that goes with it: %1")
                      .arg(said));
            check(rangeCell(box) == QStringLiteral("1-2"),
                  QStringLiteral("and a two-page donor does seed a run, so the cell is not just "
                                 "always the first number: [%1]").arg(rangeCell(box)));
        }

        // 移动页面: the same two questions, typed into its own range box -- picked by the
        // placeholder, because this dialog has a second line edit beside it.
        {
            MovePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = nullptr;
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    range = edit;
            }
            check(range != nullptr, "setup: the box that names which pages move");
            if (range) {
                check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
                range->setText(QStringLiteral("7"));
                const QString one = saidBy(box);
                check(one.contains(QStringLiteral("把 1 页移到")),
                      QStringLiteral("one page moved is not a block of them: %1").arg(one));
                check(one.contains(QStringLiteral("移完它就排在第 40 页")),
                      QStringLiteral("page 7 alone takes the last place, which is 40: %1")
                          .arg(one));
                check(!one.contains(QStringLiteral("它们")),
                      QStringLiteral("and the plural pronoun went with the dash: %1").arg(one));

                range->setText(QStringLiteral("7,9"));
                const QString two = saidBy(box);
                check(two.contains(QStringLiteral("连着排在第 39-40 页")),
                      QStringLiteral("two pages come over together, at 39 and 40: %1").arg(two));
            }
        }

        // 拆分文件: the fourth exit, and the one whose rule had a private copy of its own
        // until this slice. Its list is cut short after six pieces, so 每 1 页一份 of this
        // 40-page document is the shape where every number in it stands alone -- a dash
        // anywhere in that list would be the 40-40 bug wearing a different hat. Nothing in
        // any test read this box's summary before now, which is how two correct private
        // copies of one rule stayed unasked while two missing ones reached the screen.
        {
            SplitPagesDialog box(&tab, &editor);
            auto *size = box.findChild<QSpinBox *>();
            check(size != nullptr, "setup: the box that says how many pages go in a piece");
            if (size) {
                size->setValue(1);
                const QString said = saidBy(box);
                check(said.contains(QStringLiteral("会写成 40 份，每份最多 1 页")),
                      QStringLiteral("forty pieces of one page: %1").arg(said));
                const QString list
                    = said.section(QStringLiteral("页："), 1).section(QStringLiteral("。"), 0, 0);
                // Six pieces are listed, then the cut, then the last one -- and the cut is
                // joined to the numbers with `、` too, so the list splits into nine.
                // Split on U+3001 by its code point: QLatin1Char cannot hold it (the source
                // is UTF-8, so it would silently take one byte of a three-byte character).
                check(list.split(QChar(0x3001)).size() == 8,
                      QStringLiteral("six of them listed, then the cut, then the last one: [%1]")
                          .arg(list));
                check(list.startsWith(QStringLiteral("1、2")) && list.endsWith(QStringLiteral("40")),
                      QStringLiteral("bare numbers at both ends of the list: [%1]").arg(list));
                check(!list.contains(QLatin1Char('-')),
                      QStringLiteral("and no piece of it is a range, because none of them holds "
                                     "more than one page: [%1]").arg(list));
            }
        }
    }

    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----
    //
    // The dialog greys 确定 for exactly the requests DocumentEditor::protectWith() and
    // ::removeProtection() turn down, and it does that by reading the same two answers the
    // save reads. That sharing is the arrangement, and it is the kind that rots quietly: let
    // one of the refusals through and the reader meets a warning box after pressing 确定;
    // grey one the editor would have taken and the feature is unreachable. So both sides of
    // each refusal are asked here. The box is also rebuilt between them the way the menu
    // rebuilds it on every click -- which is why a dialog can never be seen agreeing with an
    // older version of the list.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: an unprotected document opens: %1").arg(error));

        const auto okayOf = [](ProtectPagesDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](ProtectPagesDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto choose = [](ProtectPagesDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };
        const auto groupStartingWith = [](ProtectPagesDialog &box, const QString &words) {
            const auto groups = box.findChildren<QGroupBox *>();
            for (QGroupBox *group : groups) {
                if (group->title().startsWith(words))
                    return group;
            }
            return static_cast<QGroupBox *>(nullptr);
        };

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            QGroupBox *keys = groupStartingWith(box, QStringLiteral("打开口令"));
            QGroupBox *limits = groupStartingWith(box, QStringLiteral("允许的用途"));
            check(keys && limits, "the box asks for a口令 and offers the six permissions");
            check(okay && okay->isEnabled() && keys->isEnabled() && limits->isEnabled(),
                  "nothing is pending and a file that has never been locked can still be locked, "
                  "so it opens on 加密 with every question live");

            check(choose(box, QStringLiteral("去掉口令和权限")), "the other direction is on the box");
            check(okay && !okay->isEnabled(),
                  "over a file that was never locked, 去掉口令 greys the button");
            check(saidBy(box).contains(QStringLiteral("没有东西可去掉")),
                  QStringLiteral("and says that rather than only greying: %1").arg(saidBy(box)));
            check(!editor.removeProtection(&error),
                  "the editor refuses the same request the box greyed");
            check(!keys->isEnabled() && !limits->isEnabled(),
                  "a口令 and six permissions that no answer will read are put away, not left on show");

            check(choose(box, QStringLiteral("加上口令和权限")), "back to 加密");
            check(keys->isEnabled() && limits->isEnabled(), "and the two questions are back");
        }

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            const auto edits = box.findChildren<QLineEdit *>();
            check(edits.size() == 2,
                  "the口令 is asked twice, because nothing here can report it back later");
            // The name a field answers to when nothing points at it is its group's title, so
            // a reader that cannot see the screen asked both fields 打开口令 -- measured in a
            // real window through UI Automation, which is also the only way anyone would ever
            // notice. The labels are buddied to the fields so the two answer apart.
            const auto nameOf = [](QLineEdit *edit) {
                QAccessibleInterface *face = QAccessible::queryAccessibleInterface(edit);
                return face ? face->text(QAccessible::Name) : QString();
            };
            check(nameOf(edits.at(0)) == QStringLiteral("口令") &&
                      nameOf(edits.at(1)) == QStringLiteral("再输一遍"),
                  QStringLiteral("the two fields are named apart by whatever only reads names: "
                                 "[%1] [%2]")
                      .arg(nameOf(edits.at(0)), nameOf(edits.at(1))));
            edits.at(0)->setText(QStringLiteral("hunter2"));
            check(okay && !okay->isEnabled(), "one box filled and the other empty is two different口令");
            check(saidBy(box).contains(QStringLiteral("两次输入的口令不一样")),
                  QStringLiteral("and it names that as the reason: %1").arg(saidBy(box)));
            edits.at(1)->setText(QStringLiteral("hunter2"));
            check(okay && okay->isEnabled(), "agreeing lets it through");
            const DocumentEditor::Protection asked = box.request();
            check(asked.what == DocumentEditor::Protection::What::Encrypt
                      && asked.password == QStringLiteral("hunter2"),
                  "and what the box hands over is the direction plus the key as typed");
            check(editor.protectWith(asked, &error),
                  QStringLiteral("the editor takes it: %1").arg(error));
        }

        {
            // A reader who opens the box a second time finds the six boxes as they were
            // recorded and the口令 fields empty -- showing the key back is the thing the key
            // is for. So typing it again is asking for the request that is already in the
            // list, which is the second refusal, and changing one box is not.
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            const auto edits = box.findChildren<QLineEdit *>();
            edits.at(0)->setText(QStringLiteral("hunter2"));
            edits.at(1)->setText(QStringLiteral("hunter2"));
            check(okay && !okay->isEnabled(), "the same request asked again is greyed");
            check(saidBy(box).contains(QStringLiteral("不会多改什么")),
                  QStringLiteral("with the reason: %1").arg(saidBy(box)));
            check(!editor.protectWith(box.request(), &error),
                  "and the editor turns the same request down");

            const auto permissionBoxes = box.findChildren<QCheckBox *>();
            check(permissionBoxes.size() == 6,
                  "six permissions, one per thing PDF keeps a bit for");
            permissionBoxes.first()->click();
            check(okay && okay->isEnabled(), "clearing one of them is a change, so the button goes back");
            check(editor.protectWith(box.request(), &error),
                  QStringLiteral("and the editor records it: %1").arg(error));
        }

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            check(choose(box, QStringLiteral("去掉口令和权限")),
                  "the way back out of a 加密 that was just recorded is this same box");
            check(okay && okay->isEnabled(), "with a 加密 pending there is something to take off");
            check(saidBy(box).contains(QStringLiteral("会被这一步顶掉")),
                  QStringLiteral("and it says so: %1").arg(saidBy(box)));
            check(box.request().what == DocumentEditor::Protection::What::Decrypt,
                  "the box says which way it went");
            check(editor.removeProtection(&error),
                  QStringLiteral("the editor records that too: %1").arg(error));
            check(editor.passwordOfWrittenFile().isEmpty(),
                  "and the copy a save would write from here asks for no key at all");
        }
    }

    // ---- 加水印: the box offers what the model means, and greys what it refuses ----
    //
    // The third inert recorder. Two things a model test cannot see are checked here. One is what
    // the box shows before anybody has touched it: the first version opened offering 1% 不透明度,
    // because a QSpinBox whose range starts at 1 sits at 1 until someone asks for the default --
    // found in a real window, and a watermark at 1% is a watermark nobody can see, so the number
    // is now read from the same struct the writer reads. The other is §22's rule from the 加密
    // box: the button is off for exactly the requests addWatermark() turns down, so the reader
    // learns 再要一次不会多改什么 from a grey button and one sentence, not from a warning box
    // after a press. A fresh box per click, the way the menu builds one.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        const auto okayOf = [](WatermarkDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](WatermarkDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        // The two boxes are picked by the hint they carry, not by counting: a QSpinBox owns a
        // QLineEdit of its own, so the second line edit in creation order is the opacity field
        // and writing a page range into it changes nothing anyone can see.
        const auto editHintedWith = [](WatermarkDialog &box, const QString &hint) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText() == hint)
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };
        const auto wordEdit = [&editHintedWith](WatermarkDialog &box) {
            return editHintedWith(box, QStringLiteral("例如 机密、作废、内部资料"));
        };
        const auto rangeEdit = [&editHintedWith](WatermarkDialog &box) {
            return editHintedWith(box, QStringLiteral("例如 1-3,5,8-10"));
        };
        const auto radio = [](WatermarkDialog &box, const QString &starts) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *choice : radios) {
                if (choice->text().startsWith(starts))
                    return choice;
            }
            return static_cast<QRadioButton *>(nullptr);
        };

        {
            WatermarkDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            auto *spin = box.findChild<QSpinBox *>();
            check(spin != nullptr, "the box asks how solid the marks should be");
            check(spin && spin->value() == DocumentEditor::Watermark {}.opacityPercent,
                  QStringLiteral("and it opens on the model's own default, not on its minimum: %1%")
                      .arg(spin ? spin->value() : -1));
            check(okay && !okay->isEnabled(),
                  "a box nobody has typed a word into is off, because an empty word is a smudge");
            check(saidBy(box).contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("and says which half is missing: %1").arg(saidBy(box)));
            QRadioButton *all = radio(box, QStringLiteral("全部"));
            check(all && all->isChecked(),
                  "it opens on 全部, because that is what a reader who came to stamp every page means");
            const DocumentEditor::Watermark asked = box.request();
            check(asked.text.isEmpty() && asked.pages.size() == 40,
                  "and what it would hand over is every page and no word");
        }

        {
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled() && okay->text() == QStringLiteral("盖这 40 页"),
                  QStringLiteral("one word and the button counts the pages it lands on: %1")
                      .arg(okay ? okay->text() : QStringLiteral("(none)")));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("「机密」")) && said.contains(QStringLiteral("20%"))
                      && said.contains(QStringLiteral("盖在 40 页上")),
                  QStringLiteral("the summary repeats all three answers back: %1").arg(said));
            check(said.contains(QStringLiteral("画上去的一张图")),
                  "and warns that the marks are a drawing, which is the one fact here a reader "
                  "cannot discover by trying");
        }

        {
            // 空 is not 全部. The print parser reads an empty range as the whole document, and
            // this box has a radio for saying that on purpose -- a second way to say it would let
            // Enter stamp four hundred pages because a box was left blank.
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            radio(box, QStringLiteral("指定"))->click();
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "choosing 指定页码 and naming nothing is off");
            check(saidBy(box).contains(QStringLiteral("还没有说出要盖哪几页")),
                  QStringLiteral("and is named as the missing half: %1").arg(saidBy(box)));
            rangeEdit(box)->setText(QStringLiteral("2-3,7"));
            check(okay->isEnabled() && okay->text() == QStringLiteral("盖这 3 页"),
                  "three pages named, three pages counted");
            const DocumentEditor::Watermark asked = box.request();
            QStringList spelled;
            for (int page : asked.pages)
                spelled << QString::number(page + 1);
            check(asked.pages == QVector<int>({1, 2, 6}),
                  QStringLiteral("and the numbers handed over are the ones the save reads: %1")
                      .arg(spelled.join(QLatin1Char(','))));
        }

        {
            // A page an earlier 删页 took out is not inked, and the box says the number the save
            // will draw rather than the number of boxes ticked -- pagesStillThere() is the one
            // function both ends ask.
            check(editor.deletePages({1}, &error),
                  QStringLiteral("setup: the second page goes: %1").arg(error));
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            rangeEdit(box)->setText(QStringLiteral("2-3,7"));
            check(radio(box, QStringLiteral("指定"))->isChecked(),
                  "a range typed in moves the choice to it without being clicked");
            check(saidBy(box).contains(QStringLiteral("说好的 3 页里有 2 页还在")),
                  QStringLiteral("and the box says which two: %1").arg(saidBy(box)));
            check(okayOf(box)->text() == QStringLiteral("盖这 2 页"),
                  "the button counts what is still there");
            check(editor.pagesStillThere(box.request().pages) == 2,
                  "read off the same one function the save will read");
            check(editor.addWatermark(box.request(), &error),
                  QStringLiteral("setup: the word is recorded: %1").arg(error));
        }

        {
            // Reopening is what the menu does on every click. The recorded answer is offered back
            // rather than a blank box, and asking for exactly that again is the request the editor
            // refuses -- so the button is off before the press, in the same words.
            WatermarkDialog box(&tab, &editor);
            check(wordEdit(box)->text() == QStringLiteral("机密"),
                  "the box comes back saying what is already on the list");
            check(rangeEdit(box)->text() == QStringLiteral("3,7"),
                  QStringLiteral("with the pages a save would actually ink spelled back: %1")
                      .arg(rangeEdit(box)->text()));
            // 3,7 and not 2,3,7: addWatermark() dropped the page the earlier 删页 had taken out
            // before it recorded anything, so the list holds only pages that exist to be drawn on
            // and a box opened against it cannot offer a second way to ask for a ghost.
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and the very same ask again is off");
            check(saidBy(box).contains(QStringLiteral("一模一样")),
                  QStringLiteral("said, not just greyed: %1").arg(saidBy(box)));
            check(!editor.addWatermark(box.request(), &error)
                      && error.contains(QStringLiteral("再要一次不会多改什么")),
                  QStringLiteral("and the editor refuses the identical request: %1").arg(error));

            wordEdit(box)->setText(QStringLiteral("作废"));
            check(okay->isEnabled() && okay->text() == QStringLiteral("盖这 2 页"),
                  "a different word is a change, so the button lights again");
            check(saidBy(box).contains(QStringLiteral("会把前面记下的「机密」换掉")),
                  QStringLiteral("and says it replaces rather than adds: %1").arg(saidBy(box)));
            check(editor.addWatermark(box.request(), &error),
                  QStringLiteral("the editor takes that one: %1").arg(error));
            check(editor.watermarkToWrite().text == QStringLiteral("作废"),
                  "one ask at a time: the list carries the newest word, not two layers");

            check(editor.undo(), "setup: the replacement is taken back");
            check(editor.watermarkToWrite().text == QStringLiteral("机密"),
                  "and the earlier answer is what a save would write now");
            WatermarkDialog again(&tab, &editor);
            QPushButton *againOkay = okayOf(again);
            check(againOkay && !againOkay->isEnabled(),
                  "a box built after the undo offers the undone-away answer back and greys it too, "
                  "because it reads the list as it stands now");
        }
    }

    // ---- who speaks first: the sentence a dialog picks when two of its guards hold ----
    //
    // Every edit dialog answers with ONE sentence out of a chain, and each link in that chain
    // has a test somewhere -- but a chain is a sequence, and nothing asked what the reader
    // gets when two guards are true at once. That is not a corner: 拆分挂着 and 列表是空的 are
    // both facts about the same box, and reordering the two links changes what a person reads
    // while every single-branch check stays green. §29 wrote this exact case down from a real
    // window and left it unpinned, so each pin below is built by making both guards true.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        const QString donor = directory + QStringLiteral("/dense.pdf");
        auto baseName = [](const QString &file) { return QFileInfo(file).fileName(); };
        QString error;
        // DocumentEditor is not copyable (its QPdfDocument is), so each case opens its own:
        // this fills one rather than returning one.
        const auto openBook = [&book, &error](DocumentEditor &editor) {
            check(editor.open(book, QString(), &error),
                  QStringLiteral("setup: a 40-page document to refuse in: %1").arg(error));
        };

        const auto saidOf = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto okayOf = [](QDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        // The range box, picked by its placeholder: 移动页面 has a second line edit beside it
        // (the anchor number), and taking the first child would read out the wrong one.
        const auto rangeOf = [](QDialog &box) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };
        const auto pick = [](QDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };

        // 合并 with nothing in the list AND a split pending: the list is the more basic
        // nothing-to-do, and §29 read exactly this pair on a real screen (with an empty list
        // the pending split is never mentioned) without any test saying so.
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.splitInto(15, &error),
                  QStringLiteral("setup: a split of every 15 pages is pending: %1").arg(error));
            MergePagesDialog box(&tab, &editor);
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有加上要合并进来的文件")),
                  QStringLiteral("an empty list answers first, even with a split pending: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("已经说了要拆成每")),
                  QStringLiteral("and the pending split is not mentioned while there is nothing "
                                 "to merge in: %1")
                      .arg(said));
        }

        // 合并 with a row that has not said its pages AND a split pending: here the split wins,
        // because it invalidates the whole request rather than one row of it. The order is the
        // decision, so it needs both sentences in the same run to be worth anything.
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.splitInto(15, &error), "setup: a split is pending");
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({donor}).isEmpty(), "setup: the donor row is taken");
            auto *table = box.findChild<QTableWidget *>();
            check(table != nullptr && table->item(0, 2) != nullptr, "setup: the range cell");
            table->item(0, 2)->setText(QStringLiteral(" "));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("已经说了要拆成每 15 页一份")),
                  QStringLiteral("a silent row does not outrank a request the model will refuse: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("还没有说出要合入哪几页")),
                  QStringLiteral("and the row is not named while the split is what stops it: %1")
                      .arg(said));
        }

        // 合并 with a silent row and no split: now the row is the only thing wrong, and the
        // sentence names which row-- so the same box moves from the split's answer to the
        // row's answer by undoing one list entry.
        {
            DocumentEditor editor;
            openBook(editor);
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({donor}).isEmpty(), "setup: the donor row is taken");
            auto *table = box.findChild<QTableWidget *>();
            table->item(0, 2)->setText(QStringLiteral(" "));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要合入哪几页"))
                      && said.contains(baseName(donor)),
                  QStringLiteral("without a pending split the silent row is what gets said, "
                                 "with its file named: %1")
                      .arg(said));
            // and the anchor question is further down the chain than the row question
            pick(box, QStringLiteral("指定页之后"));
            check(saidOf(box).contains(baseName(donor))
                      && !saidOf(box).contains(QStringLiteral("要说合并到第几页之后")),
                  QStringLiteral("a blank anchor does not cut in front of a silent row: %1")
                      .arg(saidOf(box)));
        }

        // 删除 with nothing typed: blank is nothing said, and the button waits (§13's finding,
        // fixed by making a blank box mean nothing rather than everything).
        {
            DocumentEditor editor;
            openBook(editor);
            DeletePagesDialog box(&tab, 0, &editor);
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要删哪几页")),
                  QStringLiteral("a box nobody typed into says that and does not count: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and the button is off, so nothing can be pressed");
        }

        // 删除 with pages typed that an earlier pending 删页 already took: those numbers are
        // original page numbers (§15), so typing 3 after deleting original page 3 names a page
        // that is not there to delete -- and the answer must not be "you said nothing".
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.deletePages({2}, &error),
                  QStringLiteral("setup: original page 3 is already taken out: %1").arg(error));
            DeletePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = rangeOf(box);
            check(range != nullptr, "setup: the box that takes the numbers");
            range->setText(QStringLiteral("3"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("这些页在前面的修改里已经删掉了")),
                  QStringLiteral("contradicting the reader is the thing to avoid, so this names "
                                 "the earlier change: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("还没有说出要删哪几页")),
                  QStringLiteral("and not the sentence for a blank box, which this is not: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and nothing is left to press");
        }

        // 删除 naming everything that is left: the floor is its own answer, and it is said
        // before the arithmetic that would otherwise report 剩下 0 页.
        {
            DocumentEditor editor;
            openBook(editor);
            DeletePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = rangeOf(box);
            range->setText(QStringLiteral("1-40"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("这样一页也不剩了")),
                  QStringLiteral("the floor answers before the arithmetic does: %1").arg(said));
            check(!said.contains(QStringLiteral("剩下 0 页")),
                  QStringLiteral("and no line of numbers is offered for a document that cannot "
                                 "exist: %1")
                      .arg(said));
        }

        // 移动 with nothing named AND no anchor number: the pages are the more basic question.
        // Getting there is itself a reading: the box opens on 当前页, which always names one
        // page, so the "no pages named" branch only exists behind the 指定页码 choice -- a box
        // left as it opened asks the anchor question first, and that is the right order.
        {
            DocumentEditor editor;
            openBook(editor);
            MovePagesDialog box(&tab, 0, &editor);
            pick(box, QStringLiteral("指定页码"));
            pick(box, QStringLiteral("指定页之后"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要移动哪几页")),
                  QStringLiteral("no pages named is said before no anchor: %1").arg(said));
            check(!said.contains(QStringLiteral("要说移到第几页之后")),
                  QStringLiteral("and the anchor question waits its turn: %1").arg(said));
            pick(box, QStringLiteral("当前页（第 1 页）"));
            check(saidOf(box).contains(QStringLiteral("要说移到第几页之后")),
                  QStringLiteral("name the page another way and the anchor is the only thing "
                                 "left missing: %1")
                      .arg(saidOf(box)));
        }

        // 水印 with neither words nor pages: the text is the first question the box asks, and
        // the page question is not raised while the marks themselves are still unsaid.
        {
            DocumentEditor editor;
            openBook(editor);
            WatermarkDialog box(&tab, &editor);
            pick(box, QStringLiteral("指定页码"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("words first: %1").arg(said));
            check(!said.contains(QStringLiteral("还没有说出要盖哪几页")),
                  QStringLiteral("and the page question is not asked yet: %1").arg(said));
        }

        // 水印 with no words but a page range typed, on a document that carries imported pages:
        // the imported-pages aside is about pages that will not be inked, so it stays out of a
        // refusal that already says nothing was named. (It used to be appended here, and the
        // reason this is a pin rather than a comment is that the aside is gated on the pages,
        // not on the answer the box just gave.)
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: one imported page: %1").arg(error));
            WatermarkDialog box(&tab, &editor);
            QLineEdit *range = rangeOf(box);
            check(range != nullptr, "setup: the box that takes the numbers");
            range->setText(QStringLiteral("3"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("a typed range with no words is still no words: %1").arg(said));
            check(!said.contains(QStringLiteral("前面合并进来的那")),
                  QStringLiteral("and nothing is explained about pages that were never asked to "
                                 "be inked: %1")
                      .arg(said));
        }
    }

    // ---- the page the status bar names is the page on the screen ----
    //
    // §13 wrote this down from a real window: after a zoom-mode change the pages in
    // front were 5 and 6 while the bar still read "2 / 40", and pressing a paging key
    // once made the two agree. The reason is that only scrolling ever works out which
    // page is current -- see scrollContentsBy -- and the changes that re-lay the pages
    // out can move a page boundary across the middle of the viewport without moving the
    // scrollbar by a pixel. No scroll event, so nothing re-derived the number, and what
    // the reader was told named a page that was not in front of them. The same number
    // feeds the page box and the six 文档 dialogs' "当前页（第 %1 页）", so it is not
    // only the corner of the window that was wrong.
    //
    // The arithmetic below is worked out by hand the way layout_smoke's is: the
    // fixture's pages are 595x842 points and the gap is 12 pixels (kSpacing), so at 100%
    // page k starts at 12 + k * 854 content pixels, and the middle of the window is the
    // scroll position plus half a viewport.
    {
        check(tab.open(directory + QStringLiteral("/many-pages.pdf")) == Document::Status::Ready,
              "the forty-page fixture opens");
        check(tab.pageCount() == 40,
              QStringLiteral("forty pages, the document the note was read on (got %1)")
                  .arg(tab.pageCount()));

        // Which page is in front, worked out from the rectangles the view hands out
        // rather than from the answer it reports: a check that asked the view both
        // questions could be answered by a view that was wrong about both. The rule is
        // the one a reader uses without noticing -- the page the middle of the window is
        // on -- and the hand-worked numbers below are what pin it, since this lambda and
        // PageLayout::pageAtViewportCenter could both be quietly wrong.
        const auto pageOnScreen = [view] {
            const double middle = view->viewport()->height() / 2.0;
            const int count = view->pageCount();
            for (int page = 0; page < count; ++page) {
                const QRectF rect = view->pageRectInViewport(page);
                if (!rect.isNull() && middle < rect.bottom())
                    return page;
            }
            return count - 1;
        };

        // The status bar is not polled; it moves when the view says so, through
        // DocumentTab's pageChanged. A fix that settled the internal page and stayed
        // quiet would leave the number where it was, so the announcement is what is
        // checked, not just the field.
        int announcements = 0;
        int lastAnnounced = -1;
        const QMetaObject::Connection listening =
            QObject::connect(view, &PageView::currentPageChanged, view, [&](int page) {
                ++announcements;
                lastAnnounced = page;
            });

        // Back to one place every case starts from: 100%, one page per row, page 11's top
        // edge at the top of the window. A case that began anywhere else would have to
        // argue about where it began as well as about what changed.
        const auto startAtPageEleven = [&](const QString &forCase) {
            view->setRotation(0);
            view->setLayoutMode(PageLayout::Mode::Continuous);
            view->setZoomMode(ZoomController::Mode::Percent);
            view->setZoom(1.0);
            view->goToPage(10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(view->verticalScrollBar()->value() == 8552,
                  QStringLiteral("%1 starts with page 11's top at the top of the window "
                                 "(scroll is %2)")
                      .arg(forCase)
                      .arg(view->verticalScrollBar()->value()));
            check(view->currentPage() == 10 && pageOnScreen() == 10,
                  QStringLiteral("%1 begins in agreement: bar %2, screen %3")
                      .arg(forCase)
                      .arg(view->currentPage() + 1)
                      .arg(pageOnScreen() + 1));
            announcements = 0;
            lastAnnounced = -1;
        };

        // The promise, said once and then asked after every one of the changes that can
        // break it.
        const auto agreesWithTheScreen = [&](const QString &after) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const int shown = pageOnScreen();
            check(shown != 10,
                  QStringLiteral("%1 moved a page boundary across the middle of the window, "
                                 "so there was something to get wrong (still page %2)")
                      .arg(after)
                      .arg(shown + 1));
            check(view->currentPage() == shown,
                  QStringLiteral("%1 names the page that is in front (bar says page %2, the "
                                 "screen shows page %3)")
                      .arg(after)
                      .arg(view->currentPage() + 1)
                      .arg(shown + 1));
            check(announcements > 0 && lastAnnounced == shown,
                  QStringLiteral("%1 says it out loud, which is the only way the bar and the "
                                 "page box can hear (announcements %2, last %3)")
                      .arg(after)
                      .arg(announcements)
                      .arg(lastAnnounced + 1));
        };

        // ---- scrolling, which is the path that already worked ----
        //
        // Here so that the one place that decides the answer has a check over every way of
        // getting there: the older test of this asks how many times the view said a page
        // changed, which a view that said the wrong one would pass just as well.
        {
            startAtPageEleven(QStringLiteral("scrolling to page 2"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(866 - middle + 10);
            check(pageOnScreen() == 1,
                  QStringLiteral("page 2 is what the middle of the window is on now (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("scrolling to page 2"));
        }

        // ---- a factor, chosen by hand or typed into the box ----
        //
        // At 400% a page occupies 3368 pixels and starts at 12 + k * 3380. The middle of
        // the window is between 8552 and 9394 for any window under 1684 px tall, and that
        // whole range is inside page 3 (6772 .. 10140).
        {
            startAtPageEleven(QStringLiteral("zooming to 400%"));
            const int scrolledTo = view->verticalScrollBar()->value();
            view->setZoom(4.0);
            check(view->verticalScrollBar()->value() == scrolledTo,
                  "and the zoom did not scroll anywhere -- which is the whole reason: there "
                  "was no scroll event to re-derive the page from");
            check(pageOnScreen() == 2,
                  QStringLiteral("page 3 is what is in front at four times the size (says %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("zooming to 400%"));
        }

        // ---- a fit mode, which is what the note was about ----
        //
        // 适应整页 shrinks the factor until a whole page fits the window's height, so the
        // pages get shorter and one of them ends up higher than the middle of the window
        // than it was. No hand number here: the factor depends on the window's height,
        // which the test reads rather than assumes.
        {
            startAtPageEleven(QStringLiteral("适应整页"));
            view->setZoomMode(ZoomController::Mode::FitPage);
            agreesWithTheScreen(QStringLiteral("适应整页"));
        }

        // ---- the same fit mode in a window wide enough for a page ----
        //
        // This case exists because the one above does not, on its own, cover the mode
        // change. Fitting a page makes the pages narrower as well as shorter, and in a
        // window that was too narrow for a page that takes the sideways scrollbar away --
        // which resizes the viewport a moment later, and that resize recomputes the fit and
        // re-takes the number. Cut the call site in setZoomMode() and the case above stays
        // green: the number is settled by the second pass, not the first.
        //
        // Wide enough for a page to hang nothing sideways, there is no second pass: the mode
        // change is the only event, so this is the case that asks it.
        {
            const QSize sizeBefore = tab.size();
            tab.resize(1400, 620);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            startAtPageEleven(QStringLiteral("适应整页 in a wide window"));
            check(view->horizontalScrollBar()->maximum() == 0,
                  QStringLiteral("the window is wide enough that nothing hangs sideways at "
                                 "100% either (scrollbar maximum %1)")
                      .arg(view->horizontalScrollBar()->maximum()));

            const int heightBefore = view->viewport()->height();
            view->setZoomMode(ZoomController::Mode::FitPage);
            check(view->viewport()->height() == heightBefore,
                  QStringLiteral("and the fitted pages are narrower still, so no scrollbar "
                                 "came or went and no resize arrives afterwards to do it "
                                 "instead (viewport height %1 -> %2)")
                      .arg(heightBefore)
                      .arg(view->viewport()->height()));
            agreesWithTheScreen(QStringLiteral("适应整页 in a wide window"));
            tab.resize(sizeBefore);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }

        // ---- two pages per row ----
        //
        // Twenty spreads of 854 pixels each, so the middle of the window (8552 .. 9394)
        // lands in spread 11, which is pages 21 and 22 -- and the layout's rule names the
        // left one of a spread first.
        {
            startAtPageEleven(QStringLiteral("turning on two-page mode"));
            view->setLayoutMode(PageLayout::Mode::TwoPage);
            check(pageOnScreen() == 20,
                  QStringLiteral("the spread in front is pages 21 and 22 (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("turning on two-page mode"));
        }

        // ---- a quarter turn ----
        //
        // The pages become 842 wide by 595 tall, so every page above the middle of the
        // window gets 247 pixels shorter and the middle reaches further into the
        // document. Which page exactly depends on the window's height, so this one is the
        // invariant alone -- with the check inside agreesWithTheScreen that a boundary
        // really did cross, which is what keeps it from passing by standing still.
        {
            startAtPageEleven(QStringLiteral("rotating"));
            view->setRotation(90);
            agreesWithTheScreen(QStringLiteral("rotating"));
        }

        // ---- a shorter window ----
        //
        // The clearest case of all, because nothing about the document changed at all: the
        // middle of the window moved up by half the difference in height. A taller window
        // makes the same point, but this screen is only so tall -- growing the window by
        // two page periods grew the viewport by one, which is why the measurement goes the
        // other way -- and a shorter window starts by parking page 11's top edge a known
        // ten pixels above the middle, so the distance to the boundary it has to cross is
        // fixed rather than a function of how tall the window happens to be.
        {
            startAtPageEleven(QStringLiteral("making the window shorter"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(8552 - middle + 10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(pageOnScreen() == 10,
                  QStringLiteral("the middle of the window is ten pixels inside page 11 "
                                 "(says page %1)")
                      .arg(pageOnScreen() + 1));

            const int heightBefore = view->viewport()->height();
            tab.resize(tab.width(), tab.height() - 200);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const int lost = heightBefore - view->viewport()->height();
            // The middle rose by half of that, so anything between 21 and 1752 pixels
            // leaves it inside page 10 -- the ask is 200, and however much of it the
            // window's own minimum size gave back, the answer is a page number rather
            // than a measurement of the platform.
            check(lost > 20 && lost < 1752,
                  QStringLiteral("the window lost %1 pixels of page, which is more than twice "
                                 "the ten pixels to spare and less than a whole page")
                      .arg(lost));
            check(pageOnScreen() == 9,
                  QStringLiteral("a shorter window reaches back over the edge the middle was "
                                 "inside (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("making the window shorter"));
        }

        // ---- opening a second document in the same tab ----
        //
        // The same fact, with the document changing instead of the window: which page the
        // middle of the window sits on is a property of the geometry, and the geometry
        // includes what is laid out. setPageSizes() answered it by writing page 1 down,
        // which is where a reader expects to start rather than where they end up -- the
        // scroll position is not reset for a new document, so a two-page file opened in a
        // tab that was showing page 2 of a forty-page one is showing page 2, and the bar
        // read "1 / 2" while it did.
        //
        // text.pdf's pages are the same 595x842 as the forty-page fixture's, which is what
        // puts the position below inside the new document rather than past its end -- a
        // position past the end would be clamped, the clamp would scroll, and the scrolling
        // path would have quietly fixed it. That is why the number is asked for before and
        // compared after: this case is the half of the situation that no scroll event ever
        // covered. Where a new document *should* start is a different question, and is
        // recorded as still open in docs/store-submission.md §25.
        {
            startAtPageEleven(QStringLiteral("opening a two-page document"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(866 - middle + 10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(pageOnScreen() == 1,
                  QStringLiteral("the middle of the window is ten pixels inside page 2 of the "
                                 "forty-page fixture (says page %1)")
                      .arg(pageOnScreen() + 1));
            const int kept = view->verticalScrollBar()->value();

            check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
                  "the two-page fixture opens in the same tab");
            check(tab.pageCount() == 2,
                  QStringLiteral("of two pages (got %1)").arg(tab.pageCount()));
            check(view->verticalScrollBar()->value() == kept,
                  QStringLiteral("and the new document is still showing the same content "
                                 "pixels as the old one (scroll %1 -> %2), so no scroll event "
                                 "happened -- which is the shape of this bug")
                      .arg(kept)
                      .arg(view->verticalScrollBar()->value()));
            agreesWithTheScreen(QStringLiteral("opening a two-page document"));
        }

        QObject::disconnect(listening);
    }

    if (failures == 0) {
        std::cout << "the view and the tab hold together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
