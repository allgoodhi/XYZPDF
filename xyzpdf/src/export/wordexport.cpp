#include "wordexport.h"

#include "docxwriter.h"

// Relative, like every include from inside src/: quoted includes resolve against
// this file's own directory first, and the project root is not on the include path.
#include "../document/document.h"
#include "../document/pagemetrics.h"

#include <QAtomicInt>
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QSharedPointer>

#include <algorithm>

namespace {

// How much further down than the page's own line spacing the next line has to
// start before it is a new paragraph rather than the next line of this one.
//
// Against the *spacing*, not the height of a line, and that distinction is the
// whole of a bug: a report whose lines are ten points tall with thirty-one
// between them -- a slide, a tightened layout, anything set with room to breathe
// -- had every one of its lines judged a paragraph of its own, because a gap of
// twenty-one points is a gap of two line heights. Forty-one lines came out as
// forty-one paragraphs.
constexpr double kParagraphGap = 1.6;

// How far short of the right margin a line can end and still be one the next
// line continues. A paragraph's last line is short -- that is what makes it the
// last one -- and a wrapped line reaches the margin.
constexpr double kShortLine = 2.5;

// And the width, as a fraction of the column, under which a line has certainly
// finished: no line of a justified paragraph is this short, because a full line
// fills the column. This is what separates a list of short items -- a list of
// parts, a set of file names, one to a line -- from a paragraph that happens to
// wrap, when the two of them are spaced exactly the same way and neither ends in
// punctuation. The line has to be *much* shorter: half the column, so that the
// raggedness of an ordinary right margin never reaches it.
constexpr double kVeryShortLine = 0.5;

// How much further in a line can start than its paragraph did before it is a new
// paragraph. A first-line indent is one or two characters, and a character is
// about a line's height wide in Chinese and half of one in Latin, so this sits
// between the two: wide enough not to fire on the raggedness of justified text,
// narrow enough to fire on an indent.
constexpr double kIndent = 1.5;

// And how far *left* of its paragraph a line can start before it is a new block
// rather than a continuation. This is what a centred heading over body text looks
// like, and it has to be well clear of kIndent: the first line of an indented
// paragraph starts further *in* than the lines under it, and a rule that broke on
// that would break up every paragraph in every Chinese document.
constexpr double kJumpLeft = 3.0;

// Whether a character is one of the scripts that do not put a space between
// words. Approximate on purpose: it decides whether joining two lines inserts a
// space, and getting it wrong by a character puts a space in the wrong place in
// one line of one paragraph.
bool isCjk(QChar character)
{
    const ushort code = character.unicode();
    return (code >= 0x2E80 && code <= 0x9FFF)    // radicals through unified ideographs
        || (code >= 0x3000 && code <= 0x303F)    // CJK punctuation
        || (code >= 0x3400 && code <= 0x4DBF)    // extension A
        || (code >= 0xF900 && code <= 0xFAFF)    // compatibility ideographs
        || (code >= 0xFF00 && code <= 0xFFEF);   // full-width forms
}

// A line joined onto the paragraph so far: with a space, unless the two sides
// are CJK -- where a line break has no space in it and one that was added shows
// up in the middle of a word.
QString joinLines(const QString &paragraph, const QString &next)
{
    if (paragraph.isEmpty())
        return next;
    if (next.isEmpty())
        return paragraph;
    if (isCjk(paragraph.at(paragraph.length() - 1)) || isCjk(next.at(0)))
        return paragraph + next;
    return paragraph + QLatin1Char(' ') + next;
}

QString translated(const char *text)
{
    return QCoreApplication::translate("WordExport", text);
}

// Whether two boxes are two pieces of one line rather than two lines.
//
// The runs of a page are not its lines: a justified line, a line with a
// superscript in it, a line a PDF placed piece by piece comes back as several
// boxes side by side. Their middles have to be at about the same height -- not
// merely overlapping, because the descenders of one line reach down into the
// next -- and the second has to begin about where the first ends, because a piece
// of text on the far side of the page is a column and not the same line.
bool sameLine(const QRectF &previous, const QRectF &next)
{
    const double body = qMax(previous.height(), next.height());
    if (body <= 0)
        return false;
    if (std::fabs(previous.center().y() - next.center().y()) > body * 0.6)
        return false;
    const double gap = next.left() - previous.right();
    return gap <= body * 3.0 && gap >= -body * 0.5;
}

// Whether a line ends a sentence, and so -- with the line being short -- ends the
// paragraph it is in.
//
// This is the signal that survives a page whose lines are evenly spaced, where
// nothing about the geometry says where one item stops and the next starts: a
// line that ends in a full stop and does not reach the margin has finished what
// it was saying. Closing quotes and brackets are looked past, because a sentence
// in Chinese usually ends with the punctuation inside them.
bool endsSentence(const QString &text)
{
    static const QString enders = QStringLiteral("。．.！!？?；;：:");
    static const QString closers = QStringLiteral("”’」』》）)]】〕〉…");
    for (int index = text.length() - 1; index >= 0; --index) {
        const QChar character = text.at(index);
        if (closers.contains(character))
            continue;
        return enders.contains(character);
    }
    return false;
}

// The page's own line spacing: how far apart consecutive lines usually are.
//
// The median of the steps between them, so that a heading with a space under it
// does not decide what normal is. With two lines there is nothing to compare
// against and this is that one step, which is why the gap rule cannot see a
// paragraph break on a page that has only two.
double linePitch(const QVector<TextLine> &lines)
{
    QVector<double> steps;
    steps.reserve(lines.size());
    for (int index = 1; index < lines.size(); ++index)
        steps.append(lines.at(index).box.top() - lines.at(index - 1).box.top());
    if (steps.isEmpty())
        return 0.0;
    std::sort(steps.begin(), steps.end());
    return steps.at(steps.size() / 2);
}

// How much of a page goes into a picture of it, in dots per inch.
//
// 150 puts a letter page at 1240x1754 pixels: sharp enough to read on screen and
// to print at a readable size, and a few hundred kilobytes a page rather than the
// few megabytes 300 dpi would cost. A page image is not the page's own data --
// that is the scan's, and it is inside the PDF where nothing here can get at it
// except by rendering it -- so this is the resolution of a *copy* of the page,
// and the copy has to be worth emailing.
constexpr double kImageDpi = 150.0;

// The most pixels one page may take. A poster-sized page at 150 dpi is thirty-five
// megapixels, which is a hundred megabytes of image in the working set and a
// picture nobody wants in a document; past this the scale comes down rather than
// the page going in at all.
constexpr qint64 kMaximumImagePixels = 8LL * 1024 * 1024;

// What the JPEG is written at. High enough that the glyphs of ordinary text do
// not smear, low enough that a page stays small.
constexpr int kImageQuality = 85;

// One page as a picture: encoded, with what it is and how big it should be.
//
// Rendered at a resolution of its own rather than at whatever the reader happens
// to be zoomed to -- a scan exported while the window was showing it at 40% would
// be unreadable, and one exported at 800% would be gigantic, and neither is what
// the document says the page is.
QByteArray pageImage(Document &document, int page, QString *extension, QSizeF *points)
{
    const QSizeF size = document.pageSize(page);
    if (size.isEmpty())
        return {};

    QSize pixels = PageMetrics::pixelsFor(size, kImageDpi / 72.0, 1.0);
    if (qint64(pixels.width()) * qint64(pixels.height()) > kMaximumImagePixels) {
        const double scale =
            std::sqrt(double(kMaximumImagePixels) / (double(pixels.width()) * double(pixels.height())));
        pixels = QSize(qMax(1, int(pixels.width() * scale)), qMax(1, int(pixels.height() * scale)));
    }

    RenderOptions options;
    // Annotations drawn in, like the reader shows them: a conversion that dropped
    // the highlights somebody made is a conversion that lost work.
    options.annotations = true;
    const QImage rendered = document.render(page, pixels, options);
    if (rendered.isNull())
        return {};

    // Flattened onto white first. A PDF page has no background of its own -- the
    // white paper is supplied by whoever shows it -- and every pixel it did not
    // paint is transparent, which in a JPEG, which has no transparency, comes out
    // black. A page put in as it comes would be a black page with the text on it.
    QImage flat(rendered.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    {
        QPainter painter(&flat);
        painter.drawImage(0, 0, rendered);
    }

    // JPEG, and PNG only if that fails: a scan is a photograph of paper, and PNG
    // of a photograph is several times the size for no visible difference. The
    // extension has to match what was actually written, because it is the name
    // the image has inside the file.
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (flat.save(&buffer, "JPEG", kImageQuality)) {
        *extension = QStringLiteral("jpeg");
    } else {
        bytes.clear();
        QBuffer second(&bytes);
        second.open(QIODevice::WriteOnly);
        if (!flat.save(&second, "PNG"))
            return {};
        *extension = QStringLiteral("png");
    }

    *points = size;
    return bytes;
}

} // namespace

QVector<TextLine> linesOfPage(const Document &document, int page)
{
    QVector<TextLine> lines;

    // The boxes first, merged into lines: a run is not a line, and taking one
    // for one turns a justified line -- or a line with a superscript in it, or any
    // line a PDF placed piece by piece -- into as many lines as it has pieces.
    QVector<QRectF> boxes;
    for (const TextRun &run : document.textRuns(page)) {
        if (run.box.width() <= 0 || run.box.height() <= 0)
            continue;
        if (!boxes.isEmpty() && sameLine(boxes.last(), run.box))
            boxes.last() = boxes.last().united(run.box);
        else
            boxes.append(run.box);
    }

    lines.reserve(boxes.size());
    for (const QRectF &box : boxes) {
        // The line's first character and one past its last, asked as a range.
        // characterIndexAt answers with the character that starts after a point,
        // so the box's left edge is the line's first character and its right edge
        // is where the next line begins -- which is exactly what pageSelection
        // wants, and exactly the text of this line. This is the same pairing the
        // search highlights and the selection are built from; there is no other
        // one available, and the backend's own comment says why guessing is worse
        // than asking.
        //
        // Asked once for the whole line rather than once per piece: the range
        // covers the gaps between the pieces, so the text comes back with the
        // spaces the page has in it -- which is the spaces a reader gets by
        // selecting that line and copying it.
        const double middle = box.center().y();
        const int from = document.characterIndexAt(page, QPointF(box.left(), middle));
        const int to = document.characterIndexAt(page, QPointF(box.right(), middle));
        if (from < 0 || to <= from)
            continue;

        const QString text = document.pageSelection(page, from, to).text.trimmed();
        if (text.isEmpty())
            continue;
        lines.append({box, text});
    }
    return lines;
}

QVector<QString> paragraphsFromLines(const QVector<TextLine> &lines)
{
    QVector<QString> paragraphs;
    if (lines.isEmpty())
        return paragraphs;

    // The height of an ordinary line, taken as the median rather than the mean so
    // that the heading at the top of a page does not decide what ordinary is.
    QVector<double> heights;
    heights.reserve(lines.size());
    for (const TextLine &line : lines)
        heights.append(line.box.height());
    std::sort(heights.begin(), heights.end());
    const double body = qMax(1.0, heights.at(heights.size() / 2));

    // The right margin, as the furthest right any line on this page reaches: a
    // page has no other way to say where the text stops, and a paragraph that is
    // still going reaches it.
    double rightMargin = 0;
    for (const TextLine &line : lines)
        rightMargin = qMax(rightMargin, line.box.right());

    // And how far apart this page's lines usually are, which is what a "gap"
    // between two of them has to be measured against.
    const double pitch = linePitch(lines);

    // The width of the column the text is set in, from the leftmost line to the
    // furthest right one: what "short" is short of.
    double leftMargin = lines.first().box.left();
    for (const TextLine &line : lines)
        leftMargin = qMin(leftMargin, line.box.left());
    const double textWidth = qMax(0.0, rightMargin - leftMargin);

    QString paragraph;
    double paragraphLeft = 0;
    QRectF previous;
    QString previousText;
    bool started = false;

    for (const TextLine &line : lines) {
        bool fresh = !started;
        if (started) {
            if (pitch > 0 && line.box.top() - previous.top() > pitch * kParagraphGap) {
                // Further down than this page's own line spacing: whatever was
                // between the two is a blank line, or the end of a block.
                fresh = true;
            } else if (textWidth > 0 && previous.width() < textWidth * kVeryShortLine) {
                // Far too short to be a line that ran out of room: whatever it is
                // -- a name, an item of a list, the last line of a paragraph --
                // the next line begins something else.
                fresh = true;
            } else if (previous.right() < rightMargin - body * kShortLine
                       && endsSentence(previousText)) {
                // The line before ended its sentence and did not reach the
                // margin: it had finished, and this is the next paragraph. Both
                // halves are needed -- a line that ends in a full stop at the
                // margin is one the next line continues, and a short line that
                // stops mid-sentence is one that ran out of room for its last
                // word.
                fresh = true;
            } else if (line.box.left() > paragraphLeft + body * kIndent) {
                fresh = true; // indented, or centred: a new paragraph either way
            } else if (line.box.left() < paragraphLeft - body * kJumpLeft) {
                // Back out to the margin from a block that started further in:
                // the centred heading is over and the text under it has begun.
                fresh = true;
            }
        }

        if (fresh) {
            if (!paragraph.isEmpty())
                paragraphs.append(paragraph);
            paragraph = line.text;
            paragraphLeft = line.box.left();
        } else {
            paragraph = joinLines(paragraph, line.text);
        }
        previous = line.box;
        previousText = line.text;
        started = true;
    }

    if (!paragraph.isEmpty())
        paragraphs.append(paragraph);
    return paragraphs;
}

WordExportReport exportToWord(Document &document, const QString &path,
                              const WordExportOptions &options)
{
    const QSharedPointer<QAtomicInt> &cancel = options.cancel;
    WordExportReport report;
    report.path = path;

    if (path.isEmpty()) {
        report.error = translated("没有指定要写入的文件。");
        return report;
    }

    DocxWriter writer;
    bool wroteAPage = false;

    const int pages = document.pageCount();
    // The paper the document is written on is the paper of the PDF: its first
    // page's size, with a small margin. Told rather than left to a reader's
    // default, because a page image on paper of the wrong size is either shrunk
    // or cut in half, and both look like the export went wrong.
    if (pages > 0)
        writer.setPageSize(document.pageSize(0));

    for (int page = 0; page < pages; ++page) {
        // Between pages, which is where a document being replaced can be
        // noticed. Half a file from one document and half from another is worse
        // than none: the reader would have no way to tell.
        if (cancel && cancel->loadRelaxed()) {
            report.cancelled = true;
            report.error = translated("文档已切换，导出停止。");
            return report;
        }

        const QVector<QString> paragraphs = paragraphsFromLines(linesOfPage(document, page));
        if (!paragraphs.isEmpty()) {
            bool firstOfPage = true;
            for (const QString &text : paragraphs) {
                writer.addParagraph(text, firstOfPage && wroteAPage);
                firstOfPage = false;
            }
            wroteAPage = true;
            ++report.pages;
            report.paragraphs += int(paragraphs.size());
        } else {
            // No text on the page at all: a scan, a drawing, a photograph of a
            // page. Nothing here reads text off an image, so the page goes in as
            // what it is -- and that is the whole of what "convert a scan" can
            // honestly mean without recognising it first.
            QString extension;
            QSizeF sizePoints;
            const QByteArray image = pageImage(document, page, &extension, &sizePoints);
            if (image.isEmpty()) {
                ++report.emptyPages;
            } else {
                writer.addImage(image, extension, sizePoints, wroteAPage);
                wroteAPage = true;
                ++report.pages;
                ++report.imagePages;
            }
        }

        // After every page, including the ones that went in as pictures: those
        // are the slow ones, and they are the reason this is here at all.
        if (options.progress)
            options.progress(page + 1, pages);
    }

    if (report.pages == 0) {
        report.error = translated(
            "这份 PDF 没有可导出的内容：既没有文字层，也没有渲染得出来的页面。");
        return report;
    }

    QString error;
    if (!writer.save(path, &error)) {
        report.error = translated("写入文件失败：%1").arg(error);
        return report;
    }
    report.ok = true;
    return report;
}
