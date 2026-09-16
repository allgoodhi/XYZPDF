// Measures one document: how long its pages take to render, how many pixels
// that is, and how many of them fit in the render cache's budget.
//
//     xyzpdf_pdfprobe <file.pdf> [page count to sample] [viewport width]
//
// Not a test -- it prints numbers rather than passing or failing, which is why
// tools/build_all_tests.py skips anything with "probe" in its name. It exists
// because "the program is slow with my document" is not a question that can be
// answered from a description of the document: whether the time goes on
// rendering or on memory depends on whether the pages are text or scans, how
// big they are, and what the screen is, and the numbers have to come off the
// machine where it happens.
//
// Run it with the built program beside it: the Qt runtime is deployed next to
// xyzpdf.exe, and this needs the same libraries.

// Relative, like every other include from outside the project root: the root
// itself is not on the include path.
#include "../src/document/pagemetrics.h"
#include "../src/document/qtpdfbackend.h"
#include "../src/render/rendercache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

QString megabytes(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2) {
        std::cerr << "usage: pdfprobe <file.pdf> [samples] [viewport width]\n";
        return 2;
    }

    const QString path = arguments.at(1);
    const int samples = arguments.size() > 2 ? arguments.at(2).toInt() : 12;
    const int viewportWidth = arguments.size() > 3 ? arguments.at(3).toInt() : 2500;

    QtPdfBackend document;
    const Document::Status status = document.load(path, QString());
    if (status != Document::Status::Ready) {
        std::cerr << "could not open " << path.toStdString() << ": "
                  << document.lastError().toStdString() << '\n';
        return 1;
    }

    const int pages = document.pageCount();
    std::cout << "file:   " << path.toStdString() << '\n';
    std::cout << "pages:  " << pages << '\n';
    if (pages == 0)
        return 0;
    const QSizeF first = document.pageSize(0);
    std::cout << "page 1: " << first.width() << " x " << first.height() << " pt\n";

    // The zoom the view opens at: fit width for a maximised window.
    const double fitWidth = (viewportWidth - 2 * 12.0) / first.width();
    std::cout << "\nfit width for a " << viewportWidth << " px viewport: "
              << QStringLiteral("%1%").arg(fitWidth * 100, 0, 'f', 1).toStdString() << '\n';

    // Sample across the document rather than the first pages: a scan at the
    // end and a text page at the front have nothing in common.
    std::vector<int> chosen;
    for (int index = 0; index < samples; ++index) {
        const int page = pages == 1 ? 0 : int(std::llround(double(index) * (pages - 1)
                                                          / (samples - 1)));
        if (chosen.empty() || chosen.back() != page)
            chosen.push_back(page);
    }

    std::cout << "\npage      size (px)          bytes      render\n";
    std::vector<double> milliseconds;
    qint64 firstPageBytes = 0;
    for (const int page : chosen) {
        const QSize pixels = PageMetrics::pixelsFor(document.pageSize(page), fitWidth, 1.0);
        QElapsedTimer timer;
        timer.start();
        const QImage image = document.render(page, pixels, RenderOptions{});
        const double elapsed = timer.elapsed();
        milliseconds.push_back(elapsed);
        if (page == 0)
            firstPageBytes = image.isNull() ? 0 : qint64(image.sizeInBytes());
        std::cout << QStringLiteral("%1%2%3%4")
                         .arg(page + 1, 4)
                         .arg(QStringLiteral("%1x%2").arg(pixels.width()).arg(pixels.height()), 16)
                         .arg(image.isNull() ? QStringLiteral("  (none)") : megabytes(image.sizeInBytes()), 10)
                         .arg(QStringLiteral("%1 ms").arg(elapsed, 0, 'f', 0), 12)
                         .toStdString()
                  << '\n';
    }

    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "\nrender time: median " << milliseconds.at(milliseconds.size() / 2) << " ms"
              << ", worst " << milliseconds.back() << " ms\n";
    std::cout << "one page at this zoom: " << megabytes(firstPageBytes).toStdString() << '\n';
    if (firstPageBytes > 0) {
        std::cout << "a " << megabytes(RenderCache::DefaultMaximumBytes).toStdString()
                  << " cache holds " << RenderCache::DefaultMaximumBytes / firstPageBytes
                  << " pages of that size\n";
    }

    // And what a band of a zoomed-in page costs, which is what the view does
    // above a few hundred percent.
    {
        const QSize whole = PageMetrics::pixelsFor(document.pageSize(0), 4.0, 1.0);
        const QRect band(0, 0, std::min(whole.width(), 2500), std::min(whole.height(), 1400));
        RenderOptions options;
        options.clip = band;
        options.scaledSize = whole;
        QElapsedTimer timer;
        timer.start();
        const QImage image = document.render(0, band.size(), options);
        std::cout << "\nat 400%: whole page would be "
                  << megabytes(qint64(whole.width()) * whole.height() * 4).toStdString()
                  << "; the band on screen is " << megabytes(image.sizeInBytes()).toStdString()
                  << " and took " << timer.elapsed() << " ms\n";
    }

    return 0;
}
