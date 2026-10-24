#include "docxwriter.h"

#include "zipwriter.h"

#include <QCryptographicHash>

#include <cmath>

namespace {

const char *kWordNamespaces =
    R"( xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main")"
    R"( xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")"
    R"( xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing")"
    R"( xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main")"
    R"( xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture")";

const char *kRelImage = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
const char *kRelLink = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
const char *kRelHeader = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/header";
const char *kRelFooter = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer";

// Text as XML character data. A control character has no spelling in XML 1.0 at
// all -- not even escaped -- and one stray byte of it makes Word refuse the whole
// file, so those are dropped rather than written.
QByteArray xmlText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D)
            continue;
        if (u == 0xFFFE || u == 0xFFFF)
            continue;
        switch (u) {
        case '&':
            out += QLatin1String("&amp;");
            break;
        case '<':
            out += QLatin1String("&lt;");
            break;
        case '>':
            out += QLatin1String("&gt;");
            break;
        case '"':
            out += QLatin1String("&quot;");
            break;
        default:
            out += c;
        }
    }
    return out.toUtf8();
}

QByteArray number(qint64 value)
{
    return QByteArray::number(value);
}

qint64 twips(double points)
{
    return qint64(std::llround(points * 20.0));
}

qint64 emu(double points)
{
    return qint64(std::llround(points * 12700.0));
}

QByteArray colourHex(const QColor &colour)
{
    return colour.name(QColor::HexRgb).mid(1).toUpper().toLatin1();
}

QByteArray borderXml(const char *side, const LayoutBorder &border)
{
    if (!border.present)
        return QByteArray("<w:") + side + " w:val=\"nil\"/>";
    // Eighths of a point, within what Word accepts (2..96).
    const int size = qBound(2, int(std::lround(border.width * 8)), 96);
    return QByteArray("<w:") + side + " w:val=\"single\" w:sz=\"" + number(size)
        + "\" w:space=\"0\" w:color=\"" + colourHex(border.color) + "\"/>";
}

QByteArray runPropertiesXml(const RunStyle &style)
{
    QByteArray xml = "<w:rPr>";
    if (!style.font.isEmpty()) {
        const QByteArray family = xmlText(style.font);
        xml += "<w:rFonts w:ascii=\"" + family + "\" w:hAnsi=\"" + family + "\" w:eastAsia=\""
            + family + "\" w:cs=\"" + family + "\"/>";
    }
    if (style.bold)
        xml += "<w:b/><w:bCs/>";
    if (style.italic)
        xml += "<w:i/><w:iCs/>";
    if (style.color.isValid())
        xml += "<w:color w:val=\"" + colourHex(style.color) + "\"/>";
    if (const qint64 spacing = twips(style.spacing))
        xml += "<w:spacing w:val=\"" + number(spacing) + "\"/>";
    const qint64 halfPoints = qMax<qint64>(2, std::llround(style.size * 2));
    xml += "<w:sz w:val=\"" + number(halfPoints) + "\"/><w:szCs w:val=\"" + number(halfPoints) + "\"/>";
    if (style.underline)
        xml += "<w:u w:val=\"single\"/>";
    if (style.superscript)
        xml += "<w:vertAlign w:val=\"superscript\"/>";
    else if (style.subscript)
        xml += "<w:vertAlign w:val=\"subscript\"/>";
    xml += "</w:rPr>";
    return xml;
}

QByteArray textRunXml(const QString &text, const RunStyle &style)
{
    // '\t' is a tab and '\n' a line break inside the paragraph (the PDF's own, kept
    // so a line ends where the PDF ends it).
    QByteArray xml = "<w:r>" + runPropertiesXml(style);
    QString piece;
    const auto flush = [&] {
        if (!piece.isEmpty())
            xml += "<w:t xml:space=\"preserve\">" + xmlText(piece) + "</w:t>";
        piece.clear();
    };
    for (const QChar c : text) {
        if (c == QLatin1Char('\t')) {
            flush();
            xml += "<w:tab/>";
        } else if (c == QLatin1Char('\n')) {
            flush();
            xml += "<w:br/>";
        } else {
            piece += c;
        }
    }
    flush();
    xml += "</w:r>";
    return xml;
}

// The paragraph mark's own run, one point high, so an image paragraph or a spacer
// is no taller than what it holds.
const char *kTinyMark = "<w:rPr><w:sz w:val=\"2\"/><w:szCs w:val=\"2\"/></w:rPr>";

