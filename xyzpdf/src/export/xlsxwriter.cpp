#include "xlsxwriter.h"

#include "excelvalue.h"
#include "zipwriter.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Text as XML character data, the same rule the Word writer uses: a control
// character has no spelling in XML 1.0 at all, and one stray byte of it makes
// Excel refuse the whole file.
QByteArray xmlText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D)
            continue;
        if (u == 0xFFFE || u == 0xFFFF)
            continue;
        switch (u) {
        case '&':
            out += QLatin1String("&amp;");
            break;
        case '<':
            out += QLatin1String("&lt;");
            break;
        case '>':
            out += QLatin1String("&gt;");
            break;
        case '"':
            out += QLatin1String("&quot;");
            break;
        default:
            out += c;
        }
    }
    return out.toUtf8();
}

QByteArray number(qint64 value)
{
    return QByteArray::number(value);
}

// A cell reference: column 0 is A, 25 is Z, 26 is AA.
QByteArray columnName(int column)
{
    QByteArray name;
    for (++column; column > 0; column = (column - 1) / 26)
        name.prepend(char('A' + (column - 1) % 26));
    return name;
}

QByteArray cellReference(int row, int column)
{
    return columnName(column) + QByteArray::number(row + 1);
}

QByteArray colourHex(const QColor &colour)
{
    return QStringLiteral("%1%2%3%4")
        .arg(colour.alpha(), 2, 16, QLatin1Char('0'))
        .arg(colour.red(), 2, 16, QLatin1Char('0'))
        .arg(colour.green(), 2, 16, QLatin1Char('0'))
        .arg(colour.blue(), 2, 16, QLatin1Char('0'))
        .toUpper()
        .toLatin1();
}

// DrawingML measures in English Metric Units: 914,400 to the inch, so 12,700 to
// the point.
qint64 emu(double points)
{
    return qRound64(points * 12700.0);
}

// Excel writes column width in characters of the workbook's default font, which
// is where these two numbers come from: 64 pixels is the default width, the
// padding is 5 pixels and a digit is 7.
double columnCharacters(double points)
{
    const double pixels = points * 96.0 / 72.0;
    return qBound(1.0, (pixels - 5.0) / 7.0, 255.0);
}

const char *borderStyle(double width)
{
    if (width >= 1.5)
        return "thick";
    if (width >= 0.9)
        return "medium";
    return "thin";
}

const char *kMainNamespace = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const char *kRelNamespace = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

