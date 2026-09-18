#include "docxwriter.h"

#include "zipwriter.h"

#include <QtGlobal>

namespace {

// Text as XML, with the five characters that would otherwise be markup turned
// into the entities for them.
//
// What XML cannot carry at all is dropped rather than escaped, because there is
// no entity for it and no way to write it down: the control characters below
// space, the two noncharacters at the end of the basic plane (#xFFFE and #xFFFF,
// which the format's character production excludes), and half of a surrogate
// pair. A PDF's text layer can contain any of them -- it is a byte stream with a
// font attached -- and a document with one in it is not a document with an odd
// character in it, it is a document Word refuses to open. Found in a real manual,
// where a line break had been encoded as #xFFFE.
//
// A *pair* of surrogates is a character like any other -- an emoji, a rare
// ideograph -- and is carried across whole.
QByteArray xmlText(const QString &text)
{
    QByteArray out;
    out.reserve(text.size() + 16);
    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        const ushort code = character.unicode();
        if (character.isHighSurrogate()) {
            if (index + 1 < text.size() && text.at(index + 1).isLowSurrogate()) {
                out.append(text.mid(index, 2).toUtf8());
                ++index;
            }
            continue;
        }
        if (character.isLowSurrogate())
            continue;
        if (code < 0x20 && code != '\t' && code != '\n' && code != '\r')
            continue;
        if (code == 0xFFFE || code == 0xFFFF)
            continue;
        switch (code) {
        case '&':
            out.append("&amp;");
            break;
        case '<':
            out.append("&lt;");
            break;
        case '>':
            out.append("&gt;");
            break;
        case '"':
            out.append("&quot;");
            break;
        case '\'':
            out.append("&apos;");
            break;
        default:
            // Through a QString: a QChar has no UTF-8 of its own, and a lone
            // surrogate of a pair outside the basic plane is still better
            // carried across than dropped.
            out.append(QString(character).toUtf8());
            break;
        }
    }
    return out;
}

// A length as Word counts them: twentieths of a point for the page, and the
// English Metric Unit -- a millionth of a metre short by a factor -- for a
// picture's size. Both are the format's, and both are integers.
qint64 twips(double points)
{
    return qint64(points * 20.0 + 0.5);
}

qint64 emu(double points)
{
    return qint64(points * 12700.0 + 0.5);
}

const char *kContentTypes = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="jpeg" ContentType="image/jpeg"/><Default Extension="png" ContentType="image/png"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>)";

const char *kPackageRelationships = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>)";

// The namespaces the document needs: w for the text, and -- because a picture is
// not a text construct -- r for the relationship it points at, wp for the drawing
// in a Word document, a for the drawing itself and pic for the picture in it.
// Declared once on the root rather than on each element, which is what every
// document Word writes does.
const char *kDocumentOpen = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"><w:body>)";

const char *kDocumentClose = R"(</w:body></w:document>)";

const char *kDrawingUri = "http://schemas.openxmlformats.org/drawingml/2006/picture";

} // namespace

void DocxWriter::setPageSize(QSizeF points)
{
    page = points;
}

void DocxWriter::setPageMargin(double points)
{
    margin = points;
}

void DocxWriter::addParagraph(const QString &text, bool pageBreakBefore)
{
    if (text.isEmpty() && !pageBreakBefore) {
        // An empty paragraph, which is a blank line and is worth keeping: it is
        // how a gap between two blocks of text survives the conversion.
        body.append("<w:p/>");
        ++paragraphs;
        return;
    }

    body.append("<w:p>");
    if (pageBreakBefore)
        body.append("<w:r><w:br w:type=\"page\"/></w:r>");
    if (!text.isEmpty()) {
        // xml:space="preserve": without it Word is entitled to strip the leading
        // and trailing spaces of the text, and a line of a PDF that was indented
        // is a line whose indentation is a space or two.
        body.append("<w:r><w:t xml:space=\"preserve\">");
        body.append(xmlText(text));
        body.append("</w:t></w:r>");
    }
    body.append("</w:p>");
    ++paragraphs;
}