// Paragraph properties every paragraph of a converted page shares: no widow
// control (it moves lines between pages), no grid snapping and no automatic space
// between East Asian and Latin text (both change where a line ends).
const char *kLayoutProperties = "<w:widowControl w:val=\"0\"/>";
const char *kLayoutTail = "<w:autoSpaceDE w:val=\"0\"/><w:autoSpaceDN w:val=\"0\"/>"
                          "<w:adjustRightInd w:val=\"0\"/><w:snapToGrid w:val=\"0\"/>";

} // namespace

void DocxWriter::setDefaultFont(const QString &family, double size)
{
    if (!family.isEmpty())
        defaultFamily = family;
    if (size > 0)
        defaultSize = size;
}

QString DocxWriter::relationship(const QString &type, const QString &target, bool external)
{
    Relationship rel;
    rel.id = QStringLiteral("rId%1").arg(nextRelationship++);
    rel.type = type;
    rel.target = target;
    rel.external = external;
    relationships.append(rel);
    return rel.id;
}

QString DocxWriter::mediaFor(const QByteArray &bytes, const QString &extension)
{
    // One copy of a picture however often it appears: a logo on every page is one
    // file in the package.
    const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    const auto known = mediaByHash.constFind(hash);
    if (known != mediaByHash.constEnd())
        return known.value();
    Media item;
    item.partName = QStringLiteral("media/image%1.%2").arg(media.size() + 1).arg(extension);
    item.relationshipId = relationship(QLatin1String(kRelImage), item.partName, false);
    item.bytes = bytes;
    media.append(item);
    mediaByHash.insert(hash, item.relationshipId);
    return item.relationshipId;
}

QByteArray DocxWriter::drawingXml(const LayoutImage &image, bool anchored, bool behind)
{
    const QString relationshipId = mediaFor(image.bytes, image.extension);
    const int id = ++drawingId;
    const QByteArray cx = number(qMax<qint64>(1, emu(image.box.width())));
    const QByteArray cy = number(qMax<qint64>(1, emu(image.box.height())));
    const QByteArray graphic =
        "<wp:docPr id=\"" + number(id) + "\" name=\"Picture " + number(id) + "\"/>"
        "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"" + number(id) + "\" name=\"image" + number(id) + "\"/>"
        "<pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"" + relationshipId.toLatin1()
        + "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
          "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + cx + "\" cy=\"" + cy + "\"/></a:xfrm>"
          "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic>";
    QByteArray xml = "<w:r><w:rPr><w:noProof/></w:rPr><w:drawing>";
    if (!anchored) {
        xml += "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"" + cx
            + "\" cy=\"" + cy + "\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>" + graphic
            + "</wp:inline>";
    } else {
        xml += "<wp:anchor distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\" simplePos=\"0\" relativeHeight=\""
            + number(behind ? 100 + id : 251658240 + id) + "\" behindDoc=\"" + (behind ? "1" : "0")
            + "\" locked=\"0\" layoutInCell=\"0\" allowOverlap=\"1\"><wp:simplePos x=\"0\" y=\"0\"/>"
              "<wp:positionH relativeFrom=\"page\"><wp:posOffset>" + number(emu(image.box.left()))
            + "</wp:posOffset></wp:positionH><wp:positionV relativeFrom=\"page\"><wp:posOffset>"
            + number(emu(image.box.top())) + "</wp:posOffset></wp:positionV><wp:extent cx=\"" + cx
            + "\" cy=\"" + cy + "\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/><wp:wrapNone/>"
            + graphic + "</wp:anchor>";
    }
    xml += "</w:drawing></w:r>";
    ++images;
    return xml;
}

