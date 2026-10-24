// A PDF read out as a Word document that looks like the PDF: the layout rules, the
// file, and the fixtures from end to end.
//
// ---- why this test unpacks the file rather than asking for it back ----
//
// A .docx is a ZIP, and this program writes both halves of that: the archive and
// the XML inside it. A test that read the file back with the same code that wrote
// it would agree with itself about a file Word refuses to open, so the unpacking
// below is written from the format -- the end record, the central directory, the
// local headers -- and it checks the one number the format publishes, the CRC of
// the string "123456789", to show that the checksum it is reading is the checksum
// an unpacker checks.
//
// ---- why the layout rules are tested on pages made here ----
//
// Each rule in pageanalysis.cpp was written against something a real document did
// (a book, a manual, a schematic -- named in the comments there). The pages below
// are the shape of each of those, built character by character, so that the rule is
// pinned without also pinning what PDFium makes of some file; the fixtures at the
// end are the other half, the whole path through PDFium. Whether the result looks
// like the PDF in Word is a question only Word (or WPS) can answer, and that loop is
// run by hand: export, print the .docx to PDF, compare the pages (store-submission
// §124 has the numbers).

#include "../src/document/pagecontent.h"
#include "../src/document/pdfiumbackend.h"
#include "../src/export/docxwriter.h"
#include "../src/export/layoutmodel.h"
#include "../src/export/pageanalysis.h"
#include "../src/export/wordexport.h"
#include "../src/export/zipwriter.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QXmlStreamReader>

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// ---- a ZIP, read the way an unpacker reads one ----

quint16 u16(const QByteArray &data, int at)
{
    return quint16(quint8(data.at(at))) | quint16(quint8(data.at(at + 1))) << 8;
}

quint32 u32(const QByteArray &data, int at)
{
    return quint32(u16(data, at)) | (quint32(u16(data, at + 2)) << 16);
}

struct ZipEntry
{
    QByteArray content;
    quint32 crc = 0;
};

// The entries of an archive, by name. An empty result means the archive could not
// be read at all, which is the answer for anything that is not one.
QHash<QString, ZipEntry> unpackZip(const QByteArray &file)
{
    QHash<QString, ZipEntry> parts;
    // The end record is last, behind a comment of unknown length, so it is found
    // by looking backwards -- which is what an unpacker does.
    const int end = file.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (end < 0)
        return parts;
    const int count = u16(file, end + 10);
    int at = int(u32(file, end + 16)); // where the central directory starts
    for (int index = 0; index < count; ++index) {
        if (at + 46 > file.size() || file.mid(at, 4) != QByteArray("PK\x01\x02", 4))
            return {};
        const quint32 crc = u32(file, at + 16);
        const quint32 compressed = u32(file, at + 20);
        const quint32 plain = u32(file, at + 24);
        const int nameLength = u16(file, at + 28);
        const int extraLength = u16(file, at + 30);
        const int commentLength = u16(file, at + 32);
        const int localAt = int(u32(file, at + 42));
        const QString name = QString::fromUtf8(file.mid(at + 46, nameLength));
        // The writer stores; anything else means it deflated and this test would
        // have to inflate, which it deliberately cannot do.
        if (compressed != plain)
            return {};
        const int localNameLength = u16(file, localAt + 26);
        const int localExtraLength = u16(file, localAt + 28);
        const int dataAt = localAt + 30 + localNameLength + localExtraLength;
        ZipEntry entry;
        entry.crc = crc;
        entry.content = file.mid(dataAt, int(compressed));
        parts.insert(name, entry);
        at += 46 + nameLength + extraLength + commentLength;
    }
    return parts;
}

bool wellFormed(const QByteArray &xml, QString *error)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
        reader.readNext();
    if (reader.hasError() && error)
        *error = reader.errorString();
    return !reader.hasError();
}

// ---- pages made here ----

bool cjk(char32_t code)
{
    return code >= 0x2E80 && code <= 0x9FFF;
}

// One line of text as PDFium reports it: each character's ink box a little inside
// the slot its advance gives it, the loose box ascent to descent. `advance` is the
// width of a Latin character in ems (a CJK one is a full em); `step` overrides
// where each next character starts, for lines a typesetter spaced by hand.
struct Line
{
    QString text;
    double x = 0;
    double baseline = 0;
    double size = 10;
    QString font = QStringLiteral("Arial");
    int weight = 400;
    double advance = 0.5;
    QColor color;
    int z = 0;
};

void add(PageContent &page, const Line &line)
{
    double x = line.x;
    for (const char32_t code : line.text.toUcs4()) {
        const double width = (cjk(code) ? 1.0 : line.advance) * line.size;
        ContentChar ch;
        ch.code = code;
        ch.loose = QRectF(x, line.baseline - 0.88 * line.size, width, 1.1 * line.size);
        ch.box = code == U' ' ? QRectF(x, line.baseline - 0.7 * line.size, width, 0.7 * line.size)
                              : QRectF(x + 0.06 * width, line.baseline - 0.7 * line.size, 0.88 * width,
                                       0.72 * line.size);
        ch.baseline = line.baseline;
        ch.size = line.size;
        ch.font = line.font;
        ch.weight = line.weight;
        ch.color = line.color;
        ch.z = line.z;
        page.chars.append(ch);
        x += width;
    }
}

