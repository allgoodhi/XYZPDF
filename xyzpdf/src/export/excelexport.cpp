#include "excelexport.h"

#include "excelvalue.h"
#include "pageanalysis.h"
#include "xlsxwriter.h"

#include "../document/document.h"
#include "../document/pagecontent.h"

#include <QCoreApplication>
#include <QVector>

#include <utility>

namespace {

QString translated(const char *text)
{
    return QCoreApplication::translate("ExcelExport", text);
}

// Every table of a page, in reading order: a two-column stretch keeps its columns
// as blocks of their own, and a table inside one is as much the page's table as a
// table below it.
void collectTables(const QVector<LayoutBlock> &blocks, QVector<const LayoutTable *> *tables)
{
    for (const LayoutBlock &block : blocks) {
        switch (block.kind) {
        case LayoutBlock::Kind::Table:
            if (block.table)
                tables->append(block.table.get());
            break;
        case LayoutBlock::Kind::Columns:
            if (block.columns) {
                collectTables(block.columns->left, tables);
                collectTables(block.columns->right, tables);
            }
            break;
        case LayoutBlock::Kind::Paragraph:
        case LayoutBlock::Kind::Image:
            break;
        }
    }
}

void collectParagraphs(const QVector<LayoutBlock> &blocks,
                       QVector<const LayoutParagraph *> *paragraphs)
{
    for (const LayoutBlock &block : blocks) {
        switch (block.kind) {
        case LayoutBlock::Kind::Paragraph:
            paragraphs->append(&block.paragraph);
            break;
        case LayoutBlock::Kind::Columns:
            if (block.columns) {
                collectParagraphs(block.columns->left, paragraphs);
                collectParagraphs(block.columns->right, paragraphs);
            }
            break;
        case LayoutBlock::Kind::Table:
        case LayoutBlock::Kind::Image:
            break;
        }
    }
}

XlsxBorder excelBorder(const LayoutBorder &border)
{
    XlsxBorder out;
    out.present = border.present;
    out.width = border.width;
    out.color = border.color;
    return out;
}

// The look of a cell: the first run of its first paragraph for the font, the
// cell's own shading and edges for the rest. Excel has one font and one fill per
// cell, not one per word, so a cell of mixed runs takes the first -- which is what
// a spreadsheet can hold, not a guess about which run mattered.
XlsxCellStyle cellStyle(const LayoutCell &cell, const QString &text)
{
    XlsxCellStyle style;
    for (const LayoutBlock &block : cell.blocks) {
        if (block.kind != LayoutBlock::Kind::Paragraph || block.paragraph.runs.isEmpty())
            continue;
        const LayoutParagraph &paragraph = block.paragraph;
        const RunStyle &run = paragraph.runs.first().style;
        style.fontFamily = run.font;
        if (run.size > 0)
            style.fontSize = run.size;
        style.bold = run.bold;
        style.italic = run.italic;
        style.fontColor = run.color;
        switch (paragraph.align) {
        case LayoutAlign::Center:
            style.alignment |= Qt::AlignHCenter;
            break;
        case LayoutAlign::Right:
            style.alignment |= Qt::AlignRight;
            break;
        case LayoutAlign::Justify:
            style.alignment |= Qt::AlignJustify;
            break;
        case LayoutAlign::Left:
            // Said out loud rather than left to the reader's default: a number with
            // no alignment written goes to the right edge of its cell, and the PDF
            // had it at the left.
            style.alignment |= Qt::AlignLeft;
            break;
        }
        break;
    }
    switch (cell.valign) {
    case LayoutCell::VAlign::Top:
        style.alignment |= Qt::AlignTop;
        break;
    case LayoutCell::VAlign::Center:
        style.alignment |= Qt::AlignVCenter;
        break;
    case LayoutCell::VAlign::Bottom:
        style.alignment |= Qt::AlignBottom;
        break;
    }
    style.fill = cell.shading;
    style.top = excelBorder(cell.top);
    style.left = excelBorder(cell.left);
    style.bottom = excelBorder(cell.bottom);
    style.right = excelBorder(cell.right);
    style.wrap = text.contains(QLatin1Char('\n'));
    return style;
}

XlsxCellStyle paragraphStyle(const LayoutParagraph &paragraph)
{
    XlsxCellStyle style;
    if (!paragraph.runs.isEmpty()) {
        const RunStyle &run = paragraph.runs.first().style;
        style.fontFamily = run.font;
        if (run.size > 0)
            style.fontSize = run.size;
        style.bold = run.bold;
        style.italic = run.italic;
        style.fontColor = run.color;
    }
    switch (paragraph.align) {
    case LayoutAlign::Center:
        style.alignment |= Qt::AlignHCenter;
        break;
    case LayoutAlign::Right:
        style.alignment |= Qt::AlignRight;
        break;
    case LayoutAlign::Justify:
        style.alignment |= Qt::AlignJustify;
        break;
    case LayoutAlign::Left:
        style.alignment |= Qt::AlignLeft;
        break;
    }
    style.alignment |= Qt::AlignTop;
    return style;
}

// One table, one worksheet. Returns false for a grid too small to be a table,
// which the analysis can produce from two crossing rules that are not one.
bool writeTable(XlsxWriter &writer, const LayoutTable &table, const QString &name)
{
    if (table.columns.size() < 2 || table.rows.size() < 2)
        return false;

    const int columns = int(table.columns.size()) - 1;
    const int rows = int(table.rows.size()) - 1;
    const int sheet = writer.addSheet(name);

    for (int column = 0; column < columns; ++column) {
        const double width = table.columns.at(column + 1) - table.columns.at(column);
        if (width > 0)
            writer.setColumnWidth(sheet, column, width);
    }
    for (int row = 0; row < rows; ++row) {
        const double height = table.rows.at(row + 1) - table.rows.at(row);
        if (height > 0)
            writer.setRowHeight(sheet, row, height);
    }

    for (const LayoutCell &cell : table.cells) {
        if (cell.row < 0 || cell.column < 0 || cell.row >= rows || cell.column >= columns)
            continue;

        const QString text = cell.text();
        const bool hasPicture = cell.imageCount() > 0;
        const bool visible = !text.isEmpty() || hasPicture || cell.shading.isValid()
            || cell.top.present || cell.left.present || cell.bottom.present || cell.right.present;
        if (!visible)
            continue;

        ExcelValue value;
        if (!text.contains(QLatin1Char('\n')))
            value = classifyExcelValue(text);
        else {
            value.kind = ExcelValue::Kind::Text;
            value.text = text;
        }

        XlsxCellStyle style = cellStyle(cell, text);
        const int lastRow = qMin(cell.row + cell.rowSpan - 1, rows - 1);
        const int lastColumn = qMin(cell.column + cell.columnSpan - 1, columns - 1);
        const bool spans = lastRow > cell.row || lastColumn > cell.column;
        // The frame as the PDF drew it, kept whole: the anchor gets it with the
        // edges that face the inside of the range taken off, and each covered cell
        // gets it with the edges that face *its* inside taken off. Deriving the
        // covered copies from the trimmed anchor is how the range's own right edge
        // went missing on the first round -- the anchor had already given that one up.
        const XlsxCellStyle frame = style;
        if (spans) {
            if (lastColumn > cell.column)
                style.right = XlsxBorder();
            if (lastRow > cell.row)
                style.bottom = XlsxBorder();
        }
        if (value.isNumeric()) {
            style.numberFormat = value.format;
            writer.setNumber(sheet, cell.row, cell.column, value.number, writer.styleFor(style));
        } else {
            // The style is what makes an empty shaded cell a cell at all, so it is
            // passed even when the string is empty.
            writer.setText(sheet, cell.row, cell.column, value.text, writer.styleFor(style));
        }
        if (spans) {
            writer.mergeCells(sheet, cell.row, cell.column, lastRow, lastColumn);
            for (int row = cell.row; row <= lastRow; ++row) {
                for (int column = cell.column; column <= lastColumn; ++column) {
                    if (row == cell.row && column == cell.column)
                        continue;
                    XlsxCellStyle edge = frame;
                    // The words and the number belong to the anchor; what the rest of
                    // the range carries is the frame and the shading.
                    edge.numberFormat.clear();
                    edge.wrap = false;
                    if (row > cell.row)
                        edge.top = XlsxBorder();
                    if (row < lastRow)
                        edge.bottom = XlsxBorder();
                    if (column > cell.column)
                        edge.left = XlsxBorder();
                    if (column < lastColumn)
                        edge.right = XlsxBorder();
                    writer.setText(sheet, row, column, QString(), writer.styleFor(edge));
                }
            }
        }

        for (const LayoutBlock &block : cell.blocks) {
            if (block.kind != LayoutBlock::Kind::Image || block.image.bytes.isEmpty())
                continue;
            writer.addImage(sheet, cell.row, cell.column, block.image.bytes, block.image.extension,
                            block.image.box.left() - cell.box.left(),
                            block.image.box.top() - cell.box.top(), block.image.box.width(),
                            block.image.box.height());
        }
    }
    return true;
}

// A page with no table, when the reader asked for its text: every line of the
// page is a row, and a tab in one splits it across the row's columns -- which is
// what a table drawn without rules is. A paragraph that the PDF wrapped over
// three lines is three rows, not one cell three lines tall: the reader asked for
// the page's lines, and a spreadsheet row is the closest thing to a line it has.
// Returns false when there was nothing to write, so an empty page does not become
// an empty sheet.
bool writeTextSheet(XlsxWriter &writer, const QVector<const LayoutParagraph *> &paragraphs,
                    const QString &name)
{
    int sheet = -1;
    int row = 0;
    for (const LayoutParagraph *paragraph : paragraphs) {
        const QString text = paragraph->text();
        if (text.isEmpty())
            continue;
        if (sheet < 0)
            sheet = writer.addSheet(name);
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QStringList pieces = line.split(QLatin1Char('\t'));
            for (int column = 0; column < pieces.size(); ++column) {
                const QString &piece = pieces.at(column);
                if (piece.isEmpty())
                    continue;
                const ExcelValue value = classifyExcelValue(piece);
                XlsxCellStyle style = paragraphStyle(*paragraph);
                if (value.isNumeric()) {
                    style.numberFormat = value.format;
                    writer.setNumber(sheet, row, column, value.number, writer.styleFor(style));
                } else {
                    writer.setText(sheet, row, column, value.text, writer.styleFor(style));
                }
            }
            ++row;
        }
    }
    return sheet >= 0;
}

} // namespace