QByteArray DocxWriter::runsXml(const QVector<LayoutRun> &runs)
{
    QByteArray xml;
    int i = 0;
    while (i < runs.size()) {
        const LayoutRun &run = runs.at(i);
        if (!run.field.isEmpty()) {
            xml += "<w:fldSimple w:instr=\" " + xmlText(run.field) + " \">"
                + textRunXml(run.text, run.style) + "</w:fldSimple>";
            ++i;
            continue;
        }
        if (run.uri.isEmpty() && run.targetPage < 0) {
            xml += textRunXml(run.text, run.style);
            ++i;
            continue;
        }
        // Consecutive runs of one link are one hyperlink.
        int j = i;
        QByteArray inner;
        while (j < runs.size() && runs.at(j).uri == run.uri && runs.at(j).targetPage == run.targetPage
               && runs.at(j).field.isEmpty()) {
            inner += textRunXml(runs.at(j).text, runs.at(j).style);
            ++j;
        }
        if (!run.uri.isEmpty()) {
            const QString id = relationship(QLatin1String(kRelLink), run.uri, true);
            xml += "<w:hyperlink r:id=\"" + id.toLatin1() + "\" w:history=\"1\">" + inner + "</w:hyperlink>";
        } else {
            xml += "<w:hyperlink w:anchor=\"page" + number(run.targetPage + 1) + "\" w:history=\"1\">"
                + inner + "</w:hyperlink>";
        }
        i = j;
    }
    return xml;
}

QByteArray DocxWriter::paragraphXml(const LayoutParagraph &paragraph, const QByteArray &leading,
                                    bool sectionBreak)
{
    QByteArray xml = "<w:p><w:pPr>";
    const double line = qMax(1.0, paragraph.lineSpacing);
    if (paragraph.framed) {
        // A frame at the paragraph's place on the page, as wide as its text with a
        // little room, starting where its first line's box starts.
        const double size = paragraph.runs.isEmpty() ? 11 : paragraph.runs.first().style.size;
        const double top = paragraph.firstBaseline - (line - 0.21 * size);
        xml += "<w:framePr w:w=\"" + number(twips(paragraph.box.width() + 4)) + "\" w:h=\""
            + number(twips(line * paragraph.lineCount)) + "\" w:hRule=\"atLeast\" w:wrap=\"around\" "
              "w:vAnchor=\"page\" w:hAnchor=\"page\" w:x=\"" + number(twips(paragraph.box.left()))
            + "\" w:y=\"" + number(twips(top)) + "\"/>";
    }
    xml += kLayoutProperties;
    if (!paragraph.tabs.isEmpty()) {
        xml += "<w:tabs>";
        for (const LayoutTab &tab : paragraph.tabs) {
            // Tab stops are measured from the text column's edge, not from the
            // paragraph's indent.
            xml += QByteArray("<w:tab w:val=\"") + (tab.right ? "right" : "left") + "\" w:pos=\""
                + number(twips((paragraph.framed ? 0.0 : paragraph.left) + tab.position)) + "\"/>";
        }
        xml += "</w:tabs>";
    }
    xml += kLayoutTail;
    xml += "<w:spacing w:before=\"" + number(qMax<qint64>(0, twips(paragraph.spaceBefore)))
        + "\" w:after=\"0\" w:line=\"" + number(twips(line)) + "\" w:lineRule=\"exact\"/>";
    // A frame's paragraph is laid out inside the frame: no indent of its own.
    const double left = paragraph.framed ? 0.0 : paragraph.left;
    const double right = paragraph.framed ? 0.0 : paragraph.right;
    xml += "<w:ind w:left=\"" + number(twips(left)) + "\" w:right=\"" + number(twips(right)) + "\"";
    if (paragraph.firstLine > 0.25)
        xml += " w:firstLine=\"" + number(twips(paragraph.firstLine)) + "\"";
    else if (paragraph.firstLine < -0.25)
        xml += " w:hanging=\"" + number(twips(-paragraph.firstLine)) + "\"";
    xml += "/>";
    switch (paragraph.align) {
    case LayoutAlign::Left:
        xml += "<w:jc w:val=\"left\"/>";
        break;
    case LayoutAlign::Center:
        xml += "<w:jc w:val=\"center\"/>";
        break;
    case LayoutAlign::Right:
        xml += "<w:jc w:val=\"right\"/>";
        break;
    case LayoutAlign::Justify:
        xml += "<w:jc w:val=\"both\"/>";
        break;
    }
    if (paragraph.outlineLevel >= 0)
        xml += "<w:outlineLvl w:val=\"" + number(qBound(0, paragraph.outlineLevel, 8)) + "\"/>";
    if (!paragraph.runs.isEmpty())
        xml += runPropertiesXml(paragraph.runs.last().style);
    if (sectionBreak)
        xml += "\x01SECT\x01";
    xml += "</w:pPr>" + leading + runsXml(paragraph.runs) + "</w:p>";
    ++paragraphs;
    return xml;
}