QByteArray relType(const char *leaf)
{
    return QByteArray("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + leaf;
}

struct FontSpec
{
    QString family;
    double size = 11;
    bool bold = false;
    bool italic = false;
    QColor colour;

    bool operator==(const FontSpec &other) const
    {
        return family == other.family && qAbs(size - other.size) < 0.01 && bold == other.bold
            && italic == other.italic && colour == other.colour;
    }
};

struct BorderSpec
{
    XlsxBorder top;
    XlsxBorder left;
    XlsxBorder bottom;
    XlsxBorder right;

    bool operator==(const BorderSpec &other) const
    {
        return top == other.top && left == other.left && bottom == other.bottom && right == other.right;
    }
};

// A font/fill/border/number-format index per style, resolved once for the whole
// workbook so identical looks share one entry (the requirement is that styles are
// deduplicated, and Excel's own files do the same).
struct StyleTables
{
    QVector<FontSpec> fonts;
    QVector<QColor> fills;           // index 0 is "none", 1 is Excel's gray125
    QVector<BorderSpec> borders;
    QVector<QString> numberFormats;  // index 0 is General (empty)
    QVector<int> fontOfStyle;
    QVector<int> fillOfStyle;
    QVector<int> borderOfStyle;
    QVector<int> formatOfStyle;
};

int indexOfFont(QVector<FontSpec> *fonts, const FontSpec &font)
{
    for (int i = 0; i < fonts->size(); ++i)
        if (fonts->at(i) == font)
            return i;
    fonts->append(font);
    return int(fonts->size()) - 1;
}

int indexOfBorder(QVector<BorderSpec> *borders, const BorderSpec &border)
{
    for (int i = 0; i < borders->size(); ++i)
        if (borders->at(i) == border)
            return i;
    borders->append(border);
    return int(borders->size()) - 1;
}

StyleTables styleTables(const QVector<XlsxCellStyle> &styles)
{
    StyleTables tables;
    // Excel expects these two fills to be the first two in every styles part: no
    // fill, and the gray125 pattern it uses for its own grid.
    tables.fills.append(QColor());
    tables.fills.append(QColor());
    tables.borders.append(BorderSpec());
    tables.numberFormats.append(QString());

    for (const XlsxCellStyle &style : styles) {
        FontSpec font;
        font.family = style.fontFamily.isEmpty() ? QStringLiteral("Calibri") : style.fontFamily;
        font.size = style.fontSize > 0 ? style.fontSize : 11;
        font.bold = style.bold;
        font.italic = style.italic;
        font.colour = style.fontColor;
        tables.fontOfStyle.append(indexOfFont(&tables.fonts, font));

        int fill = 0;
        if (style.fill.isValid()) {
            fill = -1;
            for (int i = 2; i < tables.fills.size(); ++i)
                if (tables.fills.at(i) == style.fill) {
                    fill = i;
                    break;
                }
            if (fill < 0) {
                tables.fills.append(style.fill);
                fill = int(tables.fills.size()) - 1;
            }
        }
        tables.fillOfStyle.append(fill);

        BorderSpec border;
        border.top = style.top;
        border.left = style.left;
        border.bottom = style.bottom;
        border.right = style.right;
        tables.borderOfStyle.append(indexOfBorder(&tables.borders, border));

        int format = 0;
        if (!style.numberFormat.isEmpty()) {
            format = -1;
            for (int i = 1; i < tables.numberFormats.size(); ++i)
                if (tables.numberFormats.at(i) == style.numberFormat) {
                    format = i;
                    break;
                }
            if (format < 0) {
                tables.numberFormats.append(style.numberFormat);
                format = int(tables.numberFormats.size()) - 1;
            }
        }
        tables.formatOfStyle.append(format);
    }
    return tables;
}

QByteArray borderEdge(const char *name, const XlsxBorder &edge)
{
    if (!edge.present)
        return QByteArray("<") + name + "/>";
    return QByteArray("<") + name + " style=\"" + borderStyle(edge.width) + "\"><color rgb=\""
        + colourHex(edge.color) + "\"/></" + name + ">";
}

} // namespace

XlsxWriter::XlsxWriter()
{
    // Index 0 is the default every cell uses unless it was given another, so it
    // has to exist even in a workbook where no style was ever asked for.
    styles.append(XlsxCellStyle());
}

int XlsxWriter::addSheet(const QString &name)
{
    QString wanted = sanitisedSheetName(name);
    QString unique = wanted;
    for (int suffix = 2; lowerSheetNames.contains(unique.toLower()); ++suffix) {
        // Excel refuses two sheets with the same name, ignoring case. A name that
        // collides is made distinct rather than dropped: a reader is owed a sheet
        // for every table the PDF had.
        const QString tail = QStringLiteral(" (%1)").arg(suffix);
        unique = wanted.left(31 - tail.size()) + tail;
    }
    Sheet sheet;
    sheet.name = unique;
    sheets.append(sheet);
    lowerSheetNames.insert(unique.toLower());
    return int(sheets.size()) - 1;
}

QString XlsxWriter::sheetName(int sheet) const
{
    return sheet >= 0 && sheet < sheets.size() ? sheets.at(sheet).name : QString();
}

int XlsxWriter::styleFor(const XlsxCellStyle &style)
{
    for (int i = 0; i < styles.size(); ++i)
        if (styles.at(i) == style)
            return i;
    styles.append(style);
    return int(styles.size()) - 1;
}

int XlsxWriter::sheetIndex(int sheet) const
{
    return sheet >= 0 && sheet < sheets.size() ? sheet : -1;
}

void XlsxWriter::setColumnWidth(int sheet, int column, double points)
{
    if (sheetIndex(sheet) < 0 || column < 0 || points <= 0)
        return;
    sheets[sheet].columnWidths.insert(column, points);
}

void XlsxWriter::setRowHeight(int sheet, int row, double points)
{
    if (sheetIndex(sheet) < 0 || row < 0 || points <= 0)
        return;
    sheets[sheet].rowHeights.insert(row, points);
}

