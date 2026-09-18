// A .docx, written from the paragraphs the reader can see.
//
// ---- what is in the file ----
//
// Three parts, which is the least a document can be and still open in Word:
// the content types, a package relationship pointing at the document, and the
// document itself as WordprocessingML. Every paragraph is a <w:p> with one run
// of text in it; a page of the PDF that was written before it is a page break.
//
// ---- what is not ----
//
// Fonts, sizes, bold and italic, tables, pictures. Qt PDF hands out a page's
// text and the boxes its lines occupy and nothing about how they were set -- no
// font name, no weight -- and a conversion that invented them would look like a
// better conversion than it is. What comes out is what the reader can already
// copy out of the page, arranged the way it was arranged on the page: the same
// text, in the same order, in the paragraph structure the boxes imply.
#pragma once

#include <QByteArray>
#include <QString>

class ZipWriter;

class DocxWriter
{
public:
    // One paragraph. `pageBreakBefore` starts it at the top of a new page, which
    // is how the pages of a PDF stay pages of the document.
    void addParagraph(const QString &text, bool pageBreakBefore = false);

    int paragraphCount() const { return paragraphs; }

    QByteArray toBytes() const;

    bool save(const QString &path, QString *error) const;

private:
    // The parts, put into an archive. One listing, used by both an in-memory
    // answer and a file: a test checks the bytes this class would have written
    // to disk, which is only true while there is one place the parts are named.
    void addParts(ZipWriter &zip) const;

    // The <w:p> elements, in order, as they will be written. Built as the
    // paragraphs are added rather than kept as strings: the escaping and the
    // element shape are this class's business, and doing it twice is doing it
    // differently twice.
    QByteArray body;
    int paragraphs = 0;
};