PageContent blankPage(double width = 595, double height = 842)
{
    PageContent page;
    page.ok = true;
    page.size = QSizeF(width, height);
    return page;
}

ContentPath strokedLine(QPointF a, QPointF b, double width = 0.75)
{
    ContentPath path;
    path.stroke = true;
    path.strokeColor = Qt::black;
    path.strokeWidth = width;
    path.polygons.append(QPolygonF({a, b}));
    path.bounds = QRectF(a, b).normalized().adjusted(-width / 2, -width / 2, width / 2, width / 2);
    return path;
}

QByteArray png(const QSize &size, const QColor &colour)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QVector<LayoutParagraph> paragraphsOf(const LayoutPage &page)
{
    QVector<LayoutParagraph> found;
    for (const LayoutBlock &block : page.blocks) {
        if (block.kind == LayoutBlock::Kind::Paragraph)
            found.append(block.paragraph);
    }
    return found;
}

QString allText(const LayoutPage &page)
{
    QString text;
    for (const LayoutParagraph &paragraph : paragraphsOf(page))
        text += paragraph.text() + QLatin1Char('|');
    return text;
}

QString fixture(const QString &directory, const QString &name)
{
    const QString path = directory + QLatin1Char('/') + name;
    if (!QFileInfo::exists(path)) {
        std::cerr << "FAIL: " << path.toStdString() << " is missing; run tools/make_fixtures.py\n";
        ++failures;
        return {};
    }
    return path;
}

