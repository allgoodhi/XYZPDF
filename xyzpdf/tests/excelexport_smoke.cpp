// A PDF's tables written out as a workbook: the values a cell becomes, the sheet
// names Excel allows, the package, and the whole path from a PDF to a file.
//
// ---- what the three halves are for ----
//
// The value rules (excelvalue.h) are a list, and a list is exactly the kind of
// thing a test pins one row at a time: every form the hand-off named gets a row,
// and the two shapes that must NOT become numbers -- a number with a leading zero
// and one too long for a double -- get rows too, because a rule with no
// counterexample is a rule nobody checked.
//
// The package is checked with a ZIP reader written here from the format, not with
// zipwriter.h: the same code that packed the file would agree with itself about a
// workbook Excel refuses to open. The reader's own trustworthiness is pinned by
// the one number the format publishes -- the CRC-32 of the string "123456789" --
// and every XML part is parsed, which is what "well-formed" means.
//
// The end-to-end half runs a real document: a table page drawn here with
// QPdfWriter -- a merged row, numbers, a picture in a cell -- and a rule-less text
// page beside it, plus the ruled table fixture. Whether the workbook *looks* like
// the PDF in a spreadsheet is a question only a spreadsheet can answer, and that
// loop is run by hand (export, open in WPS 表格, compare).
//
// ---- the controls ----
//
// Every claim is paired with the shape that must not satisfy it: a legal sheet name
// is left untouched where an illegal one is rewritten; a sheet with a one-cell
// "merge" has no merge element where a sheet with a span has; the same text twice
// is one shared string where two texts are two; the same style twice is one index
// where a different look is another; the option that is off skips the page that
// the option being on writes; a grid with no span writes no <mergeCells>. A test
// that only ever asked about the case it wanted would pass on code doing the
// opposite.

#include "../src/document/document.h"
#include "../src/document/pagecontent.h"
#include "../src/document/pdfiumbackend.h"
#include "../src/export/excelexport.h"
#include "../src/export/excelvalue.h"
#include "../src/export/xlsxwriter.h"
#include "../src/export/zipwriter.h"

#include <QAtomicInt>
#include <QBuffer>
#include <QColor>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTemporaryDir>
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

// ---- reading a workbook back ----

QString textOf(const QHash<QString, ZipEntry> &parts, const QString &name)
{
    return QString::fromUtf8(parts.value(name).content);
}

// The shared strings, in the order the file lists them: a cell that says t="s"
// carries an index into this list, so asking what a cell says needs the list.
QStringList sharedStringsOf(const QHash<QString, ZipEntry> &parts)
{
    QStringList found;
    static const QRegularExpression si(QStringLiteral("<si><t[^>]*>(.*?)</t></si>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const QString xml = textOf(parts, QStringLiteral("xl/sharedStrings.xml"));
    for (auto it = si.globalMatch(xml); it.hasNext();)
        found.append(it.next().captured(1));
    return found;
}

// One cell's element, attributes and value included, or empty when the reference
// is not on the sheet at all.
QString cellElement(const QHash<QString, ZipEntry> &parts, const QString &sheet,
                    const QString &reference)
{
    const QRegularExpression any(QStringLiteral("<c[^>]*r=\"%1\"[^>]*(?:/>|>(?:(?!</c>).)*</c>)")
                                     .arg(reference));
    const QRegularExpressionMatch match = any.match(textOf(parts, sheet));
    return match.hasMatch() ? match.captured(0) : QString();
}

// What a cell holds as the reader would see it: the words for a shared string, the
// stored value for a number, "<absent>" when nothing is there.
QString cellContent(const QHash<QString, ZipEntry> &parts, const QString &sheet,
                    const QString &reference)
{
    const QString cell = cellElement(parts, sheet, reference);
    if (cell.isEmpty())
        return QStringLiteral("<absent>");
    static const QRegularExpression value(QStringLiteral("<v>([^<]*)</v>"));
    const QRegularExpressionMatch found = value.match(cell);
    if (!found.hasMatch())
        return QStringLiteral("<empty>");
    if (cell.contains(QStringLiteral("t=\"s\""))) {
        const QStringList strings = sharedStringsOf(parts);
        const int index = found.captured(1).toInt();
        return index >= 0 && index < strings.size() ? strings.at(index)
                                                   : QStringLiteral("<out of range %1>").arg(index);
    }
    return found.captured(1);
}

// The address of the first cell whose content is `wanted`. An end-to-end test uses
// this rather than a fixed address because where the analysis puts a cell is the
// analysis' business; what a cell becomes is this export's.
QString findCell(const QHash<QString, ZipEntry> &parts, const QString &sheet, const QString &wanted)
{
    static const QRegularExpression refs(QStringLiteral("<c[^>]*r=\"([A-Z]+[0-9]+)\""));
    const QString xml = textOf(parts, sheet);
    for (auto it = refs.globalMatch(xml); it.hasNext();) {
        const QString reference = it.next().captured(1);
        if (cellContent(parts, sheet, reference) == wanted)
            return reference;
    }
    return QString();
}

QStringList numberFormatsOf(const QHash<QString, ZipEntry> &parts)
{
    QStringList found;
    static const QRegularExpression format(QStringLiteral("formatCode=\"([^\"]*)\""));
    const QString xml = textOf(parts, QStringLiteral("xl/styles.xml"));
    for (auto it = format.globalMatch(xml); it.hasNext();)
        found.append(it.next().captured(1));
    return found;
}

// The way the writer spells a stored number, so a reading can be compared with an
// expected value without re-deriving the formatting here.
QString numberText(double value)
{
    return QString::fromLatin1(QByteArray::number(value, 'g', 15));
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

// A second argument asks for the workbooks this run wrote to be left behind in a
// directory, which is what the by-eye round needs: tools/check_xlsx.py reads them
// with libraries that are not ours, and WPS opens them so a person can compare the
// pages. Without the argument nothing is kept, and a test run leaves no files.
QString keepDirectory;

void keepFile(const QString &file)
{
    if (keepDirectory.isEmpty())
        return;
    QFile::remove(keepDirectory + QLatin1Char('/') + QFileInfo(file).fileName());
    if (QFile::copy(file, keepDirectory + QLatin1Char('/') + QFileInfo(file).fileName()))
        std::cout << "KEPT " << QFileInfo(file).fileName().toStdString() << '\n';
    else
        std::cerr << "FAIL: " << file.toStdString() << " could not be copied out to keep\n";
}

// A relationship's target resolved against the part that names it, the way an
// unpacker does it: "../drawings/drawing1.xml" from xl/worksheets/_rels/ is
// xl/drawings/drawing1.xml.
QString resolveTarget(const QString &relsPart, const QString &target)
{
    QString base = QFileInfo(relsPart).path(); // "xl/worksheets/_rels"
    base.chop(QStringLiteral("_rels").size());
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QString walked = target;
    while (walked.startsWith(QStringLiteral("../"))) {
        walked.remove(0, 3);
        base = QFileInfo(base).path();
    }
    if (walked.startsWith(QLatin1Char('/')))
        return walked.mid(1);
    return base.isEmpty() ? walked : base + QLatin1Char('/') + walked;
}

// ---- a document made here, so the geometry is known to the digit ----
//
// One page of ruled table and one page of rule-less lines, drawn at 72 dpi so that
// a device unit is a point. The table has 2 columns (x 72/222/372) and 4 rows
// (y 100/140/180/220/260). The middle vertical rule starts at y=140 rather than at
// the top, so the first row is one cell across both columns -- a merged cell -- and
// nothing else about the grid says so:
//
//   row 1  "Heading across"           (one cell, across both columns)
//   row 2  "1,234.5" | "00123"        (a quantity, and an identifier)
//   row 3  "12%"     | a picture      (a percent, and a cell whose only ink is art)
//   row 4  "2026-10-25" | "Total"     (a date, and words)
void drawTablePage(QPainter &painter)
{
    QPen rule(Qt::black);
    rule.setWidthF(0.75);
    painter.setPen(rule);
    for (double y : {100.0, 140.0, 180.0, 220.0, 260.0})
        painter.drawLine(QPointF(72, y), QPointF(372, y));
    painter.drawLine(QPointF(72, 100), QPointF(72, 260));
    painter.drawLine(QPointF(372, 100), QPointF(372, 260));
    painter.drawLine(QPointF(222, 140), QPointF(222, 260)); // the first row has no middle rule

    painter.setPen(Qt::black);
    painter.drawText(QPointF(80, 130), QStringLiteral("Heading across"));
    painter.drawText(QPointF(80, 170), QStringLiteral("1,234.5"));
    painter.drawText(QPointF(230, 170), QStringLiteral("00123"));
    painter.setPen(QColor(200, 30, 30));
    painter.drawText(QPointF(80, 210), QStringLiteral("12%"));
    painter.setPen(Qt::black);
    painter.drawText(QPointF(80, 250), QStringLiteral("2026-10-25"));
    painter.drawText(QPointF(230, 250), QStringLiteral("Total"));

    QImage stamp(40, 20, QImage::Format_RGB32);
    stamp.fill(QColor(30, 90, 200));
    painter.drawImage(QRectF(232, 186, 80, 30), stamp);
}

bool writeSamplePdf(const QString &path)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(595, 842), QPageSize::Point, QString(), QPageSize::ExactMatch));
    writer.setResolution(72);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&writer);
    if (!painter.isActive())
        return false;
    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(10); // at 72 dpi a pixel is a point
    painter.setFont(font);
    drawTablePage(painter);

    // The second page: one line with a gap wide enough to be a tab stop (the
    // analysis calls a gap over 2.2 em a break), then a line with no gap. Two
    // gapped lines would be a borderless table, and this page must not become one.
    //
    // Each of these is one word, not a phrase: the analysis drops a space whose
    // next character has not advanced past half its own slot -- the rule that makes
    // a hand-spaced "1 9 8 3" one number -- and a space in a font Qt writes as a
    // subset does not always clear that bar, so a phrase here would come out as
    // "lineof" and the test would be pinning that instead of the row and column it
    // is here for.
    writer.newPage();
    painter.setPen(Qt::black);
    painter.drawText(QPointF(72, 110), QStringLiteral("Alpha"));
    painter.drawText(QPointF(300, 110), QStringLiteral("Beta"));
    painter.drawText(QPointF(72, 140), QStringLiteral("Gammaline"));
    painter.end();
    return QFileInfo::exists(path) && QFileInfo(path).size() > 0;
}

