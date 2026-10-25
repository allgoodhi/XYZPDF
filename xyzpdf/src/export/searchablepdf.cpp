#include "searchablepdf.h"

#include "../document/document.h"
#include "../document/pagecontent.h"
#include "../document/pagemetrics.h"
#include "../edit/textlayerwriter.h"
#include "../ocr/ocrengine.h"

#include <QCoreApplication>
#include <QImage>
#include <QPainter>

#include <cmath>

namespace {

// A scanned page is recognized at print resolution: a fax or a photocopy's small print
// is what the reader wants to search, and 300 dpi is what an OCR engine is tuned for.
constexpr double kRecognitionDpi = 300.0;

// A poster-sized page at 300 dpi would be hundreds of megabytes of pixels for an
// answer that is a few hundred words; the bigger the page, the further this brings the
// resolution down. (The OCR service has a limit of its own -- and a smaller one -- which
// Ocr::recognizeLayout handles by scaling what it was given.)
constexpr double kMaximumRecognitionPixels = 20.0 * 1024.0 * 1024.0;

// What a line box's height is as a fraction of the font size. The recognizer reports
// the ink: for a line with ascenders and a descender that is about nine tenths of the
// em; a line of capitals alone is less, and its text is placed a little above the
// baseline. The width is what the reader sees a selection drag along, and that is
// measured exactly (the writer stretches the word to the box), so this estimate only
// decides how tall the invisible glyphs are.
constexpr double kInkHeightOfEm = 0.9;

// The Word export's scan rule (pageanalysis.cpp): a page is a picture of itself when it
// has no visible characters, or almost none while pictures cover most of it.
constexpr int kFewVisibleChars = 40;
constexpr double kPictureCoverage = 0.6;

QString translated(const char *text)
{
    return QCoreApplication::translate("SearchablePdf", text);
}

// The pixels the reader sees, on paper. The renderer hands back a page over
// transparency -- a PDF page has no background of its own -- and the recognizer reads a
// transparent pixel as black (see ocrengine.h), which turns a scan of black ink on white
// paper into a black rectangle with black text on it.
QImage onPaper(const QImage &image)
{
    QImage paper(image.size(), QImage::Format_RGB32);
    paper.fill(Qt::white);
    QPainter painter(&paper);
    painter.drawImage(0, 0, image);
    return paper;
}

int visibleCharacters(const PageContent &content)
{
    int visible = 0;
    for (const ContentChar &ch : content.chars) {
        if (ch.code > 0x20)
            ++visible;
    }
    return visible;
}

double pictureCoverage(const PageContent &content)
{
    const QRectF whole(QPointF(0, 0), content.size);
    double pictured = 0;
    for (const ContentImage &image : content.images) {
        const QRectF on = image.box.intersected(whole);
        pictured += on.width() * on.height();
    }
    return content.size.width() * content.size.height() > 0
        ? pictured / (content.size.width() * content.size.height())
        : 0;
}

} // namespace