void DocxWriter::addImage(const QByteArray &bytes, const QString &extension, QSizeF sizePoints,
                          bool pageBreakBefore)
{
    if (bytes.isEmpty() || extension.isEmpty())
        return;

    // The picture is scaled to the room the page has for it, keeping its shape:
    // a page image is the shape of the page it came from, and one that does not
    // fit is a page that runs off the paper or is cut in half.
    const QSizeF room = page.isEmpty() ? sizePoints
                                       : QSizeF(qMax(1.0, page.width() - 2 * margin),
                                                qMax(1.0, page.height() - 2 * margin));
    if (!room.isEmpty() && (sizePoints.width() > room.width() || sizePoints.height() > room.height())) {
        const double scale = qMin(room.width() / sizePoints.width(),
                                  room.height() / sizePoints.height());
        sizePoints = QSizeF(sizePoints.width() * scale, sizePoints.height() * scale);
    }

    Image image;
    image.partName = QStringLiteral("word/media/image%1.%2")
                         .arg(images.size() + 1)
                         .arg(extension);
    image.relationshipId = QStringLiteral("rId%1").arg(images.size() + 2); // rId1 is the document itself
    image.bytes = bytes;
    images.append(image);

    const qint64 cx = emu(sizePoints.width());
    const qint64 cy = emu(sizePoints.height());
    const int id = images.size();

    body.append("<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>");
    if (pageBreakBefore)
        body.append("<w:r><w:br w:type=\"page\"/></w:r>");
    body.append("<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">");
    body.append(QStringLiteral("<wp:extent cx=\"%1\" cy=\"%2\"/>").arg(cx).arg(cy).toUtf8());
    body.append(QStringLiteral("<wp:docPr id=\"%1\" name=\"page-%1\"/>").arg(id).toUtf8());
    body.append(QStringLiteral("<a:graphic><a:graphicData uri=\"%1\"><pic:pic>")
                    .arg(QLatin1String(kDrawingUri))
                    .toUtf8());
    body.append(QStringLiteral("<pic:nvPicPr><pic:cNvPr id=\"%1\" name=\"page-%1\"/><pic:cNvPicPr/>"
                               "</pic:nvPicPr>")
                    .arg(id)
                    .toUtf8());
    body.append(QStringLiteral("<pic:blipFill><a:blip r:embed=\"%1\"/>"
                               "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>")
                    .arg(image.relationshipId)
                    .toUtf8());
    body.append(QStringLiteral("<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/>"
                               "</a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>")
                    .arg(cx)
                    .arg(cy)
                    .toUtf8());
    body.append("</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>");
}

void DocxWriter::addParts(ZipWriter &zip) const
{
    QByteArray document = kDocumentOpen;
    document.append(body);

    // The section properties come last and describe the page the document is
    // written on. A page image is put on a page of the size it came from, which
    // is why the caller tells this class what that size is; a document whose
    // paper is the size of the PDF's pages is a document whose pictures are not
    // cut in half by a reader's default Letter page.
    const QSizeF paper = page.isEmpty() ? QSizeF(595, 842) : page;
    const qint64 marginTwips = twips(margin);
    QByteArray section;
    section.append("<w:sectPr><w:pgSz");
    section.append(QStringLiteral(" w:w=\"%1\" w:h=\"%2\"").arg(twips(paper.width())).arg(twips(paper.height())).toUtf8());
    if (paper.width() > paper.height())
        section.append(" w:orient=\"landscape\"");
    section.append("/>");
    section.append(QStringLiteral("<w:pgMar w:top=\"%1\" w:right=\"%1\" w:bottom=\"%1\" w:left=\"%1\""
                                  " w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/>")
                       .arg(marginTwips)
                       .toUtf8());
    section.append("</w:sectPr>");

    document.append(section);
    document.append(kDocumentClose);

    // The names and the order are the format's, not this program's: an unpacker
    // finds the content types by name, and Word finds the document through the
    // package relationship. [Content_Types].xml goes in first because that is
    // where an unpacker looks when it is asked what kind of file this is.
    zip.addFile(QStringLiteral("[Content_Types].xml"), QByteArray(kContentTypes));
    zip.addFile(QStringLiteral("_rels/.rels"), QByteArray(kPackageRelationships));
    zip.addFile(QStringLiteral("word/document.xml"), document);

    // The images, and the part that says which of them the document refers to by
    // which name. A document with no pictures still has this part, empty: a
    // missing relationship part and one with nothing in it are the same document
    // to a reader, and having it always means there is one path through here
    // rather than two.
    QByteArray relationships(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (const Image &image : images) {
        relationships.append(
            QStringLiteral("<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/"
                           "officeDocument/2006/relationships/image\" Target=\"%2\"/>")
                .arg(image.relationshipId, QString(image.partName).remove(QStringLiteral("word/")))
                .toUtf8());
        zip.addFile(image.partName, image.bytes);
    }
    relationships.append("</Relationships>");
    zip.addFile(QStringLiteral("word/_rels/document.xml.rels"), relationships);
}

QByteArray DocxWriter::toBytes() const
{
    ZipWriter zip;
    addParts(zip);
    return zip.toBytes();
}

bool DocxWriter::save(const QString &path, QString *error) const
{
    ZipWriter zip;
    addParts(zip);
    return zip.write(path, error);
}
