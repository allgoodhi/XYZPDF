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
// boxes imply, with a page break between the pages -- and, for a page that is
// mostly not words, the page itself as a picture. Two kinds of page come out that
// way: one with no text layer at all, which is a scan, and one with a text layer
// and more drawn on it than written on it -- a certificate, a badge, a form, a
// page of a brochure. A reader cannot turn either into text without recognising
// it first; putting the page in as the page it is can be done exactly, and a
// document that comes out with those pages visible is worth more than one that
// comes out missing them quietly. The report counts both kinds, so that a reader
// can see which pages came out as words and which as pictures.
#pragma once

#include <QAtomicInt>
#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <functional>

class Document;

// One line of a page: the box it occupies, the text read out of that box, and the
// colour its ink turned out to be.
struct TextLine
{
    QRectF box;
    QString text;
    // Invalid for a line whose colour is nothing to write down -- black, grey, or
    // a line nothing could be sampled from. See inkColor().
    QColor color;
};

// A run of text in one colour: what a paragraph is made of, and what a Word
// paragraph is written from.
struct TextSpan
{
    QString text;
    QColor color;
};

// How a paragraph is set on the page.
//
// ---- why this is measured rather than invented ----
//
// Qt PDF does not say what font a line is set in. It does say *where* the line
// is, and where a line is says a great deal: a paragraph whose lines sit centred
// about the column is a centred paragraph, one that starts further in than the
// column is an indented one, one whose first line starts further in than its own
// body is a paragraph with a first-line indent, and the room left above it is the
// space before it. So the layout of a conversion is read out of the document
// rather than guessed at -- which is the difference between a heading that comes
// out as a heading and a page of text that comes out flat.
enum class ParagraphAlign
{
    Left,
    Center,
};

struct Paragraph
{
    QVector<TextSpan> spans;

    ParagraphAlign align = ParagraphAlign::Left;
    // Twips -- a twentieth of a point, which is what Word counts in. `indent` is
    // how far the whole paragraph is set in from the page's own text edge,
    // `firstLine` how much further in its first line starts, and `spaceBefore` how
    // much room is left above it beyond the page's own line spacing.
    int indentTwips = 0;
    int firstLineTwips = 0;
    int spaceBeforeTwips = 0;
    // Where it starts, in points down the page, which is what puts a figure cut
    // out of the page in its place among the paragraphs.
    double top = 0;

    // All of it as one string, for counting and for saying what it says.
    QString text() const;
};

// The colour of the ink inside `box` on the page as it was drawn, or an invalid
// colour when there is none worth writing down.
//
// ---- why a colour has to be sampled at all ----
//
// Qt PDF hands out a page's text and the boxes its lines occupy, and nothing about
// how any of it was set: no font, no weight, no colour. The colour is not lost,
// though -- it is in the picture, which is what the reader is looking at -- so it
// is recovered the only way left: the page is drawn once and the pixels inside
// each line's box are read back.
//
// `pixelsPerPoint` is the scale the page was drawn at, so that a box in points
// can be turned into the pixels it occupies.
//
// A line whose ink is grey -- which is nearly every line of nearly every document,
// black included -- comes back invalid rather than as a colour, because a Word
// document that says nothing about colour is one whose text follows the reader's
// own theme, and writing black into every run of it would take that away.
QColor inkColor(const QImage &page, const QRectF &box, double pixelsPerPoint);

// How much of what is drawn on a page is *not* the words that were read off it.
//
// ---- why a page has to be weighed ----
//
// A scan has no text layer and is obviously a picture. A page of a certificate is
// not so obvious: it has a text layer, and a coloured panel behind the words, and
// a letterhead, and the logos of everybody who signed it. Export its words alone
// and the page comes out as a list of lines with all of that missing, which is
// what "the conversion looks poor" means for a document like this one -- while
// putting it in as a picture keeps the page, and gives up the selectable text of
// a page nobody is going to retype anyway.
//
// The measure is taken from the same render the colours come from, so it costs
// nothing extra: the ink inside the lines is the words, and the ink outside them
// is everything else.
struct PageInk
{
    // The share of the page covered by ink that is not inside a text line and is
    // dark enough to be something drawn: a rule, a logo, a diagram, a
    // letterhead, a photograph. This is the one the decision is made on.
    double drawnShare = 0.0;
    // And the share covered by anything that is not paper, pale backgrounds
    // included. Not used to decide, because the grey behind a code listing is a
    // page of text with a background rather than a page about a picture -- but
    // kept, because it is what tells the two apart in the tests.
    double markedShare = 0.0;
};

