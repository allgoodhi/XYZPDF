// A PDF read out as a Word document: the paragraphs, and the file itself.
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
// The paragraph rules are tested on boxes with no PDF anywhere near them. They are
// rules about where text sits on a page, and a page of made-up boxes is the only
// way to say what the rule is meant to be without also saying what PDFium happens
// to do with a fixture -- which is a different thing, tested at the end.

#include "../src/document/pagemetrics.h"
#include "../src/document/pdfiumbackend.h"
#include "../src/export/docxwriter.h"
#include "../src/export/wordexport.h"
#include "../src/export/zipwriter.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QThread>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QXmlStreamReader>

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

QString fixture(const QString &directory, const QString &name)
{
    return directory + QLatin1Char('/') + name;
}

TextLine line(double x, double y, double width, double height, const QString &text)
{
    return TextLine{QRectF(x, y, width, height), text};
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

// The first value of an attribute in a piece of XML, or an empty string.
//
// A parser would be the right answer for XML in general; this is for one
// attribute of one element written by a program whose output the test is checking
// one number at a time.
QString attribute(const QByteArray &xml, const QByteArray &name)
{
    const QByteArray needle = name + "=\"";
    const int at = xml.indexOf(needle);
    if (at < 0)
        return {};
    const int start = at + needle.size();
    const int end = xml.indexOf('"', start);
    if (end < 0)
        return {};
    return QString::fromLatin1(xml.mid(start, end - start));
}

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

        // The data is behind the local header, which repeats the name and may
        // carry an extra field of its own.
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");

    // ---- where one paragraph ends and the next begins ----
    //
    // Boxes only: a body line is 12 points high here and the text reaches x=500,
    // which is what the rules are written against.
    {
        check(paragraphsFromLines({}).isEmpty(), "no lines, no paragraphs");

        // Full lines running into each other are one paragraph, joined with
        // spaces because the text is not CJK.
        const QVector<TextLine> continuing = {
            line(100, 100, 400, 12, "The first line reaches the margin"),
            line(100, 114, 400, 12, "and so does the second,"),
            line(100, 128, 200, 12, "and this one ends it."),
        };
        const QVector<Paragraph> one = paragraphsFromLines(continuing);
        check(one.size() == 1, QStringLiteral("lines that run on are one paragraph (%1)")
                                   .arg(one.size()));
        check(one.value(0).text()
                  == QStringLiteral("The first line reaches the margin and so does the second, "
                                    "and this one ends it."),
              QStringLiteral("joined with spaces (got \"%1\")").arg(one.value(0).text()));

        // A gap between two lines, wider than the spacing the page uses, is a
        // break -- measured against that spacing, which needs a page's worth of
        // lines before it means anything: four ordinary steps and then one that
        // is three times as big.
        const QVector<TextLine> gapped = {
            line(100, 100, 400, 12, "A line that reaches the margin"),
            line(100, 114, 400, 12, "and its continuation,"),
            line(100, 128, 400, 12, "and another one,"),
            line(100, 142, 400, 12, "and one more of them,"),
            line(100, 188, 400, 12, "and a line after a visible gap."),
        };
        check(paragraphsFromLines(gapped).size() == 2,
              QStringLiteral("a gap between lines is a break (%1 paragraphs)")
                  .arg(paragraphsFromLines(gapped).size()));

        // A line that ends well short of the margin ends its paragraph, which is
        // what makes the next line a new one rather than a continuation.
        const QVector<TextLine> shortEnding = {
            line(100, 100, 400, 12, "A full line of the paragraph"),
            line(100, 114, 90, 12, "and its end."),
            line(100, 128, 400, 12, "A new paragraph starts here."),
        };
        const QVector<Paragraph> two = paragraphsFromLines(shortEnding);
        check(two.size() == 2, QStringLiteral("a line ending early ends its paragraph (%1)")
                                   .arg(two.size()));
        check(two.value(1).text() == QStringLiteral("A new paragraph starts here."),
              QStringLiteral("and the next line begins the following one (got \"%1\")")
                  .arg(two.value(1).text()));

        // And a line that starts further in than its paragraph did is a new one.
        const QVector<TextLine> indented = {
            line(100, 100, 400, 12, "A paragraph that begins at the margin"),
            line(100, 114, 400, 12, "and carries on."),
            line(140, 128, 300, 12, "An indented line starts a new paragraph."),
        };
        check(paragraphsFromLines(indented).size() == 2, "an indented line is a new paragraph");

        // CJK, where a line break has no space in it: one added here would show
        // up in the middle of a Chinese sentence.
        const QVector<TextLine> chinese = {
            line(100, 100, 400, 12, QStringLiteral("这是一段中文，换行的时候")),
            line(100, 114, 400, 12, QStringLiteral("不应该多出一个空格。")),
        };
        const QVector<Paragraph> joined = paragraphsFromLines(chinese);
        check(joined.size() == 1, "Chinese lines that run on are one paragraph");
        check(joined.value(0).text() == QStringLiteral("这是一段中文，换行的时候不应该多出一个空格。"),
              QStringLiteral("with no space put between them (got \"%1\")")
                  .arg(joined.value(0).text()));
    }

    // ---- and where each paragraph sits ----
    //
    // The layout of a conversion, read out of the same boxes: a heading centred in
    // the column, a list set in from the margin, the two-character first-line
    // indent Chinese prose is set with, and the room left above a paragraph. All of
    // it is measurement -- the column here is x=100 to x=500, the way this file's
    // other paragraph tests are written.
    {
        // An ordinary paragraph is left alone: no alignment, no indents, no space.
        // That matters as much as the rest of it -- a conversion that wrote
        // paragraph properties into every paragraph would be imposing an idea of
        // how text should look on a document that has its own.
        const QVector<Paragraph> plain = paragraphsFromLines({
            line(100, 100, 400, 12, "A paragraph that begins at the margin"),
            line(100, 114, 400, 12, "and carries on to the end of the column,"),
            line(100, 128, 200, 12, "ending here."),
        });
        check(plain.size() == 1 && plain.value(0).align == ParagraphAlign::Left
                  && plain.value(0).indentTwips == 0 && plain.value(0).firstLineTwips == 0
                  && plain.value(0).spaceBeforeTwips == 0,
              QStringLiteral("an ordinary paragraph carries no layout of its own (align %1, "
                             "indent %2, first line %3, space %4)")
                  .arg(int(plain.value(0).align))
                  .arg(plain.value(0).indentTwips)
                  .arg(plain.value(0).firstLineTwips)
                  .arg(plain.value(0).spaceBeforeTwips));

        // A heading centred in the column, over text that is not: 200 wide in a
        // 400-wide column, and its middle on the column's middle.
        const QVector<Paragraph> centred = paragraphsFromLines({
            line(200, 100, 200, 12, "A heading centred in the column"),
            line(100, 128, 400, 12, "and the text under it, reaching both margins,"),
            line(100, 142, 300, 12, "which is what body text does."),
        });
        check(centred.size() == 2,
              QStringLiteral("a centred heading is its own paragraph (%1)").arg(centred.size()));
        if (centred.size() == 2) {
            check(centred.value(0).align == ParagraphAlign::Center,
                  "and it is centred, because it is centred");
            check(centred.value(0).indentTwips == 0 && centred.value(0).firstLineTwips == 0,
                  "with no indent to push it off centre");
            check(centred.value(1).align == ParagraphAlign::Left,
                  "while the text under it is set from the margin");
        }

        // A block set in from the margin: 24 points in, which is two characters of
        // a 12-point font, and what a list of items looks like.
        const QVector<Paragraph> indented = paragraphsFromLines({
            line(100, 100, 400, 12, "A paragraph that begins at the margin"),
            line(100, 114, 400, 12, "and carries on to the end of the column,"),
            line(124, 142, 376, 12, "An item set in from the margin,"),
            line(124, 156, 376, 12, "and its second line."),
        });
        check(indented.size() == 2, QStringLiteral("an indented block is its own paragraph (%1)")
                                        .arg(indented.size()));
        if (indented.size() == 2) {
            check(indented.value(1).indentTwips == 480,
                  QStringLiteral("set in by the 24 points it is set in by (%1 twips)")
                      .arg(indented.value(1).indentTwips));
            check(indented.value(1).firstLineTwips == 0,
                  "with no first-line indent on top of it");
        }

        // The first-line indent, which is the opposite: the paragraph *starts*
        // further in and its body comes back to the margin. Every Chinese paragraph
        // is set this way, and reading it as two paragraphs is the difference
        // between a page of prose and a page of fragments.
        const QVector<Paragraph> firstLine = paragraphsFromLines({
            line(124, 100, 376, 12, QStringLiteral("首行缩进两字，这是中文段落的常态，")),
            line(100, 114, 400, 12, QStringLiteral("其余各行顶格排到右边界为止，")),
            line(100, 128, 400, 12, QStringLiteral("应该算作同一段。")),
        });
        check(firstLine.size() == 1,
              QStringLiteral("a first-line indent is one paragraph, not two (%1)")
                  .arg(firstLine.size()));
        if (!firstLine.isEmpty()) {
            check(firstLine.value(0).firstLineTwips == 480,
                  QStringLiteral("with the indent written down as a first-line indent (%1 twips)")
                      .arg(firstLine.value(0).firstLineTwips));
            check(firstLine.value(0).indentTwips == 0,
                  "and not as an indent of the whole paragraph");
        }

        // And the room above a paragraph, which is the space before it: 24 points
        // more than this page's own line spacing of 14.
        const QVector<Paragraph> spaced = paragraphsFromLines({
            line(100, 100, 400, 12, "A paragraph at the top,"),
            line(100, 114, 400, 12, "with a second line to it."),
            line(100, 152, 400, 12, "A paragraph with room left above it,"),
            line(100, 166, 200, 12, "and a line to finish."),
        });
        check(spaced.size() == 2, QStringLiteral("two paragraphs (%1)").arg(spaced.size()));
        if (spaced.size() == 2) {
            check(spaced.value(0).spaceBeforeTwips == 0,
                  QStringLiteral("the first of them has nothing above it (%1)")
                      .arg(spaced.value(0).spaceBeforeTwips));
            check(spaced.value(1).spaceBeforeTwips == 480,
                  QStringLiteral("and the second has the room it was given (%1 twips)")
                      .arg(spaced.value(1).spaceBeforeTwips));
        }
    }

    // ---- and how a paragraph writes that layout out ----
    //
    // The paragraph properties, in the order the schema requires of them, and only
    // when there is something to say: a paragraph with no layout of its own must
    // come out with no <w:pPr> at all, so that ordinary body text follows whatever
    // the reader's own Word is set to.
    {
        DocxWriter plain;
        plain.addParagraph(QStringLiteral("ordinary text"));
        const QByteArray plainXml = plain.toBytes();
        check(!plainXml.contains("<w:pPr>"),
              "a paragraph with no layout writes no paragraph properties");
        check(plainXml.contains("<w:p><w:r><w:t xml:space=\"preserve\">ordinary text</w:t>"),
              "and is otherwise exactly what it was");

        DocxWriter laid;
        DocxLayout layout;
        layout.align = DocxLayout::Align::Center;
        layout.indentTwips = 480;
        layout.firstLineTwips = 240;
        layout.spaceBeforeTwips = 120;
        laid.addParagraph(QVector<DocxRun>{{QStringLiteral("a heading"), QColor()}}, false, layout);
        const QByteArray laidXml = laid.toBytes();
        check(laidXml.contains("<w:spacing w:before=\"120\"/>"), "the room above it is written");
        check(laidXml.contains("<w:ind w:left=\"480\" w:firstLine=\"240\"/>"),
              "the indents are written together, as one element");
        check(laidXml.contains("<w:jc w:val=\"center\"/>"), "and the centring");
        // The order is not taste: the schema fixes it, and Word rejects a document
        // whose paragraph properties are in any other order.
        check(laidXml.indexOf("<w:spacing") < laidXml.indexOf("<w:ind")
                  && laidXml.indexOf("<w:ind") < laidXml.indexOf("<w:jc"),
              "in the order the schema requires of them (spacing, ind, jc)");

        DocxLayout centred;
        centred.align = DocxLayout::Align::Center;
        DocxWriter onlyCentre;
        onlyCentre.addParagraph(QVector<DocxRun>{{QStringLiteral("a centred line"), QColor()}}, false,
                                centred);
        const QByteArray centredXml = onlyCentre.toBytes();
        check(centredXml.contains("<w:jc w:val=\"center\"/>")
                  && !centredXml.contains("<w:ind") && !centredXml.contains("<w:spacing"),
              "a paragraph that is only centred writes only that");

        DocxTable table;
        table.columnWidthsPoints = {80.0, 120.0};
        table.cells.resize(2);
        for (auto &row : table.cells)
            row.resize(2);
        table.cells[0][0].append(QVector<DocxRun>{{QStringLiteral("top-left"), QColor()}});
        table.cells[1][1].append(QVector<DocxRun>{{QStringLiteral("bottom-right"), QColor()}});
        DocxWriter tabular;
        tabular.addTable(table);
        const QByteArray tableXml = tabular.toBytes();
        check(tabular.tableCount() == 1 && tableXml.contains("<w:tbl>")
                  && tableXml.count("<w:tr>") == 2 && tableXml.count("<w:tc>") == 4,
              "a two-by-two PDF grid becomes a real Word table");
        check(tableXml.contains("top-left") && tableXml.contains("bottom-right"),
              "and its cell text remains text rather than a picture");
    }

    // ---- the archive, which is the half Word has to be able to open ----
    {
        ZipWriter zip;
        zip.addFile(QStringLiteral("hello.txt"), QByteArrayLiteral("123456789"));
        zip.addFile(QStringLiteral("中文.txt"), QByteArrayLiteral("你好"));

        const QHash<QString, ZipEntry> parts = unpackZip(zip.toBytes());
        check(parts.size() == 2, QStringLiteral("both entries are in the archive (%1)")
                                     .arg(parts.size()));

        // The CRC of this string is published by the format's own documentation.
        // Checking it is what says the checksum in the file is the checksum an
        // unpacker will be checking, rather than a number this program made up.
        const quint32 expectedCrc = 0xCBF43926u;
        check(parts.value(QStringLiteral("hello.txt")).crc == expectedCrc,
              QStringLiteral("with the checksum the format gives for those bytes (got %1)")
                  .arg(parts.value(QStringLiteral("hello.txt")).crc, 8, 16, QLatin1Char('0')));
        check(parts.value(QStringLiteral("hello.txt")).content == QByteArrayLiteral("123456789"),
              "and the bytes that went in come back out");

        // A name that is not ASCII, read back as the UTF-8 it was written as.
        check(parts.contains(QStringLiteral("中文.txt")), "a name that is not ASCII survives");
        check(parts.value(QStringLiteral("中文.txt")).content == QByteArray("你好"),
              "with its content");
    }

    // ---- the document, which is the other half ----
    {
        DocxWriter writer;
        writer.addParagraph(QStringLiteral("First paragraph."));
        writer.addParagraph(QStringLiteral("Second page, & <tagged> \"quoted\"."), true);
        check(writer.paragraphCount() == 2, "two paragraphs were added");

        const QHash<QString, ZipEntry> parts = unpackZip(writer.toBytes());
        check(parts.contains(QStringLiteral("[Content_Types].xml")),
              "the content types are in the file");
        check(parts.contains(QStringLiteral("_rels/.rels")), "and the package relationship");
        check(parts.contains(QStringLiteral("word/document.xml")), "and the document itself");

        const QByteArray xml = parts.value(QStringLiteral("word/document.xml")).content;
        check(xml.contains("First paragraph."), "with the first paragraph in it");
        check(xml.contains("<w:br w:type=\"page\"/>"), "a page break where one was asked for");
        // Escaped rather than written raw: text out of a PDF can contain any of
        // these, and one unescaped ampersand is a file Word will not open.
        check(xml.contains("&amp; &lt;tagged&gt; &quot;quoted&quot;."),
              QStringLiteral("with the markup characters of the text escaped"));
        check(!xml.contains("<tagged>"), "and none of that text left as markup");
    }

    // ---- characters XML has no way to write down ----
    //
    // A PDF's text layer can contain anything, and some of what it contains cannot
    // be in an XML file at all: the control characters below space, the two
    // noncharacters at the end of the basic plane, and half of a surrogate pair.
    // A document with one of those in it is a document Word refuses to open --
    // which is how a real manual behaved, its line breaks encoded as #xFFFE.
    {
        DocxWriter odd;
        const QString text = QStringLiteral("before") + QChar(0xFFFE) + QChar(0xFFFF)
                             + QChar(0x0007) + QChar(0xD800) + QStringLiteral("after");
        odd.addParagraph(text);
        const QByteArray xml = unpackZip(odd.toBytes()).value(QStringLiteral("word/document.xml"))
                                   .content;

        // The bytes each of those would be written as, spelled out rather than
        // asked of Qt: QString::toUtf8() of a lone half is the encoder's business,
        // and a needle that came back empty would make this check pass by asking
        // whether the document contains nothing at all.
        const QVector<QByteArray> forbidden = {
            QByteArrayLiteral("\xEF\xBF\xBE"), // #xFFFE
            QByteArrayLiteral("\xEF\xBF\xBF"), // #xFFFF
            QByteArrayLiteral("\x07"),         // a character below space
            QByteArrayLiteral("\xEF\xBF\xBD"), // a lone half, as UTF-8 writes it
            QByteArrayLiteral("\xED\xA0\x80"), // .. or as WTF-8 keeps it
        };
        QByteArray found;
        for (const QByteArray &needle : forbidden) {
            if (xml.contains(needle))
                found = needle.toHex();
        }
        check(found.isEmpty(),
              QStringLiteral("no character XML cannot carry is written (%1)")
                  .arg(QString::fromLatin1(found)));
        check(xml.contains("beforeafter"),
              QStringLiteral("and the text around it is all still there"));

        // And the whole part parses, which is the thing that actually matters:
        // #xFFFE and #xFFFF are not characters an XML document can hold at all, so
        // a file with one in it is a file Word refuses to open rather than a file
        // with an odd character in it. A real manual came out that way once, its
        // line breaks encoded as #xFFFE.
        QXmlStreamReader reader(xml);
        while (!reader.atEnd())
            reader.readNext();
        check(!reader.hasError(),
              QStringLiteral("and it is well-formed XML (%1)").arg(reader.errorString()));

        // A pair of them, on the other hand, is an ordinary character and goes
        // across whole.
        DocxWriter paired;
        const QString smile = QString::fromUcs4(U"\U0001F600");
        check(smile.size() == 2, "the test's character is a surrogate pair");
        paired.addParagraph(smile);
        const QByteArray withPair = unpackZip(paired.toBytes())
                                        .value(QStringLiteral("word/document.xml"))
                                        .content;
        check(withPair.contains(smile.toUtf8()), "a surrogate pair survives as itself");
    }

    // ---- a real document, from the fixture to the file ----
    {
        const QString path = fixture(directory, QStringLiteral("text.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the text fixture opens");

            // The pairing of text and boxes, which is what the whole conversion
            // rests on: a line's box turned into a range, and the range read back
            // as the text of that line.
            const QVector<TextLine> lines = linesOfPage(document, 0);
            check(!lines.isEmpty(), "a page of text has lines");
            bool found = false;
            for (const TextLine &one : lines) {
                if (one.text.contains(QStringLiteral("XYZPDF")))
                    found = true;
            }
            check(found, QStringLiteral("and the text in them is the text of the page (%1 lines)")
                             .arg(lines.size()));

            QTemporaryDir output;
            check(output.isValid(), "there is somewhere to write the export");
            const QString file = output.filePath(QStringLiteral("export.docx"));

            const WordExportReport report = exportToWord(document, file);
            check(report.ok, QStringLiteral("the export writes the file (%1)").arg(report.error));
            check(report.pages == 2, QStringLiteral("both pages of the fixture (%1)")
                                         .arg(report.pages));
            check(report.emptyPages == 0, QStringLiteral("with none of them empty (%1)")
                                              .arg(report.emptyPages));
            check(report.paragraphs >= 2, QStringLiteral("and paragraphs to show for it (%1)")
                                              .arg(report.paragraphs));
            // A document whose pages all have text gets no pictures at all: the
            // image path is for pages that have nothing to read, not a fallback
            // for pages that do.
            check(report.imagePages == 0,
                  QStringLiteral("a document of text stays text (%1 pictures)")
                      .arg(report.imagePages));
            check(report.path == file, "and says where it put it");
            check(QFileInfo::exists(file) && QFileInfo(file).size() > 0,
                  "the file is on disk and is not empty");

            const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
            check(parts.contains(QStringLiteral("word/document.xml")),
                  "and what it wrote is a docx");
            check(parts.value(QStringLiteral("word/document.xml"))
                      .content.contains("XYZPDF"),
                  "with the page's own text in it");
        }
    }

    // ---- a page with no text at all: a picture of a page ----
    //
    // Nothing here reads text off an image, so the page goes in as what it is.
    // A conversion that dropped the scanned pages of a document, or that produced
    // an empty file and called it done, would be a conversion the reader has to
    // notice the gaps in.
    {
        const QString path = fixture(directory, QStringLiteral("picture.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the picture fixture opens");
            check(document.text(0).trimmed().isEmpty(), "and has no text layer to read");

            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("picture.docx"));
            const WordExportReport report = exportToWord(document, file);
            check(report.ok, QStringLiteral("the export writes it (%1)").arg(report.error));
            check(report.pages == 1 && report.imagePages == 1,
                  QStringLiteral("one page, put in as a picture (%1 written, %2 pictures)")
                      .arg(report.pages)
                      .arg(report.imagePages));
            check(report.paragraphs == 0, "with no paragraphs, because there was no text");

            const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
            check(parts.contains(QStringLiteral("word/media/image1.jpeg"))
                      || parts.contains(QStringLiteral("word/media/image1.png")),
                  "the image is a part of the document");
            const QByteArray xml = parts.value(QStringLiteral("word/document.xml")).content;
            check(xml.contains("<w:drawing>"), "referred to by a drawing in the document");
            check(parts.contains(QStringLiteral("word/_rels/document.xml.rels")),
                  "and by a relationship, which is how a reader finds it");

            const QStringList names = parts.keys();
            QString imageName;
            for (const QString &name : names) {
                if (name.startsWith(QStringLiteral("word/media/")))
                    imageName = name;
            }
            const QImage picture = QImage::fromData(parts.value(imageName).content);
            check(!picture.isNull(), QStringLiteral("and the picture decodes (%1)").arg(imageName));

            // The fixture draws a filled red rectangle over the upper half of the
            // page. A picture that decoded is not the same as a picture with the
            // page in it: the whole point of this path is that the ink survives.
            if (!picture.isNull()) {
                const QColor inside = picture.pixelColor(picture.width() / 2, picture.height() / 4);
                const QColor outside =
                    picture.pixelColor(picture.width() / 2, picture.height() * 9 / 10);
                check(inside.red() > 150 && inside.green() < 110 && inside.blue() < 110,
                      QStringLiteral("the picture has the page's ink in it (%1,%2,%3)")
                          .arg(inside.red())
                          .arg(inside.green())
                          .arg(inside.blue()));
                check(outside.red() > 230 && outside.green() > 230 && outside.blue() > 230,
                      QStringLiteral("and the rest of the page is the paper it was drawn on "
                                     "(%1,%2,%3)")
                          .arg(outside.red())
                          .arg(outside.green())
                          .arg(outside.blue()));
            }

            // The picture is the shape of the page it came from, not of the page
            // it was scaled into: a page image is a page, and one that has been
            // stretched is a page that has been distorted.
            const double cx = attribute(xml, "cx").toDouble();
            const double cy = attribute(xml, "cy").toDouble();
            const QSizeF page = document.pageSize(0);
            const double wanted = page.width() / page.height();
            check(cx > 0 && cy > 0 && std::fabs(cx / cy - wanted) < 0.01,
                  QStringLiteral("with the page's own shape (%1 against %2)")
                      .arg(cx / cy)
                      .arg(wanted));
        }
    }

    // ---- one line in pieces, and lines that are spaced widely ----
    //
    // Both of these are from a real report, and both came out wrong in the same
    // conversion: the boxes a page comes back in are not its lines, and the space
    // between lines is not measured in line heights. A forty-one-line page came
    // out as forty-one paragraphs, with a sentence broken into one word to a
    // paragraph where its line had been drawn piece by piece.
    {
        const QString path = fixture(directory, QStringLiteral("runs.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the piecemeal fixture opens");
            check(document.textRuns(0).size() >= 2,
                  QStringLiteral("the backend gives its one line in pieces (%1 boxes)")
                      .arg(document.textRuns(0).size()));

            const QVector<TextLine> lines = linesOfPage(document, 0);
            check(lines.size() == 1,
                  QStringLiteral("which are one line and not %1").arg(lines.size()));
            const QString text = lines.value(0).text;
            check(text.contains(QStringLiteral("piece one"))
                      && text.contains(QStringLiteral("and piece two")),
                  QStringLiteral("with both of its pieces in it (got \"%1\")").arg(text));
            check(paragraphsFromLines(lines).size() == 1,
                  "and it is one paragraph, not one per piece");
        }
    }
    {
        const QString path = fixture(directory, QStringLiteral("spaced.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the widely spaced fixture opens");

            const QVector<TextLine> lines = linesOfPage(document, 0);
            check(lines.size() == 3, QStringLiteral("three lines (%1)").arg(lines.size()));
            const QVector<Paragraph> paragraphs = paragraphsFromLines(lines);
            check(paragraphs.size() == 2,
                  QStringLiteral("are two paragraphs, not three (%1)").arg(paragraphs.size()));
            check(paragraphs.value(0).text().contains(QStringLiteral("wraps onto a second line")),
                  QStringLiteral("the wrapped pair joined into one (got \"%1\")")
                      .arg(paragraphs.value(0).text().left(60)));
        }
    }

    // ---- a page that has words and is not mostly words ----
    //
    // A certificate: a coloured panel, a letterhead, a row of logos -- and a text
    // layer, so nothing calls it a scan. Exporting its words alone leaves a list
    // of lines where the document was, which is what "the conversion looks poor"
    // means for a page like this. The two halves of that are the measurement --
    // how much of what is drawn is not the words -- and the decision to put the
    // page in as the page.
    {
        // The measurement first, on three pages made here: words on paper, the
        // same words on a pale panel, and the same words on a panel with
        // something dark drawn on it -- a certificate, without its text layer.
        QImage written(600, 200, QImage::Format_RGB32);
        written.fill(Qt::white);
        QImage panelled(600, 200, QImage::Format_RGB32);
        panelled.fill(Qt::white);
        QImage letterheaded(600, 200, QImage::Format_RGB32);
        letterheaded.fill(Qt::white);
        {
            QPainter onPanel(&panelled);
            onPanel.fillRect(QRect(20, 40, 560, 120), QColor(217, 217, 242));
            QPainter onLetterhead(&letterheaded);
            onLetterhead.fillRect(QRect(20, 40, 560, 120), QColor(217, 217, 242));
            onLetterhead.fillRect(QRect(20, 10, 300, 24), Qt::black);       // a letterhead
            onLetterhead.fillRect(QRect(60, 170, 80, 24), QColor(40, 40, 120)); // a mark
        }
        for (QImage *page : {&written, &panelled, &letterheaded}) {
            // A bar of ink inside the line box, standing in for the words: this
            // test has no GUI application behind it, and drawing a glyph would
            // need the font database that only one brings.
            QPainter painter(page);
            painter.fillRect(QRect(40, 88, 380, 12), Qt::black);
        }
        // The line the words sit on. Only boxes are needed to weigh a page: the
        // text of a line is not what makes it heavy.
        const QVector<QRectF> lines = {QRectF(40, 80, 500, 30)};
        const PageInk asWords = inkOutsideLines(written, lines, 1.0);
        const PageInk asPanel = inkOutsideLines(panelled, lines, 1.0);
        const PageInk asLetterhead = inkOutsideLines(letterheaded, lines, 1.0);
        check(asWords.markedShare < 0.03 && asWords.drawnShare < 0.03,
              QStringLiteral("a line of text on paper is nearly all text (%1 marked, %2 drawn)")
                  .arg(asWords.markedShare)
                  .arg(asWords.drawnShare));
        check(asPanel.markedShare > 0.4,
              QStringLiteral("a pale panel under the words is most of that page (%1)")
                  .arg(asPanel.markedShare));
        check(asPanel.drawnShare < 0.03,
              QStringLiteral("and is still a page of text: a background is not a picture (%1)")
                  .arg(asPanel.drawnShare));
        check(asLetterhead.drawnShare > 0.03,
              QStringLiteral("but ink drawn outside the words is (%1)")
                  .arg(asLetterhead.drawnShare));

        const QString path = fixture(directory, QStringLiteral("graphic.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the panel fixture opens");
            check(!document.text(0).trimmed().isEmpty(), "and it does have words on it");

            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("graphic.docx"));
            const WordExportReport report = exportToWord(document, file);
            check(report.ok, QStringLiteral("it exports (%1)").arg(report.error));
            // The page has words of its own below the drawing. Those remain text,
            // while the panel and letterhead remain pictures, so a mixed page is
            // not needlessly flattened into a photograph.
            check(report.imagePages == 0,
                  QStringLiteral("the page itself is not a picture (%1)").arg(report.imagePages));
            check(report.paragraphs == 1,
                  QStringLiteral("the words outside the drawing stay editable (%1)")
                      .arg(report.paragraphs));
            check(report.figures >= 1,
                  QStringLiteral("the drawing is retained as a picture (%1)")
                      .arg(report.figures));

            const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
            QStringList pictures;
            for (const QString &name : parts.keys()) {
                if (name.startsWith(QStringLiteral("word/media/")))
                    pictures.append(name);
            }
            check(!pictures.isEmpty(),
                  QStringLiteral("each drawn part of the page is a part of the document (%1)")
                      .arg(pictures.size()));

            // The panel, which is the whole of what a text-only conversion loses,
            // is still present in one of the extracted images.
            bool foundPanel = false;
            bool allDecoded = true;
            for (const QString &name : std::as_const(pictures)) {
                const QImage picture = QImage::fromData(parts.value(name).content);
                if (picture.isNull()) {
                    allDecoded = false;
                    continue;
                }
                const QColor colour = picture.pixelColor(picture.width() / 2, picture.height() / 4);
                if (colour.red() > 190 && colour.blue() > 190 && colour.blue() >= colour.red())
                    foundPanel = true;
            }
            check(allDecoded, "and every one of them decodes");
            check(foundPanel, "one of which still has the panel in it");

            // Both forms exist: page prose through a text run, and the panel as a
            // drawing placed in the document's reading order.
            const QByteArray xml = parts.value(QStringLiteral("word/document.xml")).content;
            check(xml.contains("XYZPDF a line of its own"),
                  "the page's own words are written as editable text");
            check(xml.contains("<w:drawing>"), "alongside the retained drawing");
        }
    }

    // ---- a stop is honoured, and no half file is left ----
    //
    // The reading of a page is two character queries per line and that is where the
    // time of an export goes: a page of a real manual is nine seconds. A stop that
    // is only looked at between pages is therefore a button that does nothing for
    // nine seconds -- which is what the reader of exactly that manual reported, in
    // those words. So the flag is looked at between lines, and the file is written
    // only at the end.
    //
    // What can still be pinned here, and what cannot. The per-line check is asserted
    // two ways that do not depend on the clock: a stop asked for before the read
    // takes none of the page, and a stop never asked for takes all of it. The
    // mid-page case used to be driven by a thread that slept for a quarter of the
    // page's own read time, and it no longer is: this fixture's seventy lines read
    // in about a millisecond, which is less than the granularity a second thread can
    // be woken at, so the stop lands after the page has finished -- and an assertion
    // that a timer can always lose is one that passes on a loop that never looked at
    // the flag (the control shape this project has been caught in four times). The
    // between-pages leg below keeps a real window because forty pages are long
    // enough to interrupt, and its two numbers are printed rather than judged.
    {
        const QString path = fixture(directory, QStringLiteral("dense.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the dense fixture opens");

            QElapsedTimer whole;
            whole.start();
            const QVector<TextLine> all = linesOfPage(document, 0);
            const qint64 wholeTime = whole.elapsed();
            check(all.size() >= 60,
                  QStringLiteral("its first page has its seventy lines (%1, %2 ms)")
                      .arg(all.size())
                      .arg(wholeTime));

            // A stop asked for before the read begins. The loop looks at the flag at
            // the top of every line, so this takes none of the page -- and the same
            // document, unread flag, takes all of it. Those two together are what can
            // be said about the check without a stopwatch.
            QAtomicInt stopped{1};
            PdfiumBackend cold;
            check(cold.load(path, QString()) == Document::Status::Ready,
                  "the dense fixture opens again for the stopped read");
            const QVector<TextLine> none = linesOfPage(cold, 0, &stopped);
            check(none.isEmpty(),
                  QStringLiteral("a stop asked for before the read takes none of the page (%1)")
                      .arg(none.size()));
            check(linesOfPage(cold, 0).size() == all.size(),
                  "and the same page reads whole when no stop was asked for");

            // The export itself stops without writing anything: the file is written at
            // the very end, so a stop that lands before it means the reader is left
            // with no file rather than half of one.
            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("stopped.docx"));
            WordExportOptions options;
            options.cancel = QSharedPointer<QAtomicInt>::create(1);
            const WordExportReport refused = exportToWord(document, file, options);
            check(refused.cancelled, "an export asked to stop before it starts reports itself cancelled");
            check(!QFileInfo::exists(file), "and wrote no file at all");
        }
    }

    // ---- a stop that lands while a document is being exported ----
    //
    // The between-pages half of the same promise, on a document long enough for a
    // second thread to be noticed: the forty-page fixture reads in something like a
    // tenth of a second, so a stop armed a quarter of the way through a measured
    // full export has a window it can actually hit. The two numbers are printed,
    // because which of them is interesting depends on the machine.
    {
        const QString path = fixture(directory, QStringLiteral("many-pages.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the forty-page fixture opens for the export stop");

            QTemporaryDir measure;
            const QString measureFile = measure.filePath(QStringLiteral("whole.docx"));
            QElapsedTimer clock;
            clock.start();
            const WordExportReport full = exportToWord(document, measureFile);
            const qint64 fullMs = qMax(qint64(1), clock.elapsed());
            check(full.ok && QFileInfo::exists(measureFile),
                  QStringLiteral("and a whole export of it writes a file (%1 ms)").arg(fullMs));

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

            std::cout << "MEASURE export stop: whole " << fullMs << " ms, stopped after "
                      << taken << " ms, armed at " << armAfter << " ms\n";
            check(report.cancelled, "a stop armed mid-export is noticed and reported");
            check(!QFileInfo::exists(file), "and no half document was written");
            check(taken < fullMs,
                  QStringLiteral("and it stopped before the whole export would have finished (%1 ms "
                                 "against %2)")
                      .arg(taken)
                      .arg(fullMs));
        }
    }

    // ---- a ruled PDF grid becomes editable Word cells ----
    {
        const QString path = fixture(directory, QStringLiteral("table.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the ruled table fixture opens");
            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("table.docx"));
            const WordExportReport report = exportToWord(document, file);
            check(report.ok && report.tables == 1 && report.imagePages == 0,
                  QStringLiteral("its grid becomes one editable table (%1 tables, %2 images)")
                      .arg(report.tables).arg(report.imagePages));
            const QByteArray xml = unpackZip(readAll(file)).value(QStringLiteral("word/document.xml"))
                                       .content;
            check(xml.count("<w:tr>") == 2 && xml.count("<w:tc>") == 4,
                  "with its two rows and two columns");
            check(xml.contains("top left") && xml.contains("bottom right"),
                  "and text is placed in cells rather than a table screenshot");
        }
    }

    // ---- a text page and a picture page in one document ----
    //
    // The shape of most documents that need this at all: a typed page and a
    // scanned one, which have to come out in order, in one file, one as words and
    // one as a picture, with a page break between them.
    {
        const QString path = fixture(directory, QStringLiteral("mixed.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the mixed fixture opens");

            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("mixed.docx"));

            // And it says how far along it is while it does it: one report per
            // page, ending at the last, and one before the first -- because a page
            // of a real manual is seconds of work, and a reader has to be offered a
            // way out while there is still time to take it. A page image is a
            // render too, and a reader watching a scanned document convert has to
            // be able to tell work happening from a program that has stopped.
            QVector<QPair<int, int>> reported;
            WordExportOptions options;
            options.progress = [&reported](int done, int total) {
                reported.append(QPair<int, int>(done, total));
            };

            const WordExportReport report = exportToWord(document, file, options);
            check(report.ok, QStringLiteral("it exports (%1)").arg(report.error));
            check(!reported.isEmpty() && reported.first() == QPair<int, int>(0, 2),
                  QStringLiteral("it says how big the job is before starting it (%1 of %2)")
                      .arg(reported.isEmpty() ? -1 : reported.first().first)
                      .arg(reported.isEmpty() ? -1 : reported.first().second));
            check(reported.size() == report.pages + 1,
                  QStringLiteral("one report per page after that (%1 for %2 pages)")
                      .arg(reported.size() - 1)
                      .arg(report.pages));
            check(!reported.isEmpty() && reported.last() == QPair<int, int>(2, 2),
                  QStringLiteral("the last of them at the end (got %1 of %2)")
                      .arg(reported.isEmpty() ? 0 : reported.last().first)
                      .arg(reported.isEmpty() ? 0 : reported.last().second));
            check(report.pages == 2, QStringLiteral("both of its pages (%1)").arg(report.pages));
            check(report.imagePages == 1,
                  QStringLiteral("one of them as a picture (%1)").arg(report.imagePages));
            check(report.paragraphs >= 1,
                  QStringLiteral("and the other as text (%1 paragraphs)").arg(report.paragraphs));

            const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
            const QByteArray xml = parts.value(QStringLiteral("word/document.xml")).content;
            check(xml.contains("XYZPDF typed cover"), "the typed page's text is in it");
            check(xml.contains("<w:drawing>"), "and the scanned page's picture");
            // The break between the two is what keeps them on separate pages: the
            // text runs first, and the picture starts the next page.
            check(xml.indexOf("XYZPDF typed cover") < xml.indexOf("<w:drawing>"),
                  "in the order they are in the document");
            check(xml.indexOf("<w:drawing>") > xml.indexOf("<w:br w:type=\"page\"/>"),
                  "with a page break in front of the picture");
        }
    }

    // ---- colour, which is in the page and not in the text layer ----
    //
    // PDFium hands out a page's characters and the boxes its lines occupy and
    // nothing about how any of it was set: no font, no weight, no colour. The
    // colour is not lost though -- it is in the picture, which is what the reader
    // is looking at -- so the export draws the page once and reads the ink back
    // out of it. Two halves to check: the reading, on an image that is known
    // because it was made here, and the writing, on a document that has colour in
    // it.
    {
        // A red line on white paper.
        QImage red(600, 60, QImage::Format_RGB32);
        red.fill(Qt::white);
        {
            QPainter painter(&red);
            painter.setPen(QColor(200, 30, 30));
            painter.drawLine(20, 30, 580, 30);
        }
        const QColor sampled = inkColor(red, QRectF(10, 20, 580, 20), 1.0);
        check(sampled.isValid() && sampled.red() > 150 && sampled.green() < 110
                  && sampled.blue() < 110,
              QStringLiteral("red ink is sampled as red (%1)").arg(sampled.name()));

        // Black and grey are not colours. Nearly every line of nearly every
        // document is one of them, and a Word file that wrote black into all of
        // them would have overridden the reader's theme to say nothing.
        QImage grey(600, 60, QImage::Format_RGB32);
        grey.fill(Qt::white);
        {
            QPainter painter(&grey);
            painter.setPen(QColor(40, 40, 40));
            painter.drawLine(20, 30, 580, 30);
        }
        check(!inkColor(grey, QRectF(10, 20, 580, 20), 1.0).isValid(),
              "black text is left to the reader's own colour");

        QImage paper(600, 60, QImage::Format_RGB32);
        paper.fill(Qt::white);
        check(!inkColor(paper, QRectF(10, 20, 580, 20), 1.0).isValid(),
              "and so is a box with no ink in it");
        check(!inkColor(QImage(), QRectF(0, 0, 10, 10), 1.0).isValid(),
              "as is a page that was never drawn");

        // A box that is dark all through is a filled shape, or a dark page with
        // light text on it. What is in it is not ink on paper, and the colour of it
        // is not the colour of any text -- so it is left alone rather than used.
        QImage filled(600, 60, QImage::Format_RGB32);
        filled.fill(QColor(20, 40, 200));
        check(!inkColor(filled, QRectF(10, 20, 580, 20), 1.0).isValid(),
              "a box that is dark all through is not a line of text");
    }
    {
        const QString path = fixture(directory, QStringLiteral("colored.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the coloured fixture opens");

            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("colored.docx"));
            const WordExportReport report = exportToWord(document, file);
            check(report.ok, QStringLiteral("it exports (%1)").arg(report.error));

            const QByteArray xml = unpackZip(readAll(file)).value(QStringLiteral("word/document.xml"))
                                       .content;
            const int colours = xml.count("<w:color w:val=\"");
            check(colours == 1,
                  QStringLiteral("only the coloured line carries a colour (%1)").arg(colours));

            const int at = xml.indexOf("<w:color w:val=\"");
            const QByteArray value = xml.mid(at + 16, 6);
            const QColor written(QStringLiteral("#") + QString::fromLatin1(value));
            check(written.isValid() && written.red() > 150 && written.green() < 110
                      && written.blue() < 110,
                  QStringLiteral("and it is the red that was on the page (%1)")
                      .arg(QString::fromLatin1(value)));

            // On the right line: the black one is written with no colour at all,
            // and the red one's colour comes after the black line's text.
            const int blackText = xml.indexOf(">XYZPDF</w:t>");
            check(blackText >= 0 && blackText < at,
                  QStringLiteral("attached to the line after the black one (%1 against %2)")
                      .arg(blackText)
                      .arg(at));
            check(xml.contains("XYZPDF red line"), "with the red line's text in it");
        }
    }

    // ---- a page that is blank and one that is text: no pictures either side ----
    {
        const QString path = fixture(directory, QStringLiteral("blank.pdf"));
        if (QFileInfo::exists(path)) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the blank fixture opens");
            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("blank.docx"));
            const WordExportReport report = exportToWord(document, file);
            // A blank page is white paper: it has no text, and it does have a page
            // to render, so it goes in as the picture it is.
            check(report.ok && report.pages == 1 && report.imagePages == 1,
                  QStringLiteral("a blank page is a picture of a blank page (%1 pages, %2 "
                                 "pictures, %3 empty)")
                      .arg(report.pages)
                      .arg(report.imagePages)
                      .arg(report.emptyPages));
        }
    }

    // ---- a page of several lines, read line by line ----
    //
    // The extraction, on a page that has more than one line to get wrong: each
    // one's box has to turn into that one's text and not its neighbour's.
    {
        const QString path = fixture(directory, QStringLiteral("lines.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the three-line fixture opens");
            const QVector<TextLine> lines = linesOfPage(document, 0);
            check(lines.size() == 3,
                  QStringLiteral("its three lines come out as three (%1)").arg(lines.size()));
            // One line's box turned into that line's text, not its neighbour's:
            // the fixture draws "XYZPDF first line", "second line here" and
            // "third line" on three lines of one page.
            const QStringList wanted = {QStringLiteral("first line"),
                                        QStringLiteral("second line"),
                                        QStringLiteral("third line")};
            for (int index = 0; index < lines.size() && index < wanted.size(); ++index) {
                check(lines.at(index).text.contains(wanted.at(index)),
                      QStringLiteral("line %1 is the one that says so (got \"%2\")")
                          .arg(index + 1)
                          .arg(lines.at(index).text));
            }
        }
    }

    if (failures == 0) {
        std::cout << "a PDF comes out as a Word document, paragraphs and all\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