// The same ruled table on every one of `pages` pages, which is what a stop in the
// middle of a job needs: forty pages of prose have no table in them, and an export
// with nothing to write finishes before anybody could press cancel.
bool writeManyTablePdf(const QString &path, int pages)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(595, 842), QPageSize::Point, QString(), QPageSize::ExactMatch));
    writer.setResolution(72);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&writer);
    if (!painter.isActive())
        return false;
    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(10);
    painter.setFont(font);
    for (int page = 0; page < pages; ++page) {
        if (page > 0)
            writer.newPage();
        drawTablePage(painter);
    }
    painter.end();
    return QFileInfo::exists(path) && QFileInfo(path).size() > 0;
}

// The border XML that a cell's own style points at, which is the only way to ask
// "does this cell have a right edge" of a file: Excel keeps the answer in another
// part, indexed through the cell's style.
QString borderOfCell(const QHash<QString, ZipEntry> &parts, const QString &sheet,
                     const QString &reference)
{
    const QString cell = cellElement(parts, sheet, reference);
    static const QRegularExpression styleIndex(QStringLiteral("s=\"(\\d+)\""));
    const QRegularExpressionMatch styleMatch = styleIndex.match(cell);
    if (!styleMatch.hasMatch())
        return QString(); // style 0, the plain default with no lines on it
    const int wanted = styleMatch.captured(1).toInt();
    const QString styles = textOf(parts, QStringLiteral("xl/styles.xml"));
    // Two regions of the same part, asked for by name: the xf list is indexed by
    // position, and cellStyleXfs holds an xf of its own before cellXfs starts.
    const QRegularExpressionMatch xfs
        = QRegularExpression(QStringLiteral("<cellXfs[^>]*>(.*?)</cellXfs>"),
                             QRegularExpression::DotMatchesEverythingOption).match(styles);
    const QRegularExpressionMatch borders
        = QRegularExpression(QStringLiteral("<borders[^>]*>(.*?)</borders>"),
                             QRegularExpression::DotMatchesEverythingOption).match(styles);
    if (!xfs.hasMatch() || !borders.hasMatch())
        return QString();
    int borderId = 0;
    int seen = 0;
    static const QRegularExpression xf(QStringLiteral("<xf [^>]*"));
    for (auto it = xf.globalMatch(xfs.captured(1)); it.hasNext();) {
        const QRegularExpressionMatch found = it.next();
        if (seen++ != wanted)
            continue;
        const QRegularExpressionMatch id
            = QRegularExpression(QStringLiteral("borderId=\"(\\d+)\"")).match(found.captured(0));
        borderId = id.hasMatch() ? id.captured(1).toInt() : 0;
        break;
    }
    QStringList entries;
    static const QRegularExpression border(QStringLiteral("<border>.*?</border>|<border/>"),
                                           QRegularExpression::DotMatchesEverythingOption);
    for (auto it = border.globalMatch(borders.captured(1)); it.hasNext();) {
        entries.append(it.next().captured(0));
        if (entries.size() > borderId)
            break;
    }
    return borderId < entries.size() ? entries.at(borderId) : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    if (argc > 2)
        keepDirectory = QString::fromLocal8Bit(argv[2]);

    // ==== what a cell's words become ====

    struct Case
    {
        const char *text;
        ExcelValue::Kind kind;
        double number;
        const char *format;
    };
    const Case cases[] = {
        { "1,234.5", ExcelValue::Kind::Number, 1234.5, "#,##0.0" },
        { "-12", ExcelValue::Kind::Number, -12, "0" },
        { "12", ExcelValue::Kind::Number, 12, "0" },
        { "12.50", ExcelValue::Kind::Number, 12.5, "0.00" },
        { "+7", ExcelValue::Kind::Number, 7, "0" },
        // The bracketed negative keeps its brackets: the format's negative side
        // spells them, so the cell reads the way the PDF wrote it and still sums.
        { "(12)", ExcelValue::Kind::Number, -12, "0;(0)" },
        { "(1,234.5)", ExcelValue::Kind::Number, -1234.5, "#,##0.0;(#,##0.0)" },
        // A percent is stored as the fraction Excel expects, with a format that
        // multiplies it back for the display.
        { "12%", ExcelValue::Kind::Number, 0.12, "0%" },
        { "12.5%", ExcelValue::Kind::Number, 0.125, "0.0%" },
        // ... and "12,5" is a European decimal, which this program does not claim
        // to know: it is neither of the two groupings, so it stays words.
        { "12,5%", ExcelValue::Kind::Text, 0, "" },
        { "¥1,200", ExcelValue::Kind::Number, 1200, "\"¥\"#,##0" },
        { "$3.50", ExcelValue::Kind::Number, 3.5, "\"$\"0.00" },
        { "￥99", ExcelValue::Kind::Number, 99, "\"￥\"0" },
        { "2026-10-25", ExcelValue::Kind::Date, 0, "yyyy-mm-dd" },
        { "2026/10/25", ExcelValue::Kind::Date, 0, "yyyy/m/d" },
        { "2026年10月25日", ExcelValue::Kind::Date, 0, "yyyy\"年\"m\"月\"d\"日\"" },
        // The two counterexamples the hand-off asked for by name.
        { "00123", ExcelValue::Kind::Text, 0, "" },
        { "1234567890123456", ExcelValue::Kind::Text, 0, "" },
        // ... and the shapes either side of those two lines, so that the rules mean
        // "leading zero" and "sixteen digits" rather than "something near it".
        { "123456789012345", ExcelValue::Kind::Number, 123456789012345.0, "0" },
        { "0", ExcelValue::Kind::Number, 0, "0" },
        { "2026-13-40", ExcelValue::Kind::Text, 0, "" },
        { "2026-02-30", ExcelValue::Kind::Text, 0, "" },
        { "abc", ExcelValue::Kind::Text, 0, "" },
        { "12 34", ExcelValue::Kind::Text, 0, "" },
        { "007", ExcelValue::Kind::Text, 0, "" },
        { "12,34.5", ExcelValue::Kind::Text, 0, "" }, // grouping that is not threes
        { "1,234.5678", ExcelValue::Kind::Number, 1234.5678, "#,##0.0000" },
    };
    const char *kindName[] = { "text", "a number", "a date" };
    for (const Case &one : cases) {
        const QString source = QString::fromUtf8(one.text);
        const ExcelValue value = classifyExcelValue(source);
        const int kindIndex = one.kind == ExcelValue::Kind::Text
            ? 0
            : one.kind == ExcelValue::Kind::Number ? 1 : 2;
        const int gotIndex = value.kind == ExcelValue::Kind::Text
            ? 0
            : value.kind == ExcelValue::Kind::Number ? 1 : 2;
        if (value.kind != one.kind) {
            check(false, QStringLiteral("\"%1\" is %2, not %3")
                             .arg(source, QString::fromLatin1(kindName[gotIndex]),
                                  QString::fromLatin1(kindName[kindIndex])));
            continue;
        }
        if (one.kind == ExcelValue::Kind::Text) {
            // The counterexample half: a cell that stays text keeps its words
            // exactly, zeros and all -- the reader compares what they see.
            check(value.text == source,
                  QStringLiteral("\"%1\" stays text with its own words (%2)").arg(source, value.text));
            check(value.format.isEmpty(),
                  QStringLiteral("and carries no number format (%1: %2)").arg(source, value.format));
            continue;
        }
        check(value.format == QString::fromUtf8(one.format),
              QStringLiteral("\"%1\" gets the format \"%2\" (got \"%3\")")
                  .arg(source, QString::fromUtf8(one.format), value.format));
        if (one.kind == ExcelValue::Kind::Number) {
            check(std::fabs(value.number - one.number) < 1e-9,
                  QStringLiteral("\"%1\" stores as %2 (got %3)")
                      .arg(source).arg(one.number).arg(value.number));
        } else {
            const double serial = double(QDate(1899, 12, 30).daysTo(QDate(2026, 10, 25)));
            check(std::fabs(value.number - serial) < 0.001,
                  QStringLiteral("\"%1\" stores as the day Excel counts (%2, not %3)")
                      .arg(source).arg(value.number).arg(serial));
        }
    }
    // An empty cell is not a zero, and padding around a number does not hide it.
    check(classifyExcelValue(QStringLiteral("")).kind == ExcelValue::Kind::Text,
          "an empty cell stays text rather than becoming a zero");
    check(classifyExcelValue(QStringLiteral(" 42 ")).kind == ExcelValue::Kind::Number,
          "and padding around a number does not hide it");

    // ==== the names a worksheet may have ====

    {
        const QString table = sheetNameForTable(3, 2);
        check(table == QStringLiteral("第 3 页 表 2") || table == QStringLiteral("Page 3 Table 2"),
              QStringLiteral("a table's sheet is named for its page and its place (%1)").arg(table));
        const QString text = sheetNameForText(4);
        check(text == QStringLiteral("第 4 页 文字") || text == QStringLiteral("Page 4 Text"),
              QStringLiteral("and a text sheet for its page (%1)").arg(text));
        check(table != sheetNameForTable(1, 2), "the second table of a page is not the first one's name");

        const QString longName(45, QLatin1Char('x'));
        check(sanitisedSheetName(longName).size() == 31,
              QStringLiteral("a 45-character name is cut to Excel's 31 (%1)")
                  .arg(sanitisedSheetName(longName).size()));
        // The control: a name that is already legal comes back unchanged, so the
        // cut above is the limit doing it and not the function chewing everything.
        check(sanitisedSheetName(QStringLiteral("Page 3 Table 2")) == QStringLiteral("Page 3 Table 2"),
              "a legal name is left exactly as it was");
        check(sanitisedSheetName(longName) == QString(31, QLatin1Char('x')),
              "and cutting is what happened, not rebuilding");

        check(sanitisedSheetName(QStringLiteral("a[b]c:d*e?f/g\\h")) == QStringLiteral("a_b_c_d_e_f_g_h"),
              "each of the seven characters Excel forbids becomes an underscore");
        check(!sanitisedSheetName(QStringLiteral("Report: Q1/Q2")).contains(QLatin1Char(':')),
              "and none of them survives, whatever put it in the title");
        check(!sanitisedSheetName(QString(32, QLatin1Char('['))).contains(QLatin1Char('[')),
              "not even when the whole name is one");

        check(sanitisedSheetName(QStringLiteral("  spaced  ")) == QStringLiteral("spaced"),
              "the padding around a name is trimmed");
        check(sanitisedSheetName(QStringLiteral("'quoted'")) == QStringLiteral("quoted"),
              "a name in apostrophes loses them, which is what Excel asks for");
        check(sanitisedSheetName(QStringLiteral("Sheet")) == QStringLiteral("Sheet"),
              "and a plain name is not trimmed or relabelled on the way");
        // Truncation can leave one of the two things Excel refuses at the end, so
        // the rule runs after the cut as well as before it.
        const QString almost(29, QLatin1Char('a'));
        check(!sanitisedSheetName(almost + QStringLiteral("''")).endsWith(QLatin1Char('\'')),
              QStringLiteral("a name cut to 31 does not end in an apostrophe (%1)")
                  .arg(sanitisedSheetName(almost + QStringLiteral("''"))));
        check(sanitisedSheetName(QStringLiteral("   ")) == QStringLiteral("Sheet"),
              "and a name with nothing in it still names a sheet");
        check(sanitisedSheetName(QString(QChar(0x0001)) + QStringLiteral("x")) == QStringLiteral("x"),
              "a control character is dropped rather than written as itself");
    }

    // ==== the workbook, read back part by part ====

    {
        XlsxWriter writer;
        const int first = writer.addSheet(QStringLiteral("第 1 页 表 1"));
        const int second = writer.addSheet(QStringLiteral("第 1 页 表 1")); // the name twice
        check(second != first && writer.sheetCount() == 2, "a repeated sheet name is made distinct");
        check(writer.sheetName(second) != writer.sheetName(first),
              QStringLiteral("and the two sheets really are named apart (%1 / %2)")
                  .arg(writer.sheetName(first), writer.sheetName(second)));
        // The control for the collision: the first one keeps what it was given.
        check(writer.sheetName(first) == QStringLiteral("第 1 页 表 1"),
              QStringLiteral("while the first keeps the name it was given (%1)")
                  .arg(writer.sheetName(first)));

        // Styles: the same look twice is one entry, a different look is another.
        XlsxCellStyle plain;
        plain.fontSize = 10;
        plain.fontFamily = QStringLiteral("Arial");
        const int plainStyle = writer.styleFor(plain);
        check(writer.styleFor(plain) == plainStyle, "the same look twice is one style");
        XlsxCellStyle bold = plain;
        bold.bold = true;
        const int boldStyle = writer.styleFor(bold);
        check(boldStyle != plainStyle, "and a bold one is a style of its own");
        XlsxCellStyle shaded; // same font as the default, a fill and a wrap of its own
        shaded.wrap = true;
        shaded.fill = QColor(230, 230, 230);
        XlsxCellStyle centered;
        centered.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        XlsxCellStyle bordered;
        bordered.top = { true, 0.75, Qt::black };
        bordered.bottom = { true, 0.75, Qt::black };
        bordered.left = { true, 0.75, Qt::black };
        bordered.right = { true, 0.75, Qt::black };
        XlsxCellStyle money;
        money.numberFormat = QStringLiteral("#,##0.00");
        XlsxCellStyle red;
        red.fontColor = QColor(200, 30, 30);
        const int shadedStyle = writer.styleFor(shaded);
        const int centerStyle = writer.styleFor(centered);
        const int borderStyle = writer.styleFor(bordered);
        const int moneyStyle = writer.styleFor(money);
        const int redStyle = writer.styleFor(red);
        check(redStyle != 0 && plainStyle != 0 && borderStyle != moneyStyle,
              QStringLiteral("each look asked for got an index of its own (%1 %2 %3 %4 %5)")
                  .arg(plainStyle).arg(boldStyle).arg(shadedStyle).arg(centerStyle).arg(borderStyle));

        writer.setColumnWidth(first, 0, 150); // 200 px -> (200-5)/7 characters
        writer.setColumnWidth(first, 1, 60);
        writer.setRowHeight(first, 0, 40);
        writer.setRowHeight(first, 1, 18);
        writer.setText(first, 0, 0, QStringLiteral("Heading across"), borderStyle);
        writer.mergeCells(first, 0, 0, 0, 1);
        writer.setNumber(first, 1, 0, 1234.5, moneyStyle);
        writer.setText(first, 1, 1, QStringLiteral("00123"), plainStyle);
        // The same words twice: count says two cells point into the table,
        // uniqueCount says the table holds them once.
        writer.setText(first, 2, 1, QStringLiteral("00123"), plainStyle);
        writer.setNumber(first, 2, 0, 0.12, writer.styleFor(XlsxCellStyle{ .numberFormat = QStringLiteral("0%") }));
        writer.setText(second, 0, 1, QStringLiteral("Words"), plainStyle);
        // An empty cell with a look is still a cell; an empty cell without one is
        // not on the sheet at all.
        writer.setText(second, 0, 0, QString(), shadedStyle);
        writer.setText(second, 5, 5, QString(), 0);
        writer.mergeCells(second, 1, 1, 1, 1); // one cell is not a merge

        // The picture, and the two things a picture is not allowed to be.
        const QByteArray bytes = png(QSize(40, 28), QColor(178, 34, 34));
        writer.addImage(first, 2, 0, bytes, QStringLiteral("png"), 6, 4, 100, 70);
        writer.addImage(first, 2, 0, QByteArray(), QStringLiteral("png"), 0, 0, 10, 10);
        writer.addImage(first, 2, 0, bytes, QStringLiteral("tiff"), 0, 0, 10, 10);

        const QHash<QString, ZipEntry> parts = unpackZip(writer.toBytes());
        const QStringList names = parts.keys();

        // The minimal set, and nothing beside it.
        const QStringList expected = {
            QStringLiteral("[Content_Types].xml"),
            QStringLiteral("_rels/.rels"),
            QStringLiteral("xl/workbook.xml"),
            QStringLiteral("xl/_rels/workbook.xml.rels"),
            QStringLiteral("xl/styles.xml"),
            QStringLiteral("xl/sharedStrings.xml"),
            QStringLiteral("xl/worksheets/sheet1.xml"),
            QStringLiteral("xl/worksheets/sheet2.xml"),
            QStringLiteral("xl/worksheets/_rels/sheet1.xml.rels"),
            QStringLiteral("xl/drawings/drawing1.xml"),
            QStringLiteral("xl/drawings/_rels/drawing1.xml.rels"),
            QStringLiteral("xl/media/image1.png"),
        };
        QStringList missing;
        for (const QString &name : expected)
            if (!names.contains(name))
                missing.append(name);
        check(missing.isEmpty(),
              QStringLiteral("the package holds every part a workbook needs (missing %1)")
                  .arg(missing.join(QLatin1Char(','))));
        QStringList extra;
        for (const QString &name : names)
            if (!expected.contains(name))
                extra.append(name);
        check(extra.isEmpty(), QStringLiteral("and no part that is not needed (%1)")
                                   .arg(extra.join(QLatin1Char(','))));
        // The control: the second sheet was given no picture, so it has no drawing
        // of its own -- an empty drawing part is one Excel must open to find
        // nothing in.
        check(!names.contains(QStringLiteral("xl/worksheets/_rels/sheet2.xml.rels"))
                  && !names.contains(QStringLiteral("xl/drawings/drawing2.xml")),
              "a sheet with no picture gets no drawing part");

        // Every XML part parses, which is the one thing a hand-written writer
        // cannot be trusted to get right by agreeing with itself.
        int parsed = 0;
        for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
            if (!it.key().endsWith(QStringLiteral(".xml")) && !it.key().endsWith(QStringLiteral(".rels")))
                continue;
            QString error;
            check(wellFormed(it.value().content, &error),
                  QStringLiteral("%1 is well-formed XML (%2)").arg(it.key(), error));
            ++parsed;
        }
        check(parsed == 11, QStringLiteral("and every part was looked at (%1 parsed)").arg(parsed));

        // The archive itself: stored, and with a checksum that means the same to an
        // unpacker as it means here.
        ZipWriter zip;
        zip.addFile(QStringLiteral("hello.txt"), QByteArrayLiteral("123456789"));
        const QHash<QString, ZipEntry> probe = unpackZip(zip.toBytes());
        check(probe.value(QStringLiteral("hello.txt")).crc == 0xCBF43926u,
              "the checksum this reader reads is the one an unpacker checks");
        check(probe.value(QStringLiteral("hello.txt")).content == QByteArrayLiteral("123456789"),
              "and the bytes it hands back are the bytes that went in");

        // Relationships: every target a part names is a part that is in the archive.
        const QString workbook = textOf(parts, QStringLiteral("xl/workbook.xml"));
        static const QRegularExpression sheetRef(QStringLiteral("r:id=\"(rId\\d+)\""));
        const QString workbookRels = textOf(parts, QStringLiteral("xl/_rels/workbook.xml.rels"));
        int sheetReferences = 0;
        for (auto it = sheetRef.globalMatch(workbook); it.hasNext();) {
            const QString id = it.next().captured(1);
            ++sheetReferences;
            check(workbookRels.contains(QStringLiteral("Id=\"%1\"").arg(id)),
                  QStringLiteral("the workbook's %1 is a relationship the file defines").arg(id));
        }
        check(sheetReferences == 2,
              QStringLiteral("both sheets are listed, in order (%1 references)").arg(sheetReferences));
        static const QRegularExpression relTarget(QStringLiteral("Target=\"([^\"]+)\""));
        int relationshipTargets = 0;
        for (const QString &rels : { QStringLiteral("_rels/.rels"),
                                     QStringLiteral("xl/_rels/workbook.xml.rels"),
                                     QStringLiteral("xl/worksheets/_rels/sheet1.xml.rels"),
                                     QStringLiteral("xl/drawings/_rels/drawing1.xml.rels") }) {
            const QString body = textOf(parts, rels);
            check(!body.isEmpty(), QStringLiteral("%1 is in the archive").arg(rels));
            for (auto it = relTarget.globalMatch(body); it.hasNext();) {
                const QString part = resolveTarget(rels, it.next().captured(1));
                check(parts.contains(part),
                      QStringLiteral("%1 points at %2, which is in the archive").arg(rels, part));
                ++relationshipTargets;
            }
        }
        // Package, workbook, sheet and drawing: four relationship parts, and every
        // target they name was looked for. A loop that never found a Target= would
        // otherwise read exactly like a loop where all of them were right.
        check(relationshipTargets == 7,
              QStringLiteral("seven relationship targets checked (%1)").arg(relationshipTargets));
        // Content types: every override names a part that is there.
        static const QRegularExpression overrideName(QStringLiteral("PartName=\"([^\"]+)\""));
        int overrides = 0;
        for (auto it = overrideName.globalMatch(textOf(parts, QStringLiteral("[Content_Types].xml")));
             it.hasNext();) {
            QString part = it.next().captured(1);
            if (part.startsWith(QLatin1Char('/')))
                part.remove(0, 1);
            check(parts.contains(part),
                  QStringLiteral("[Content_Types].xml declares %1, which is in the archive").arg(part));
            ++overrides;
        }
        check(overrides == 6,
              QStringLiteral("workbook, styles, strings, two sheets and one drawing are declared (%1)")
                  .arg(overrides));
        check(textOf(parts, QStringLiteral("[Content_Types].xml")).contains(
                  QStringLiteral("Extension=\"jpeg\"")),
              "a JPEG media type is declared as well as a PNG one");

        // What the first sheet says.
        const QString sheet1 = QStringLiteral("xl/worksheets/sheet1.xml");
        const QString sheet1Xml = textOf(parts, sheet1);
        check(sheet1Xml.contains(QStringLiteral("<dimension ref=\"A1:B3\"/>")),
              QStringLiteral("the used range covers the cells, the merge and the picture (%1)")
                  .arg(QRegularExpression(QStringLiteral("<dimension ref=\"([^\"]+)\""))
                           .match(sheet1Xml)
                           .captured(1)));
        check(cellContent(parts, sheet1, QStringLiteral("A1")) == QStringLiteral("Heading across"),
              "the heading's words are in the cell they were put in");
        check(cellContent(parts, sheet1, QStringLiteral("A2")) == numberText(1234.5),
              QStringLiteral("a number is stored as one (%1)").arg(cellContent(parts, sheet1,
                                                                               QStringLiteral("A2"))));
        check(!cellElement(parts, sheet1, QStringLiteral("A2")).contains(QStringLiteral("t=\"s\"")),
              "and not as a string that happens to look like one");
        check(cellContent(parts, sheet1, QStringLiteral("B2")) == QStringLiteral("00123"),
              QStringLiteral("a number with a leading zero keeps its zero (%1)")
                  .arg(cellContent(parts, sheet1, QStringLiteral("B2"))));
        check(cellElement(parts, sheet1, QStringLiteral("B2")).contains(QStringLiteral("t=\"s\"")),
              "because it went in as text");
        check(cellContent(parts, sheet1, QStringLiteral("A3")) == numberText(0.12),
              QStringLiteral("a percent stores as the fraction (%1)")
                  .arg(cellContent(parts, sheet1, QStringLiteral("A3"))));
        check(sheet1Xml.contains(QStringLiteral("<mergeCell ref=\"A1:B1\"/>")),
              "the span is a merged range, not one cell and an empty one");
        check(cellElement(parts, sheet1, QStringLiteral("B1")).isEmpty(),
              "and the merged range's second cell is left to the merge");
        check(sheet1Xml.contains(QStringLiteral("width=\"27.86\"")),
              QStringLiteral("a 150-point column is 27.86 characters (%1)")
                  .arg(QRegularExpression(QStringLiteral("<col min=\"1\"[^/]*"))
                           .match(sheet1Xml)
                           .captured(0)));
        check(sheet1Xml.contains(QStringLiteral("<row r=\"1\" ht=\"40.00\" customHeight=\"1\"")),
              "and a 40-point row keeps its height");
        check(sheet1Xml.contains(QStringLiteral("<drawing r:id=\"rId1\"/>")),
              "the sheet points at its drawing");

        // The second sheet: the styled empty cell is there, the unstyled one is
        // not, and the one-cell merge wrote nothing.
        const QString sheet2 = QStringLiteral("xl/worksheets/sheet2.xml");
        const QString sheet2Xml = textOf(parts, sheet2);
        check(!sheet2Xml.contains(QStringLiteral("<mergeCell")),
              "a sheet with a one-cell 'merge' has no merge element at all");
        check(!cellElement(parts, sheet2, QStringLiteral("A1")).isEmpty(),
              "an empty cell with a look is still written");
        check(cellElement(parts, sheet2, QStringLiteral("A1")).contains(QStringLiteral("s=\"")),
              QStringLiteral("with its style index (%1)")
                  .arg(cellElement(parts, sheet2, QStringLiteral("A1"))));
        // The control for the line above: no words and no style is not a cell.
        check(cellElement(parts, sheet2, QStringLiteral("F6")).isEmpty(),
              "and an empty cell with no look is not on the sheet at all");
        check(cellElement(parts, sheet2, QStringLiteral("B1")).contains(QStringLiteral("s=\"")),
              "and a word with a style carries its style index too");
        check(sheet2Xml.contains(QStringLiteral("<dimension ref=\"A1:B1\"/>")),
              QStringLiteral("its used range is the cells that exist (%1)")
                  .arg(QRegularExpression(QStringLiteral("<dimension ref=\"([^\"]+)\""))
                           .match(sheet2Xml)
                           .captured(1)));

        // Styles: the formats, the fill, the bold, the colour, the borders, wrap.
        const QStringList formats = numberFormatsOf(parts);
        check(formats == QStringList{ QStringLiteral("#,##0.00"), QStringLiteral("0%") },
              QStringLiteral("two custom number formats, deduplicated (%1)").arg(formats.join(QLatin1Char(','))));
        const QString styles = textOf(parts, QStringLiteral("xl/styles.xml"));
        check(styles.contains(QStringLiteral("<b/>")), "a bold style is a bold font");
        check(styles.contains(QStringLiteral("rgb=\"FFC81E1E\"")), "and a red one a red font");
        check(styles.contains(QStringLiteral("patternType=\"solid\"")), "and a shaded one a solid fill");
        check(styles.contains(QStringLiteral("wrapText=\"1\"")), "and a wrapped one wraps");
        check(styles.contains(QStringLiteral("horizontal=\"center\" vertical=\"center\"")),
              "and a centered one is centered both ways");
        check(styles.contains(QStringLiteral("style=\"thin\"")), "the 0.75-point rules are thin borders");
        check(styles.count(QStringLiteral("<border>")) == 2,
              QStringLiteral("one bordered entry beside the plain one (%1)")
                  .arg(styles.count(QStringLiteral("<border>"))));
        check(styles.count(QStringLiteral("<font>")) == 4,
              QStringLiteral("four distinct fonts, not one per cell (%1)")
                  .arg(styles.count(QStringLiteral("<font>"))));
        check(styles.contains(QStringLiteral("numFmtId=\"164\"")),
              "the first custom format is Excel's 164, where custom formats start");
        check(styles.contains(QStringLiteral("<fill><patternFill patternType=\"gray125\"/></fill>")),
              "the two fills Excel expects are the first two");
        // The control for the deduplication: four uses of three distinct texts, so
        // a file that had uniqueCount == count had no table at all.
        const QString strings = textOf(parts, QStringLiteral("xl/sharedStrings.xml"));
        check(strings.contains(QStringLiteral("count=\"4\" uniqueCount=\"3\"")),
              QStringLiteral("three shared strings for four uses (%1)")
                  .arg(strings.left(120)));
        check(sharedStringsOf(parts).size() == 3,
              QStringLiteral("and the list really holds three (%1)").arg(sharedStringsOf(parts).size()));

        // The picture: anchored inside its cell, at its size, pointing at media
        // that is in the archive and is the bytes that went in.
        const QString drawing = textOf(parts, QStringLiteral("xl/drawings/drawing1.xml"));
        check(drawing.contains(QStringLiteral("<xdr:col>0</xdr:col><xdr:colOff>76200</xdr:colOff>")),
              "6 points in from its column is 76,200 EMU");
        check(drawing.contains(QStringLiteral("<xdr:row>2</xdr:row><xdr:rowOff>50800</xdr:rowOff>")),
              "and 4 points down from its row is 50,800");
        check(drawing.contains(QStringLiteral("cx=\"1270000\" cy=\"889000\"")),
              "100 by 70 points, in the units DrawingML counts in");
        check(textOf(parts, QStringLiteral("xl/drawings/_rels/drawing1.xml.rels"))
                  .contains(QStringLiteral("Target=\"../media/image1.png\"")),
              "the drawing's relationship names its media");
        check(parts.value(QStringLiteral("xl/media/image1.png")).content == bytes,
              "the media part is the bytes that were handed over, unchanged");
        // The control for the two rejected calls: one anchor, whatever the number
        // of calls made.
        check(drawing.count(QStringLiteral("<xdr:oneCellAnchor>")) == 1,
              QStringLiteral("an empty picture and an unknown type write nothing (%1 anchors)")
                  .arg(drawing.count(QStringLiteral("<xdr:oneCellAnchor>"))));

        // The same picture twice is one part, and both anchors still point at it.
        XlsxWriter again;
        const int one = again.addSheet(QStringLiteral("One"));
        again.addImage(one, 0, 0, bytes, QStringLiteral("png"), 0, 0, 10, 10);
        again.addImage(one, 1, 0, bytes, QStringLiteral("png"), 0, 0, 10, 10);
        const QHash<QString, ZipEntry> twice = unpackZip(again.toBytes());
        check(twice.contains(QStringLiteral("xl/media/image1.png"))
                  && !twice.contains(QStringLiteral("xl/media/image2.png")),
              "the same picture twice is stored once");
        check(textOf(twice, QStringLiteral("xl/drawings/drawing1.xml"))
                  .count(QStringLiteral("<xdr:oneCellAnchor>"))
              == 2,
              "and both anchors are still there");

        // A workbook with no styles asked for and no media is still a workbook: the
        // default style and the default string table exist whatever the caller did.
        XlsxWriter bare;
        const int only = bare.addSheet(QStringLiteral("Only"));
        bare.setText(only, 0, 0, QStringLiteral("a"));
        const QHash<QString, ZipEntry> bareParts = unpackZip(bare.toBytes());
        check(bareParts.value(QStringLiteral("xl/sharedStrings.xml")).content.contains("uniqueCount=\"1\""),
              QStringLiteral("one string, declared as one (%1)")
                  .arg(textOf(bareParts, QStringLiteral("xl/sharedStrings.xml")).left(160)));
        check(!bareParts.contains(QStringLiteral("xl/drawings/drawing1.xml")),
              "and no drawing without a picture");
    }

    // ==== from a page of a PDF to a workbook ====

    QTemporaryDir output;
    const QString sample = output.filePath(QStringLiteral("sample.pdf"));
    if (!writeSamplePdf(sample)) {
        std::cerr << "FAIL: the sample PDF could not be drawn here\n";
        ++failures;
    } else {
        PdfiumBackend document;
        check(document.load(sample, QString()) == Document::Status::Ready, "the sample opens");
        check(document.pageCount() == 2,
              QStringLiteral("and has the two pages that were drawn (%1)").arg(document.pageCount()));
        const PageContent content = pageContentOf(document, 0, true);
        check(content.ok && !content.paths.isEmpty() && content.chars.size() > 40,
              QStringLiteral("its rules and words are read (%1 paths, %2 characters)")
                  .arg(content.paths.size()).arg(content.chars.size()));

        // The default: the table page is written, the rule-less page is skipped.
        const QString workbook = output.filePath(QStringLiteral("sample.xlsx"));
        const ExcelExportReport skipped = exportToExcel(document, workbook);
        check(skipped.ok && skipped.tables == 1 && skipped.worksheets == 1,
              QStringLiteral("one table, one worksheet (%1 tables, %2 sheets: %3)")
                  .arg(skipped.tables).arg(skipped.worksheets).arg(skipped.error));
        check(skipped.pages == 2 && skipped.skippedPages == 1 && skipped.textSheets == 0,
              QStringLiteral("the rule-less page is skipped by default (%1 read, %2 skipped, %3 text)")
                  .arg(skipped.pages).arg(skipped.skippedPages).arg(skipped.textSheets));

        keepFile(sample);
        keepFile(workbook);
        const QHash<QString, ZipEntry> file = unpackZip(readAll(workbook));
        check(!file.isEmpty(), "and what it wrote is an archive");
        const QString sheet = QStringLiteral("xl/worksheets/sheet1.xml");
        const QString sheetXml = textOf(file, sheet);
        check(textOf(file, QStringLiteral("xl/workbook.xml")).contains(
                  QStringLiteral("<sheet name=\"第 1 页 表 1\"")),
              QStringLiteral("the sheet is named for its page and its place (%1)")
                  .arg(QRegularExpression(QStringLiteral("<sheet name=\"([^\"]*)\""))
                           .match(textOf(file, QStringLiteral("xl/workbook.xml")))
                           .captured(1)));
        check(sheetXml.contains(QStringLiteral("<mergeCell ref=\"A1:B1\"/>")),
              QStringLiteral("the row with no middle rule is one merged cell (%1)")
                  .arg(QRegularExpression(QStringLiteral("<mergeCell ref=\"([^\"]+)\""))
                           .match(sheetXml)
                           .captured(1)));
        // The frame of a merged range is spread over the cells it covers, with the
        // edges that face the inside of the range taken off each: the anchor keeps
        // its top and left and gives up its right, the covered cell keeps the right
        // edge the range needs and gives up the line that would cut through it.
        // Found by eye in WPS -- with only the anchor styled, the heading row's
        // right edge was simply missing from the page.
        const QString anchorBorder = borderOfCell(file, sheet, QStringLiteral("A1"));
        const QString coveredBorder = borderOfCell(file, sheet, QStringLiteral("B1"));
        std::cout << "MEASURE merged row borders: A1 " << anchorBorder.toStdString() << " ;; B1 "
                  << coveredBorder.toStdString() << '\n';
        check(anchorBorder.contains(QStringLiteral("<top"))
                  && !anchorBorder.contains(QStringLiteral("<right style=")),
              QStringLiteral("the anchor keeps its outside edges and drops the inner one (%1)")
                  .arg(anchorBorder));
        check(coveredBorder.contains(QStringLiteral("<right style="))
                  && !coveredBorder.contains(QStringLiteral("<left style=")),
              QStringLiteral("and the covered cell carries the range's right edge (%1)")
                  .arg(coveredBorder));
        // The PDF's words sit at the left of their cells, so a number has to be told
        // to stay there: with no alignment written, Excel puts numbers at the right.
        check(textOf(file, QStringLiteral("xl/styles.xml")).contains(QStringLiteral("horizontal=\"left\"")),
              "the left alignment the PDF shows is written, not left to the default");
        check(sheetXml.contains(QStringLiteral("<dimension ref=\"A1:B4\"/>")),
              QStringLiteral("four rows and two columns of grid (%1)")
                  .arg(QRegularExpression(QStringLiteral("<dimension ref=\"([^\"]+)\""))
                           .match(sheetXml)
                           .captured(1)));
        // Where each value went is the analysis' business; what it became is ours.
        check(findCell(file, sheet, numberText(1234.5)) != QString(),
              "the grouped number is stored as 1234.5");
        check(findCell(file, sheet, QStringLiteral("00123")) != QString(),
              "the identifier is stored as the words 00123");
        const QString moneyAddress = findCell(file, sheet, numberText(1234.5));
        const QString zeroAddress = findCell(file, sheet, QStringLiteral("00123"));
        if (!moneyAddress.isEmpty())
            check(!cellElement(file, sheet, moneyAddress).contains(QStringLiteral("t=\"s\"")),
                  "the first because it is a number");
        if (!zeroAddress.isEmpty())
            check(cellElement(file, sheet, zeroAddress).contains(QStringLiteral("t=\"s\"")),
                  "and the second because it is not");
        const double serial = double(QDate(1899, 12, 30).daysTo(QDate(2026, 10, 25)));
        check(findCell(file, sheet, numberText(serial)) != QString(),
              QStringLiteral("the date is stored as the day Excel counts (%1)")
                  .arg(numberText(serial)));
        check(findCell(file, sheet, numberText(0.12)) != QString(),
              "the percent is stored as the fraction");
        check(findCell(file, sheet, QStringLiteral("Total")) != QString(),
              "and the words that are neither stayed words");
        const QStringList fileFormats = numberFormatsOf(file);
        check(fileFormats.contains(QStringLiteral("#,##0.0")) && fileFormats.contains(QStringLiteral("0%"))
                  && fileFormats.contains(QStringLiteral("yyyy-mm-dd")),
              QStringLiteral("with the formats that show them as the PDF wrote them (%1)")
                  .arg(fileFormats.join(QLatin1Char(','))));
        check(file.contains(QStringLiteral("xl/media/image1.png"))
                  || file.contains(QStringLiteral("xl/media/image1.jpeg")),
              "the picture inside the cell came out with it");
        check(sheetXml.contains(QStringLiteral("<drawing r:id=\"rId1\"/>")),
              "the sheet points at the drawing that holds it");
        // The colour of the red line: printed rather than asserted, because whether
        // the round trip through PDFium and the analysis carries it is a question
        // the reading answers, and the number here is what the by-eye round in WPS
        // will be compared against.
        QStringList colours;
        static const QRegularExpression rgb(QStringLiteral("<color rgb=\"([0-9A-F]{8})\""));
        const QString fileStyles = textOf(file, QStringLiteral("xl/styles.xml"));
        for (auto it = rgb.globalMatch(fileStyles); it.hasNext();)
            colours.append(it.next().captured(1));
        std::cout << "MEASURE font colours in the sample workbook: " << colours.join(',').toStdString()
                  << '\n';

        // The same document with the text option on: the page that was skipped
        // becomes a sheet, its lines become rows, and the wide gap becomes a column.
        const QString withText = output.filePath(QStringLiteral("sample-text.xlsx"));
        ExcelExportOptions options;
        options.exportText = true;
        const ExcelExportReport whole = exportToExcel(document, withText, options);
        check(whole.ok && whole.worksheets == 2 && whole.textSheets == 1 && whole.skippedPages == 0,
              QStringLiteral("the option adds the page that was skipped (%1 sheets, %2 text, %3 skipped)")
                  .arg(whole.worksheets).arg(whole.textSheets).arg(whole.skippedPages));
        keepFile(withText);
        const QHash<QString, ZipEntry> both = unpackZip(readAll(withText));
        const QString workbookXml = textOf(both, QStringLiteral("xl/workbook.xml"));
        check(workbookXml.contains(QStringLiteral("<sheet name=\"第 2 页 文字\"")),
              QStringLiteral("under its page's own name (%1)")
                  .arg(QRegularExpression(QStringLiteral("<sheet name=\"[^\"]*\" sheetId=\"2\""))
                           .match(workbookXml)
                           .captured(0)));
        const QString textSheet = QStringLiteral("xl/worksheets/sheet2.xml");
        const QString textSheetXml = textOf(both, textSheet);
        QStringList textRows;
        static const QRegularExpression rowElement(
            QStringLiteral("<row[^>]*>(?:(?!</row>).)*</row>|<row[^>]*/>"),
            QRegularExpression::DotMatchesEverythingOption);
        for (auto it = rowElement.globalMatch(textSheetXml); it.hasNext();)
            textRows.append(it.next().captured(0));
        std::cout << "MEASURE text sheet rows: " << textRows.join(QStringLiteral(" ;; ")).toStdString()
                  << '\n';
        QStringList textStrings = sharedStringsOf(both);
        for (QString &one : textStrings)
            one.replace(QLatin1Char(' '), QStringLiteral("_"));
        std::cout << "MEASURE text sheet strings: " << textStrings.join(QStringLiteral(" | ")).toStdString()
                  << '\n';
        check(findCell(both, textSheet, QStringLiteral("Alpha")) != QString(),
              "the first line's first word is on the sheet");
        const QString alphaAddress = findCell(both, textSheet, QStringLiteral("Alpha"));
        const QString betaAddress = findCell(both, textSheet, QStringLiteral("Beta"));
        check(!alphaAddress.isEmpty() && !betaAddress.isEmpty()
                  && betaAddress.startsWith(QStringLiteral("B"))
                  && betaAddress.mid(1) == alphaAddress.mid(1),
              QStringLiteral("and the word across the gap is in the next column of the same row (%1 / %2)")
                  .arg(alphaAddress, betaAddress));
        check(findCell(both, textSheet, QStringLiteral("Gammaline")) != QString(),
              "and the next line is a row of its own, not a second cell");
        check(textSheetXml.contains(QStringLiteral("<row r=\"2\"><c r=\"A2\"")),
              QStringLiteral("in the second row's first column (%1)")
                  .arg(textRows.size()));
        // The control: the page that has a table keeps only its table -- no line of
        // the first page's prose is on the text sheet, and no word of the second
        // page's is on the table's.
        check(!sheetXml.contains(QStringLiteral("Alpha"))
                  && !textOf(both, textSheet).contains(QStringLiteral("Heading across")),
              "a page with a table does not also give its lines to the text sheet");
    }

    // ==== the fixtures, from the PDF to the workbook ====

    if (const QString path = fixture(directory, QStringLiteral("table.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the ruled table fixture opens");
        const QString file = output.filePath(QStringLiteral("table.xlsx"));
        const ExcelExportReport report = exportToExcel(document, file);
        check(report.ok && report.tables == 1 && report.worksheets == 1,
              QStringLiteral("its grid becomes one worksheet (%1 tables: %2)")
                  .arg(report.tables).arg(report.error));
        keepFile(file);
        const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
        const QString sheet = QStringLiteral("xl/worksheets/sheet1.xml");
        const QString sheetXml = textOf(parts, sheet);
        check(findCell(parts, sheet, QStringLiteral("top left")) == QStringLiteral("A1")
                  && findCell(parts, sheet, QStringLiteral("top right")) == QStringLiteral("B1"),
              QStringLiteral("the four cells hold the four labels, in their own places (%1 / %2)")
                  .arg(findCell(parts, sheet, QStringLiteral("top left")),
                       findCell(parts, sheet, QStringLiteral("top right"))));
        check(findCell(parts, sheet, QStringLiteral("bottom left")) == QStringLiteral("A2")
                  && findCell(parts, sheet, QStringLiteral("bottom right")) == QStringLiteral("B2"),
              QStringLiteral("in the next row (%1 / %2)")
                  .arg(findCell(parts, sheet, QStringLiteral("bottom left")),
                       findCell(parts, sheet, QStringLiteral("bottom right"))));
        check(!sheetXml.contains(QStringLiteral("<mergeCell")),
              "and a grid with no span has no merged range");
        check(sheetXml.contains(QStringLiteral("<dimension ref=\"A1:B2\"/>")),
              QStringLiteral("the used range is the grid (%1)")
                  .arg(QRegularExpression(QStringLiteral("<dimension ref=\"([^\"]+)\""))
                           .match(sheetXml)
                           .captured(1)));
        // The fixture draws its rules 1 point wide, which is Excel's "medium"; the
        // number is printed because which weight a PDF stroke becomes is a reading
        // of the writer's table, not something this page has to re-derive.
        QStringList borderWeights;
        static const QRegularExpression weight(QStringLiteral("style=\"(thin|medium|thick)\""));
        const QString fixtureStyles = textOf(parts, QStringLiteral("xl/styles.xml"));
        for (auto it = weight.globalMatch(fixtureStyles); it.hasNext();)
            borderWeights.append(it.next().captured(1));
        borderWeights.removeDuplicates();
        std::cout << "MEASURE border weights the fixture's rules became: "
                  << borderWeights.join(QLatin1Char(',')).toStdString() << '\n';
        check(!borderWeights.isEmpty(), "the PDF's rules become Excel's own borders");
        check(sheetXml.contains(QStringLiteral("s=\"")) || sheetXml.contains(QStringLiteral("<c r=\"A1\"/>")),
              QStringLiteral("and the cells carry their style index (%1)")
                  .arg(cellElement(parts, sheet, QStringLiteral("A1"))));
        for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
            if (!it.key().endsWith(QStringLiteral(".xml")) && !it.key().endsWith(QStringLiteral(".rels")))
                continue;
            QString error;
            check(wellFormed(it.value().content, &error),
                  QStringLiteral("the fixture's %1 is well-formed (%2)").arg(it.key(), error));
        }
    }

    // A document with no table anywhere: nothing to write, and it says so instead
    // of writing an empty workbook.
    if (const QString path = fixture(directory, QStringLiteral("text.pdf")); !path.isEmpty()) {
        PdfiumBackend document;
        check(document.load(path, QString()) == Document::Status::Ready, "the text fixture opens");
        const QString file = output.filePath(QStringLiteral("text.xlsx"));
        const ExcelExportReport none = exportToExcel(document, file);
        check(!none.ok && !none.cancelled && !none.error.isEmpty() && !QFileInfo::exists(file),
              QStringLiteral("no table is refused with a sentence, not an empty file (%1)").arg(none.error));
        // The control: the same document, the same call, with the option on -- so
        // the refusal above is the absence of a table, not a failure to read one.
        ExcelExportOptions options;
        options.exportText = true;
        const ExcelExportReport words = exportToExcel(document, file, options);
        check(words.ok && words.tables == 0 && words.textSheets == 2 && words.worksheets == 2,
              QStringLiteral("and the same pages write text sheets when asked (%1 of %2: %3)")
                  .arg(words.textSheets).arg(words.worksheets).arg(words.error));
        keepFile(file);
        const QHash<QString, ZipEntry> parts = unpackZip(readAll(file));
        check(findCell(parts, QStringLiteral("xl/worksheets/sheet1.xml"), QStringLiteral("XYZPDF one"))
                  != QString(),
              "with the pages' own words in them");
    }

    // A stop asked for before the export starts, and one that lands in the middle:
    // either way the report says it was stopped and no half workbook is left. The
    // document is sixteen pages of the same ruled table, because a job with nothing
    // to write finishes before anybody could press cancel.
    const QString many = output.filePath(QStringLiteral("many-tables.pdf"));
    const int manyPages = 16;
    if (!writeManyTablePdf(many, manyPages)) {
        std::cerr << "FAIL: the many-page sample could not be drawn here\n";
        ++failures;
    } else {
        PdfiumBackend document;
        check(document.load(many, QString()) == Document::Status::Ready, "the sixteen-page sample opens");
        check(document.pageCount() == manyPages,
              QStringLiteral("and has the pages that were drawn (%1)").arg(document.pageCount()));

        const QString file = output.filePath(QStringLiteral("stopped.xlsx"));
        ExcelExportOptions stopped;
        stopped.cancel = QSharedPointer<QAtomicInt>::create(1);
        const ExcelExportReport refused = exportToExcel(document, file, stopped);
        check(refused.cancelled && !refused.ok && !QFileInfo::exists(file),
              "an export stopped before it starts writes nothing");

        const QString midway = output.filePath(QStringLiteral("stopped-midway.xlsx"));
        auto flag = QSharedPointer<QAtomicInt>::create(0);
        int lastReported = 0;
        ExcelExportOptions options;
        options.cancel = flag;
        options.progress = [flag, &lastReported](int done, int) {
            lastReported = done;
            if (done == 10)
                flag->storeRelaxed(1);
        };
        const ExcelExportReport partway = exportToExcel(document, midway, options);
        std::cout << "MEASURE export stop: asked at page 10, last progress at page " << lastReported
                  << " of " << document.pageCount() << '\n';
        check(partway.cancelled && !QFileInfo::exists(midway),
              "a stop asked for mid-export is reported and leaves no file");
        check(lastReported >= 10 && lastReported <= 11,
              QStringLiteral("and it noticed at the next page, not at the end (%1)").arg(lastReported));

        // The control for both: unstopped, the same document does write -- all
        // sixteen tables, each on a sheet named for its own page.
        const QString wholeFile = output.filePath(QStringLiteral("whole.xlsx"));
        QVector<QPair<int, int>> reported;
        ExcelExportOptions whole;
        whole.progress = [&reported](int done, int total) { reported.append({ done, total }); };
        const ExcelExportReport full = exportToExcel(document, wholeFile, whole);
        check(!full.cancelled && full.ok && full.tables == manyPages && full.worksheets == manyPages,
              QStringLiteral("unstopped, the same pages make a file of sixteen sheets (%1 of %2: %3)")
                  .arg(full.tables).arg(full.worksheets).arg(full.error));
        keepFile(many);
        keepFile(wholeFile);
        const QHash<QString, ZipEntry> parts = unpackZip(readAll(wholeFile));
        const QString book = textOf(parts, QStringLiteral("xl/workbook.xml"));
        check(book.contains(QStringLiteral("<sheet name=\"第 1 页 表 1\""))
                  && book.contains(QStringLiteral("<sheet name=\"第 16 页 表 1\"")),
              QStringLiteral("and each one is named for its own page (%1)")
                  .arg(QRegularExpression(QStringLiteral("<sheet name=\"[^\"]*\" sheetId=\"16\""))
                           .match(book)
                           .captured(0)));
        // Progress names the size of the job at the start and its end at the end.
        check(!reported.isEmpty() && reported.first() == QPair<int, int>(0, manyPages)
                  && reported.last() == QPair<int, int>(manyPages, manyPages),
              QStringLiteral("it says how big the job is and when it is done (%1 reports)")
                  .arg(reported.size()));
    }

    // A job with nowhere to write is refused before any page is read.
    {
        PdfiumBackend document;
        if (const QString path = fixture(directory, QStringLiteral("table.pdf")); !path.isEmpty())
            document.load(path, QString());
        const ExcelExportReport nowhere = exportToExcel(document, QString());
        check(!nowhere.ok && !nowhere.error.isEmpty(),
              QStringLiteral("and a job with no file to write says so (%1)").arg(nowhere.error));
    }

    if (failures == 0) {
        std::cout << "a PDF's tables come out as a workbook\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
