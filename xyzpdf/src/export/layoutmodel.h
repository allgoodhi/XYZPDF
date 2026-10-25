// A PDF page as a Word page: what the layout analysis (pageanalysis.h) makes of a
// PageContent, and what the writer (docxwriter.h) turns into WordprocessingML.
//
// ---- the model in one paragraph ----
//
// A converted page has to *look* like the PDF page and still be a document someone
// can edit. So the words stay words, in paragraphs that flow, and every one of
// those paragraphs carries the numbers that put it where the PDF had it: exact line
// spacing (so a font's own metrics cannot move a line), the room above it, its
// indents and alignment, its tab stops. Tables are real tables with the PDF's grid,
// spans, borders and shading. Pictures go in where they stand -- inline when they
// stand alone, anchored to the page when text sits beside them -- and whatever is
// drawn but is neither (a separator rule, a coloured panel) is anchored behind the
// text at its exact place. All geometry is in points, top-left origin, like
// pagecontent.h.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <memory>

struct RunStyle
{
    // A family a Word reader can find ("Arial", "SimSun"), not the PDF's
    // PostScript name.
    QString font;
    // Whether the text is East Asian, which decides which of Word's font slots the
    // family goes into.
    bool eastAsian = false;
    double size = 11.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    // Invalid for the reader's own text colour (black in the PDF).
    QColor color;
    bool superscript = false;
    bool subscript = false;
    // Extra room after every character, in points (negative to set them closer):
    // what makes a line as wide in Word as it is in the PDF.
    double spacing = 0;
    // Arabic, Hebrew: written in reading order and marked so Word sets it right to left.
    bool rightToLeft = false;

    bool operator==(const RunStyle &other) const
    {
        return font == other.font && eastAsian == other.eastAsian
            && qAbs(size - other.size) < 0.26 && bold == other.bold && italic == other.italic
            && underline == other.underline && color == other.color
            && superscript == other.superscript && subscript == other.subscript
            && qAbs(spacing - other.spacing) < 0.026 && rightToLeft == other.rightToLeft;
    }
    bool operator!=(const RunStyle &other) const { return !(*this == other); }
};

struct LayoutRun
{
    // May contain '\t', which the writer turns into a tab.
    QString text;
    RunStyle style;
    // A web address, or a page of this document to jump to (0-based), or neither.
    QString uri;
    int targetPage = -1;
    // "PAGE" for a run that is the page number: written as a field showing `text`.
    QString field;
};

enum class LayoutAlign
{
    Left,
    Center,
    Right,
    Justify,
};

struct LayoutTab
{
    // From the paragraph's own left edge (the writer adds the indent back).
    double position = 0;
    bool right = false;
};

struct LayoutParagraph
{
    QVector<LayoutRun> runs;
    LayoutAlign align = LayoutAlign::Left;
    // Measured from the region's left and right edges (page margins, or a cell's
    // inside), in points. `firstLine` is relative to `left` and negative for a
    // hanging first line.
    double left = 0;
    double right = 0;
    double firstLine = 0;
    // Room above the paragraph beyond the previous block, and the exact distance
    // from one baseline to the next.
    double spaceBefore = 0;
    double lineSpacing = 0;
    QVector<LayoutTab> tabs;
    // 0..8 for a heading Word should list in its navigation pane, -1 otherwise.
    int outlineLevel = -1;
    // How many lines the PDF set it in.
    int lineCount = 1;
    // Placed on the page at `box` rather than in the flow: text that starts above
    // the region it would flow in (beside a running header, in the top margin), which
    // the flow could only put lower down -- and push everything after it down too.
    bool framed = false;
    // Where it was, for the analysis's own bookkeeping and the tests.
    QRectF box;
    double firstBaseline = 0;
    double lastBaseline = 0;

    QString text() const
    {
        QString all;
        for (const LayoutRun &run : runs)
            all += run.text;
        return all;
    }
};

