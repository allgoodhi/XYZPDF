// A PDF read out as a Word document that looks like the PDF.
//
// ---- how ----
//
// Three steps, each in its own file:
//
//   pagecontent.h   what each page is made of, read out of PDFium: every character
//                   with its font, size, weight and colour; every picture with its
//                   own bytes; every drawn path; every link.
//   pageanalysis.h  how the page is laid out: rows of text into paragraphs with
//                   their alignment, indents, tab stops and spacing; ruled lines
//                   into tables with merged cells, borders and shading, and the
//                   text and pictures into their cells; drawings into figures;
//                   the running header and footer out of the body.
//   docxwriter.h    the WordprocessingML: a section per page, with the page's size
//                   and margins, and the numbers that put every line where the PDF
//                   had it.
//
// This file runs them over the document: one cheap pass over every page's text to
// find what repeats at the top and bottom of the pages and what size the body text
// is, then a full pass that writes each page.
//
// It replaces an export that worked from what Qt PDF could give it -- a line's text
// and its box -- and guessed the rest from rendered pixels; store-submission.md §124
// lists what that got wrong.
#pragma once

#include <QAtomicInt>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;

// What the export did, for a window to say out loud.
struct WordExportReport
{
    bool ok = false;
    // The document changed under the export and it stopped, rather than writing
    // a file that is half of one document and half of another.
    bool cancelled = false;
    // A sentence to show, set whenever ok is false.
    QString error;
    QString path;
    // Pages that were written, as text or as a picture.
    int pages = 0;
    // Of those, the ones with no text, which went in as a picture of the page.
    int imagePages = 0;
    // Pictures and drawings placed among a page's text.
    int figures = 0;
    // Tables rebuilt from the PDF's ruled lines.
    int tables = 0;
    // Pages that produced nothing at all.
    int emptyPages = 0;
    int paragraphs = 0;
};

struct WordExportOptions
{
    // Set by the caller when the document it is reading is about to be replaced.
    // Checked between pages, because half a file from one document and half from
    // another is worse than none.
    QSharedPointer<QAtomicInt> cancel;

    // Called after each page, from the worker thread that is doing the reading.
    std::function<void(int done, int total)> progress;
};

WordExportReport exportToWord(Document &document, const QString &path,
                              const WordExportOptions &options = WordExportOptions());