QByteArray DocxWriter::imageParagraphXml(const LayoutBlock &block, const QByteArray &leading,
                                         bool sectionBreak)
{
    QByteArray xml = "<w:p><w:pPr>";
    xml += kLayoutProperties;
    xml += kLayoutTail;
    xml += "<w:spacing w:before=\"" + number(qMax<qint64>(0, twips(block.spaceBefore)))
        + "\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>";
    xml += "<w:ind w:left=\"" + number(twips(block.indent)) + "\" w:right=\"0\"/><w:jc w:val=\"left\"/>";
    xml += kTinyMark;
    if (sectionBreak)
        xml += "\x01SECT\x01";
    xml += "</w:pPr>" + leading + drawingXml(block.image, false, false) + "</w:p>";
    return xml;
}

QByteArray DocxWriter::spacerXml(double height, const QByteArray &leading, bool sectionBreak)
{
    QByteArray xml = "<w:p><w:pPr>";
    xml += kLayoutProperties;
    xml += kLayoutTail;
    xml += "<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"" + number(qMax<qint64>(20, twips(height)))
        + "\" w:lineRule=\"exact\"/>";
    xml += kTinyMark;
    if (sectionBreak)
        xml += "\x01SECT\x01";
    xml += "</w:pPr>" + leading + "</w:p>";
    return xml;
}

QByteArray DocxWriter::tableXml(const LayoutTable &table, double indent)
{
    const int columns = table.columns.size() - 1;
    const int rows = table.rows.size() - 1;
    if (columns <= 0 || rows <= 0)
        return {};
    ++tables;
    const double total = table.columns.last() - table.columns.first();
    QByteArray xml = "<w:tbl><w:tblPr><w:tblW w:w=\"" + number(twips(total)) + "\" w:type=\"dxa\"/>"
        "<w:tblInd w:w=\"" + number(twips(indent)) + "\" w:type=\"dxa\"/>"
        "<w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/>"
        "<w:left w:w=\"0\" w:type=\"dxa\"/><w:bottom w:w=\"0\" w:type=\"dxa\"/>"
        "<w:right w:w=\"0\" w:type=\"dxa\"/></w:tblCellMar><w:tblLook w:val=\"0000\"/></w:tblPr><w:tblGrid>";
    for (int c = 0; c < columns; ++c)
        xml += "<w:gridCol w:w=\"" + number(twips(table.columns.at(c + 1) - table.columns.at(c))) + "\"/>";
    xml += "</w:tblGrid>";
    for (int r = 0; r < rows; ++r) {
        // Word adds a row's top border to its height, so the border comes off what
        // is asked for: otherwise every row of a ruled table is that much taller
        // than the PDF's and the page below the table slides down (measured: 0.6 pt
        // a row with 0.7 pt rules).
        double border = 0;
        for (const LayoutCell &cell : table.cells) {
            if (cell.row == r && cell.top.present)
                border = qMax(border, cell.top.width);
        }
        const double height = qMax(1.0, table.rows.at(r + 1) - table.rows.at(r) - border);
        xml += "<w:tr><w:trPr><w:cantSplit/><w:trHeight w:val=\"" + number(twips(height))
            + "\" w:hRule=\"atLeast\"/></w:trPr>";
        int c = 0;
        while (c < columns) {
            const LayoutCell *cell = table.cellAt(r, c);
            if (!cell) {
                xml += "<w:tc><w:tcPr><w:tcW w:w=\"" + number(twips(table.columns.at(c + 1) - table.columns.at(c)))
                    + "\" w:type=\"dxa\"/></w:tcPr><w:p><w:pPr>" + kTinyMark + "</w:pPr></w:p></w:tc>";
                ++c;
                continue;
            }
            const double width = table.columns.at(cell->column + cell->columnSpan) - table.columns.at(cell->column);
            QByteArray properties = "<w:tcPr><w:tcW w:w=\"" + number(twips(width)) + "\" w:type=\"dxa\"/>";
            if (cell->columnSpan > 1)
                properties += "<w:gridSpan w:val=\"" + number(cell->columnSpan) + "\"/>";
            const bool continuation = cell->row < r;
            if (cell->rowSpan > 1)
                properties += continuation ? "<w:vMerge/>" : "<w:vMerge w:val=\"restart\"/>";
            properties += "<w:tcBorders>" + borderXml("top", cell->top) + borderXml("left", cell->left)
                + borderXml("bottom", cell->bottom) + borderXml("right", cell->right) + "</w:tcBorders>";
            if (cell->shading.isValid())
                properties += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + colourHex(cell->shading) + "\"/>";
            properties += "<w:tcMar><w:top w:w=\"" + number(twips(cell->padTop)) + "\" w:type=\"dxa\"/><w:left w:w=\""
                + number(twips(cell->padLeft)) + "\" w:type=\"dxa\"/><w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\""
                + number(twips(cell->padRight)) + "\" w:type=\"dxa\"/></w:tcMar>";
            properties += "</w:tcPr>";
            xml += "<w:tc>" + properties;
            if (continuation || cell->blocks.isEmpty()) {
                xml += QByteArray("<w:p><w:pPr>") + kTinyMark + "</w:pPr></w:p>";
            } else {
                QByteArray inner = blocksXml(cell->blocks, QByteArray(), false);
                // A cell's last element has to be a paragraph.
                if (cell->blocks.last().kind == LayoutBlock::Kind::Table)
                    inner += QByteArray("<w:p><w:pPr>") + kTinyMark + "</w:pPr></w:p>";
                xml += inner;
            }
            xml += "</w:tc>";
            c += cell->columnSpan;
        }
        xml += "</w:tr>";
    }
    xml += "</w:tbl>";
    return xml;
}