void XlsxWriter::setText(int sheet, int row, int column, const QString &text, int style)
{
    if (sheetIndex(sheet) < 0 || row < 0 || column < 0)
        return;
    if (text.isEmpty() && style == 0)
        return;
    Cell cell;
    cell.used = true;
    cell.style = style;
    if (!text.isEmpty()) {
        cell.text = text;
        cell.stringId = sharedString(text);
    }
    sheets[sheet].rows[row].insert(column, cell);
}

void XlsxWriter::setNumber(int sheet, int row, int column, double number, int style)
{
    if (sheetIndex(sheet) < 0 || row < 0 || column < 0)
        return;
    Cell cell;
    cell.used = true;
    cell.isNumber = true;
    cell.number = number;
    cell.style = style;
    sheets[sheet].rows[row].insert(column, cell);
}

void XlsxWriter::mergeCells(int sheet, int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    if (sheetIndex(sheet) < 0)
        return;
    if (firstRow > lastRow)
        std::swap(firstRow, lastRow);
    if (firstColumn > lastColumn)
        std::swap(firstColumn, lastColumn);
    if (firstRow < 0 || firstColumn < 0 || firstRow == lastRow && firstColumn == lastColumn)
        return;
    Merge merge;
    merge.firstRow = firstRow;
    merge.firstColumn = firstColumn;
    merge.lastRow = lastRow;
    merge.lastColumn = lastColumn;
    sheets[sheet].merges.append(merge);
}

void XlsxWriter::addImage(int sheet, int row, int column, const QByteArray &bytes,
                          const QString &extension, double offsetXPoints, double offsetYPoints,
                          double widthPoints, double heightPoints)
{
    if (sheetIndex(sheet) < 0 || row < 0 || column < 0 || bytes.isEmpty() || widthPoints <= 0
        || heightPoints <= 0)
        return;
    const QString ext = extension.toLower();
    if (ext != QLatin1String("png") && ext != QLatin1String("jpeg"))
        return;
    const int mediaIndex = mediaFor(bytes, ext);
    if (mediaIndex < 0)
        return;
    Picture picture;
    picture.row = row;
    picture.column = column;
    picture.offsetX = qMax(0.0, offsetXPoints);
    picture.offsetY = qMax(0.0, offsetYPoints);
    picture.width = widthPoints;
    picture.height = heightPoints;
    picture.media = mediaIndex;
    sheets[sheet].pictures.append(picture);
}

int XlsxWriter::sharedString(const QString &text)
{
    const auto found = stringIds.constFind(text);
    if (found != stringIds.constEnd())
        return found.value();
    strings.append(text);
    const int index = int(strings.size()) - 1;
    stringIds.insert(text, index);
    return index;
}

int XlsxWriter::mediaFor(const QByteArray &bytes, const QString &extension)
{
    const auto found = mediaByHash.constFind(bytes);
    if (found != mediaByHash.constEnd())
        return found.value();
    Media item;
    item.partName = QStringLiteral("media/image%1.%2").arg(media.size() + 1).arg(extension);
    item.extension = extension;
    item.bytes = bytes;
    media.append(item);
    const int index = int(media.size()) - 1;
    mediaByHash.insert(bytes, index);
    return index;
}

QByteArray XlsxWriter::workbookXml() const
{
    QByteArray sheetsXml;
    for (int i = 0; i < sheets.size(); ++i) {
        sheetsXml += "<sheet name=\"" + xmlText(sheets.at(i).name) + "\" sheetId=\"" + number(i + 1)
            + "\" r:id=\"rId" + number(i + 1) + "\"/>";
    }
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"")
        + kMainNamespace + "\" xmlns:r=\"" + kRelNamespace + "\"><bookViews><workbookView/>"
          "</bookViews><sheets>" + sheetsXml + "</sheets></workbook>";
}

