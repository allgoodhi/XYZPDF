// The performance baseline, measured rather than asserted.
//
// ---- why this is a program that prints and not a test that passes ----
//
// docs/PLAN.md asks M1 and M2 to be verifiable in numbers -- "a 500-page
// document scrolls without lag", "memory has a ceiling", "searching a large
// document does not freeze the UI" -- and a number cannot be asserted the way a
// colour can. Any threshold written into a test is a threshold that will be
// raised the first time it fails on somebody's slower machine, which turns the
// test into a rumour. So this measures, prints a table, and leaves the judging
// to the record in docs/perf.md, where a regression is visible as a change in a
// column rather than as a red build.
//
// tools/build_all_tests.py builds every target whose name does not say it is
// something else, and 'bench' is one of those names: this compiles with the
// suite, so it cannot quietly rot the way an unrunnable script does, and it does
// not make the suite slower or greener-or-redder than it is.
//
// ---- how to read the numbers ----
//
// The committed fixtures are hand-written and deliberately spare: one line of
// text per page, no images. They answer "what does the program cost per page",
// and they answer it cheap -- a real page of a datasheet costs an order of
// magnitude more, which is why the second argument exists. Anything measured on
// a file outside the repository is labelled as such, because nobody else can
// reproduce it.
//
// Everything is single-shot on a cold cache, in one process, in the order
// written -- which is what makes the memory figure mean something. Re-run it a
// few times before believing a difference of a few per cent.
//
// Run it:
//
//     build-local-release\tests\xyzpdf_perf_bench.exe tests/fixtures
//     build-local-release\tests\xyzpdf_perf_bench.exe tests/fixtures H:\pdf_test\CH340.PDF

#include "../src/document/document.h"
#include "../src/document/qtpdfbackend.h"
#include "../src/document/textsearch.h"
#include "../src/render/rendercache.h"
#include "../src/render/rendertoken.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QVector>

#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {

// Points to device pixels at the zoom a screen shows: 96 dpi over PDF's 72.
constexpr double kScreenZoom = 96.0 / 72.0;

// The pages rendered, spread across the document rather than taken from its
// front: the first pages of a file are often the least representative thing in
// it, and the question is what scrolling costs.
constexpr int kRendered = 40;

qint64 memoryKb(bool peak)
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters;
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters)))
        return -1;
    return static_cast<qint64>(peak ? counters.PeakWorkingSetSize
                                    : counters.WorkingSetSize) / 1024;
#else
    Q_UNUSED(peak);
    return -1;
#endif
}

// One row of the table. Printed as markdown so the output can be pasted into
// docs/perf.md rather than retyped -- a table that has to be copied by hand is a
// table that stops being updated.
void row(const QString &what, const QString &value, const QString &note)
{
    printf("| %-38s | %11s | %-40s |\n",
           qPrintable(what), qPrintable(value), qPrintable(note));
    fflush(stdout);
}

double milliseconds(const std::function<void()> &work)
{
    QElapsedTimer clock;
    clock.start();
    work();
    // nsecsElapsed, not elapsed: elapsed() is already whole milliseconds, and a
    // page of a synthetic document renders in less than one. Reported in the
    // wrong unit the whole table comes out as zeros, which look like a fast
    // program rather than like a measurement that is not measuring.
    return static_cast<double>(clock.nsecsElapsed()) / 1.0e6;
}

QString ms(double value)
{
    return QString::number(value, 'f', value < 10.0 ? 2 : 1) + QStringLiteral(" ms");
}

