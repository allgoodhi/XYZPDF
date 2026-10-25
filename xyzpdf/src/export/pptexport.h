// A PDF read out as a PowerPoint presentation: one slide per page, every shape at
// the place the PDF had it.
//
// The pipeline is the Word export's, with a different last step:
//
//   pagecontent.h   what the page is made of, read out of PDFium;
//   pageanalysis.h  the page laid out -- paragraphs with their baselines, tables,
//                   pictures, panels and rules;
//   pptxwriter.h    the PresentationML, every box at its own offset and extent.
//
// A slide has no flow, so unlike Word there is no line to keep from re-wrapping:
// the paragraph's box in points is the shape's frame in EMU, and its exact line
// spacing holds the baselines. The first page fixes the slide size; pages of
// another size are scaled to fit it and centred, and the report says how many
// were.
#pragma once

#include <QAtomicInt>
#include <QSharedPointer>
#include <QSizeF>
#include <QString>

#include <functional>

class Document;
class PptxWriter;

// What the export did, for a window to say out loud.
struct PptExportReport
{
    bool ok = false;
    // The document changed under the export, or the reader stopped it; nothing was
    // written.
    bool cancelled = false;
    QString error; // a sentence to show, set whenever ok is false
    QString path;
    // Slides written, their size, and how many pages were not that size and had to
    // be scaled and centred.
    int pages = 0;
    int scaledPages = 0;
    QSizeF slideSize;
    // Pages with no text, which went in as a picture of the whole page.
    int picturePages = 0;
    // Pictures and drawn panels/rules placed on the slides.
    int figures = 0;
    int tables = 0;
    int tableCells = 0;
    int textBoxes = 0;
    // With a slide per page these are the same thing; kept because a page whose
    // render failed leaves a slide with nothing on it, and that is worth saying.
    int emptyPages = 0;
};

struct PptExportOptions
{
    // Set by the caller when the document it is reading is about to be replaced.
    // Checked between pages: a presentation half from one document and half from
    // another is worse than none.
    QSharedPointer<QAtomicInt> cancel;

    // Called after each page, from the worker thread that is doing the reading.
    std::function<void(int done, int total)> progress;
};

PptExportReport exportToPowerPoint(Document &document, const QString &path,
                                   const PptExportOptions &options = PptExportOptions());

// The slide size the export would use for a document: its first page, in points.
// Empty when the document has no pages. The window asks before the job runs only
// to report the shape; the export asks again itself.
QSizeF powerPointSlideSize(Document &document);
