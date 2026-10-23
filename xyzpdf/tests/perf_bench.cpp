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
// ---- two columns ----
//
// docs/pdfium-plan.md sec.5.1/5.2 compared the two engines with a probe; sec.10
// F7-B asks that comparison to live here instead, as a column of this table. So
// when a PDFium release was found at configure time (XYZPDF_HAVE_PDFIUM) the
// same file is measured twice, once through each backend, in one process. When
// none was found the PDFium column reads "-": a fresh clone still builds and
// runs this, and the record says which build produced the numbers it quotes.
//
// One order effect is worth knowing before reading the table: Qt PDF is
// measured first, so the PDFium column is the one that reads a warm file cache.
// How much that is worth has not been measured (sec.5.2's separate runs on the
// 600-page fixture read 1.6 vs 0.3 ms, the same direction); the render and text
// rows are CPU-bound and should not care.
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
// Everything is single-shot, in one process, in the order written -- which is
// what makes the memory figure mean something. Re-run it a few times before
// believing a difference of a few per cent.
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

#ifdef XYZPDF_HAVE_PDFIUM
#include "../src/document/pdfiumbackend.h"
#endif

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

// Well below the program's 256 MB default on purpose: the point is to force
// eviction on a 600-page fixture, not to fit it.
constexpr qint64 kCacheLimitBytes = 64LL * 1024 * 1024;

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
void rowPair(const QString &what, const QString &qt, const QString &pdfium, const QString &note)
{
    printf("| %-42s | %12s | %12s | %-34s |\n",
           qPrintable(what), qPrintable(qt), qPrintable(pdfium), qPrintable(note));
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

// Everything one engine said about one document. Two of these are filled per
// file, one through each backend, and the table prints them side by side.
struct Measurements
{
    bool opened = false;
    int pages = 0;
    double openMs = 0;
    QVector<QSizeF> sizes;
    double sizesMs = 0;

    int renderedCount = 0;
    double renderMs = 0;
    QSize firstPixel;

    double textMs = 0;
    qsizetype characters = 0;
    double searchMs = 0;
    int found = 0;

    int inserted = 0;
    double cacheMs = 0;
    qint64 highWater = 0;
    int kept = 0;
};

// Measures `document` on `path`. Returns false when the file could not be read,
// which the caller reports rather than tabulates.
bool measure(Document &document, const QString &path, qint64 cacheLimitBytes,
             Measurements &measured)
{
    Document::Status status = Document::Status::FileError;
    measured.openMs = milliseconds([&] { status = document.load(path, QString()); });
    if (status != Document::Status::Ready) {
        std::fprintf(stderr, "%s did not open (%d): %s\n", qPrintable(path),
                     static_cast<int>(status), qPrintable(document.lastError()));
        return false;
    }
    measured.opened = true;
    measured.pages = document.pageCount();

    // Page geometry, read for every page before anything is drawn: the layout
    // needs all of it, and a per-page cost here is paid once per document rather
    // than once per repaint.
    measured.sizesMs = milliseconds([&] {
        measured.sizes.reserve(measured.pages);
        for (int page = 0; page < measured.pages; ++page)
            measured.sizes.append(document.pageSize(page));
    });

    const int rendered = qMin(kRendered, measured.pages);
    const int step = qMax(1, measured.pages / rendered);
    measured.renderMs = milliseconds([&] {
        for (int page = 0; page < measured.pages; page += step) {
            const QSize pixels(qRound(measured.sizes[page].width() * kScreenZoom),
                               qRound(measured.sizes[page].height() * kScreenZoom));
            if (measured.firstPixel.isEmpty())
                measured.firstPixel = pixels;
            RenderOptions options;
            options.optimizedForLcd = true;
            if (document.render(page, pixels, options).isNull())
                return;
            ++measured.renderedCount;
        }
    });
    if (measured.renderedCount == 0) {
        std::fprintf(stderr, "%s rendered nothing\n", qPrintable(path));
        return false;
    }

    QVector<QString> texts;
    measured.textMs = milliseconds([&] {
        texts.reserve(measured.pages);
        for (int page = 0; page < measured.pages; ++page)
            texts.append(document.text(page));
    });
    // Counted rather than assumed, because "this fixture is a scan" is a claim
    // about its contents and the whole reason to have the file is that it has
    // none. A number here would mean the fixture is not testing what the row
    // above appears to test.
    for (const QString &text : std::as_const(texts))
        measured.characters += text.trimmed().size();

    measured.searchMs = milliseconds([&] {
        for (int page = 0; page < texts.size(); ++page)
            measured.found += TextSearch::matchesInPage(page, texts[page],
                                                        QStringLiteral("a"), false).size();
    });

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
    measured.cacheMs = milliseconds([&] {
        quint64 generation = 1;
        for (int page = 0; page < measured.pages; ++page) {
            const QSize pixels(qRound(measured.sizes[page].width() * kScreenZoom),
                               qRound(measured.sizes[page].height() * kScreenZoom));
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
            ++measured.inserted;
            measured.highWater = qMax(measured.highWater, cache.usedBytes());
        }
        measured.kept = cache.count();
    });

    return true;
}

QString pagesNote(const Measurements &measured, const QFileInfo &info)
{
    return QStringLiteral("%1 pages, %2 KB on disk")
        .arg(measured.pages)
        .arg(info.size() / 1024);
}

QString millisPerPage(double millisecondsValue, int pages)
{
    return QStringLiteral("%1 ms/page")
        .arg(pages > 0 ? millisecondsValue / pages : 0.0, 0, 'f', 2);
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

#ifdef XYZPDF_HAVE_PDFIUM
    printf("Qt %s, PDFium %s\n", qVersion(), XYZPDF_PDFIUM_VERSION);
#else
    printf("Qt %s, PDFium not installed (the second column reads \"-\")\n", qVersion());
#endif
    printf("| Measurement | Qt PDF | PDFium | Note |\n");
    printf("|-------------|--------|--------|------|\n");
    rowPair("process at start", QString::number(memoryKb(false)) + " KB", "-",
            QStringLiteral("before any document is read"));

    int reported = 0;
    for (const QString &path : std::as_const(documents)) {
        const QFileInfo info(path);
        if (!info.exists()) {
            std::fprintf(stderr, "missing %s\n", qPrintable(path));
            continue;
        }

        Measurements qt;
        QtPdfBackend qtDocument;
        if (!measure(qtDocument, path, kCacheLimitBytes, qt))
            continue;

        Measurements pdfium;
        bool havePdfium = false;
#ifdef XYZPDF_HAVE_PDFIUM
        {
            PdfiumBackend pdfiumDocument;
            havePdfium = measure(pdfiumDocument, path, kCacheLimitBytes, pdfium);
        }
#endif
        // A dash, not a zero: "not measured" and "measured as nothing" are the
        // two readings this table must never confuse.
        const auto pdfiumMs = [&](double value) {
            return havePdfium ? ms(value) : QStringLiteral("-");
        };
        const auto pdfiumValue = [&](const QString &value) {
            return havePdfium ? value : QStringLiteral("-");
        };

        rowPair(QStringLiteral("%1: open").arg(info.fileName()),
                ms(qt.openMs), pdfiumMs(pdfium.openMs),
                havePdfium
                    ? QStringLiteral("%1 pages (PDFium %2), %3 KB")
                          .arg(qt.pages)
                          .arg(pdfium.pages)
                          .arg(info.size() / 1024)
                    : pagesNote(qt, info));

        rowPair(QStringLiteral("%1: every page size").arg(info.fileName()),
                ms(qt.sizesMs), pdfiumMs(pdfium.sizesMs),
                QStringLiteral("%1 ms per thousand pages")
                    .arg(qt.sizesMs * 1000.0 / qMax(1, qt.pages), 0, 'f', 1));

        rowPair(QStringLiteral("%1: render %2 pages").arg(info.fileName()).arg(qt.renderedCount),
                ms(qt.renderMs), pdfiumMs(pdfium.renderMs),
                QStringLiteral("%1, %2x%3 px")
                    .arg(millisPerPage(qt.renderMs, qt.renderedCount))
                    .arg(qt.firstPixel.width())
                    .arg(qt.firstPixel.height()));

        rowPair(QStringLiteral("%1: text of all pages").arg(info.fileName()),
                ms(qt.textMs), pdfiumMs(pdfium.textMs),
                millisPerPage(qt.textMs, qt.pages));

        rowPair(QStringLiteral("%1: text layer").arg(info.fileName()),
                qt.characters == 0 ? QStringLiteral("none")
                                   : QStringLiteral("%1 chars").arg(qt.characters),
                pdfiumValue(pdfium.characters == 0
                                ? QStringLiteral("none")
                                : QStringLiteral("%1 chars").arg(pdfium.characters)),
                QStringLiteral("a scan has to answer \"none\" here"));

        rowPair(QStringLiteral("%1: search all pages").arg(info.fileName()),
                ms(qt.searchMs), pdfiumMs(pdfium.searchMs),
                QStringLiteral("%1, %2 matches")
                    .arg(millisPerPage(qt.searchMs, qt.pages))
                    .arg(qt.found));

        rowPair(QStringLiteral("%1: %2 pages through a %3 MB cache")
                    .arg(info.fileName())
                    .arg(qt.inserted)
                    .arg(kCacheLimitBytes / (1024 * 1024)),
                ms(qt.cacheMs), pdfiumMs(pdfium.cacheMs),
                havePdfium
                    ? QStringLiteral("peak %1 / %2 MB, %3 / %4 entries kept")
                          .arg(qt.highWater / (1024.0 * 1024.0), 0, 'f', 1)
                          .arg(pdfium.highWater / (1024.0 * 1024.0), 0, 'f', 1)
                          .arg(qt.kept)
                          .arg(pdfium.kept)
                    : QStringLiteral("peak %1 MB, %2 entries kept")
                          .arg(qt.highWater / (1024.0 * 1024.0), 0, 'f', 1)
                          .arg(qt.kept));

        rowPair(QStringLiteral("%1: stayed inside its limit").arg(info.fileName()),
                qt.highWater <= kCacheLimitBytes ? QStringLiteral("yes")
                                                 : QStringLiteral("NO"),
                pdfiumValue(pdfium.highWater <= kCacheLimitBytes ? QStringLiteral("yes")
                                                                 : QStringLiteral("NO")),
                QStringLiteral("the limit is %1 MB").arg(kCacheLimitBytes / (1024 * 1024)));

        ++reported;
    }

    rowPair("peak working set, whole run", QString::number(memoryKb(true)) + " KB", "-",
            QStringLiteral("after %1 document(s) were measured").arg(reported));

    if (reported == 0) {
        std::fprintf(stderr, "nothing could be measured\n");
        return 2;
    }
    return 0;
}
