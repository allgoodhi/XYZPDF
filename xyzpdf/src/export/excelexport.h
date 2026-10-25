// A PDF's tables read out as an .xlsx workbook.
//
// ---- how ----
//
// The same two steps the Word export uses, and the same two files for them:
//
//   pagecontent.h   the page read out of PDFium (words, pictures, paths).
//   pageanalysis.h  the page laid out: ruled lines become tables with merged
//                   cells, borders, shading, and their text and pictures inside.
//
// This file collects the tables of every page -- including the ones inside a
// two-column stretch, which the analysis keeps as blocks of their own -- and
// gives each to the workbook writer (xlsxwriter.h) as one worksheet. Pages with
// no table are skipped, unless the reader asked for their text: then each such
// page becomes one worksheet with the page's lines as rows.
//
// Tables are the unit on purpose. A PDF page is a sheet of paper; a spreadsheet
// sheet is a grid. Putting the page's text on a grid only makes sense where the
// PDF already had one, which is what a table is -- and where it did not, the
// paragraphs go in one column, one per row, which is what the reader chose.
#pragma once

#include <QAtomicInt>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;

// What the export did, for a window to say out loud.
struct ExcelExportReport
{
    bool ok = false;
    // The document changed under the export and it stopped, rather than writing
    // a file that is half of one document and half of another.
    bool cancelled = false;
    // A sentence to show, set whenever ok is false.
    QString error;
    QString path;
    // Pages read.
    int pages = 0;
    // Tables written, and the worksheets they became.
    int tables = 0;
    int worksheets = 0;
    // Of those worksheets, the ones holding a table-less page's text.
    int textSheets = 0;
    // Pages with no table that were not written (the option was off, or there was
    // nothing to write).
    int skippedPages = 0;
    // Of those, pages with neither a table nor any text to put in a sheet.
    int emptyPages = 0;
};

struct ExcelExportOptions
{
    // Set by the caller when the document it is reading is about to be replaced.
    // Checked between pages: half a file from one document and half from another
    // is worse than none.
    QSharedPointer<QAtomicInt> cancel;

    // Called after each page, from the worker thread that is doing the reading.
    std::function<void(int done, int total)> progress;

    // The reader asked for the text of pages that have no table as well: each
    // such page becomes a worksheet of its own, its lines one per row (a tab in
    // a line splits it across the row's columns). Pages that do have tables are
    // written as tables only -- the tables are the text that is on a grid.
    bool exportText = false;
};

ExcelExportReport exportToExcel(Document &document, const QString &path,
                                const ExcelExportOptions &options = ExcelExportOptions());
