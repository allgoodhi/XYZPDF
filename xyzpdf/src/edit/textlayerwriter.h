// The invisible text layer a scanned page gains: the words recognition read, written
// into the page's own content stream so that the page looks the same and can be
// searched and selected.
//
// ---- what "looks the same" costs ----
//
// Append, never rewrite. qpdf adds the layer as a stream of its own at the end of the
// page's /Contents (copying a shared array first, the rule appendWatermarkContent() in
// documenteditor.cpp records) and wraps the page's own streams in q ... Q, so that the
// state they left behind cannot move these coordinates. The words are drawn with text
// render mode 3 -- "neither fill nor stroke" (PDF 32000-1 sec.9.3.6, table 106) -- so they
// have no pixels, and the same page rendered before and after is the same picture.
//
// ---- the shared font ----
//
// One Type0/CIDFontType2 font for the whole file (PDF 32000-1 sec.9.7.4 and 9.7.6),
// with /Encoding /Identity-H and /CIDToGIDMap /Identity, so a CID is a glyph index and
// the only thing that has to be true of the embedded program is that it has that many
// glyphs. They are empty and one em wide (glyphlessfont.h); /ToUnicode is what makes
// the words readable to a copy or a search, because no font cmap is involved.
//
// The font object is shared by every page, which is what keeps a five-hundred-page
// scan from carrying five hundred copies of it.
//
// ---- one character per show-text is not enough ----
//
// A show-text of exactly one two-byte code whose /ToUnicode value is above U+00FF is left
// out of the text a reader can reach -- measured with this program's own engine, on three
// shapes of the same page: seven Chinese characters as seven show-texts read back as
// nothing, the same seven as one show-text read back as seven, and a single Latin-1
// character read back. So a run of one-character words, which is what the recognizer hands
// out for Chinese, is written as one string over the run's box. See
// joinLoneWideCharacters() in the .cpp.
//
// ---- what it will not do ----
//
// It will not write the file it is reading, and it will not write a document whose
// words all turned out to be empty: both are refusals with a sentence rather than a
// quiet success, and both are checked before a byte is staged.
#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

// One recognized word, in the space a reader sees: points, top-left origin, the page's
// /Rotate applied -- the space pageContentOf() reports, and recognition's boxes once
// they have been divided by the render's own scale.
struct TextLayerWord
{
    QString text;
    QRectF box;
    // The size of the line the word sits on, in points. The writer puts the baseline
    // one descent above the box's bottom -- the embedded font is 0.8 em above the
    // baseline and 0.2 below -- so the box a reader selects is the box recognition
    // measured.
    double size = 0;
};

struct TextLayerPage
{
    // Zero-based, in the document's own order.
    int page = 0;
    // Displayed points to PDF user space, which is what a content stream draws in.
    // Column 1 is one point to the right on screen, column 2 one point down; built by
    // the caller from userSpacePoint(), because the caller is the side that holds the
    // document. A page marked /Rotate needs every column, not just a translation.
    QTransform displayedToUser;
    QVector<TextLayerWord> words;
};

struct TextLayerWriteReport
{
    bool ok = false;
    // A sentence to show, set whenever ok is false.
    QString error;
    QString path;
    // Words actually written: a word with no text, no box or no size is skipped.
    int words = 0;
    // The pages that gained a stream.
    int pages = 0;
};

// Writes `source` plus the layer described by `pages` to `destination`.
//
// The source is opened again through qpdf, read-only; the output is written beside the
// destination and moved into place only after qpdf can open it again with the page
// count the source had -- the same shape as the in-place save (docs/handoff-03.md F8),
// for the same reason: a half-written PDF is worse than a refusal.
//
// `password` is what the reader gave to open the document, and it is what qpdf is
// handed; an encrypted source stays encrypted (QPDFWriter::setPreserveEncryption), and
// a source qpdf cannot open is refused with the parser's own sentence rather than
// writing an unencrypted copy nobody asked for.
TextLayerWriteReport writeTextLayer(const QString &source, const QString &destination,
                                    const QString &password,
                                    const QVector<TextLayerPage> &pages);