QByteArray documentXmlOf(const QString &file)
{
    return unpackZip(readAll(file)).value(QStringLiteral("word/document.xml")).content;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("tests/fixtures");
    AnalysisOptions plain;

    // ---- a paragraph keeps the PDF's own line breaks ----
    //
    // A paragraph left to wrap by itself breaks wherever the reader's glyph widths
    // say, and one extra line pushes the rest of the page onto the next (a 172-page
    // book came back as 303). So lines stay lines: one paragraph, broken where the
    // PDF broke it, at the PDF's distance from baseline to baseline.
    {
        PageContent page = blankPage();
        add(page, {QStringLiteral("The first line of a paragraph runs to the margin"), 72, 100, 10});
        add(page, {QStringLiteral("and the second line runs on to the same margin."), 72, 114, 10});
        add(page, {QStringLiteral("The last one is short."), 72, 128, 10});
        const LayoutPage laid = analysePage(page, plain);
        const QVector<LayoutParagraph> paragraphs = paragraphsOf(laid);
        check(paragraphs.size() == 1, QStringLiteral("three lines of one paragraph are one paragraph (%1)")
                                          .arg(paragraphs.size()));
        const QString text = paragraphs.value(0).text();
        check(text.count(QLatin1Char('\n')) == 2,
              QStringLiteral("with the PDF's two line breaks kept (got \"%1\")").arg(text));
        check(std::fabs(paragraphs.value(0).lineSpacing - 14.0) < 0.3,
              QStringLiteral("at the PDF's own line pitch (%1)").arg(paragraphs.value(0).lineSpacing));
        check(std::fabs(laid.marginLeft - 72) < 2,
              QStringLiteral("and the page's left margin is where the text starts (%1)").arg(laid.marginLeft));
    }

    // ---- a space the next character is moved back over is no space ----
    //
    // A real Chinese book sets the digits and letters of its body text one by one:
    // "1983" is "1", a space, "9" moved back over that space, and so on. Taken at
    // its word, that is "1 9 8 3" in the document.
    {
        PageContent page = blankPage();
        Line spaced{QStringLiteral("1 9 8 3"), 72, 100, 10};
        add(page, spaced);
        // Pull each character after a space back into the space's slot.
        for (int i = 1; i < page.chars.size(); ++i) {
            if (page.chars.at(i - 1).code == U' ') {
                const double back = page.chars.at(i - 1).loose.width() * 0.88;
                for (int k = i; k < page.chars.size(); ++k) {
                    page.chars[k].box.translate(-back, 0);
                    page.chars[k].loose.translate(-back, 0);
                }
            }
        }
        add(page, {QStringLiteral("two words"), 72, 140, 10});
        const QString text = allText(analysePage(page, plain));
        check(text.contains(QStringLiteral("1983")), QStringLiteral("a hand-spaced number is one number (%1)").arg(text));
        check(text.contains(QStringLiteral("two words")), QStringLiteral("and a real word gap is still a space (%1)").arg(text));
    }

    // ---- a ruled table, a merged cell and a picture in a cell ----
    //
    // What the user who asked for this rewrite named first: a table with pictures in
    // it. The grid is the PDF's lines; a cell whose inner line is missing spans; a
    // picture inside a cell's lines goes into that cell, and the words stay words.
    {
        PageContent page = blankPage();
        const double left = 72, middle = 222, right = 372, top = 100, mid = 140, bottom = 260;
        page.paths.append(strokedLine({left, top}, {right, top}));
        page.paths.append(strokedLine({left, mid}, {right, mid}));
        page.paths.append(strokedLine({left, bottom}, {right, bottom}));
        page.paths.append(strokedLine({left, top}, {left, bottom}));
        page.paths.append(strokedLine({right, top}, {right, bottom}));
        page.paths.append(strokedLine({middle, mid}, {middle, bottom})); // none in the first row
        add(page, {QStringLiteral("Heading across"), 80, 125, 10});
        add(page, {QStringLiteral("Picture"), 80, 160, 10});
        add(page, {QStringLiteral("Notes"), 230, 160, 10});
        ContentImage picture;
        picture.box = QRectF(90, 175, 100, 70);
        picture.bytes = png(QSize(40, 28), QColor(178, 34, 34));
        picture.extension = QStringLiteral("png");
        page.images.append(picture);
        const LayoutPage laid = analysePage(page, plain);
        std::shared_ptr<LayoutTable> table;
        for (const LayoutBlock &block : laid.blocks) {
            if (block.kind == LayoutBlock::Kind::Table)
                table = block.table;
        }
        check(table != nullptr, "the ruled grid is a table");
        if (table) {
            check(table->columns.size() == 3 && table->rows.size() == 3,
                  QStringLiteral("of two columns and two rows (%1 x %2 grid lines)")
                      .arg(table->columns.size()).arg(table->rows.size()));
            const LayoutCell *heading = table->cellAt(0, 0);
            check(heading && heading->columnSpan == 2 && heading->text().contains(QStringLiteral("Heading across")),
                  "its first row is one cell across both columns, with its words in it");
            const LayoutCell *withPicture = table->cellAt(1, 0);
            check(withPicture && withPicture->imageCount() == 1
                      && withPicture->text().contains(QStringLiteral("Picture")),
                  QStringLiteral("the picture is inside its cell, beside the cell's words (%1 pictures)")
                      .arg(withPicture ? withPicture->imageCount() : -1));
            const LayoutCell *notes = table->cellAt(1, 1);
            check(notes && notes->text() == QStringLiteral("Notes"), "and the next cell has its own words");
            check(withPicture && withPicture->top.present && withPicture->left.present,
                  "the cell carries the PDF's borders");
        }
    }

    // ---- bold is a face's weight, and italic is a face of its own ----
    //
    // A book's KaiTi header was the italic face at weight 310, its KaiTi body text
    // the upright face at 465: compared as one family, every KaiTi word in the body
    // came out bold.
    {
        PageContent page = blankPage();
        add(page, {QStringLiteral("header words"), 72, 60, 10, QStringLiteral("FZKTJW--GB1-0,Italic"), 310});
        add(page, {QStringLiteral("body words"), 72, 200, 10, QStringLiteral("FZKTJW--GB1-0"), 465});
        add(page, {QStringLiteral("heavy words"), 72, 300, 10, QStringLiteral("FZKTJW--GB1-0"), 700});
        const LayoutPage laid = analysePage(page, plain);
        bool bodyBold = false, heavyBold = false;
        for (const LayoutParagraph &paragraph : paragraphsOf(laid)) {
            for (const LayoutRun &run : paragraph.runs) {
                if (run.text.contains(QStringLiteral("body")))
                    bodyBold = bodyBold || run.style.bold;
                if (run.text.contains(QStringLiteral("heavy")))
                    heavyBold = heavyBold || run.style.bold;
            }
        }
        check(!bodyBold, "an upright face is not bold because its italic face is lighter");
        check(heavyBold, "and a weight of 700 is bold whatever else is on the page");
    }

    // ---- the running header and footer ----
    //
    // What repeats at the edge of the pages. Two ways this went wrong in a real
    // book: a line of code that happened to repeat (". . .", "end") became a
    // 67-point footer with the page's last line still below it, pushing that line
    // off its page; and a short chapter's header, seen on only one other page, was
    // not taken while the "download" tag beside it was.
    {
        QVector<QVector<BandRow>> pages(6);
        QVector<QSizeF> sizes(6, QSizeF(516, 728));
        for (int p = 0; p < 6; ++p) {
            BandRow tag;
            tag.box = QRectF(30, 47, 60, 16);
            tag.text = tag.folded = QStringLiteral("download");
            tag.top = true;
            pages[p].append(tag);
            BandRow number;
            number.box = QRectF(250, 700, 12, 10);
            number.text = QString::number(p + 1);
            number.folded = QStringLiteral("#");
            number.pageNumber = true;
            pages[p].append(number);
            // A code line near the bottom on half the pages, with a body line under it.
            if (p % 2 == 0) {
                BandRow code;
                code.box = QRectF(48, 630, 40, 9);
                code.text = code.folded = QStringLiteral("end");
                pages[p].append(code);
                BandRow body;
                body.box = QRectF(28, 660, 400, 12);
                body.text = body.folded = QStringLiteral("body text %1").arg(p);
                pages[p].append(body);
            }
        }
        // A chapter title in the header of pages 0 and 2 only: seen once elsewhere.
        for (int p : {0, 2}) {
            BandRow chapter;
            chapter.box = QRectF(290, 23, 150, 30);
            chapter.text = chapter.folded = QStringLiteral("Chapter 6 #");
            chapter.top = true;
            pages[p].append(chapter);
        }
        const RunningRows running = headerFooterRows(pages, sizes);
        check(running.header.value(0).size() == 2,
              QStringLiteral("the tag and the chapter title beside it are the header (%1 rows)")
                  .arg(running.header.value(0).size()));
        check(running.footer.value(0).size() == 1 && running.footer.value(0).value(0).top() > 690,
              QStringLiteral("only the page number is the footer, not the code line above body text (%1 rows)")
                  .arg(running.footer.value(0).size()));
        check(running.footer.value(1).size() == 1, "and a page without that code line has its page number too");
    }

    // ---- a caption beside a figure is placed, not flowed ----
    //
    // Text beside an anchored figure flows down its column; the figure's own words
    // (its caption) sit in the figure's column. In the flow, the caption became a
    // line of its own between two lines of the text beside it, and the page's last
    // lines went to the next page.
    {
        PageContent page = blankPage();
        const QString words = QStringLiteral("words beside the figure run to here");
        for (int line = 0; line < 8; ++line)
            add(page, {words, 72, 112.0 + 15 * line, 10, QStringLiteral("Arial"), 400, 0.55});
        ContentImage figure;
        figure.box = QRectF(330, 100, 150, 100);
        figure.bytes = png(QSize(30, 20), QColor(70, 130, 180));
        figure.extension = QStringLiteral("png");
        page.images.append(figure);
        add(page, {QStringLiteral("Figure 1"), 370, 210, 9});
        const LayoutPage laid = analysePage(page, plain);
        int lines = 0;
        double widestGap = 0;
        bool first = true;
        bool captionFramed = false, captionInFlow = false;
        for (const LayoutParagraph &paragraph : paragraphsOf(laid)) {
            if (paragraph.text().contains(QStringLiteral("Figure 1"))) {
                captionFramed = captionFramed || paragraph.framed;
                captionInFlow = captionInFlow || !paragraph.framed;
            } else if (!paragraph.framed) {
                lines += paragraph.lineCount;
                if (!first)
                    widestGap = qMax(widestGap, paragraph.spaceBefore);
                first = false;
            }
        }
        check(captionFramed && !captionInFlow, "the caption is placed where it is, out of the flow");
        // Each line of the text beside it then follows the one before at the PDF's
        // pitch: no room was opened between two of them for a caption line.
        check(lines == 8 && widestGap < 5.0,
              QStringLiteral("and the eight lines beside the figure flow on unbroken (%1 lines, widest gap %2 pt)")
                  .arg(lines).arg(widestGap));
        check(laid.anchors.size() == 1, QStringLiteral("the figure is anchored at its place (%1)").arg(laid.anchors.size()));
    }

    // ---- a page that is a drawing is the picture it is ----
    //
    // A circuit schematic: hundreds of wires, its words a few characters each. Laid
    // out as text the wires became tables and the labels lines of their own. The
    // control is the same labels with a handful of lines, which is a page of text.
    {
        const auto schematic = [](int wires) {
            PageContent page = blankPage(842, 595);
            for (int i = 0; i < wires; ++i) {
                const double x = 30 + (i % 20) * 39;
                const double y = 30 + (i / 20) * 33;
                page.paths.append(strokedLine({x, y}, {x + 12, y}));
            }
            for (int i = 0; i < 60; ++i)
                add(page, {QStringLiteral("R%1").arg(i), 40.0 + (i % 10) * 75, 50 + (i / 10) * 80.0, 7});
            return page;
        };
        check(analysePage(schematic(320), plain).pictureOnly, "a schematic page goes in as a picture");
        check(!analysePage(schematic(10), plain).pictureOnly, "and the same labels without the drawing do not");

        // A scan with its running header typed over it: a page-sized picture with
        // a line of real text is still a scanned page.
        PageContent scan = blankPage(516, 728);
        ContentImage body;
        body.box = QRectF(20, 70, 470, 640);
        body.bytes = png(QSize(47, 64), Qt::white);
        body.extension = QStringLiteral("png");
        scan.images.append(body);
        add(scan, {QStringLiteral("Appendix 161"), 300, 50, 10});
        check(analysePage(scan, plain).pictureOnly, "a scanned page with a header line is a picture of the page");
    }

    // ---- what is painted over is not on the page ----
    {
        const auto covered = [](int pathZ) {
            PageContent page = blankPage();
            add(page, {QStringLiteral("hidden words"), 80, 120, 12, QStringLiteral("Arial"), 400, 0.5, QColor(), 1});
            add(page, {QStringLiteral("visible words"), 80, 400, 12, QStringLiteral("Arial"), 400, 0.5, QColor(), 3});
            ContentPath panel;
            panel.fill = true;
            panel.fillColor = QColor(217, 217, 242);
            panel.bounds = QRectF(60, 90, 400, 60);
            panel.polygons.append(QPolygonF(panel.bounds));
            panel.z = pathZ;
            page.paths.append(panel);
            return allText(analysePage(page, AnalysisOptions()));
        };
        const QString over = covered(2);
        check(!over.contains(QStringLiteral("hidden")), QStringLiteral("words a panel is painted over are left out (%1)").arg(over));
        check(over.contains(QStringLiteral("visible")), "and words elsewhere are not");
        check(covered(0).contains(QStringLiteral("hidden")), "words painted over a panel are kept");
    }

    // ---- turned text belongs to a drawing ----
    {
        PageContent page = blankPage();
        add(page, {QStringLiteral("upright line"), 72, 100, 10});
        add(page, {QStringLiteral("axis label"), 72, 300, 10});
        for (ContentChar &ch : page.chars) {
            if (ch.baseline > 200)
                ch.angle = 3.14159265358979 / 2;
        }
        const QString text = allText(analysePage(page, plain));
        check(text.contains(QStringLiteral("upright")) && !text.contains(QStringLiteral("axis")),
              QStringLiteral("a turned label is not a paragraph (%1)").arg(text));
    }

    // ---- fonts: the name Word can find ----
    {
        check(wordFontFamily(QStringLiteral("ABCDEF+Arial-BoldMT")) == QStringLiteral("Arial"), "a subset Arial is Arial");
        check(wordFontFamily(QStringLiteral("TimesNewRomanPS-ItalicMT")) == QStringLiteral("Times New Roman"),
              "Times by its PostScript name is Times New Roman");
        check(wordFontFamily(QStringLiteral("FZSSJW--GB1-0")) == QStringLiteral("SimSun"), "a FounderType Song is SimSun");
        check(wordFontFamily(QStringLiteral("FZKTJW--GB1-0")) == QStringLiteral("KaiTi"), "a FounderType Kai is KaiTi");
        check(wordFontFamily(QStringLiteral("#CB#CE#CC#E5")) == QStringLiteral("SimSun"),
              "a GBK-spelled name is decoded (宋体 is SimSun)");
        // A writer that names its fonts "CIDFont+F2" still embeds the real thing.
        check(wordFontFamily(QStringLiteral("CIDFont+F2"), QStringLiteral("Arial")) == QStringLiteral("Arial"),
              "a name that says nothing gives way to the embedded font's own family");
        check(wordFontFamily(QStringLiteral("CIDFont+F2")) != QStringLiteral("Arial"),
              "and without one the name is not made up");
    }

    // ---- every line as wide in Word as in the PDF ----
    //
    // Word sets a line with the font's own advances. Where the PDF set the same
    // words wider (justified) or tighter, the line is given the character spacing
    // that makes up the difference -- otherwise its last word wraps onto a line of
    // its own, page after page. Arial's widths are Windows's, so this is measured
    // against the real font.
    {
        const auto spacing = [](double advance) {
            PageContent page = blankPage();
            add(page, {QStringLiteral("measured words"), 72, 100, 10, QStringLiteral("Arial"), 400, advance});
            const QVector<LayoutParagraph> paragraphs = paragraphsOf(analysePage(page, AnalysisOptions()));
            return paragraphs.isEmpty() || paragraphs.first().runs.isEmpty() ? 0.0
                                                                               : paragraphs.first().runs.first().style.spacing;
        };
        const double wide = spacing(0.9);
        const double tight = spacing(0.35);
        check(wide > 0.5, QStringLiteral("a line the PDF set wide is spaced out (%1 pt)").arg(wide));
        check(tight < -0.2, QStringLiteral("and one it set tight is drawn together (%1 pt)").arg(tight));
        check(std::fabs(wide * 20 - std::round(wide * 20)) < 1e-6, "in the twentieths of a point Word takes");
    }

    // ---- the archive ----
    {
        ZipWriter zip;
        zip.addFile(QStringLiteral("hello.txt"), QByteArrayLiteral("123456789"));
        zip.addFile(QStringLiteral("中文.txt"), QByteArrayLiteral("你好"));
        const QHash<QString, ZipEntry> parts = unpackZip(zip.toBytes());
        check(parts.size() == 2, QStringLiteral("both entries are in the archive (%1)").arg(parts.size()));
        // The CRC of this string is published by the format's own documentation.
        check(parts.value(QStringLiteral("hello.txt")).crc == 0xCBF43926u,
              QStringLiteral("with the checksum the format gives for those bytes (got %1)")
                  .arg(parts.value(QStringLiteral("hello.txt")).crc, 8, 16, QLatin1Char('0')));
        check(parts.value(QStringLiteral("hello.txt")).content == QByteArrayLiteral("123456789"),
              "and the bytes that went in come back out");
        check(parts.value(QStringLiteral("中文.txt")).content == QByteArray("你好"), "a name that is not ASCII survives");
    }

    // ---- the document a laid-out page becomes ----
    {
        LayoutPage page;
        page.size = QSizeF(516, 728);
        page.marginLeft = 30;
        page.marginTop = 80;
        LayoutParagraph body;
        RunStyle style;
        style.font = QStringLiteral("SimSun");
        style.size = 10;
        style.spacing = -0.35;
        body.runs.append({QStringLiteral("Line one & <two>\nline two"), style});
        body.lineSpacing = 15.5;
        body.spaceBefore = 6;
        body.lineCount = 2;
        LayoutBlock paragraphBlock;
        paragraphBlock.paragraph = body;
        page.blocks.append(paragraphBlock);
        LayoutParagraph caption;
        caption.runs.append({QStringLiteral("Figure 1"), RunStyle()});
        caption.framed = true;
        caption.box = QRectF(330, 210, 40, 10);
        caption.firstBaseline = 218;
        caption.lineSpacing = 11;
        LayoutBlock captionBlock;
        captionBlock.paragraph = caption;
        page.blocks.append(captionBlock);
        auto table = std::make_shared<LayoutTable>();
        table->columns = {30, 130, 230};
        table->rows = {300, 330, 360};
        LayoutCell across;
        across.columnSpan = 2;
        across.top.present = true;
        LayoutBlock inCell;
        inCell.paragraph.runs.append({QStringLiteral("across"), RunStyle()});
        across.blocks.append(inCell);
        LayoutCell down;
        down.row = 1;
        down.rowSpan = 1;
        LayoutCell other;
        other.row = 1;
        other.column = 1;
        table->cells = {across, down, other};
        table->box = QRectF(30, 300, 200, 60);
        LayoutBlock tableBlock;
        tableBlock.kind = LayoutBlock::Kind::Table;
        tableBlock.table = table;
        tableBlock.top = 300;
        tableBlock.bottom = 360;
        page.blocks.append(tableBlock);
        LayoutAnchor anchor;
        anchor.image.bytes = png(QSize(10, 10), Qt::red);
        anchor.image.extension = QStringLiteral("png");
        anchor.image.box = QRectF(330, 100, 150, 100);
        page.anchors.append(anchor);
        LayoutParagraph running;
        running.runs.append({QStringLiteral("2"), RunStyle(), QString(), -1, QStringLiteral("PAGE")});
        running.framed = true;
        running.box = QRectF(40, 40, 10, 20);
        running.firstBaseline = 58;
        running.lineSpacing = 24;
        page.header.append(running);
        LayoutPage second = page;
        second.size = QSizeF(400, 500);

        DocxWriter writer;
        writer.addPage(page);
        writer.addPage(second);
        const QHash<QString, ZipEntry> parts = unpackZip(writer.toBytes());
        const QByteArray xml = parts.value(QStringLiteral("word/document.xml")).content;
        check(parts.contains(QStringLiteral("[Content_Types].xml")) && parts.contains(QStringLiteral("_rels/.rels"))
                  && !xml.isEmpty(),
              "the package has its content types, relationships and document");
        for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
            if (!it.key().endsWith(QLatin1String(".xml")) && !it.key().endsWith(QLatin1String(".rels")))
                continue;
            QString error;
            check(wellFormed(it.value().content, &error),
                  QStringLiteral("%1 is well-formed XML (%2)").arg(it.key(), error));
        }
        check(xml.count("<w:sectPr>") == 2, QStringLiteral("each PDF page is a section of its own (%1)").arg(xml.count("<w:sectPr>")));
        check(xml.contains("<w:pgSz w:w=\"10320\" w:h=\"14560\"/>") && xml.contains("<w:pgSz w:w=\"8000\" w:h=\"10000\"/>"),
              "at the size of its page");
        check(xml.contains("w:before=\"120\" w:after=\"0\" w:line=\"310\" w:lineRule=\"exact\""),
              "a paragraph has the PDF's room above it and its exact line pitch");
        check(xml.contains("<w:br/>"), "and its line break");
        check(xml.contains("<w:spacing w:val=\"-7\"/>"), "and the character spacing that fits its width");
        check(xml.contains("Line one &amp; &lt;two&gt;"), "text is escaped");
        check(xml.contains("<w:framePr ") && xml.indexOf("<w:framePr ") < xml.indexOf("Figure 1"),
              "a placed paragraph is a frame at its place");
        check(xml.contains("<w:gridSpan w:val=\"2\"/>"), "a merged cell spans its columns");
        check(xml.contains("<wp:anchor ") && xml.contains("relativeFrom=\"page\""),
              "a picture with text beside it is anchored to the page");
        check(xml.contains("<w:headerReference "), "and the page has its running header");
        QString header;
        for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
            if (it.key().startsWith(QLatin1String("word/header")))
                header += QString::fromUtf8(it.value().content);
        }
        check(header.contains(QStringLiteral("PAGE")) && header.contains(QStringLiteral("<w:framePr ")),
              "whose page number is a field, placed where the PDF has it");
        check(writer.pageCount() == 2 && writer.tableCount() == 2, "the writer counts what it wrote");
    }

    // ---- characters XML has no way to write down ----
    //
    // A PDF's text layer can contain anything, and some of it cannot be in an XML
    // file at all. A real manual's line breaks came out as #xFFFE once, and Word
    // refused the file.
    {
        LayoutPage page;
        page.size = QSizeF(595, 842);
        LayoutBlock block;
        block.paragraph.runs.append({QStringLiteral("before") + QChar(0xFFFE) + QChar(0xFFFF) + QChar(0x0007)
                                         + QChar(0xD800) + QStringLiteral("after") + QString::fromUcs4(U"\U0001F600"),
                                     RunStyle()});
        page.blocks.append(block);
        DocxWriter writer;
        writer.addPage(page);
        const QByteArray xml = unpackZip(writer.toBytes()).value(QStringLiteral("word/document.xml")).content;
        // Spelled out rather than asked of Qt: a needle that came back empty would
        // pass by asking whether the document contains nothing at all.
        const QVector<QByteArray> forbidden = {
            QByteArrayLiteral("\xEF\xBF\xBE"), QByteArrayLiteral("\xEF\xBF\xBF"), QByteArrayLiteral("\x07"),
            QByteArrayLiteral("\xEF\xBF\xBD"), QByteArrayLiteral("\xED\xA0\x80"),
        };
        QByteArray found;
        for (const QByteArray &needle : forbidden) {
            if (xml.contains(needle))
                found = needle.toHex();
        }
        check(found.isEmpty(), QStringLiteral("no character XML cannot carry is written (%1)").arg(QString::fromLatin1(found)));
        check(xml.contains("beforeafter"), "and the text around it is all still there");
        check(xml.contains(QString::fromUcs4(U"\U0001F600").toUtf8()), "a surrogate pair survives as itself");
        QString error;
        check(wellFormed(xml, &error), QStringLiteral("and it is well-formed XML (%1)").arg(error));
    }

    // ==== the fixtures, from the PDF to the file ====

    // Two pages of different sizes, each a section of its own size.
    if (const QString path = fixture(directory, QStringLiteral("text.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the text fixture opens");
        const PageContent content = pageContentOf(document, 0);
        bool found = false;
        QString read;
        for (const ContentChar &ch : content.chars)
            read += QString::fromUcs4(&ch.code, 1);
        found = read.contains(QStringLiteral("XYZPDF one"));
        check(content.ok && found, QStringLiteral("its characters are read with their places (%1)").arg(read));
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("export.docx"));
        const WordExportReport report = exportToWord(document, file);
        check(report.ok && report.pages == 2 && report.imagePages == 0 && report.emptyPages == 0,
              QStringLiteral("both pages export as text (%1, %2 pages, %3 pictures)")
                  .arg(report.error).arg(report.pages).arg(report.imagePages));
        check(report.path == file && QFileInfo(file).size() > 0, "to the file asked for");
        const QByteArray xml = documentXmlOf(file);
        check(xml.contains("XYZPDF one") && xml.contains("XYZPDF two"), "with each page's own text");
        check(xml.contains("<w:pgSz w:w=\"11900\" w:h=\"16840\"/>") && xml.contains("<w:pgSz w:w=\"8000\" w:h=\"10000\"/>"),
              "and each page at its own size");
    }

    // A page with no text: the picture of the page, with the page's shape and ink.
    if (const QString path = fixture(directory, QStringLiteral("picture.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the picture fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("picture.docx"));
        const WordExportReport report = exportToWord(document, file);
        check(report.ok && report.pages == 1 && report.imagePages == 1 && report.paragraphs == 0,
              QStringLiteral("one page, put in as a picture (%1 pictures, %2 paragraphs)")
                  .arg(report.imagePages).arg(report.paragraphs));
        const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
        QString imageName;
        for (const QString &name : parts.keys()) {
            if (name.startsWith(QStringLiteral("word/media/")))
                imageName = name;
        }
        const QImage picture = QImage::fromData(parts.value(imageName).content);
        check(!picture.isNull(), QStringLiteral("the picture decodes (%1)").arg(imageName));
        if (!picture.isNull()) {
            // The fixture fills a red rectangle over the upper half of the page.
            const QColor inside = picture.pixelColor(picture.width() / 2, picture.height() / 4);
            const QColor outside = picture.pixelColor(picture.width() / 2, picture.height() * 9 / 10);
            check(inside.red() > 150 && inside.green() < 110 && inside.blue() < 110,
                  QStringLiteral("with the page's ink in it (%1)").arg(inside.name()));
            check(outside.lightness() > 230, QStringLiteral("and paper around it (%1)").arg(outside.name()));
            check(std::fabs(double(picture.width()) / picture.height() - 595.0 / 842.0) < 0.01,
                  "in the page's own shape");
        }
    }

    // A ruled grid is an editable table.
    if (const QString path = fixture(directory, QStringLiteral("table.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the ruled table fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("table.docx"));
        const WordExportReport report = exportToWord(document, file);
        check(report.ok && report.tables == 1 && report.imagePages == 0,
              QStringLiteral("its grid becomes one table (%1 tables)").arg(report.tables));
        const QByteArray xml = documentXmlOf(file);
        check(xml.count("<w:tr>") == 2 && xml.count("<w:tc>") == 4, "with its two rows and two columns");
        check(xml.contains("top left") && xml.contains("bottom right"), "and its words in its cells");
    }

    // A typed page and a scanned one: in order, with progress, one as words and one
    // as a picture.
    if (const QString path = fixture(directory, QStringLiteral("mixed.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the mixed fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("mixed.docx"));
        QVector<QPair<int, int>> reported;
        WordExportOptions options;
        options.progress = [&reported](int done, int total) { reported.append({done, total}); };
        const WordExportReport report = exportToWord(document, file, options);
        check(report.ok && report.pages == 2 && report.imagePages == 1 && report.paragraphs >= 1,
              QStringLiteral("one page of words and one picture (%1 pages, %2 pictures, %3 paragraphs)")
                  .arg(report.pages).arg(report.imagePages).arg(report.paragraphs));
        check(!reported.isEmpty() && reported.first() == QPair<int, int>(0, 2) && reported.last() == QPair<int, int>(2, 2),
              QStringLiteral("it says how big the job is and when it is done (%1 reports)").arg(reported.size()));
        const QByteArray xml = documentXmlOf(file);
        const int text = xml.indexOf("XYZPDF typed cover");
        const int firstSection = xml.indexOf("<w:sectPr>");
        const int drawing = xml.indexOf("<w:drawing>");
        // A section ends with the paragraph whose properties carry its sectPr, and
        // those properties come before that paragraph's text.
        check(text >= 0 && text < drawing && firstSection < drawing && xml.count("<w:sectPr>") == 2,
              "the typed page first, then the picture in a section of its own");
    }

    // Colour comes from the PDF: one red line, one black one.
    if (const QString path = fixture(directory, QStringLiteral("colored.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the coloured fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("colored.docx"));
        check(exportToWord(document, file).ok, "it exports");
        const QByteArray xml = documentXmlOf(file);
        static const QRegularExpression colour(QStringLiteral("<w:color w:val=\"([0-9A-F]{6})\"/>"));
        QStringList colours;
        for (auto it = colour.globalMatch(QString::fromUtf8(xml)); it.hasNext();)
            colours.append(it.next().captured(1));
        colours.removeDuplicates();
        check(colours == QStringList{QStringLiteral("FF0000")},
              QStringLiteral("the red line is red and nothing else has a colour (%1)").arg(colours.join(QLatin1Char(','))));
        const int black = xml.indexOf("XYZPDF</w:t>");
        const int paragraphStart = xml.lastIndexOf("<w:p>", black);
        check(black >= 0 && !xml.mid(paragraphStart, black - paragraphStart).contains("<w:color"),
              "and the black line is left to the reader's own text colour");
    }

    // Words a panel is painted over stay under it; the line under the panel is words
    // and the panel a picture.
    if (const QString path = fixture(directory, QStringLiteral("graphic.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the panel fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("graphic.docx"));
        const WordExportReport report = exportToWord(document, file);
        check(report.ok && report.imagePages == 0, QStringLiteral("it exports as a page of its own (%1)").arg(report.error));
        const QByteArray xml = documentXmlOf(file);
        check(xml.contains("XYZPDF a line of its own, under the panel"), "the line under the panel is words");
        check(!xml.contains("words on a panel"), "and the words the panel covers are not written over it");
        check(xml.contains("<w:drawing>"), "the panel and its marks are in the document");
    }

    // A link is a link.
    if (const QString path = fixture(directory, QStringLiteral("links.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the links fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("links.docx"));
        check(exportToWord(document, file).ok, "it exports");
        const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
        const QByteArray xml = parts.value(QStringLiteral("word/document.xml")).content;
        check(xml.contains("<w:hyperlink r:id=") && parts.value(QStringLiteral("word/_rels/document.xml.rels"))
                                                         .content.contains("https://example.com/xyzpdf"),
              "a web link goes to its address");
        check(xml.contains("<w:hyperlink w:anchor=\"page2\"") && xml.contains("w:name=\"page2\""),
              "and a jump inside the document goes to its page");
    }

    // A blank page is a picture of paper; a page whose text is turned is a picture.
    for (const QString name : {QStringLiteral("blank.pdf"), QStringLiteral("rotated.pdf")}) {
        if (const QString path = fixture(directory, name); !path.isEmpty()) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready, name + QStringLiteral(" opens"));
            QTemporaryDir output;
            const WordExportReport report = exportToWord(document, output.filePath(QStringLiteral("page.docx")));
            check(report.ok && report.pages == 1 && report.imagePages == 1,
                  QStringLiteral("%1 goes in as the picture of its page (%2 pictures)").arg(name).arg(report.imagePages));
        }
    }

    // A stop asked for before the export starts leaves no file.
    if (const QString path = fixture(directory, QStringLiteral("dense.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the dense fixture opens");
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("stopped.docx"));
        WordExportOptions options;
        options.cancel = QSharedPointer<QAtomicInt>::create(1);
        const WordExportReport refused = exportToWord(document, file, options);
        check(refused.cancelled && !QFileInfo::exists(file), "an export stopped before it starts writes nothing");
        const WordExportReport whole = exportToWord(document, file);
        check(whole.ok && whole.paragraphs >= 2, QStringLiteral("and the same export unstopped writes its pages (%1)")
                                                     .arg(whole.paragraphs));
    }

    // A stop that lands mid-export: noticed, reported, and no half file. The two
    // numbers are printed because which one is interesting depends on the machine.
    if (const QString path = fixture(directory, QStringLiteral("many-pages.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the forty-page fixture opens");
        QTemporaryDir measure;
        QElapsedTimer clock;
        clock.start();
        const WordExportReport full = exportToWord(document, measure.filePath(QStringLiteral("whole.docx")));
        const qint64 fullMs = qMax(qint64(1), clock.elapsed());
        check(full.ok && full.pages == 40, QStringLiteral("a whole export of it writes forty pages (%1 ms)").arg(fullMs));
        QTemporaryDir output;
        const QString file = output.filePath(QStringLiteral("stopped-midway.docx"));
        auto flag = QSharedPointer<QAtomicInt>::create(0);
        const qint64 armAfter = qMax(qint64(1), fullMs / 4);
        QFuture<void> stopper = QtConcurrent::run([flag, armAfter] {
            QThread::msleep(armAfter);
            flag->storeRelaxed(1);
        });
        WordExportOptions options;
        options.cancel = flag;
        QElapsedTimer part;
        part.start();
        const WordExportReport report = exportToWord(document, file, options);
        const qint64 taken = part.elapsed();
        stopper.waitForFinished();
        std::cout << "MEASURE export stop: whole " << fullMs << " ms, stopped after " << taken << " ms, armed at "
                  << armAfter << " ms\n";
        check(report.cancelled && !QFileInfo::exists(file), "a stop armed mid-export is reported and writes nothing");
    }

    if (failures == 0) {
        std::cout << "a PDF comes out as a Word document that looks like it\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
