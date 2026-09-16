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
#include "../src/ui/ocrdialog.h"
#include "../src/view/pageview.h"
#include "../src/view/zoomcontroller.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QString>
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

    // ---- a drag on a page with no text is a box, and the box comes back ----
    //
    // blank.pdf has no text layer, so every point on its page answers the
    // cursor's question with "no line here" -- and the drag is a box, reported
    // in the page's own coordinates, because that is what the recognition crop
    // is cut from. The fractions below are the same ones the selection test
    // used, so the two results can be compared by eye: the same gesture, on a
    // page with text and on one without.
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

            // And on a page that has text, the same drag is a selection, not a
            // box: the rule follows the pointer, and the pointer says I-beam
            // there.
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
            sendMouse(view->viewport(), QEvent::MouseButtonPress, onLine, true);
            sendMouse(view->viewport(), QEvent::MouseMove, across, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, across);
            check(captures == 1, "a drag over text reports no region");
            check(tab.hasSelection(), "and selects text instead");

            // Off again before the locals go out of scope: the connection
            // outlives this block, and it captures them by reference.
            QObject::disconnect(listening);
        }
    }

    // ---- Alt and a box over text: read, not selected ----
    //
    // The case the whole feature is for, and the one its own test did not
    // cover: the drag on the blank fixture proves a window opens and says
    // nothing about what is in it. Here the box is drawn around the fixture's
    // line of text, and what has to come back is the words that are in it.
    //
    // Alt because the pointer is over a line: a plain drag there is a
    // selection, and Alt is what asks for a box instead.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a box over its text");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn");

        const int dialogsBefore = tab.findChildren<OcrDialog *>().size();

        const QRectF page = view->pageRectInViewport(0);
        // Around the line of text, with room to spare: the fixture draws it
        // near the top left of a 595x842 page, at y between 63 and 80 points.
        const QPointF from(page.left() + page.width() * 0.05,
                           page.top() + page.height() * 0.05);
        const QPointF to(page.left() + page.width() * 0.45, page.top() + page.height() * 0.13);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true, Qt::AltModifier);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true, Qt::AltModifier);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to, false, Qt::AltModifier);

        check(!tab.hasSelection(), "Alt and a drag over text does not select it");

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

    // The recognition a box drag kicks off runs on the global pool. Whatever
    // it decided to do, the test does not leave work behind at exit: a process
    // teardown under a running WinRT call is a hang, not a failure.
    QThreadPool::globalInstance()->waitForDone(30000);

    if (failures == 0) {
        std::cout << "the view and the tab hold together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
