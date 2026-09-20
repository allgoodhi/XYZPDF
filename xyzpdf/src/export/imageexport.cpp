#include "imageexport.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QDir>
#include <QImage>
#include <QSize>

#include <cmath>

namespace {

// A page at a high dpi is a big image; a poster page at 600 dpi would be a
// hundred megabytes. Cap the source pixels and let the dpi fall where it may for
// such pages -- ordinary pages at ordinary dpi never reach it.
constexpr qint64 kMaxImagePixels = 25LL * 1024 * 1024;

const char *formatName(ImageExport::Format format)
{
    return format == ImageExport::Format::Jpeg ? "JPEG" : "PNG";
}

} // namespace

namespace ImageExport {

QString fileNameFor(const QString &baseName, int page, Format format)
{
    // Zero-padded to three digits so a folder sorts them as page 1, 2, ... 10
    // rather than 1, 10, 100, 2. The page arrives 1-based, as a human counts them.
    const QString extension = format == Format::Jpeg ? QStringLiteral("jpg")
                                                     : QStringLiteral("png");
    return QStringLiteral("%1 %2.%3")
        .arg(baseName, QString().asprintf("%03d", qMax(1, page)), extension);
}

Report run(Document &document, const QVector<int> &pages, const QString &directory,
           const QString &baseName, Format format, int dpi,
           const std::function<void(int, int)> &progress, const QAtomicInt *cancel)
{
    Report report;
    report.directory = directory;
    if (pages.isEmpty()) {
        report.error = QObject::tr("没有要导出的页面。");
        return report;
    }
    if (directory.isEmpty() || baseName.trimmed().isEmpty()) {
        report.error = QObject::tr("没有指定导出的文件夹或文件名。");
        return report;
    }

    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral("."))) {
        report.error = QObject::tr("无法创建导出文件夹：%1").arg(directory);
        return report;
    }

    const int total = pages.size();
    for (int index = 0; index < total; ++index) {
        if (cancel && cancel->loadRelaxed()) {
            report.cancelled = true;
            report.error = QObject::tr("导出已取消。");
            return report;
        }

        const int page = pages.at(index);
        const QSizeF sizePoints = document.pageSize(page);
        if (sizePoints.isEmpty()) {
            report.error = QObject::tr("第 %1 页无法读取尺寸，导出中止。").arg(page + 1);
            return report;
        }

        QSize pixels(int(std::lround(sizePoints.width() / 72.0 * dpi)),
                     int(std::lround(sizePoints.height() / 72.0 * dpi)));
        if (qint64(pixels.width()) * qint64(pixels.height()) > kMaxImagePixels) {
            const double scale =
                std::sqrt(double(kMaxImagePixels) / double(qint64(pixels.width()) * pixels.height()));
            pixels = QSize(qMax(1, int(pixels.width() * scale)), qMax(1, int(pixels.height() * scale)));
        }

        RenderOptions options;
        options.annotations = true;
        options.optimizedForLcd = false; // a saved picture, not a screen preview
        const QImage image = document.render(page, pixels, options);
        if (image.isNull()) {
            report.error = QObject::tr("第 %1 页渲染失败，导出中止。").arg(page + 1);
            return report;
        }

        const QString path = dir.filePath(fileNameFor(baseName, page + 1, format));
        if (!image.save(path, formatName(format), format == Format::Jpeg ? 95 : -1)) {
            report.error = QObject::tr("无法写入 %1。").arg(path);
            return report;
        }
        ++report.pages;
        if (progress)
            progress(report.pages, total);
    }

    report.ok = true;
    return report;
}

} // namespace ImageExport
