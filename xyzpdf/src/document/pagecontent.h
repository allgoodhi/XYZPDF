// What a page is made of, read out of the PDF itself: every character with its
// font, size, weight and colour; every picture with its own bytes; every drawn
// path with its geometry; every link.
//
// ---- why this exists ----
//
// The Word export used to work from what Qt PDF could give it -- a line's text and
// the box around it -- and guessed everything else from the rendered pixels: table
// rules by scanning for long dark runs, pictures by cutting out whatever ink was not
// a word, colours by sampling the glyphs. Every one of those guesses had a page it
// got wrong (a picture inside a table cell vanished, a merged cell turned the whole
// table into a picture, a bar chart came out as three pictures). PDFium hands out
// the objects the page is actually drawn from, so the export reads those instead
// and guesses nothing it can be told.
//
// Plain data on purpose: no PDFium type crosses this header, so the layout analysis
// that consumes it can be tested on pages written by hand, without a PDF.
//
// ---- coordinates ----
//
// Everything is in points, in the page as it is displayed: top-left origin, y
// downwards, the page's own /Rotate already applied. That is the space the reader
// sees and the space a Word page is laid out in, so nothing downstream ever has to
// think about PDF user space.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>

class Document;

struct ContentChar
{
    char32_t code = 0;
    // The glyph's own ink box, and the box the font reserves for it (ascent to
    // descent, advance width): the first says where the character is, the second
    // how much line it occupies.
    QRectF box;
    QRectF loose;
    // Where the baseline is, in the displayed space.
    double baseline = 0;
    // The size the character is drawn at, in points.
    double size = 0;
    // The PDF's base font name with any subset prefix ("ABCDEF+") removed.
    QString font;
    // The family the embedded font program calls itself ("Calibri" for a font a
    // writer named "CIDFont+F2"); empty when the font is not embedded.
    QString family;
    int weight = 400;
    bool italic = false;
    QColor color;
    // Radians, counter-clockwise; 0 for ordinary horizontal text.
    double angle = 0;
    // A space or line break PDFium inserted between words or lines because the
    // content stream did not carry one -- real for reading, absent from the page.
    bool generated = false;
    // Paint order, as for ContentPath: text a later opaque shape covers is not on
    // the page as a reader sees it. 0 when the page's drawing was not read.
    int z = 0;
};

struct ContentImage
{
    QRectF box;
    // The picture as it should go into a document: the PDF's own JPEG bytes when
    // the image is stored that way, otherwise PNG.
    QByteArray bytes;
    QString extension;
    QSize pixels;
    // Paint order on the page (larger is drawn later, over what is smaller).
    int z = 0;
};

struct ContentPath
{
    // Each subpath as a polygon in displayed points; curves are flattened.
    QVector<QPolygonF> polygons;
    bool fill = false;
    bool stroke = false;
    QColor fillColor;
    QColor strokeColor;
    double strokeWidth = 0;
    QRectF bounds;
    // Any curved segment, which no table rule or panel has.
    bool curved = false;
    int z = 0;
};

struct ContentLink
{
    QRectF box;
    // A web address, or empty for a jump inside the document.
    QString uri;
    int targetPage = -1;
};

struct PageContent
{
    bool ok = false;
    QSizeF size;
    QVector<ContentChar> chars;
    QVector<ContentImage> images;
    QVector<ContentPath> paths;
    QVector<ContentLink> links;
    // Characters drawn in an invisible render mode: the text layer an OCR program
    // lays over a scan. Not in `chars`; counted so a caller can tell a scan with a
    // hidden text layer from a page of real text.
    int invisibleChars = 0;
};

// The content of one page, or an empty result with ok == false for a document whose
// backend cannot say (anything but PDFium) or a page that will not load.
//
// `graphics` false reads the characters and links only -- no pictures, no paths --
// which is what a pass over every page to find its running header wants: a page of
// photographs costs its decoding only once, in the pass that writes it.
PageContent pageContentOf(Document &document, int page, bool graphics = true);

// The other direction of the mapping everything above is in: a point of the displayed
// page (points, top-left origin, /Rotate applied) as the PDF's own user space -- the
// space a content stream appended to the page is written in.
//
// This exists because the searchable-PDF export has to put recognition boxes, which are
// in the space a reader sees, back where the file draws them. Asked of PDFium's
// FPDF_DeviceToPage for the same reason displayTransform() asks FPDF_PageToDevice: the
// library knows the crop box and the page's own /Rotate, and a hand-rolled inverse would
// have to be right about both -- and to have the same idea of which corner is the origin.
//
// False when the backend cannot answer (anything but PDFium) or the page will not load;
// `userSpace` is untouched then.
bool userSpacePoint(Document &document, int page, QPointF displayed, QPointF *userSpace);
