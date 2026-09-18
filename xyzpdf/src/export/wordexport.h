// A PDF read out as a Word document.
//
// ---- what a conversion can be, from here ----
//
// Qt PDF gives a page's text as one string and the boxes its lines occupy as a
// list, with nothing pairing them. So the text of a line is asked for the way
// everything else in this program asks for it: the box's two edges turned into
// character indices, and the range between them read back (see linesOfPage).
// That gives the text and the shape it was set in, which is what a paragraph is
// made of -- and nothing about fonts, sizes or weight, because the backend does
// not have them to give.
//
// The result is the page's text, in reading order, in the paragraphs the line
// boxes imply, with a page break between the pages -- and, for a page with no
// text at all, the page itself as a picture. A scan cannot be converted to text
// by a reader that does not recognise text; putting the page in as the page it is
// can be done exactly, and a document that comes out with the scanned pages
// visible is worth more than one that comes out missing them quietly. The report
// counts both kinds, so that a reader can see which pages came out as words and
// which as pictures.
#pragma once

#include <QAtomicInt>
#include <QRectF>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <functional>

class Document;

// One line of a page: the box it occupies, and the text read out of that box.
struct TextLine
{
    QRectF box;
    QString text;
};

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
    // Of those, the ones that had no text layer and went in as a page image.
    int imagePages = 0;
    // Pages that produced nothing at all: no text, and no picture either -- a
    // render that failed, or a page with nothing on it that the renderer could
    // draw. Counted and reported rather than passed over: "12 of 14 pages" is a
    // fact about the document, and "we are done" is not.
    int emptyPages = 0;
    int paragraphs = 0;
};

// The lines of one page, in the order the backend gives them (which is the
// document's own reading order), with the text of each read out of its box.
QVector<TextLine> linesOfPage(const Document &document, int page);

// The paragraphs of one page's lines.
//
// Lines that follow each other down the page are the same paragraph unless
// something says otherwise: a gap between them, the previous one ending well
// short of the right margin, or this one starting further in than the paragraph
// did. Every one of those is a rule about boxes and numbers -- which is what
// makes this a function that can be tested without a PDF, on pages whose
// paragraphs are known.
QVector<QString> paragraphsFromLines(const QVector<TextLine> &lines);

// Reads every page and writes a .docx of what it found.
//
// Long, and it blocks: it is a text call and a selection per page -- and a render
// for every page that has no text -- each of them under the same lock as the
// renderer. Not const, because rendering is not: a page with no text layer is
// drawn to be put in as a picture, and that goes through the backend the way any
// other render does.
struct WordExportOptions
{
    // Set by the caller when the document it is reading is about to be replaced.
    // Checked between pages, which is the only place it can be noticed, because
    // half a file from one document and half from another is worse than none.
    QSharedPointer<QAtomicInt> cancel;

    // Called after each page, from the worker thread that is doing the reading.
    // A page with a picture on it takes a render -- tens to hundreds of
    // milliseconds -- so a scanned document is minutes of work, and a document
    // being converted with nothing on screen to say how far along it is looks
    // like a document that has stopped.
    std::function<void(int done, int total)> progress;
};

WordExportReport exportToWord(Document &document, const QString &path,
                              const WordExportOptions &options = WordExportOptions());
