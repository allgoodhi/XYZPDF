// A workbook written from what the layout analysis found in a PDF (layoutmodel.h).
//
// ---- what is written ----
//
// One worksheet per table. A sheet holds the table's grid: column widths and row
// heights taken from the PDF's own lines, one cell per grid cell with its value,
// merged cells for spans, the PDF's borders and shading, and a picture floating
// at its place inside the cell that held it. Text that is a number or a date goes
// in as one, with a number format that shows it the way the PDF wrote it (see
// excelvalue.h); everything else is a shared string.
//
// The package is the minimal correct set SpreadsheetML needs -- content types,
// the package and workbook relationships, the workbook, the worksheets, styles,
// shared strings, and drawings and media when there are pictures -- written by
// hand with zipwriter.h for the same reason the Word export writes its own: a
// writer whose only reader is its own test proves nothing about Excel.
//
// Nothing here guesses: it writes what it is given. Rows and columns are 0-based
// in this header and 1-based in the file, which is the one place the difference
// lives.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <Qt>

class ZipWriter;

struct XlsxBorder
{
    bool present = false;
    // A PDF stroke width, in points: only the presence of a line and its colour
    // are carried; the width picks one of Excel's three line weights.
    double width = 0.5;
    QColor color = Qt::black;

    bool operator==(const XlsxBorder &other) const
    {
        return present == other.present && (!present || (qAbs(width - other.width) < 0.01
                                                         && color == other.color));
    }
};

struct XlsxCellStyle
{
    QString fontFamily; // empty -> the writer's default (Calibri)
    double fontSize = 11;
    bool bold = false;
    bool italic = false;
    // Invalid for the reader's own black, as in layoutmodel.h.
    QColor fontColor;
    QColor fill; // invalid -> no fill
    // Multi-line cell content: the newline is kept and Excel wraps the cell.
    bool wrap = false;
    XlsxBorder top;
    XlsxBorder left;
    XlsxBorder bottom;
    XlsxBorder right;
    // An Excel number format code, e.g. "#,##0.0". Empty -> General.
    QString numberFormat;
    // Horizontal (AlignHCenter / AlignRight / AlignJustify) and vertical (AlignTop /
    // AlignVCenter / AlignBottom) as Excel's own alignment. Empty -> the cell's default.
    Qt::Alignment alignment;

    bool operator==(const XlsxCellStyle &other) const
    {
        return fontFamily == other.fontFamily && qAbs(fontSize - other.fontSize) < 0.01
            && bold == other.bold && italic == other.italic && fontColor == other.fontColor
            && fill == other.fill && wrap == other.wrap && top == other.top && left == other.left
            && bottom == other.bottom && right == other.right && numberFormat == other.numberFormat
            && alignment == other.alignment;
    }
};

class XlsxWriter
{
public:
    XlsxWriter();

    // Appends a sheet and returns its index. The name is made unique against the
    // sheets already added (Excel treats names case-insensitively), and is
    // sanitised first: at most 31 characters, no []:*?/\.
    int addSheet(const QString &name);
    QString sheetName(int sheet) const;
    int sheetCount() const { return int(sheets.size()); }

    // One style per distinct look: the same fields twice give the same index back.
    // Index 0 is the workbook's default and is always there.
    int styleFor(const XlsxCellStyle &style);

    void setColumnWidth(int sheet, int column, double points);
    void setRowHeight(int sheet, int row, double points);

    // A text cell keeps its newlines; a number cell gets the style's number format.
    // An empty text with no style writes nothing, because a cell with neither a
    // value nor a look is not on the sheet.
    void setText(int sheet, int row, int column, const QString &text, int style = 0);
    void setNumber(int sheet, int row, int column, double number, int style = 0);

    // One entry per merged rectangle: (firstRow, firstColumn) is the cell that
    // carries the value; the rest of the rectangle is not written.
    void mergeCells(int sheet, int firstRow, int firstColumn, int lastRow, int lastColumn);

    // A picture anchored inside a cell: its top-left corner `offsetXPoints` and
    // `offsetYPoints` in from the cell's own corner, at `widthPoints` by
    // `heightPoints`. `extension` is "png" or "jpeg"; anything else is ignored.
    void addImage(int sheet, int row, int column, const QByteArray &bytes,
                  const QString &extension, double offsetXPoints, double offsetYPoints,
                  double widthPoints, double heightPoints);

    // Every part of the package by name, as it will be written -- what a test reads
    // instead of its own idea of the format.
    QHash<QString, QByteArray> parts() const;

    QByteArray toBytes() const;
    bool save(const QString &path, QString *error) const;

private:
    struct Cell
    {
        bool used = false;
        bool isNumber = false;
        double number = 0;
        QString text;
        int stringId = -1;
        int style = 0;
    };
    struct Merge
    {
        int firstRow = 0;
        int firstColumn = 0;
        int lastRow = 0;
        int lastColumn = 0;
    };
    struct Picture
    {
        int row = 0;
        int column = 0;
        double offsetX = 0;
        double offsetY = 0;
        double width = 0;
        double height = 0;
        int media = -1;
    };
    struct Sheet
    {
        QString name;
        QMap<int, QMap<int, Cell>> rows;
        QMap<int, double> rowHeights;
        QMap<int, double> columnWidths;
        QVector<Merge> merges;
        QVector<Picture> pictures;
    };
    struct Media
    {
        QString partName; // "media/image1.png"
        QString extension;
        QByteArray bytes;
    };

    int sheetIndex(int sheet) const;
    int sharedString(const QString &text);
    int mediaFor(const QByteArray &bytes, const QString &extension);

    QByteArray sheetXml(int index, const QByteArray &drawingId) const;
    QByteArray drawingXml(int index, QVector<QByteArray> *relationships) const;
    QByteArray stylesXml() const;
    QByteArray sharedStringsXml() const;
    QByteArray workbookXml() const;
    void addParts(ZipWriter &zip) const;

    QVector<Sheet> sheets;
    QSet<QString> lowerSheetNames;
    QVector<XlsxCellStyle> styles;
    QVector<QString> strings;
    QHash<QString, int> stringIds;
    QVector<Media> media;
    QHash<QByteArray, int> mediaByHash;
};
