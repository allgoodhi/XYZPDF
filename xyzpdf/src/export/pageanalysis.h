// From what a page is made of (pagecontent.h) to how it is laid out (layoutmodel.h).
//
// Pure computation over plain data: the one thing it asks of the outside world is a
// picture of a region of the page, for a drawing made of vector paths, and that
// comes in as a callback. So every rule here can be tested on a page written by hand.
#pragma once

#include "layoutmodel.h"

#include "../document/pagecontent.h"

#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

#include <functional>

struct AnalysisOptions
{
    // A region of the page (points) drawn on white at `dpi`. Used for drawings made
    // of paths, which have no bytes of their own to carry over. May be empty, and
    // then such drawings are left out.
    std::function<QImage(const QRectF &region, double dpi)> renderRegion;

    // Rows the caller decided are the page's running header and footer (see
    // headerFooterRows()): left out of the body and returned as page.header /
    // page.footer instead.
    QVector<QRectF> headerRows;
    QVector<QRectF> footerRows;
    // The page's own number, 1-based, so a footer that says it becomes a PAGE field.
    int pageNumber = 0;

    // The size most of the document's text is set in, for deciding what is a heading.
    // 0 means "work it out from this page".
    double bodySize = 0;
};

LayoutPage analysePage(const PageContent &content, const AnalysisOptions &options);

// ---- the parts, for the tests and for the export's cross-page pass ----

// A row of text in the top or bottom band of a page, as the cross-page pass sees it:
// where it is and what it says with its digits folded, so "Page 3" and "Page 4" are
// the same row.
struct BandRow
{
    QRectF box;
    QString text;
    QString folded;
    bool top = false;
    // It says nothing but a page number ("3", "- 3 -", "Page 3 of 9", "第 3 页").
    bool pageNumber = false;
};

QVector<BandRow> bandRows(const PageContent &content);

// The rows of each page that repeat across the document in the same place, or that
// are a page number: those are the running header and footer. `pages[i]` is
// bandRows() of page i; the answer is, per page, the rows to treat as header and
// as footer.
struct RunningRows
{
    QVector<QVector<QRectF>> header;
    QVector<QVector<QRectF>> footer;
};
RunningRows headerFooterRows(const QVector<QVector<BandRow>> &pages,
                             const QVector<QSizeF> &sizes);

// A ruled line on the page: the stroke of a table border, an underline, a
// separator. Horizontal lines have `position` = y and run from..to along x.
struct RuleLine
{
    bool horizontal = true;
    double position = 0;
    double from = 0;
    double to = 0;
    double width = 0.5;
    QColor color = Qt::black;
};

// The ruled tables a set of lines draws: a grid per connected group of lines with
// at least two lines each way, with cells merged wherever an inner line is missing.
// Only geometry -- cells come back with boxes, spans and borders but no content.
// `used[i]` is set for every line that went into a table.
QVector<std::shared_ptr<LayoutTable>> tablesFromRules(const QVector<RuleLine> &lines,
                                                      QVector<bool> *used);

// Close parallel lines merged into one (a table drawn with a frame per cell has two
// lines a point apart between neighbours), and pieces of one line joined.
QVector<RuleLine> normalisedRules(const QVector<RuleLine> &lines);

// A PDF font name as the family a Word reader can find: "ABCDEF+Arial-BoldMT" ->
// "Arial", "TimesNewRomanPS-ItalicMT" -> "Times New Roman", "SimSun" stays,
// GBK-encoded Chinese names are decoded and mapped ("宋体" -> "SimSun"). A name that
// says nothing ("CIDFont+F2") gives way to `embeddedFamily`, what the embedded font
// program calls itself, when this machine has that family.
QString wordFontFamily(const QString &pdfFont, const QString &embeddedFamily = QString());
