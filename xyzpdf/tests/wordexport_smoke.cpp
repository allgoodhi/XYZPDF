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
// way to say what the rule is meant to be without also saying what Qt PDF happens
// to do with a fixture -- which is a different thing, tested at the end.

#include "../src/document/qtpdfbackend.h"
#include "../src/export/docxwriter.h"
#include "../src/export/wordexport.h"
#include "../src/export/zipwriter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRectF>
#include <QString>
#include <QElapsedTimer>
#include <QTemporaryDir>

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
        const QVector<QString> one = paragraphsFromLines(continuing);
        check(one.size() == 1, QStringLiteral("lines that run on are one paragraph (%1)")
                                   .arg(one.size()));
        check(one.value(0)
                  == QStringLiteral("The first line reaches the margin and so does the second, "
                                    "and this one ends it."),
              QStringLiteral("joined with spaces (got \"%1\")").arg(one.value(0)));

        // A gap between two lines, wider than the line spacing, is a break.
        const QVector<TextLine> gapped = {
            line(100, 100, 400, 12, "A line that reaches the margin"),
            line(100, 114, 400, 12, "and its continuation,"),
            line(100, 160, 400, 12, "and a line after a visible gap."),
        };
        check(paragraphsFromLines(gapped).size() == 2, "a gap between lines is a break");

        // A line that ends well short of the margin ends its paragraph, which is
        // what makes the next line a new one rather than a continuation.
        const QVector<TextLine> shortEnding = {
            line(100, 100, 400, 12, "A full line of the paragraph"),
            line(100, 114, 90, 12, "and its end."),
            line(100, 128, 400, 12, "A new paragraph starts here."),
        };
        const QVector<QString> two = paragraphsFromLines(shortEnding);
        check(two.size() == 2, QStringLiteral("a line ending early ends its paragraph (%1)")
                                   .arg(two.size()));
        check(two.value(1) == QStringLiteral("A new paragraph starts here."),
              QStringLiteral("and the next line begins the following one (got \"%1\")")
                  .arg(two.value(1)));

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
        const QVector<QString> joined = paragraphsFromLines(chinese);
        check(joined.size() == 1, "Chinese lines that run on are one paragraph");
        check(joined.value(0) == QStringLiteral("这是一段中文，换行的时候不应该多出一个空格。"),
              QStringLiteral("with no space put between them (got \"%1\")").arg(joined.value(0)));
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

    // ---- a real document, from the fixture to the file ----
    {
        const QString path = fixture(directory, QStringLiteral("text.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            QtPdfBackend document;
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

    // ---- a page with no text at all ----
    //
    // A scan has nothing to export, and the honest answer is a sentence saying so
    // rather than an empty file: a reader who opens the result and finds it empty
    // has been told nothing about why.
    {
        const QString path = fixture(directory, QStringLiteral("blank.pdf"));
        if (!QFileInfo::exists(path)) {
            std::cerr << "FAIL: " << path.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            QtPdfBackend document;
            check(document.load(path, QString()) == Document::Status::Ready,
                  "the blank fixture opens");

            QTemporaryDir output;
            const QString file = output.filePath(QStringLiteral("blank.docx"));
            const WordExportReport report = exportToWord(document, file);
            check(!report.ok, "an export of a page with no text does not claim to have worked");
            check(report.pages == 0 && report.emptyPages == 1,
                  QStringLiteral("it says how many pages had nothing (%1 written, %2 empty)")
                      .arg(report.pages)
                      .arg(report.emptyPages));
            check(!report.error.isEmpty(), "and says why, in a sentence");
            check(!QFileInfo::exists(file), "and leaves no empty file behind");
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
            QtPdfBackend document;
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
