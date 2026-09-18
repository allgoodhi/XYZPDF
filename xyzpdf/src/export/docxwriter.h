// A .docx, written from the paragraphs the reader can see.
//
// ---- what is in the file ----
//
// Four parts, which is the least a document can be and still open in Word: the
// content types, a package relationship pointing at the document, the document
// itself as WordprocessingML, and -- when there are pictures -- the relationships
// between the document and the images in it.
//
// ---- what a page can be ----
//
// A paragraph of text, or a picture of a page. Text is what a page with a text
// layer becomes: the same text, in the same order, in the paragraph structure the
// boxes imply, with a page break between the pages. A picture is what a page with
// no text layer becomes -- a scan, a drawing, a photograph of a page -- because
// there is nothing to read out of it and the honest conversion of a picture is
// the picture.
//
// ---- what is not in the file ----
//
// Fonts, sizes, bold and italic, tables, and the pictures *inside* a page that
// also has text. Qt PDF hands out a page's text and the boxes its lines occupy
// and nothing about how they were set -- no font name, no weight -- and a
// conversion that invented them would look like a better conversion than it is.
#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

class ZipWriter;

class DocxWriter
{
public:
    // One paragraph. `pageBreakBefore` starts it at the top of a new page, which
    // is how the pages of a PDF stay pages of the document.
    void addParagraph(const QString &text, bool pageBreakBefore = false);

    // A page put in as a picture, in a paragraph of its own, centred.
    //
    // `bytes` are the encoded image and `extension` what they are: "jpeg" or
    // "png". `sizePoints` is the size the picture should appear at, which is
    // scaled down to fit the page described by setPageSize() and left alone when
    // it already fits.
    void addImage(const QByteArray &bytes, const QString &extension, QSizeF sizePoints,
                  bool pageBreakBefore = false);

    // The page the document is written on, and the margin around its edge, in
    // points.
    //
    // Told rather than assumed: a page image is the shape of the page it came
    // from, and a converted document that put an A4 page image on a Letter page
    // would either shrink it or cut it in half. Anything not set is A4 with a
    // half-inch margin, which is what a document with no page of its own gets.
    void setPageSize(QSizeF points);
    void setPageMargin(double points);

    int paragraphCount() const { return paragraphs; }
    int imageCount() const { return int(images.size()); }

    QByteArray toBytes() const;

    bool save(const QString &path, QString *error) const;

private:
    struct Image
    {
        // The name it has inside the archive, and the id it is referenced by
        // from the document: the two things a picture needs to be in a Word file.
        QString partName;
        QString relationshipId;
        QByteArray bytes;
    };

    // The parts, put into an archive. One listing, used by both an in-memory
    // answer and a file: a test checks the bytes this class would have written
    // to disk, which is only true while there is one place the parts are named.
    void addParts(ZipWriter &zip) const;

    // The <w:p> elements, in order, as they will be written. Built as the
    // paragraphs are added rather than kept as strings: the escaping and the
    // element shape are this class's business, and doing it twice is doing it
    // differently twice.
    QByteArray body;
    QVector<Image> images;
    int paragraphs = 0;

    QSizeF page;
    double margin = 36.0; // half an inch
};