struct LayoutImage
{
    QByteArray bytes;
    QString extension; // "jpeg" or "png"
    // Where it is on the page and the size it is shown at.
    QRectF box;
};

struct LayoutTable;
struct LayoutColumns;

struct LayoutBlock
{
    enum class Kind
    {
        Paragraph,
        Image,
        Table,
        // A stretch of the page set in two columns: Word gets a section with two columns, the
        // left column's blocks, a column break, the right column's blocks.
        Columns,
    };
    Kind kind = Kind::Paragraph;
    LayoutParagraph paragraph;
    LayoutImage image;
    std::shared_ptr<LayoutTable> table;
    std::shared_ptr<LayoutColumns> columns;
    // Room above an image or a table (a paragraph keeps its own in `paragraph`),
    // and how far in from the region's left edge it starts.
    double spaceBefore = 0;
    double indent = 0;
    // The block's extent on the page.
    double top = 0;
    double bottom = 0;
};

struct LayoutColumns
{
    // In points: the left column's width, the gap, the right column's width. The left column
    // starts at the page's left margin.
    double leftWidth = 0;
    double gap = 0;
    double rightWidth = 0;
    // Each column's blocks, laid out against that column's own edges.
    QVector<LayoutBlock> left;
    QVector<LayoutBlock> right;
};

struct LayoutBorder
{
    bool present = false;
    double width = 0.5;
    QColor color = Qt::black;
};

struct LayoutCell
{
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    QRectF box;
    QVector<LayoutBlock> blocks;
    LayoutBorder top;
    LayoutBorder left;
    LayoutBorder bottom;
    LayoutBorder right;
    QColor shading;
    enum class VAlign
    {
        Top,
        Center,
        Bottom,
    };
    VAlign valign = VAlign::Top;
    // The space between the cell's edge and its content, in points.
    double padLeft = 0;
    double padRight = 0;
    double padTop = 0;

    QString text() const
    {
        QString all;
        for (const LayoutBlock &block : blocks) {
            if (block.kind != LayoutBlock::Kind::Paragraph)
                continue;
            if (!all.isEmpty())
                all += QLatin1Char('\n');
            all += block.paragraph.text();
        }
        return all;
    }
    int imageCount() const
    {
        int count = 0;
        for (const LayoutBlock &block : blocks)
            count += block.kind == LayoutBlock::Kind::Image ? 1 : 0;
        return count;
    }
};

struct LayoutTable
{
    // Grid lines, in points: columns.size() - 1 columns and rows.size() - 1 rows.
    QVector<double> columns;
    QVector<double> rows;
    // One entry per cell that starts a span; the grid positions a span covers are
    // not listed again.
    QVector<LayoutCell> cells;
    QRectF box;

    const LayoutCell *cellAt(int row, int column) const
    {
        for (const LayoutCell &cell : cells) {
            if (row >= cell.row && row < cell.row + cell.rowSpan && column >= cell.column
                && column < cell.column + cell.columnSpan)
                return &cell;
        }
        return nullptr;
    }
};

// Something placed on the page at an exact position rather than in the flow: a
// picture with text beside it, or a drawn shape (a rule, a panel) behind the text.
struct LayoutAnchor
{
    LayoutImage image;
    bool behindText = false;
};

struct LayoutPage
{
    QSizeF size;
    // The page margins the flow is laid out in.
    double marginLeft = 72;
    double marginRight = 72;
    double marginTop = 72;
    double marginBottom = 72;
    QVector<LayoutBlock> blocks;
    QVector<LayoutAnchor> anchors;
    // Lines the export moved into Word's header and footer (see wordexport.cpp,
    // which decides across pages what repeats), and where they sit.
    QVector<LayoutParagraph> header;
    QVector<LayoutParagraph> footer;
    double headerDistance = 36;
    double footerDistance = 36;
    // A page with no text: it goes in as its own picture.
    bool pictureOnly = false;
};