QByteArray XlsxWriter::stylesXml() const
{
    const StyleTables tables = styleTables(styles);

    QByteArray fonts;
    for (const FontSpec &font : tables.fonts) {
        fonts += "<font>";
        if (font.bold)
            fonts += "<b/>";
        if (font.italic)
            fonts += "<i/>";
        fonts += "<sz val=\"" + QByteArray::number(font.size, 'f', 2) + "\"/>";
        if (font.colour.isValid())
            fonts += "<color rgb=\"" + colourHex(font.colour) + "\"/>";
        fonts += "<name val=\"" + xmlText(font.family) + "\"/><family val=\"2\"/></font>";
    }

    QByteArray fills;
    for (int i = 0; i < tables.fills.size(); ++i) {
        if (i == 0)
            fills += "<fill><patternFill patternType=\"none\"/></fill>";
        else if (i == 1)
            fills += "<fill><patternFill patternType=\"gray125\"/></fill>";
        else
            fills += "<fill><patternFill patternType=\"solid\"><fgColor rgb=\""
                + colourHex(tables.fills.at(i))
                + "\"/><bgColor indexed=\"64\"/></patternFill></fill>";
    }

    QByteArray borders;
    for (const BorderSpec &border : tables.borders) {
        borders += "<border>" + borderEdge("left", border.left) + borderEdge("right", border.right)
            + borderEdge("top", border.top) + borderEdge("bottom", border.bottom)
            + "<diagonal/></border>";
    }

    QByteArray numFmts;
    int customFormats = 0;
    for (int i = 1; i < tables.numberFormats.size(); ++i) {
        numFmts += "<numFmt numFmtId=\"" + number(164 + i - 1) + "\" formatCode=\""
            + xmlText(tables.numberFormats.at(i)) + "\"/>";
        ++customFormats;
    }

    QByteArray xfs;
    for (int i = 0; i < styles.size(); ++i) {
        const XlsxCellStyle &style = styles.at(i);
        const int fontId = tables.fontOfStyle.value(i, 0);
        const int fillId = tables.fillOfStyle.value(i, 0);
        const int borderId = tables.borderOfStyle.value(i, 0);
        const int formatId = tables.formatOfStyle.value(i, 0);
        const bool horizontal = (style.alignment & (Qt::AlignLeft | Qt::AlignHCenter
                                                   | Qt::AlignRight | Qt::AlignJustify))
            != 0;
        const bool vertical = (style.alignment & (Qt::AlignTop | Qt::AlignVCenter
                                                  | Qt::AlignBottom)) != 0;
        const bool align = horizontal || vertical || style.wrap;
        xfs += "<xf numFmtId=\"" + number(formatId == 0 ? 0 : 164 + formatId - 1) + "\" fontId=\""
            + number(fontId) + "\" fillId=\"" + number(fillId) + "\" borderId=\"" + number(borderId)
            + "\" xfId=\"0\"";
        if (formatId != 0)
            xfs += " applyNumberFormat=\"1\"";
        if (fontId != 0)
            xfs += " applyFont=\"1\"";
        if (fillId != 0)
            xfs += " applyFill=\"1\"";
        if (borderId != 0)
            xfs += " applyBorder=\"1\"";
        if (!align) {
            xfs += "/>";
            continue;
        }
        xfs += " applyAlignment=\"1\"><alignment";
        if (style.alignment & Qt::AlignHCenter)
            xfs += " horizontal=\"center\"";
        else if (style.alignment & Qt::AlignRight)
            xfs += " horizontal=\"right\"";
        else if (style.alignment & Qt::AlignJustify)
            xfs += " horizontal=\"justify\"";
        else if (style.alignment & Qt::AlignLeft)
            xfs += " horizontal=\"left\"";
        if (style.alignment & Qt::AlignTop)
            xfs += " vertical=\"top\"";
        else if (style.alignment & Qt::AlignVCenter)
            xfs += " vertical=\"center\"";
        else if (style.alignment & Qt::AlignBottom)
            xfs += " vertical=\"bottom\"";
        if (style.wrap)
            xfs += " wrapText=\"1\"";
        xfs += "/></xf>";
    }

    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                      "<styleSheet xmlns=\"")
        + kMainNamespace + "\">"
          "<numFmts count=\"" + number(customFormats) + "\">" + numFmts + "</numFmts>"
          "<fonts count=\"" + number(tables.fonts.size()) + "\">" + fonts + "</fonts>"
          "<fills count=\"" + number(tables.fills.size()) + "\">" + fills + "</fills>"
          "<borders count=\"" + number(tables.borders.size()) + "\">" + borders + "</borders>"
          "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
          "</cellStyleXfs><cellXfs count=\"" + number(styles.size()) + "\">" + xfs + "</cellXfs>"
          "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/>"
          "</cellStyles><dxfs count=\"0\"/><tableStyles count=\"0\" defaultTableStyle=\"TableStyleMedium2\" "
          "defaultPivotStyle=\"PivotStyleLight16\"/></styleSheet>";
}