// Everything that can be said about one document, as rows. Returns false when the
// file could not be read, which the caller reports rather than tabulates.
bool reportDocument(const QString &path, qint64 cacheLimitBytes)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        std::fprintf(stderr, "missing %s\n", qPrintable(path));
        return false;
    }

    QtPdfBackend document;
    Document::Status status = Document::Status::FileError;
    const double opened = milliseconds([&] {
        status = document.load(path, QString());
    });
    if (status != Document::Status::Ready) {
        std::fprintf(stderr, "%s did not open (%d): %s\n", qPrintable(path),
                     static_cast<int>(status), qPrintable(document.lastError()));
        return false;
    }

    const int pages = document.pageCount();
    row(QStringLiteral("%1: open").arg(info.fileName()), ms(opened),
        QStringLiteral("%1 pages, %2 KB on disk")
            .arg(pages)
            .arg(info.size() / 1024));

    // Page geometry, read for every page before anything is drawn: the layout
    // needs all of it, and a per-page cost here is paid once per document rather
    // than once per repaint.
    QVector<QSizeF> sizes;
    const double measuredSizes = milliseconds([&] {
        sizes.reserve(pages);
        for (int page = 0; page < pages; ++page)
            sizes.append(document.pageSize(page));
    });
    row(QStringLiteral("%1: every page size").arg(info.fileName()), ms(measuredSizes),
        QStringLiteral("%1 ms per thousand pages").arg(measuredSizes * 1000.0 / pages, 0, 'f', 1));

    const int rendered = qMin(kRendered, pages);
    const int step = qMax(1, pages / rendered);
    QSize firstPixel;
    int renderedCount = 0;
    const double renderTime = milliseconds([&] {
        for (int page = 0; page < pages; page += step) {
            const QSize pixels(qRound(sizes[page].width() * kScreenZoom),
                               qRound(sizes[page].height() * kScreenZoom));
            if (firstPixel.isEmpty())
                firstPixel = pixels;
            RenderOptions options;
            options.optimizedForLcd = true;
            if (document.render(page, pixels, options).isNull())
                return;
            ++renderedCount;
        }
    });
    if (renderedCount == 0) {
        std::fprintf(stderr, "%s rendered nothing\n", qPrintable(path));
        return false;
    }
    row(QStringLiteral("%1: render %2 pages").arg(info.fileName()).arg(renderedCount),
        ms(renderTime),
        QStringLiteral("%1 ms/page at %2x%3 px")
            .arg(renderTime / renderedCount, 0, 'f', 1)
            .arg(firstPixel.width())
            .arg(firstPixel.height()));

    QVector<QString> texts;
    const double extracted = milliseconds([&] {
        texts.reserve(pages);
        for (int page = 0; page < pages; ++page)
            texts.append(document.text(page));
    });
    row(QStringLiteral("%1: text of all pages").arg(info.fileName()), ms(extracted),
        QStringLiteral("%1 ms/page").arg(extracted / pages, 0, 'f', 2));

    // Counted rather than assumed, because "this fixture is a scan" is a claim
    // about its contents and the whole reason to have the file is that it has
    // none. A number here would mean the fixture is not testing what the row
    // above appears to test.
    qsizetype characters = 0;
    for (const QString &text : std::as_const(texts))
        characters += text.trimmed().size();
    row(QStringLiteral("%1: text layer").arg(info.fileName()),
        characters == 0 ? QStringLiteral("none") : QStringLiteral("%1 chars").arg(characters),
        QStringLiteral("a scan has to answer \"none\" here"));

    int found = 0;
    const double searched = milliseconds([&] {
        for (int page = 0; page < texts.size(); ++page)
            found += TextSearch::matchesInPage(page, texts[page],
                                               QStringLiteral("a"), false).size();
    });
    row(QStringLiteral("%1: search all pages").arg(info.fileName()), ms(searched),
        QStringLiteral("%2 ms/page, %3 matches")
            .arg(searched / pages, 0, 'f', 3)
            .arg(found));

    // ---- the ceiling ----
    //
    // The claim M1 makes is that memory has a bound. Pages are pushed through a
    // cache limited well below what the whole document would need, and the cache
    // has to stay under it however far the reader has scrolled.
    //
    // The tokens carry a generation because a token without one is "recognisably
    // empty" and the cache refuses it outright -- a bench that forgot this
    // reported a peak of 0 MB and looked like a pass.
    RenderCache cache(cacheLimitBytes);
    int inserted = 0;
    qint64 highWater = 0;
    const double cached = milliseconds([&] {
        quint64 generation = 1;
        for (int page = 0; page < pages; ++page) {
            const QSize pixels(qRound(sizes[page].width() * kScreenZoom),
                               qRound(sizes[page].height() * kScreenZoom));
            RenderOptions options;
            options.optimizedForLcd = true;
            const QImage image = document.render(page, pixels, options);
            if (image.isNull())
                continue;
            RenderToken token;
            token.generation = generation++;
            token.page = page;
            token.pixels = pixels;
            cache.insert(token, image);
            ++inserted;
            highWater = qMax(highWater, cache.usedBytes());
        }
    });
    row(QStringLiteral("%1: %2 pages through a %3 MB cache")
            .arg(info.fileName())
            .arg(inserted)
            .arg(cacheLimitBytes / (1024 * 1024)),
        ms(cached),
        QStringLiteral("peak %1 MB of %2 MB, %3 entries kept")
            .arg(highWater / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(cacheLimitBytes / (1024.0 * 1024.0), 0, 'f', 0)
            .arg(cache.count()));
    row(QStringLiteral("%1: stayed inside its limit").arg(info.fileName()),
        highWater <= cacheLimitBytes ? QStringLiteral("yes") : QStringLiteral("NO"),
        QStringLiteral("whole render of the file cost %1 ms")
            .arg(cached, 0, 'f', 0));

    return true;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QCoreApplication application(argc, argv);

    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    QStringList documents;
    documents << directory + QStringLiteral("/huge.pdf");
    // Anything else named on the command line is measured the same way. These are
    // the real documents: a datasheet with photographs, a textbook of a few
    // hundred pages. They are not in the repository, so a number taken from one
    // is labelled as not reproducible by whoever reads the record.
    for (int index = 2; index < argc; ++index)
        documents << QString::fromLocal8Bit(argv[index]);

#ifdef Q_OS_WIN
    SetProcessDPIAware();
#endif

    printf("| Measurement | Value | Note |\n");
    printf("|-------------|-------|------|\n");
    row("process at start", QString::number(memoryKb(false)) + " KB",
        "before any document is read");

    int reported = 0;
    for (const QString &path : std::as_const(documents))
        if (reportDocument(path, 64LL * 1024 * 1024))
            ++reported;

    row("peak working set, whole run", QString::number(memoryKb(true)) + " KB",
        QStringLiteral("after %1 document(s) were measured").arg(reported));

    if (reported == 0) {
        std::fprintf(stderr, "nothing could be measured\n");
        return 2;
    }
    return 0;
}
