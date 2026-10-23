// Rendering off the UI thread, and the rule that a stale result is dropped.
//
// This is the constraint the plan calls the one that has to be right the first
// time -- XYZip's preview took four attempts at it, each time by tracking "is
// this still the page I asked for" with something that was true most of the
// time. So the rule is tested directly here: ask for a page, make the answer
// pointless before it comes back, and check that it is reported as discarded
// rather than delivered.
//
// A real render is what makes the test meaningful. A fake renderer that answers
// immediately would never have anything in flight to be stale.

#include "../src/document/qtpdfbackend.h"
#include "../src/render/pagerenderer.h"
#include "../src/render/rendercache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QString>
#include <QTimer>

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

// Waits for the renderer to say something, or gives up. Ten seconds is not a
// performance assertion -- it is the difference between a failing test and a
// test run that never ends.
struct Outcome
{
    bool answered = false;
    bool discarded = false;
    bool timedOut = false;
    RenderToken token;
    QImage image;
};

Outcome waitForOne(PageRenderer &renderer, int timeoutMilliseconds = 10000)
{
    Outcome outcome;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&renderer, &PageRenderer::rendered, &loop,
                     [&outcome, &loop](const RenderToken &token, const QImage &image) {
                         outcome.answered = true;
                         outcome.token = token;
                         outcome.image = image;
                         loop.quit();
                     });
    QObject::connect(&renderer, &PageRenderer::discarded, &loop,
                     [&outcome, &loop](const RenderToken &token) {
                         outcome.discarded = true;
                         outcome.token = token;
                         loop.quit();
                     });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&outcome, &loop] {
        outcome.timedOut = true;
        loop.quit();
    });

    timer.start(timeoutMilliseconds);
    loop.exec();
    return outcome;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    const QString path = directory + QStringLiteral("/text.pdf");

    QtPdfBackend document;
    if (document.load(path, QString()) != Document::Status::Ready) {
        std::cerr << "FAIL: could not open the fixture " << path.toStdString() << '\n';
        return 1;
    }

    PageRenderer renderer;
    renderer.setDocument(&document);

    // ---- a render that is still wanted ----
    {
        RenderToken token;
        token.generation = renderer.generation();
        token.page = 0;
        token.pixels = QSize(240, 340);
        check(renderer.request(token), "a request is queued");
        check(renderer.inFlight() >= 1, "and is in flight");

        const Outcome outcome = waitForOne(renderer);
        check(!outcome.timedOut, "the render came back");
        check(outcome.answered, "and was delivered rather than discarded");
        check(outcome.image.size() == QSize(240, 340),
              "at exactly the size that was asked for");
        check(!outcome.image.isNull(), "as a real image");
        check(outcome.token.page == 0, "for the page that was asked for");
        check(renderer.inFlight() == 0, "and nothing is left in flight");

        // The image is not a blank sheet of the right size: a renderer that
        // returned an empty image would satisfy every check above.
        bool hasInk = false;
        for (int y = 0; y < outcome.image.height() && !hasInk; y += 4) {
            for (int x = 0; x < outcome.image.width(); x += 4) {
                const QColor pixel = outcome.image.pixelColor(x, y);
                if (pixel.lightness() < 200) {
                    hasInk = true;
                    break;
                }
            }
        }
        check(hasInk, "with something drawn on it");

        // ---- and the cache keeps it ----
        RenderCache cache(8 * 1024 * 1024);
        cache.insert(outcome.token, outcome.image);
        check(cache.contains(outcome.token), "the rendered page goes into the cache");
        check(!cache.image(outcome.token).isNull(), "and comes back out of it");
    }

    // ---- a band of a page ----
    //
    // What a page at 800% is rendered as: the size is still the whole page's,
    // because the band's coordinates are in that scale, and the image that
    // comes back is the band. Without this a single page would be 288 MB.
    {
        const RenderToken token =
            makeRenderToken(renderer.generation(), 0, QSize(1000, 1414), QRect(0, 0, 240, 340),
                            0, true);
        check(renderer.request(token), "a band is queued");

        const Outcome outcome = waitForOne(renderer);
        check(!outcome.timedOut, "it comes back");
        check(outcome.answered, "and is delivered");
        check(outcome.image.size() == QSize(240, 340),
              QStringLiteral("the size of the band rather than of the page (%1x%2)")
                  .arg(outcome.image.width())
                  .arg(outcome.image.height()));
        check(outcome.token.clip == QRect(0, 0, 240, 340), "carrying the band it was asked for");
    }

    // ---- a render that is overtaken ----
    //
    // The page is asked for, and then the zoom changes -- or the document is
    // closed, or a page is turned. Either way the answer on its way is an
    // answer to a question nobody is asking, and it must not reach the screen.
    {
        RenderToken token;
        token.generation = renderer.generation();
        token.page = 1;
        token.pixels = QSize(160, 200);

        // Bumped after the request and before the result: the window between
        // the two is where every stale-image bug lives.
        renderer.request(token);
        renderer.beginGeneration();
        check(renderer.generation() != token.generation, "the generation has moved on");

        const Outcome outcome = waitForOne(renderer);
        check(!outcome.timedOut, "the render still came back");
        check(outcome.discarded, "but was reported as discarded");
        check(!outcome.answered, "and was not delivered");
        check(outcome.token.page == 1, "and it was the page that was overtaken");
    }

    // ---- a token from an old generation is refused outright ----
    //
    // The second half of the same rule: not everything has to be rendered to be
    // told no.
    {
        RenderToken stale;
        stale.generation = renderer.generation() - 1;
        stale.page = 0;
        stale.pixels = QSize(100, 100);
        check(!renderer.request(stale), "a request from an old generation is not queued");
    }

    // ---- what is not worth queueing ----
    {
        RenderToken invalid;
        check(!renderer.request(invalid), "a token with no page and no generation is refused");

        RenderToken empty = invalid;
        empty.generation = renderer.generation();
        empty.page = 0;
        check(!renderer.request(empty), "and one with no size");

        RenderToken beyond = empty;
        beyond.pixels = QSize(50, 50);
        beyond.page = 99;
        // Out of range is the renderer's business to refuse or the backend's to
        // return nothing for; what must not happen is a crash or a hang.
        const bool queued = renderer.request(beyond);
        if (queued) {
            const Outcome outcome = waitForOne(renderer, 5000);
            check(!outcome.timedOut, "a page past the end is answered rather than left hanging");
        }
    }

    // ---- a new document ----
    //
    // setDocument is what a tab does when it is given another file, and it has
    // to leave nothing of the old one behind: every request made against it is
    // now an answer to a question about a different document.
    {
        QtPdfBackend other;
        check(other.load(directory + QStringLiteral("/rotated.pdf"), QString())
                  == Document::Status::Ready,
              "the second fixture opens");
        const quint64 before = renderer.generation();
        renderer.setDocument(&other);
        check(renderer.generation() != before, "a new document moves the generation on");
        check(renderer.inFlight() == 0, "and there is nothing of the old one in flight");

        RenderToken token;
        token.generation = renderer.generation();
        token.page = 0;
        token.pixels = QSize(120, 160);
        renderer.request(token);
        const Outcome outcome = waitForOne(renderer);
        check(!outcome.timedOut && outcome.answered, "and rendering works against it");
    }

    if (failures == 0) {
        std::cout << "the page renderer holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