QByteArray XlsxWriter::sharedStringsXml() const
{
    int references = 0;
    for (const Sheet &sheet : sheets)
        for (const QMap<int, Cell> &row : sheet.rows)
            for (const Cell &cell : row)
                references += cell.stringId >= 0 ? 1 : 0;

    QByteArray body;
    for (const QString &string : strings)
        body += "<si><t xml:space=\"preserve\">" + xmlText(string) + "</t></si>";
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<sst xmlns=\"")
        + kMainNamespace + "\" count=\"" + number(references) + "\" uniqueCount=\""
        + number(strings.size()) + "\">" + body + "</sst>";
}

QByteArray XlsxWriter::sheetXml(int index, const QByteArray &drawingId) const
{
    const Sheet &sheet = sheets.at(index);

    // The used range: every cell, merge and picture. Excel reads it as the sheet's
    // extent; a sheet with nothing on it still has cell A1.
    int lastRow = 0;
    int lastColumn = 0;
    for (auto row = sheet.rows.constBegin(); row != sheet.rows.constEnd(); ++row) {
        if (row.value().isEmpty())
            continue;
        lastRow = qMax(lastRow, row.key());
        lastColumn = qMax(lastColumn, row.value().lastKey());
    }
    for (const Merge &merge : sheet.merges) {
        lastRow = qMax(lastRow, merge.lastRow);
        lastColumn = qMax(lastColumn, merge.lastColumn);
    }
    for (const Picture &picture : sheet.pictures) {
        lastRow = qMax(lastRow, picture.row);
        lastColumn = qMax(lastColumn, picture.column);
    }
    // A row height with no cell is still a row of the sheet.
    if (!sheet.rowHeights.isEmpty())
        lastRow = qMax(lastRow, sheet.rowHeights.lastKey());
    const QByteArray dimension = "A1:" + cellReference(lastRow, lastColumn);

    QByteArray columns;
    for (auto it = sheet.columnWidths.constBegin(); it != sheet.columnWidths.constEnd(); ++it) {
        columns += "<col min=\"" + number(it.key() + 1) + "\" max=\"" + number(it.key() + 1)
            + "\" width=\"" + QByteArray::number(columnCharacters(it.value()), 'f', 2)
            + "\" customWidth=\"1\"/>";
    }

    // Rows with a height but no cells still have to be written, or the height
    // would belong to nothing.
    QSet<int> rowNumbers;
    for (auto it = sheet.rows.constBegin(); it != sheet.rows.constEnd(); ++it)
        if (!it.value().isEmpty())
            rowNumbers.insert(it.key());
    for (auto it = sheet.rowHeights.constBegin(); it != sheet.rowHeights.constEnd(); ++it)
        rowNumbers.insert(it.key());

    QList<int> sortedRows = rowNumbers.values();
    std::sort(sortedRows.begin(), sortedRows.end());

    QByteArray rowsXml;
    for (int row : std::as_const(sortedRows)) {
        rowsXml += "<row r=\"" + number(row + 1) + "\"";
        const auto height = sheet.rowHeights.constFind(row);
        if (height != sheet.rowHeights.constEnd())
            rowsXml += " ht=\"" + QByteArray::number(height.value(), 'f', 2)
                + "\" customHeight=\"1\"";
        const QMap<int, Cell> cells = sheet.rows.value(row);
        if (cells.isEmpty()) {
            rowsXml += "/>";
            continue;
        }
        rowsXml += ">";
        for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
            const Cell &cell = it.value();
            rowsXml += "<c r=\"" + cellReference(row, it.key()) + "\"";
            if (cell.style != 0)
                rowsXml += " s=\"" + number(cell.style) + "\"";
            if (cell.isNumber) {
                rowsXml += "><v>" + QByteArray::number(cell.number, 'g', 15) + "</v></c>";
            } else if (cell.stringId >= 0) {
                rowsXml += " t=\"s\"><v>" + number(cell.stringId) + "</v></c>";
            } else {
                rowsXml += "/>";
            }
        }
        rowsXml += "</row>";
    }

    QByteArray merges;
    if (!sheet.merges.isEmpty()) {
        merges = "<mergeCells count=\"" + number(sheet.merges.size()) + "\">";
        for (const Merge &merge : sheet.merges)
            merges += "<mergeCell ref=\"" + cellReference(merge.firstRow, merge.firstColumn) + ":"
                + cellReference(merge.lastRow, merge.lastColumn) + "\"/>";
        merges += "</mergeCells>";
    }

    QByteArray xml = QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                "<worksheet xmlns=\"")
        + kMainNamespace + "\" xmlns:r=\"" + kRelNamespace + "\"><dimension ref=\"" + dimension
        + "\"/><sheetViews><sheetView workbookViewId=\"0\"/></sheetViews>"
          "<sheetFormatPr defaultRowHeight=\"15\"/>";
    if (!columns.isEmpty())
        xml += "<cols>" + columns + "</cols>";
    xml += "<sheetData>" + rowsXml + "</sheetData>" + merges;
    if (!drawingId.isEmpty())
        xml += "<drawing r:id=\"" + drawingId + "\"/>";
    xml += "</worksheet>";
    return xml;
}