ExcelExportReport exportToExcel(Document &document, const QString &path,
                                const ExcelExportOptions &options)
{
    ExcelExportReport report;
    report.path = path;
    if (path.isEmpty()) {
        report.error = translated(QT_TRANSLATE_NOOP("ExcelExport", "没有指定要写入的文件。"));
        return report;
    }

    const QSharedPointer<QAtomicInt> &cancel = options.cancel;
    const auto abandoned = [&cancel] { return cancel && cancel->loadRelaxed(); };
    const auto stop = [&report] {
        report.cancelled = true;
        report.error = translated(QT_TRANSLATE_NOOP("ExcelExport", "导出已取消。"));
        return report;
    };

    const int pages = document.pageCount();
    if (options.progress && pages > 0)
        options.progress(0, pages);

    XlsxWriter writer;
    for (int page = 0; page < pages; ++page) {
        if (abandoned())
            return stop();

        const PageContent content = pageContentOf(document, page, true);
        if (abandoned())
            return stop();
        ++report.pages;

        LayoutPage laid;
        if (content.ok) {
            AnalysisOptions analysis;
            analysis.pageNumber = page + 1;
            // No render callback: a picture outside a table has no cell to sit in
            // and is not part of an export of tables, so rendering one would be
            // minutes of work for something that is then thrown away.
            laid = analysePage(content, analysis);
        } else {
            laid.size = document.pageSize(page);
            laid.pictureOnly = true;
        }

        QVector<const LayoutTable *> tables;
        collectTables(laid.blocks, &tables);

        int written = 0;
        for (int index = 0; index < tables.size(); ++index) {
            if (abandoned())
                return stop();
            if (writeTable(writer, *tables.at(index), sheetNameForTable(page + 1, index + 1))) {
                ++written;
                ++report.tables;
                ++report.worksheets;
            }
        }
        if (written == 0) {
            // No table on this page -- or only a grid too small to be one, which is
            // the same thing to a reader.
            QVector<const LayoutParagraph *> paragraphs;
            collectParagraphs(laid.blocks, &paragraphs);
            bool anyText = false;
            for (const LayoutParagraph *paragraph : std::as_const(paragraphs))
                anyText = anyText || !paragraph->text().isEmpty();
            if (!anyText)
                ++report.emptyPages;
            if (options.exportText && anyText) {
                if (writeTextSheet(writer, paragraphs, sheetNameForText(page + 1))) {
                    ++report.textSheets;
                    ++report.worksheets;
                }
            } else {
                // No table to write and the reader did not ask for this page's
                // text: the page is skipped, and counted so the report can say so.
                ++report.skippedPages;
            }
        }

        if (options.progress)
            options.progress(page + 1, pages);
    }

    if (abandoned())
        return stop();

    if (report.worksheets == 0) {
        report.error = translated(
            QT_TRANSLATE_NOOP("ExcelExport", "这份 PDF 里没有找到表格，没有可导出的内容。"));
        return report;
    }

    QString error;
    if (!writer.save(path, &error)) {
        report.error = translated(QT_TRANSLATE_NOOP("ExcelExport", "写入文件失败：%1")).arg(error);
        return report;
    }
    report.ok = true;
    return report;
}
