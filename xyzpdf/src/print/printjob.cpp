#include "printjob.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QImage>
#include <QPainter>
#include <QPrinter>

#include <algorithm>
#include <cmath>

namespace {

// The most pixels one printed page is rendered into. A 300 dpi A4 is about 3.5
// million; a 1200 dpi poster page would be tens of millions and a lot of memory
// for a difference nobody sees on paper, so the source image is capped and the
// painter scales it to the cell.
constexpr qint64 kMaxPrintPixels = 16LL * 1024 * 1024;

// A point is 1/72 of an inch; the printer counts in its own dots per inch.
double pointsToPixels(double points, int dpi)
{
    return points / 72.0 * double(dpi);
}

} // namespace

namespace Print {

QVector<int> parsePageRange(const QString &spec, int pageCount)
{
    QVector<int> pages;
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty()) {
        pages.reserve(pageCount);
        for (int page = 0; page < pageCount; ++page)
            pages.append(page);
        return pages;
    }

    for (const QString &piece : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString token = piece.trimmed();
        if (token.isEmpty())
            continue;
        const int dash = token.indexOf(QLatin1Char('-'));
        bool okFrom = false;
        bool okTo = false;
        int from;
        int to;
        if (dash < 0) {
            from = token.toInt(&okFrom);
            to = from;
            okTo = okFrom;
        } else {
            from = token.left(dash).trimmed().toInt(&okFrom);
            to = token.mid(dash + 1).trimmed().toInt(&okTo);
        }
        if (!okFrom || !okTo)
            continue; // a token that names no number is not a page
        if (to < from)
            std::swap(from, to); // "5-3" is the same span as "3-5"
        for (int page = from; page <= to; ++page) {
            if (page >= 1 && page <= pageCount)
                pages.append(page - 1); // 1-based on paper, 0-based here
        }
    }

    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

QPair<int, int> nUpGrid(int nUp)
{
    switch (nUp) {
    case 2:
        return {2, 1};
    case 4:
        return {2, 2};
    case 6:
        return {3, 2};
    case 9:
        return {3, 3};
    case 16:
        return {4, 4};
    case 1:
    default:
        return {1, 1};
    }
}

Report run(Document &document, QPrinter &printer, const Settings &settings,
           const std::function<void(int, int)> &progress, const QAtomicInt *cancel)
{
    Report report;
    const int pageCount = document.pageCount();
    if (pageCount <= 0) {
        report.error = QObject::tr("这份文档没有可打印的页面。");
        return report;
    }

    QVector<int> pages;
    switch (settings.range) {
    case Settings::Range::All:
        pages = parsePageRange(QString(), pageCount);
        break;
    case Settings::Range::Current:
        pages.append(std::clamp(settings.currentPage, 0, pageCount - 1));
        break;
    case Settings::Range::Custom:
        pages = parsePageRange(settings.custom, pageCount);
        break;
    }
    if (pages.isEmpty()) {
        report.error = QObject::tr("页码范围里没有可打印的页面。");
        return report;
    }

    const int dpi = printer.resolution();
    const QRect area = printer.pageLayout().paintRectPixels(dpi);
    if (area.width() <= 0 || area.height() <= 0) {
        report.error = QObject::tr("打印机的可打印区域无效。");
        return report;
    }

    const auto [columns, rows] = nUpGrid(settings.nUp);
    const int perSheet = columns * rows;
    const int cellWidth = area.width() / columns;
    const int cellHeight = area.height() / rows;
    const int sheetCount = (pages.size() + perSheet - 1) / perSheet;

    QPainter painter(&printer);
    if (!painter.isActive()) {
        report.error = QObject::tr("无法开始打印任务。");
        return report;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    for (int sheet = 0; sheet < sheetCount; ++sheet) {
        if (cancel && cancel->loadRelaxed()) {
            report.cancelled = true;
            report.error = QObject::tr("打印已取消。");
            return report;
        }
        if (sheet > 0)
            printer.newPage();

        for (int slot = 0; slot < perSheet; ++slot) {
            const int ordinal = sheet * perSheet + slot;
            if (ordinal >= pages.size())
                break; // the last sheet is often only partly full
            const int page = pages.at(ordinal);

            const QRect cell(area.left() + (slot % columns) * cellWidth,
                             area.top() + (slot / columns) * cellHeight, cellWidth, cellHeight);

            const QSizeF sizePoints = document.pageSize(page);
            if (sizePoints.isEmpty())
                continue;
            const double naturalWidth = pointsToPixels(sizePoints.width(), dpi);
            const double naturalHeight = pointsToPixels(sizePoints.height(), dpi);

            // Where the page goes on the sheet, and how big it is rendered.
            double drawWidth;
            double drawHeight;
            if (settings.scale == Settings::Scale::Fit) {
                const double fit = std::min(cell.width() / naturalWidth,
                                             cell.height() / naturalHeight);
                drawWidth = naturalWidth * fit;
                drawHeight = naturalHeight * fit;
            } else {
                drawWidth = naturalWidth;
                drawHeight = naturalHeight;
            }
            const QRectF target(cell.left() + (cell.width() - drawWidth) / 2.0,
                                cell.top() + (cell.height() - drawHeight) / 2.0, drawWidth,
                                drawHeight);

            // Render at the size it will be drawn, capped so a large sheet at a
            // high resolution does not ask for a hundred-megabyte image; the
            // painter scales the capped result up to `target`.
            qint64 pixels = qint64(std::ceil(drawWidth)) * qint64(std::ceil(drawHeight));
            double sourceScale = 1.0;
            if (pixels > kMaxPrintPixels)
                sourceScale = std::sqrt(double(kMaxPrintPixels) / double(pixels));
            const QSize imageSize(qMax(1, int(std::ceil(drawWidth * sourceScale))),
                                  qMax(1, int(std::ceil(drawHeight * sourceScale))));

            RenderOptions options;
            options.annotations = true;
            options.optimizedForLcd = false; // paper, not a screen
            const QImage image = document.render(page, imageSize, options);
            if (image.isNull())
                continue; // a page that will not render is skipped, not fatal
            painter.drawImage(target, image);
            ++report.pages;
        }
        ++report.sheets;
        if (progress)
            progress(report.sheets, sheetCount);
    }

    painter.end();
    report.ok = true;
    return report;
}

} // namespace Print