QByteArray XlsxWriter::drawingXml(int index, QVector<QByteArray> *relationships) const
{
    const Sheet &sheet = sheets.at(index);
    QByteArray anchors;
    for (int i = 0; i < sheet.pictures.size(); ++i) {
        const Picture &picture = sheet.pictures.at(i);
        if (picture.media < 0 || picture.media >= media.size())
            continue;
        const QString target = QStringLiteral("../") + media.at(picture.media).partName;
        relationships->append(target.toUtf8());
        const QByteArray id = "rId" + number(relationships->size());
        const QByteArray width = number(emu(picture.width));
        const QByteArray height = number(emu(picture.height));
        anchors += "<xdr:oneCellAnchor><xdr:from><xdr:col>" + number(picture.column)
            + "</xdr:col><xdr:colOff>" + number(emu(picture.offsetX))
            + "</xdr:colOff><xdr:row>" + number(picture.row) + "</xdr:row><xdr:rowOff>"
            + number(emu(picture.offsetY)) + "</xdr:rowOff></xdr:from><xdr:ext cx=\"" + width
            + "\" cy=\"" + height + "\"/><xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"" + number(i + 1)
            + "\" name=\"Picture " + number(i + 1)
            + "\"/><xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></xdr:cNvPicPr></xdr:nvPicPr>"
              "<xdr:blipFill><a:blip r:embed=\""
            + id + "\"/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill><xdr:spPr><a:xfrm>"
                   "<a:off x=\"0\" y=\"0\"/><a:ext cx=\""
            + width + "\" cy=\"" + height
            + "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr></xdr:pic>"
              "<xdr:clientData/></xdr:oneCellAnchor>";
    }
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                      "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/"
                      "spreadsheetDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/"
                      "2006/main\" xmlns:r=\"")
        + kRelNamespace + "\">" + anchors + "</xdr:wsDr>";
}

