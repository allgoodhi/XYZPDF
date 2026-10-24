#include "wordexport.h"

#include "docxwriter.h"
#include "pageanalysis.h"

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

// A picture of a page is a copy of the page, so it has to be worth reading: 200 dpi
// keeps an A4 page's small print and fine lines, and a poster-sized page is brought
// down to this many pixels rather than becoming a hundred megabytes.
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

// A region of a page drawn on white: what a drawing made of paths turns into.
QImage renderRegion(Document &document, int page, const QRectF &region, double dpi)
{
    const QSizeF size = document.pageSize(page);
    if (size.isEmpty() || region.isEmpty())
        return {};
    double scale = dpi / 72.0;
    // A drawing that would be enormous at the asked resolution comes down to a size
    // a document can carry.
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
    return QCoreApplication::translate("WordExport", text);
}

} // namespace

WordExportReport exportToWord(Document &document, const QString &path, const WordExportOptions &options)
{
    WordExportReport report;
    report.path = path;
    if (path.isEmpty()) {
        report.error = translated(QT_TRANSLATE_NOOP("WordExport", "没有指定要写入的文件。"));
        return report;
    }

    const QSharedPointer<QAtomicInt> &cancel = options.cancel;
    const auto abandoned = [&cancel] { return cancel && cancel->loadRelaxed(); };
    const auto stop = [&report] {
        report.cancelled = true;
        report.error = translated(QT_TRANSLATE_NOOP("WordExport", "导出已取消。"));
        return report;
    };

    const int pages = document.pageCount();
    if (options.progress && pages > 0)
        options.progress(0, pages);

    // ---- pass one: the text of every page ----
    // What repeats at the top and bottom of the pages is a running header or footer
    // only when it repeats, which no single page can say; and a heading is a
    // heading because it is larger than the document's body text, not the page's.
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
    QString bodyFamily;
    best = -1;
    for (auto it = byFamily.constBegin(); it != byFamily.constEnd(); ++it) {
        if (it.value() > best) {
            best = it.value();
            bodyFamily = it.key();
        }
    }

    DocxWriter writer;
    writer.setDefaultFont(bodyFamily, bodySize);

    // ---- pass two: every page, written ----
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

        if (laid.pictureOnly) {
            // No words to keep: the page goes in as the picture it is, at the size
            // of the page, so the document still has the page where it was.
            const QImage image = renderRegion(document, page, QRectF(QPointF(0, 0), laid.size), kPageDpi);
            if (image.isNull()) {
                ++report.emptyPages;
                if (options.progress)
                    options.progress(page + 1, pages);
                continue;
            }
            LayoutAnchor anchor;
            anchor.behindText = true;
            anchor.image.box = QRectF(QPointF(0, 0), laid.size);
            anchor.image.bytes = jpegOrPng(image, &anchor.image.extension);
            laid.anchors.append(anchor);
            ++report.imagePages;
        }

        const int tablesBefore = writer.tableCount();
        const int imagesBefore = writer.imageCount();
        const int paragraphsBefore = writer.paragraphCount();
        writer.addPage(laid);
        ++report.pages;
        report.tables += writer.tableCount() - tablesBefore;
        report.paragraphs += writer.paragraphCount() - paragraphsBefore;
        if (!laid.pictureOnly) {
            for (const LayoutBlock &block : std::as_const(laid.blocks))
                report.figures += block.kind == LayoutBlock::Kind::Image ? 1 : 0;
            for (const LayoutAnchor &anchor : std::as_const(laid.anchors))
                report.figures += anchor.behindText ? 0 : 1;
        }
        Q_UNUSED(imagesBefore)

        if (options.progress)
            options.progress(page + 1, pages);
    }

    if (report.pages == 0) {
        report.error = translated(
            QT_TRANSLATE_NOOP("WordExport", "这份 PDF 没有可导出的内容：既没有文字层，也没有渲染得出来的页面。"));
        return report;
    }
    if (abandoned())
        return stop();

    QString error;
    if (!writer.save(path, &error)) {
        report.error = translated(QT_TRANSLATE_NOOP("WordExport", "写入文件失败：%1")).arg(error);
        return report;
    }
    report.ok = true;
    return report;
}