QByteArray DocxWriter::blocksXml(const QVector<LayoutBlock> &blocks, QByteArray leading,
                                 bool sectionBreak)
{
    QByteArray xml;
    for (int i = 0; i < blocks.size(); ++i) {
        const LayoutBlock &block = blocks.at(i);
        const bool last = i == blocks.size() - 1;
        switch (block.kind) {
        case LayoutBlock::Kind::Paragraph:
            if (block.paragraph.framed) {
                // A frame carries neither the page's bookmark and anchors nor its
                // section break: those go with the next block in the flow.
                xml += paragraphXml(block.paragraph, QByteArray(), false);
                if (last && (sectionBreak || !leading.isEmpty())) {
                    xml += spacerXml(1.0, leading, sectionBreak);
                    leading.clear();
                }
                break;
            }
            xml += paragraphXml(block.paragraph, leading, sectionBreak && last);
            leading.clear();
            break;
        case LayoutBlock::Kind::Image:
            xml += imageParagraphXml(block, leading, sectionBreak && last);
            leading.clear();
            break;
        case LayoutBlock::Kind::Table: {
            // A table has no room above it of its own: a paragraph exactly that
            // high stands in. One is needed anyway between two tables, which Word
            // would otherwise join into one.
            const bool afterTable = i > 0 && blocks.at(i - 1).kind == LayoutBlock::Kind::Table;
            if (block.spaceBefore > 0.3 || afterTable || !leading.isEmpty())
                xml += spacerXml(qMax(1.0, block.spaceBefore), leading, false);
            leading.clear();
            xml += tableXml(*block.table, block.indent);
            if (last && sectionBreak)
                xml += spacerXml(1.0, QByteArray(), true);
            break;
        }
        }
    }
    if (blocks.isEmpty() && (sectionBreak || !leading.isEmpty()))
        xml += spacerXml(1.0, leading, sectionBreak);
    return xml;
}

QString DocxWriter::headerFor(const QVector<LayoutParagraph> &lines, bool footer)
{
    QByteArray body;
    for (const LayoutParagraph &line : lines) {
        LayoutParagraph copy = line;
        copy.spaceBefore = 0;
        body += paragraphXml(copy, QByteArray(), false);
        --paragraphs; // header lines are not the document's paragraphs
    }
    // The lines are frames placed on the page (pageanalysis.cpp); the part's own
    // flow is one empty paragraph too small to push the body down.
    body += QByteArray("<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" w:line=\"20\" "
                       "w:lineRule=\"exact\"/>") + kTinyMark + "</w:pPr></w:p>";
    const QByteArray key = (footer ? "F" : "H") + body;
    const auto known = headerByContent.constFind(key);
    if (known != headerByContent.constEnd())
        return known.value();
    HeaderPart part;
    part.footer = footer;
    part.partName = QStringLiteral("%1%2.xml").arg(footer ? QStringLiteral("footer") : QStringLiteral("header"))
                        .arg(headers.size() + 1);
    part.relationshipId = relationship(QLatin1String(footer ? kRelFooter : kRelHeader), part.partName, false);
    part.xml = QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:")
        + (footer ? "ftr" : "hdr") + kWordNamespaces + ">" + body + "</w:" + (footer ? "ftr" : "hdr") + ">";
    headers.append(part);
    headerByContent.insert(key, part.relationshipId);
    return part.relationshipId;
}