SearchablePdfReport makeSearchablePdf(Document &document, const QString &source,
                                      const QString &password, const QString &destination,
                                      const SearchablePdfOptions &options)
{
    SearchablePdfReport report;
    report.path = destination;

    const QSharedPointer<QAtomicInt> &cancel = options.cancel;
    const auto abandoned = [&cancel] { return cancel && cancel->loadRelaxed(); };
    const auto stop = [&report] {
        report.cancelled = true;
        report.error = translated(QT_TRANSLATE_NOOP(
            "SearchablePdf", "已取消：原来的文件没有改动，也没有写出新文件。"));
        return report;
    };

    const int pageCount = document.pageCount();
    if (pageCount <= 0) {
        report.error = translated(QT_TRANSLATE_NOOP("SearchablePdf", "这个文件没有页面。"));
        return report;
    }
    if (options.progress)
        options.progress(0, pageCount);

    bool askedAboutEngine = false;
    QVector<TextLayerPage> described;

    for (int page = 0; page < pageCount; ++page) {
        if (abandoned())
            return stop();

        // Characters only: a page of photographs costs its decoding once, in the pass
        // that renders it, and this pass only has to answer "is this a scan".
        const PageContent text = pageContentOf(document, page, false);
        if (!text.ok) {
            // The page cannot say what is on it. Leaving it alone is the only answer
            // that cannot make a page worse.
            ++report.skipped;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }

        // An invisible text layer is a text layer: somebody -- an OCR program, or an
        // earlier run of this one -- has already put the words here.
        if (text.invisibleChars > 0) {
            ++report.skipped;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }

        const int visible = visibleCharacters(text);
        if (visible > kFewVisibleChars) {
            ++report.skipped;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }
        if (visible > 0) {
            // A running header typed over a scan still makes the page a scan, and the
            // Word export decides that by how much of the page is pictures. The same
            // reading, so the two exports agree on what a scanned page is.
            const PageContent graphics = pageContentOf(document, page, true);
            if (!graphics.ok || pictureCoverage(graphics) < kPictureCoverage) {
                ++report.skipped;
                if (options.progress)
                    options.progress(page + 1, pageCount);
                continue;
            }
        }

        if (!askedAboutEngine) {
            askedAboutEngine = true;
            if (!Ocr::isAvailable()) {
                report.error = translated(QT_TRANSLATE_NOOP(
                    "SearchablePdf",
                    "这台电脑没有装文字识别的语言包，扫描页没有识别。"
                    "可以在 Windows 设置里添加语言之后再试。"));
                return report;
            }
        }

        const QSizeF size = text.size.isEmpty() ? document.pageSize(page) : text.size;
        if (size.isEmpty()) {
            ++report.empty;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }

        double scale = kRecognitionDpi / 72.0;
        const double pixels = size.width() * scale * size.height() * scale;
        if (pixels > kMaximumRecognitionPixels)
            scale *= std::sqrt(kMaximumRecognitionPixels / pixels);
        const QSize wanted = PageMetrics::pixelsFor(size, scale, 1.0);
        if (wanted.isEmpty()) {
            ++report.empty;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }

        // What the reader sees: the page's own annotations included, because a note
        // whose text is on the page is text a search should find.
        const QImage paper = onPaper(document.render(page, wanted, RenderOptions()));
        if (abandoned())
            return stop();
        if (paper.isNull()) {
            ++report.empty;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }

        ++report.recognized;
        const Ocr::LayoutResult reading = Ocr::recognizeLayout(paper);
        if (!reading.ok) {
            // Not "found nothing": the service itself failed, and a file written past
            // this would be a file whose pages are silently half read.
            report.error = reading.error;
            return report;
        }

        // Recognition's boxes are in the pixels it was handed; the page's own
        // coordinates are points, and the two axes are the same scale up to rounding.
        const double xScale = size.width() / double(paper.width());
        const double yScale = size.height() / double(paper.height());

        TextLayerPage layer;
        layer.page = page;
        for (const Ocr::LayoutLine &line : reading.lines) {
            if (line.words.isEmpty() || line.box.height() <= 0)
                continue;
            const double wordSize = line.box.height() * yScale / kInkHeightOfEm;
            if (!(wordSize > 0))
                continue;
            for (const Ocr::LayoutWord &word : line.words) {
                const QString text = word.text;
                if (text.trimmed().isEmpty() || word.box.isEmpty())
                    continue;
                TextLayerWord described;
                described.text = text;
                described.box = QRectF(word.box.left() * xScale, word.box.top() * yScale,
                                       word.box.width() * xScale, word.box.height() * yScale);
                described.size = wordSize;
                layer.words.append(described);
            }
        }

        if (layer.words.isEmpty()) {
            ++report.empty;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }

        // Where the page's own coordinate system is. Asked of the page itself, because
        // a /Rotate or a crop box changes which way is right and where the origin is --
        // and a layer placed with a hand-rolled guess is text in the wrong place on
        // exactly the scans that came out of a scanner turned sideways.
        QPointF origin;
        QPointF right;
        QPointF down;
        if (!userSpacePoint(document, page, QPointF(0, 0), &origin)
            || !userSpacePoint(document, page, QPointF(1, 0), &right)
            || !userSpacePoint(document, page, QPointF(0, 1), &down)) {
            ++report.empty;
            if (options.progress)
                options.progress(page + 1, pageCount);
            continue;
        }
        layer.displayedToUser = QTransform(right.x() - origin.x(), right.y() - origin.y(),
                                           down.x() - origin.x(), down.y() - origin.y(),
                                           origin.x(), origin.y());
        described.append(layer);

        if (options.progress)
            options.progress(page + 1, pageCount);
    }

    if (abandoned())
        return stop();

    if (described.isEmpty()) {
        report.error
            = report.recognized == 0
            ? translated(QT_TRANSLATE_NOOP(
                  "SearchablePdf", "这份 PDF 的每一页都已经有文字层了，不需要再加。"))
            : translated(QT_TRANSLATE_NOOP(
                  "SearchablePdf", "没有从扫描页里认出可以放进文字层的文字。"));
        return report;
    }

    // The writer reads the file again through qpdf and writes a new one; the waiting
    // happens before that last step, because a cancelled run must leave no file behind.
    const TextLayerWriteReport written
        = writeTextLayer(source, destination, password, described);
    if (!written.ok) {
        report.ok = false;
        report.error = written.error;
        return report;
    }

    report.ok = true;
    report.words = written.words;
    report.writtenPages = written.pages;
    return report;
}
