#include "wordexport.h"

#include "docxwriter.h"

// Relative, like every include from inside src/: quoted includes resolve against
// this file's own directory first, and the project root is not on the include path.
#include "../document/document.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QSharedPointer>

#include <algorithm>

namespace {

// How much of a gap, in line heights, makes the next line a new paragraph. Under
// this it is the line spacing of the same paragraph; over it the page was laid
// out with a gap in it, which is what a paragraph break looks like.
constexpr double kParagraphGap = 0.6;

// How far short of the right margin a line can end and still be something a
// paragraph continues past. A paragraph's last line is short -- that is what
// makes it the last one -- and a wrapped line reaches the margin.
constexpr double kShortLine = 1.5;

// How much further in a line can start than its paragraph did before it is a new
// paragraph rather than the same one carrying on. A first-line indent is one or
// two characters, and a character is about a line's height wide in Chinese and
// about half of one in Latin, so this is between the two on purpose.
constexpr double kIndent = 0.8;

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

} // namespace

QVector<TextLine> linesOfPage(const Document &document, int page)
{
    QVector<TextLine> lines;
    const QVector<TextRun> runs = document.textRuns(page);
    lines.reserve(runs.size());

    for (const TextRun &run : runs) {
        if (run.box.width() <= 0 || run.box.height() <= 0)
            continue;

        // The line's first character and one past its last, asked as a range.
        // characterIndexAt answers with the character that starts after a point,
        // so the box's left edge is the line's first character and its right edge
        // is where the next line begins -- which is exactly what pageSelection
        // wants, and exactly the text of this line. This is the same pairing the
        // search highlights and the selection are built from; there is no other
        // one available, and the backend's own comment says why guessing is worse
        // than asking.
        const double middle = run.box.center().y();
        const int from = document.characterIndexAt(page, QPointF(run.box.left(), middle));
        const int to = document.characterIndexAt(page, QPointF(run.box.right(), middle));
        if (from < 0 || to <= from)
            continue;

        const QString text = document.pageSelection(page, from, to).text.trimmed();
        if (text.isEmpty())
            continue;
        lines.append({run.box, text});
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

    QString paragraph;
    double paragraphLeft = 0;
    QRectF previous;
    bool started = false;

    for (const TextLine &line : lines) {
        bool fresh = !started;
        if (started) {
            const double gap = line.box.top() - previous.bottom();
            if (gap > body * kParagraphGap) {
                fresh = true; // a blank line between the two
            } else if (previous.right() < rightMargin - body * kShortLine) {
                // The line before ended well before the margin: it was the last
                // line of its paragraph, and this is the first of the next.
                fresh = true;
            } else if (line.box.left() > paragraphLeft + body * kIndent) {
                fresh = true; // indented, or centred: a new paragraph either way
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
        started = true;
    }

    if (!paragraph.isEmpty())
        paragraphs.append(paragraph);
    return paragraphs;
}

WordExportReport exportToWord(const Document &document, const QString &path)
{
    return exportToWord(document, path, QSharedPointer<QAtomicInt>());
}

WordExportReport exportToWord(const Document &document, const QString &path,
                              const QSharedPointer<QAtomicInt> &cancel)
{
    WordExportReport report;
    report.path = path;

    if (path.isEmpty()) {
        report.error = translated("没有指定要写入的文件。");
        return report;
    }

    DocxWriter writer;
    bool wroteAPage = false;

    const int pages = document.pageCount();
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
        if (paragraphs.isEmpty()) {
            ++report.emptyPages;
            continue;
        }

        bool firstOfPage = true;
        for (const QString &text : paragraphs) {
            writer.addParagraph(text, firstOfPage && wroteAPage);
            firstOfPage = false;
        }
        wroteAPage = true;
        ++report.pages;
        report.paragraphs += int(paragraphs.size());
    }

    if (report.pages == 0) {
        report.error = translated(
            "这份 PDF 没有可导出的文字层：它可能是扫描件或整页图片。"
            "可以先用“识别”工具框选文字，识别出来的内容可以复制。");
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