void DocxWriter::addPage(const LayoutPage &page)
{
    Section section;
    section.geometry = page;
    section.geometry.blocks.clear();
    section.geometry.anchors.clear();
    const int index = sections.size();

    // The page's first paragraph carries a bookmark, which is what an internal link
    // jumps to, and every anchored picture of the page: an anchored picture needs a
    // paragraph on its page to hang from, and its position is the page's, not that
    // paragraph's.
    QByteArray leading = "<w:bookmarkStart w:id=\"" + number(index) + "\" w:name=\"page" + number(index + 1)
        + "\"/><w:bookmarkEnd w:id=\"" + number(index) + "\"/>";

    if (page.pictureOnly) {
        // A page that is a picture: the picture itself, anchored at the page's
        // corner and the size of the page, behind an empty paragraph.
        section.geometry.marginLeft = section.geometry.marginRight = 0;
        section.geometry.marginTop = section.geometry.marginBottom = 0;
        for (const LayoutAnchor &anchor : page.anchors)
            leading += drawingXml(anchor.image, true, true);
        section.body = spacerXml(1.0, leading, true);
    } else {
        for (const LayoutAnchor &anchor : page.anchors)
            leading += drawingXml(anchor.image, true, anchor.behindText);
        section.body = blocksXml(page.blocks, leading, true);
    }
    if (!page.header.isEmpty())
        section.headerId = headerFor(page.header, false);
    if (!page.footer.isEmpty())
        section.footerId = headerFor(page.footer, true);
    sections.append(section);
}

QByteArray DocxWriter::sectionXml(const Section &section) const
{
    const LayoutPage &page = section.geometry;
    QByteArray xml = "<w:sectPr>";
    const QString header = section.headerId.isEmpty() ? emptyHeaderId : section.headerId;
    const QString footer = section.footerId.isEmpty() ? emptyFooterId : section.footerId;
    if (!header.isEmpty())
        xml += "<w:headerReference w:type=\"default\" r:id=\"" + header.toLatin1() + "\"/>";
    if (!footer.isEmpty())
        xml += "<w:footerReference w:type=\"default\" r:id=\"" + footer.toLatin1() + "\"/>";
    const bool landscape = page.size.width() > page.size.height();
    xml += "<w:pgSz w:w=\"" + number(twips(page.size.width())) + "\" w:h=\"" + number(twips(page.size.height()))
        + "\"" + (landscape ? " w:orient=\"landscape\"" : "") + "/>";
    xml += "<w:pgMar w:top=\"" + number(twips(page.marginTop)) + "\" w:right=\"" + number(twips(page.marginRight))
        + "\" w:bottom=\"" + number(twips(page.marginBottom)) + "\" w:left=\"" + number(twips(page.marginLeft))
        + "\" w:header=\"" + number(twips(page.headerDistance)) + "\" w:footer=\""
        + number(twips(page.footerDistance)) + "\" w:gutter=\"0\"/>";
    xml += "<w:cols w:space=\"0\"/>";
    xml += "</w:sectPr>";
    return xml;
}

QByteArray DocxWriter::documentXml() const
{
    QByteArray body;
    for (int i = 0; i < sections.size(); ++i) {
        QByteArray page = sections.at(i).body;
        const QByteArray section = sectionXml(sections.at(i));
        if (i + 1 < sections.size()) {
            page.replace("\x01SECT\x01", section);
            body += page;
        } else {
            // The last section's properties belong to the body, not to a paragraph.
            page.replace("\x01SECT\x01", QByteArray());
            body += page + section;
        }
    }
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document")
        + kWordNamespaces + "><w:body>" + body + "</w:body></w:document>";
}

