#include "pptexport.h"

#include "pageanalysis.h"
#include "pptxwriter.h"

#include "../document/document.h"
#include "../document/pagecontent.h"
#include "../document/pagemetrics.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QPainter>

#include <cmath>

namespace {

// A page that is a picture of itself goes on the slide as a picture. 200 dpi is the
// same number the Word export uses: a slide is often shown larger than a sheet of
// paper, so it has to be worth zooming into.
constexpr double kPageDpi = 200.0;
constexpr qint64 kMaximumPagePixels = 8LL * 1024 * 1024;

QImage onPaper(const QImage &image)
{
    QImage paper(image.size(), QImage::Format_RGB32);
    paper.fill(Qt::white);
    QPainter painter(&paper);
    painter.drawImage(0, 0, image);
    return paper;
}

QByteArray jpegOrPng(const QImage &image, QString *extension)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (image.save(&buffer, "JPEG", 88)) {
        *extension = QStringLiteral("jpeg");
        return bytes;
    }
    bytes.clear();
    QBuffer second(&bytes);
    second.open(QIODevice::WriteOnly);
    if (!image.save(&second, "PNG"))
        return {};
    *extension = QStringLiteral("png");
    return bytes;
}

QImage renderRegion(Document &document, int page, const QRectF &region, double dpi)
{
    const QSizeF size = document.pageSize(page);
    if (size.isEmpty() || region.isEmpty())
        return {};
    double scale = dpi / 72.0;
    const double pixels = region.width() * scale * region.height() * scale;
    if (pixels > double(kMaximumPagePixels))
        scale *= std::sqrt(double(kMaximumPagePixels) / pixels);
    const QSize whole = PageMetrics::pixelsFor(size, scale, 1.0);
    const QRect clip = QRectF(region.left() * scale, region.top() * scale, region.width() * scale,
                              region.height() * scale)
                           .toAlignedRect()
                           .intersected(QRect(QPoint(0, 0), whole));
    if (clip.isEmpty())
        return {};
    RenderOptions options;
    options.annotations = true;
    options.printing = true;
    options.clip = clip;
    options.scaledSize = whole;
    const QImage drawn = document.render(page, clip.size(), options);
    return drawn.isNull() ? QImage() : onPaper(drawn);
}

QString translated(const char *text)
{
    return QCoreApplication::translate("PptExport", text);
}

} // namespace

QSizeF powerPointSlideSize(Document &document)
{
    if (document.pageCount() <= 0)
        return {};
    return document.pageSize(0);
}

PptExportReport exportToPowerPoint(Document &document, const QString &path, const PptExportOptions &options)
{
    PptExportReport report;
    report.path = path;
    if (path.isEmpty()) {
        report.error = translated(QT_TRANSLATE_NOOP("PptExport", "没有指定要写入的文件。"));
        return report;
    }

    const QSharedPointer<QAtomicInt> &cancel = options.cancel;
    const auto abandoned = [&cancel] { return cancel && cancel->loadRelaxed(); };
    const auto stop = [&report] {
        report.cancelled = true;
        report.error = translated(QT_TRANSLATE_NOOP("PptExport", "导出已取消。"));
        return report;
    };

    const int pages = document.pageCount();
    if (pages <= 0) {
        report.error = translated(QT_TRANSLATE_NOOP(
            "PptExport", "这份 PDF 没有可导出的内容：既没有文字层，也没有渲染得出来的页面。"));
        return report;
    }
    if (options.progress)
        options.progress(0, pages);

    // ---- pass one: the text of every page ----
    // The running header/footer and the body size are document-wide findings, the
    // same ones the Word export makes; a single page cannot tell that a line repeats.
    QVector<QVector<BandRow>> bands(pages);
    QVector<QSizeF> sizes(pages);
    QHash<int, qint64> bySize;
    QHash<QString, qint64> byFamily;
    for (int page = 0; page < pages; ++page) {
        if (abandoned())
            return stop();
        const PageContent text = pageContentOf(document, page, false);
        sizes[page] = text.ok ? text.size : document.pageSize(page);
        if (!text.ok)
            continue;
        bands[page] = bandRows(text);
        for (const ContentChar &ch : text.chars) {
            if (ch.code <= 0x20)
                continue;
            ++bySize[int(std::lround(ch.size * 2))];
            ++byFamily[wordFontFamily(ch.font, ch.family)];
        }
    }
    const RunningRows running = headerFooterRows(bands, sizes);
    double bodySize = 11;
    qint64 best = -1;
    for (auto it = bySize.constBegin(); it != bySize.constEnd(); ++it) {
        if (it.value() > best) {
            best = it.value();
            bodySize = it.key() / 2.0;
        }
    }

    // ---- pass two: every page, written ----
    PptxWriter writer;
    for (int page = 0; page < pages; ++page) {
        if (abandoned())
            return stop();
        const PageContent content = pageContentOf(document, page, true);
        if (abandoned())
            return stop();

        LayoutPage laid;
        if (content.ok) {
            AnalysisOptions analysis;
            analysis.renderRegion = [&document, page](const QRectF &region, double dpi) {
                return renderRegion(document, page, region, dpi);
            };
            analysis.headerRows = running.header.value(page);
            analysis.footerRows = running.footer.value(page);
            analysis.pageNumber = page + 1;
            analysis.bodySize = bodySize;
            laid = analysePage(content, analysis);
        } else {
            laid.size = document.pageSize(page);
            laid.pictureOnly = true;
        }

        bool blank = false;
        if (laid.pictureOnly) {
            const QImage image = renderRegion(document, page, QRectF(QPointF(0, 0), laid.size), kPageDpi);
            if (image.isNull()) {
                // A slide with nothing on it still keeps the page count; say so.
                blank = true;
            } else {
                LayoutAnchor anchor;
                anchor.behindText = true;
                anchor.image.box = QRectF(QPointF(0, 0), laid.size);
                anchor.image.bytes = jpegOrPng(image, &anchor.image.extension);
                laid.anchors.append(anchor);
                ++report.picturePages;
            }
        }

        const int tablesBefore = writer.tableCount();
        const int cellsBefore = writer.tableCellCount();
        const int boxesBefore = writer.textBoxCount();
        writer.addSlide(laid);
        ++report.pages;
        report.tables += writer.tableCount() - tablesBefore;
        report.tableCells += writer.tableCellCount() - cellsBefore;
        report.textBoxes += writer.textBoxCount() - boxesBefore;
        if (blank)
            ++report.emptyPages;
        if (!laid.pictureOnly) {
            for (const LayoutBlock &block : std::as_const(laid.blocks))
                report.figures += block.kind == LayoutBlock::Kind::Image ? 1 : 0;
            for (const LayoutAnchor &anchor : std::as_const(laid.anchors))
                report.figures += anchor.behindText ? 0 : 1;
        }

        if (options.progress)
            options.progress(page + 1, pages);
    }

    report.scaledPages = writer.scaledSlideCount();
    report.slideSize = writer.slideSize();

    if (abandoned())
        return stop();

    QString error;
    if (!writer.save(path, &error)) {
        report.error = translated(QT_TRANSLATE_NOOP("PptExport", "写入文件失败：%1")).arg(error);
        return report;
    }
    report.ok = true;
    return report;
}