QHash<QString, QByteArray> XlsxWriter::parts() const
{
    QHash<QString, QByteArray> out;

    QByteArray types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-"
        "officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-"
        "officedocument.spreadsheetml.styles+xml\"/>"
        "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-"
        "officedocument.spreadsheetml.sharedStrings+xml\"/>";
    for (int i = 0; i < sheets.size(); ++i) {
        types += "<Override PartName=\"/xl/worksheets/sheet" + number(i + 1)
            + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
              "spreadsheetml.worksheet+xml\"/>";
    }

    // Drawing numbers are assigned in sheet order, and only to sheets that have a
    // picture: an empty drawing part is a part Excel has to open to find nothing.
    QVector<int> drawingNumber(sheets.size(), 0);
    int drawings = 0;
    for (int i = 0; i < sheets.size(); ++i)
        if (!sheets.at(i).pictures.isEmpty())
            drawingNumber[i] = ++drawings;
    for (int i = 1; i <= drawings; ++i) {
        types += "<Override PartName=\"/xl/drawings/drawing" + number(i)
            + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>";
    }
    types += "</Types>";
    out.insert(QStringLiteral("[Content_Types].xml"), types);

    out.insert(QStringLiteral("_rels/.rels"),
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
               "2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");

    out.insert(QStringLiteral("xl/workbook.xml"), workbookXml());

    QByteArray workbookRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
                              "relationships\">";
    for (int i = 0; i < sheets.size(); ++i) {
        workbookRels += "<Relationship Id=\"rId" + number(i + 1) + "\" Type=\""
            + relType("worksheet") + "\" Target=\"worksheets/sheet" + number(i + 1) + ".xml\"/>";
    }
    workbookRels += "<Relationship Id=\"rId" + number(sheets.size() + 1) + "\" Type=\""
        + relType("styles") + "\" Target=\"styles.xml\"/>";
    workbookRels += "<Relationship Id=\"rId" + number(sheets.size() + 2) + "\" Type=\""
        + relType("sharedStrings") + "\" Target=\"sharedStrings.xml\"/></Relationships>";
    out.insert(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRels);

    out.insert(QStringLiteral("xl/styles.xml"), stylesXml());
    out.insert(QStringLiteral("xl/sharedStrings.xml"), sharedStringsXml());

    for (int i = 0; i < sheets.size(); ++i) {
        const QByteArray drawingId = drawingNumber.at(i) > 0 ? QByteArray("rId1") : QByteArray();
        out.insert(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), sheetXml(i, drawingId));
        if (drawingNumber.at(i) > 0) {
            out.insert(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(i + 1),
                       QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                  "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/"
                                  "2006/relationships\"><Relationship Id=\"rId1\" Type=\"")
                           + relType("drawing") + "\" Target=\"../drawings/drawing"
                           + number(drawingNumber.at(i)) + ".xml\"/></Relationships>");
        }
    }

    for (int i = 0; i < sheets.size(); ++i) {
        if (drawingNumber.at(i) == 0)
            continue;
        QVector<QByteArray> relationships;
        out.insert(QStringLiteral("xl/drawings/drawing%1.xml").arg(drawingNumber.at(i)),
                   drawingXml(i, &relationships));
        QByteArray drawingRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/"
                                 "2006/relationships\">";
        for (int rel = 0; rel < relationships.size(); ++rel)
            drawingRels += "<Relationship Id=\"rId" + number(rel + 1) + "\" Type=\""
                + relType("image") + "\" Target=\"" + xmlText(QString::fromUtf8(relationships.at(rel)))
                + "\"/>";
        drawingRels += "</Relationships>";
        out.insert(QStringLiteral("xl/drawings/_rels/drawing%1.xml.rels").arg(drawingNumber.at(i)),
                   drawingRels);
    }

    for (const Media &item : media)
        out.insert(QStringLiteral("xl/") + item.partName, item.bytes);

    return out;
}

void XlsxWriter::addParts(ZipWriter &zip) const
{
    const QHash<QString, QByteArray> all = parts();
    const QStringList pinned = {
        QStringLiteral("[Content_Types].xml"),
        QStringLiteral("_rels/.rels"),
        QStringLiteral("xl/workbook.xml"),
        QStringLiteral("xl/_rels/workbook.xml.rels"),
        QStringLiteral("xl/styles.xml"),
        QStringLiteral("xl/sharedStrings.xml"),
    };
    for (const QString &name : pinned)
        zip.addFile(name, all.value(name));

    // Worksheets in order, then everything else sorted by name. The order inside
    // the ZIP is not part of the format, but a stable one makes two exports of the
    // same document compare equal.
    QStringList rest;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        if (!pinned.contains(it.key()))
            rest.append(it.key());
    rest.sort();
    for (const QString &name : std::as_const(rest))
        zip.addFile(name, all.value(name));
}

QByteArray XlsxWriter::toBytes() const
{
    ZipWriter zip;
    addParts(zip);
    return zip.toBytes();
}

bool XlsxWriter::save(const QString &path, QString *error) const
{
    ZipWriter zip;
    addParts(zip);
    return zip.write(path, error);
}
