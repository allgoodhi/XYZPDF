#include "docxwriter.h"

#include "zipwriter.h"

namespace {

// Text as XML, with the five characters that would otherwise be markup turned
// into the entities for them.
//
// The control characters are dropped rather than escaped: XML 1.0 has no way to
// represent them at all -- not even as entities -- and a PDF's text layer can
// contain them, because a PDF can contain anything. A file with one in it is a
// file Word refuses to open, which is a bad way to find out that a page had a
// stray form feed on it.
QByteArray xmlText(const QString &text)
{
    QByteArray out;
    out.reserve(text.size() + 16);
    for (const QChar character : text) {
        const ushort code = character.unicode();
        if (code < 0x20 && code != '\t' && code != '\n' && code != '\r')
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

const char *kContentTypes = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>)";

const char *kPackageRelationships = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>)";

const char *kDocumentOpen = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>)";

// An empty sectPr at the end of the body: the section properties have to be the
// last thing in a document, and a document with none is a document Word opens
// with a complaint. Everything in it is a default, which is what a conversion
// with no layout information should be asking for.
const char *kDocumentClose = R"(<w:sectPr/></w:body></w:document>)";

} // namespace

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

void DocxWriter::addParts(ZipWriter &zip) const
{
    QByteArray document = kDocumentOpen;
    document.append(body);
    document.append(kDocumentClose);

    // The names and the order are the format's, not this program's: an unpacker
    // finds the content types by name, and Word finds the document through the
    // package relationship. [Content_Types].xml goes in first because that is
    // where an unpacker looks when it is asked what kind of file this is.
    zip.addFile(QStringLiteral("[Content_Types].xml"), QByteArray(kContentTypes));
    zip.addFile(QStringLiteral("_rels/.rels"), QByteArray(kPackageRelationships));
    zip.addFile(QStringLiteral("word/document.xml"), document);
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