QHash<QString, QByteArray> DocxWriter::parts() const
{
    // The empty header and footer the pages without one point at, so they do not
    // inherit the previous page's. Made here, once the whole document is known.
    DocxWriter *self = const_cast<DocxWriter *>(this);
    bool anyHeader = false, anyFooter = false;
    for (const HeaderPart &part : std::as_const(headers)) {
        anyHeader = anyHeader || !part.footer;
        anyFooter = anyFooter || part.footer;
    }
    if (anyHeader && self->emptyHeaderId.isEmpty())
        self->emptyHeaderId = self->headerFor({}, false);
    if (anyFooter && self->emptyFooterId.isEmpty())
        self->emptyFooterId = self->headerFor({}, true);

    QHash<QString, QByteArray> out;
    QByteArray types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/settings.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>";
    for (const HeaderPart &part : std::as_const(headers)) {
        types += "<Override PartName=\"/word/" + part.partName.toLatin1() + "\" ContentType=\""
            + (part.footer ? "application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"
                           : "application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml")
            + "\"/>";
    }
    types += "</Types>";
    out.insert(QStringLiteral("[Content_Types].xml"), types);
    out.insert(QStringLiteral("_rels/.rels"),
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
               "relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
    out.insert(QStringLiteral("word/document.xml"), documentXml());

    QByteArray rels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                      "relationships/styles\" Target=\"styles.xml\"/>"
                      "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                      "relationships/settings\" Target=\"settings.xml\"/>";
    for (const Relationship &rel : std::as_const(relationships)) {
        rels += "<Relationship Id=\"" + rel.id.toLatin1() + "\" Type=\"" + rel.type.toLatin1() + "\" Target=\""
            + xmlText(rel.target) + "\"" + (rel.external ? " TargetMode=\"External\"" : "") + "/>";
    }
    rels += "</Relationships>";
    out.insert(QStringLiteral("word/_rels/document.xml.rels"), rels);

    const QByteArray family = xmlText(defaultFamily);
    const QByteArray size = number(qMax<qint64>(2, std::llround(defaultSize * 2)));
    out.insert(QStringLiteral("word/styles.xml"),
               QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles")
                   + kWordNamespaces + "><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"" + family
                   + "\" w:hAnsi=\"" + family + "\" w:eastAsia=\"" + family + "\" w:cs=\"" + family
                   + "\"/><w:sz w:val=\"" + size + "\"/><w:szCs w:val=\"" + size
                   + "\"/><w:lang w:val=\"en-US\" w:eastAsia=\"zh-CN\"/></w:rPr></w:rPrDefault>"
                     "<w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
                     "</w:pPr></w:pPrDefault></w:docDefaults>"
                     "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
                     "<w:qFormat/></w:style>"
                     "<w:style w:type=\"character\" w:default=\"1\" w:styleId=\"DefaultParagraphFont\">"
                     "<w:name w:val=\"Default Paragraph Font\"/><w:uiPriority w:val=\"1\"/><w:semiHidden/>"
                     "<w:unhideWhenUsed/></w:style>"
                     "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/>"
                     "<w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/><w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/>"
                     "<w:left w:w=\"0\" w:type=\"dxa\"/><w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"0\" "
                     "w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style></w:styles>");
    out.insert(QStringLiteral("word/settings.xml"),
               QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:settings")
                   + kWordNamespaces + "><w:zoom w:percent=\"100\"/><w:defaultTabStop w:val=\"720\"/>"
                   "<w:characterSpacingControl w:val=\"doNotCompress\"/><w:compat><w:compatSetting "
                   "w:name=\"compatibilityMode\" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/>"
                   "</w:compat></w:settings>");
    for (const HeaderPart &part : std::as_const(headers))
        out.insert(QStringLiteral("word/") + part.partName, part.xml);
    for (const Media &item : std::as_const(media))
        out.insert(QStringLiteral("word/") + item.partName, item.bytes);
    return out;
}

void DocxWriter::addParts(ZipWriter &zip) const
{
    const QHash<QString, QByteArray> all = parts();
    // [Content_Types].xml first: some readers look for it at the front.
    const QStringList order = {QStringLiteral("[Content_Types].xml"), QStringLiteral("_rels/.rels"),
                               QStringLiteral("word/document.xml"), QStringLiteral("word/_rels/document.xml.rels"),
                               QStringLiteral("word/styles.xml"), QStringLiteral("word/settings.xml")};
    for (const QString &name : order)
        zip.addFile(name, all.value(name));
    QStringList rest;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!order.contains(it.key()))
            rest.append(it.key());
    }
    rest.sort();
    for (const QString &name : std::as_const(rest))
        zip.addFile(name, all.value(name));
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