PageInk inkOutsideLines(const QImage &page, const QVector<QRectF> &boxes, double pixelsPerPoint);

// The parts of a page that are drawn rather than written: a diagram, a table, a
// photograph, the panel behind a letterhead. In points, in the order they sit on
// the page, and never overlapping.
//
// ---- what a mixed page needs ----
//
// A page of a manual has both at once: prose, and next to it a figure. Exporting
// the page as a picture keeps the figure and throws the prose away, because what
// comes out is a picture of a page rather than a document; exporting only the
// words keeps the prose and throws the figure away. Neither is a conversion. So
// the figure is *cut out* of the page -- the page's own render, cropped to where
// the drawing is -- and put in as a picture, while the words are written as words
// where they sit.
//
// Found on a coarse grid rather than by segmenting pixels: what is wanted is a
// rectangle per figure, and cells of a few points joined to their neighbours give
// those directly. A cell counts when it has ink in it that is not inside a text
// line -- the same measure inkOutsideLines takes, at a different resolution.
QVector<QRectF> figureRegions(const QImage &page, const QVector<QRectF> &boxes,
                              double pixelsPerPoint);

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
    // Of those, the ones that went in as a page image. A scan has no text layer;
    // a page whose drawing content could not be separated safely takes this path
    // as well, because dropping it would be less faithful than retaining the page.
    int imagePages = 0;
    // And the figures cut out of pages that had words on them: a diagram, a table,
    // a photograph, put into the document where it sits on the page. They are not
    // pages -- the words of their pages are in the file as words, beside them.
    int figures = 0;
    // Ruled grids recognised as real Word tables. Their cell text remains
    // editable; diagrams continue to be inserted as pictures.
    int tables = 0;
    // Pages that produced nothing at all: no text, and no picture either -- a
    // render that failed, or a page with nothing on it that the renderer could
    // draw. Counted and reported rather than passed over: "12 of 14 pages" is a
    // fact about the document, and "we are done" is not.
    int emptyPages = 0;
    int paragraphs = 0;
};

// The boxes of one page's lines, merged, in the order the backend gives them --
// which is the document's own reading order.
//
// Cheap, and that is the point of it existing separately: the reading of a page's
// *text* is two character queries per line and is where nearly all of an export's
// time goes, while the two questions asked of a page before that -- is it mostly
// words, and what colour is its text -- need only where the lines are. A page that
// turns out to be a picture therefore never has its text read at all.
QVector<QRectF> lineBoxesOfPage(const Document &document, int page);

// The lines of one page, in the order the backend gives them (which is the
// document's own reading order), with the text of each read out of its box.
//
// `cancel`, when given, is looked at between lines -- and this is the one that
// matters, because the reading of a page is where nearly all of the time goes: a
// page of a hundred and thirty lines is nine seconds of character queries, and a
// reader who asks for a stop in the middle of one should not have to wait for it
// to finish. What comes back when it is set is a prefix of the page's lines,
// which is a thing no caller may draw: the caller checks the same flag
// immediately afterwards, and that is what makes this a stop rather than a
// shorter page.
QVector<TextLine> linesOfPage(const Document &document, int page,
                              const QAtomicInt *cancel = nullptr);

// The paragraphs of one page's lines.
//
// Lines that follow each other down the page are the same paragraph unless
// something says otherwise: a gap wider than the page's own line spacing, a line
// far shorter than the column, a line that ended its sentence short of the
// margin, or one that starts further in or further out than the paragraph did.
// Every one of those is a rule about boxes and numbers -- which is what makes this
// a function that can be tested without a PDF, on pages whose paragraphs are
// known.
//
// A line whose colour differs from the one before it starts a new span rather
// than joining the text: a paragraph with a phrase picked out in red in it is a
// paragraph with two runs, and joining them would lose the red.
QVector<Paragraph> paragraphsFromLines(const QVector<TextLine> &lines);

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
