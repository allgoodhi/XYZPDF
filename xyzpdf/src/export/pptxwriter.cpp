#include "pptxwriter.h"

#include "zipwriter.h"

#include <QCryptographicHash>
#include <QImage>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

const char *kNamespaces =
    R"( xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main")"
    R"( xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")"
    R"( xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main")";

const char *kRelSlide = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide";
const char *kRelSlideLayout = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout";
const char *kRelSlideMaster = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster";
const char *kRelTheme = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme";
const char *kRelImage = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
const char *kRelHyperlink = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";

// Text as XML character data. A control character has no spelling in XML 1.0 at
// all -- not even escaped -- and one stray byte of it makes PowerPoint refuse the
// whole file, so those are dropped rather than written. A tab is kept: it is how
// a vertical line of Chinese is laid out, and DrawingML carries tabs as character
// data beside its tab stops.
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

QByteArray colourHex(const QColor &colour)
{
    return colour.name(QColor::HexRgb).mid(1).toUpper().toLatin1();
}

QByteArray alignment(LayoutAlign align)
{
    switch (align) {
    case LayoutAlign::Center:
        return "ctr";
    case LayoutAlign::Right:
        return "r";
    case LayoutAlign::Justify:
        return "just";
    case LayoutAlign::Left:
        break;
    }
    return "l";
}

// A picture that is one flat colour -- the panels and rules the layout analysis
// turns into PNGs of their own colour -- is written as a rectangle instead: it
// stays sharp at any zoom, and it is the shape the PDF drew rather than a picture
// of it. Anything else, including a photograph carried over as itself, stays a
// picture.
bool solidColourOf(const QByteArray &bytes, const QString &extension, QColor *colour)
{
    if (extension.compare(QLatin1String("png"), Qt::CaseInsensitive) != 0)
        return false;
    const QImage image = QImage::fromData(bytes);
    if (image.isNull() || image.width() * image.height() > 300 * 300)
        return false;
    const QRgb first = image.pixel(0, 0);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != first)
                return false;
        }
    }
    *colour = QColor::fromRgb(first);
    return true;
}

} // namespace

qint64 PptxWriter::emu(double points) const
{
    return qint64(std::llround(points * 12700.0));
}

PptxWriter::Placement PptxWriter::placementFor(const QSizeF &page) const
{
    Placement placement;
    if (page.isEmpty() || slide.isEmpty())
        return placement;
    const double sx = slide.width() / page.width();
    const double sy = slide.height() / page.height();
    placement.scale = qMin(sx, sy);
    if (qAbs(placement.scale - 1.0) < 1e-6) {
        placement.scale = 1;
        return placement;
    }
    placement.dx = (slide.width() - page.width() * placement.scale) / 2.0;
    placement.dy = (slide.height() - page.height() * placement.scale) / 2.0;
    return placement;
}

QString PptxWriter::relationship(const QString &type, const QString &target, bool external)
{
    Relationship rel;
    rel.id = QStringLiteral("rId%1").arg(nextRelationship++);
    rel.type = type;
    rel.target = target;
    rel.external = external;
    currentRelationships.append(rel);
    return rel.id;
}

QString PptxWriter::mediaFor(const QByteArray &bytes, const QString &extension)
{
    // One copy of a picture however often it appears: a logo on every slide is one
    // file in the package.
    const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    const auto known = mediaByHash.constFind(hash);
    if (known != mediaByHash.constEnd())
        return known.value();
    Media item;
    item.partName = QStringLiteral("media/image%1.%2").arg(media.size() + 1).arg(extension);
    item.bytes = bytes;
    media.append(item);
    mediaByHash.insert(hash, item.partName);
    return item.partName;
}

QString PptxWriter::hyperlinkFor(const LayoutRun &run)
{
    if (!run.uri.isEmpty())
        return relationship(QLatin1String(kRelHyperlink), run.uri, true);
    if (run.targetPage >= 0) {
        return relationship(QLatin1String(kRelSlide),
                            QStringLiteral("../slides/slide%1.xml").arg(run.targetPage + 1));
    }
    return {};
}

QByteArray PptxWriter::runsXml(const QVector<LayoutRun> &runs, const Placement &place, int slideNumber,
                               bool leadWithTab)
{
    QByteArray xml;
    QVector<LayoutRun> pieces = runs;
    if (leadWithTab && !pieces.isEmpty()) {
        // The first line of a paragraph starts further in than the rest, and the box
        // is at the rest's left. A tab to the paragraph's own first-line stop moves
        // just that line -- the same thing Word's w:firstLine does in its flow.
        LayoutRun tab;
        tab.text = QStringLiteral("\t");
        tab.style = pieces.first().style;
        pieces.prepend(tab);
    }
    for (const LayoutRun &run : pieces) {
        const RunStyle &style = run.style;
        const qint64 size = qMax<qint64>(100, qint64(std::llround(style.size * place.scale * 100.0)));
        QByteArray properties = "<a:rPr lang=\"en-US\" sz=\"" + number(size) + "\" dirty=\"0\"";
        if (style.bold)
            properties += " b=\"1\"";
        if (style.italic)
            properties += " i=\"1\"";
        if (style.underline)
            properties += " u=\"sng\"";
        if (const qint64 spacing = std::llround(style.spacing * place.scale * 100.0))
            properties += " spc=\"" + number(spacing) + "\"";
        if (style.superscript)
            properties += " baseline=\"30000\"";
        else if (style.subscript)
            properties += " baseline=\"-25000\"";
        properties += ">";
        if (style.color.isValid())
            properties += "<a:solidFill><a:srgbClr val=\"" + colourHex(style.color) + "\"/></a:solidFill>";
        if (!style.font.isEmpty()) {
            const QByteArray family = xmlText(style.font);
            properties += "<a:latin typeface=\"" + family + "\"/><a:ea typeface=\"" + family
                + "\"/><a:cs typeface=\"" + family + "\"/>";
        }
        const QString link = hyperlinkFor(run);
        if (!link.isEmpty()) {
            properties += "<a:hlinkClick r:id=\"" + link.toLatin1() + "\"";
            if (!run.uri.isEmpty())
                properties += "/>";
            else
                properties += " action=\"ppaction://hlinksldjump\"/>";
        }
        properties += "</a:rPr>";

        if (!run.field.isEmpty()) {
            // A page number: PowerPoint's own slide-number field, so it follows the
            // slide if the deck is reordered -- and shows the page the PDF had.
            const QByteArray id = "{" + QByteArray::number(fieldId++).rightJustified(8, '0')
                + "-0000-4000-8000-000000000000}";
            xml += "<a:fld id=\"" + id + "\" type=\"slidenum\">" + properties + "<a:t>"
                + number(slideNumber) + "</a:t></a:fld>";
            continue;
        }

        // The PDF's own line break is a sibling of the runs, never inside one: an
        // a:r carries exactly one a:t, and a break written inside a run is dropped --
        // measured in WPS, where a three-line paragraph came back as one line running
        // off the slide.
        QString piece;
        const auto flush = [&] {
            if (!piece.isEmpty()) {
                xml += "<a:r>" + properties + "<a:t xml:space=\"preserve\">" + xmlText(piece)
                    + "</a:t></a:r>";
                piece.clear();
            }
        };
        for (const QChar c : run.text) {
            if (c == QLatin1Char('\n')) {
                flush();
                xml += "<a:br/>";
            } else {
                piece += c;
            }
        }
        flush();
    }
    return xml;
}

QByteArray PptxWriter::paragraphXml(const LayoutParagraph &paragraph, const Placement &place,
                                   double regionLeft, double regionWidth, int slideNumber, bool framed)
{
    const double line = qMax(1.0, paragraph.lineSpacing);
    QByteArray properties = "<a:pPr";
    properties += " algn=\"" + alignment(paragraph.align) + "\"";
    // Every paragraph is a box of its own at the PDF's own place, so the flow
    // indents Word needed are already in the offset: no margin, no automatic spacing
    // that would move the first baseline. Only the first line's indent has to be put
    // back, because the box is at the left the *other* lines start at.
    properties += " marL=\"0\" marR=\"0\" indent=\"0\"";
    bool rightToLeft = false;
    for (const LayoutRun &run : paragraph.runs)
        rightToLeft = rightToLeft || run.style.rightToLeft;
    properties += rightToLeft ? " rtl=\"1\"" : " rtl=\"0\"";
    properties += " eaLnBrk=\"1\" latinLnBrk=\"0\" hangingPunct=\"0\">";
    properties += "<a:lnSpc><a:spcPts val=\"" + number(qMax<qint64>(100, std::llround(line * place.scale * 100.0)))
        + "\"/></a:lnSpc><a:spcBef><a:spcPts val=\"0\"/></a:spcBef>";
    // A first-line indent only exists when there is another line to be different
    // from: a single-line paragraph carries its own left in the box, and the analysis
    // leaves `firstLine` at 0 for it.
    const bool leadWithTab = framed ? false
                                    : paragraph.firstLine > 0.5 && paragraph.lineCount > 1
                                        && (paragraph.align == LayoutAlign::Left
                                            || paragraph.align == LayoutAlign::Justify);
    QVector<LayoutTab> stops = paragraph.tabs;
    if (leadWithTab) {
        LayoutTab first;
        first.position = paragraph.firstLine;
        stops.append(first);
    }
    if (!stops.isEmpty()) {
        std::sort(stops.begin(), stops.end(), [](const LayoutTab &a, const LayoutTab &b) {
            return a.position < b.position;
        });
        properties += "<a:tabLst>";
        for (const LayoutTab &tab : std::as_const(stops)) {
            properties += QByteArray("<a:tab pos=\"") + number(emu(tab.position * place.scale))
                + "\" algn=\"" + (tab.right ? "r" : "l") + "\"/>";
        }
        properties += "</a:tabLst>";
    }
    properties += "</a:pPr>";

    Q_UNUSED(regionLeft)
    Q_UNUSED(regionWidth)
    return "<a:p>" + properties + runsXml(paragraph.runs, place, slideNumber, leadWithTab) + "</a:p>";
}

void PptxWriter::addParagraphBox(const LayoutParagraph &paragraph, const Placement &place,
                                 double regionLeft, double regionWidth, bool framed)
{
    if (paragraph.runs.isEmpty())
        return;
    double left = paragraph.box.left();
    double width = paragraph.box.width();
    if (!framed && (paragraph.align == LayoutAlign::Center || paragraph.align == LayoutAlign::Right)) {
        // A centred or right-set paragraph needs the column it was set in, not the
        // ink of its longest line: every line was moved within that column.
        left = regionLeft + paragraph.left;
        width = qMax(4.0, regionWidth - paragraph.left - paragraph.right);
    }
    width = qMax(1.0, width);
    const double height = qMax(paragraph.box.height(), paragraph.lineSpacing * qMax(1, paragraph.lineCount));
    if (width < 0.5 || height < 0.5)
        return;

    const int id = nextShape++;
    QByteArray xml = "<p:sp><p:nvSpPr><p:cNvPr id=\"" + number(id) + "\" name=\"Text " + number(id)
        + "\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x=\""
        + number(emu(place.dx + left * place.scale)) + "\" y=\""
        + number(emu(place.dy + paragraph.box.top() * place.scale)) + "\"/><a:ext cx=\""
        + number(emu(width * place.scale)) + "\" cy=\"" + number(emu((height + 0.5) * place.scale))
        + "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/>"
          "<a:ln><a:noFill/></a:ln></p:spPr>";
    // No autofit, no insets: the box is where the text goes, and the exact line
    // spacing is what keeps the baselines where the PDF had them. Wrap is off
    // because the PDF's own line breaks are in the text as a:br -- a box that
    // wrapped for itself would add a line the PDF does not have.
    xml += "<p:txBody><a:bodyPr wrap=\"none\" lIns=\"0\" tIns=\"0\" rIns=\"0\" bIns=\"0\" "
           "anchor=\"t\" anchorCtr=\"0\" rtlCol=\"0\"><a:noAutofit/></a:bodyPr><a:lstStyle/>";
    xml += paragraphXml(paragraph, place, regionLeft, regionWidth, int(slides.size()) + 1, framed);
    xml += "</p:txBody></p:sp>";
    slides.last().tree += xml;
    ++textBoxes;
}

QByteArray PptxWriter::pictureXml(const QString &relationshipId, const LayoutImage &image, const Placement &place)
{
    const int id = nextShape++;
    return "<p:pic><p:nvPicPr><p:cNvPr id=\"" + number(id) + "\" name=\"Picture " + number(id)
        + "\"/><p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>"
          "<p:blipFill><a:blip r:embed=\"" + relationshipId.toLatin1()
        + "\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr><a:xfrm><a:off x=\""
        + number(emu(place.dx + image.box.left() * place.scale)) + "\" y=\""
        + number(emu(place.dy + image.box.top() * place.scale)) + "\"/><a:ext cx=\""
        + number(emu(qMax(0.5, image.box.width()) * place.scale)) + "\" cy=\""
        + number(emu(qMax(0.5, image.box.height()) * place.scale))
        + "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>";
}

void PptxWriter::addPicture(const LayoutImage &image, const Placement &place)
{
    if (image.bytes.isEmpty() || image.box.isEmpty())
        return;
    const QString part = mediaFor(image.bytes, image.extension);
    const QString id = relationship(QLatin1String(kRelImage), QStringLiteral("../") + part);
    slides.last().tree += pictureXml(id, image, place);
    ++pictures;
}

void PptxWriter::addImage(const LayoutImage &image, const Placement &place)
{
    addPicture(image, place);
}

void PptxWriter::addRectangle(const QRectF &box, const QColor &colour, const Placement &place)
{
    if (box.width() < 0.05 || box.height() < 0.05 || !colour.isValid())
        return;
    const int id = nextShape++;
    slides.last().tree += "<p:sp><p:nvSpPr><p:cNvPr id=\"" + number(id) + "\" name=\"Shape "
        + number(id) + "\"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x=\""
        + number(emu(place.dx + box.left() * place.scale)) + "\" y=\""
        + number(emu(place.dy + box.top() * place.scale)) + "\"/><a:ext cx=\""
        + number(emu(box.width() * place.scale)) + "\" cy=\"" + number(emu(box.height() * place.scale))
        + "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val=\""
        + colourHex(colour) + "\"/></a:solidFill><a:ln><a:noFill/></a:ln></p:spPr></p:sp>";
    ++shapes;
}

void PptxWriter::addAnchors(const QVector<LayoutAnchor> &anchors, const Placement &place)
{
    for (const LayoutAnchor &anchor : anchors) {
        QColor colour;
        if (solidColourOf(anchor.image.bytes, anchor.image.extension, &colour))
            addRectangle(anchor.image.box, colour, place);
        else
            addPicture(anchor.image, place);
    }
}

void PptxWriter::addCell(const LayoutCell &cell, const Placement &place)
{
    ++cells;
    if (cell.shading.isValid())
        addRectangle(cell.box, cell.shading, place);
    // A border is a filled rectangle the width of the stroke: the same picture as a
    // table's line, and it stays at the PDF's own position whatever the cell holds.
    const auto edge = [this, &cell, &place](bool horizontal, double width, double at, const LayoutBorder &border) {
        if (!border.present)
            return;
        const double thickness = qBound(0.4, border.width, 12.0);
        QRectF box;
        if (horizontal)
            box = QRectF(cell.box.left() - thickness / 2, at - thickness / 2,
                         cell.box.width() + thickness, thickness);
        else
            box = QRectF(at - thickness / 2, cell.box.top() - thickness / 2, thickness,
                         cell.box.height() + thickness);
        addRectangle(box, border.color.isValid() ? border.color : QColor(Qt::black), place);
        Q_UNUSED(width)
    };
    edge(true, cell.box.width(), cell.box.top(), cell.top);
    edge(true, cell.box.width(), cell.box.bottom(), cell.bottom);
    edge(false, cell.box.height(), cell.box.left(), cell.left);
    edge(false, cell.box.height(), cell.box.right(), cell.right);

    const double regionLeft = cell.box.left() + cell.padLeft;
    const double regionWidth = qMax(1.0, cell.box.width() - cell.padLeft - cell.padRight);
    addBlocks(cell.blocks, place, regionLeft, regionWidth);
}

void PptxWriter::addTable(const LayoutTable &table, const Placement &place)
{
    if (table.cells.isEmpty())
        return;
    ++tables;
    for (const LayoutCell &cell : table.cells)
        addCell(cell, place);
}

void PptxWriter::addBlocks(const QVector<LayoutBlock> &blocks, const Placement &place, double regionLeft,
                           double regionWidth)
{
    for (const LayoutBlock &block : blocks) {
        switch (block.kind) {
        case LayoutBlock::Kind::Paragraph:
            addParagraphBox(block.paragraph, place, regionLeft, regionWidth, block.paragraph.framed);
            break;
        case LayoutBlock::Kind::Image:
            addImage(block.image, place);
            break;
        case LayoutBlock::Kind::Table:
            addTable(*block.table, place);
            break;
        case LayoutBlock::Kind::Columns: {
            const LayoutColumns &columns = *block.columns;
            addBlocks(columns.left, place, regionLeft, qMax(1.0, columns.leftWidth));
            addBlocks(columns.right, place, regionLeft + columns.leftWidth + columns.gap,
                      qMax(1.0, columns.rightWidth));
            break;
        }
        }
    }
}

void PptxWriter::addSlide(const LayoutPage &page)
{
    if (slides.isEmpty())
        slide = page.size; // every slide takes the first page's shape
    Slide item;
    item.number = int(slides.size()) + 1;
    const Placement place = placementFor(page.size);
    item.scaled = place.scale != 1 || place.dx != 0 || place.dy != 0;
    if (item.scaled)
        ++scaledSlides;
    currentRelationships.clear();
    nextRelationship = 2; // rId1 is the layout
    nextShape = 2;
    slides.append(item);

    // Behind the text first: panels, rules and the page's own picture.
    addAnchors(page.anchors, place);
    for (const LayoutParagraph &paragraph : page.header)
        addParagraphBox(paragraph, place, 0, 0, true);
    if (!page.pictureOnly) {
        addBlocks(page.blocks, place, page.marginLeft,
                  qMax(1.0, page.size.width() - page.marginLeft - page.marginRight));
    }
    for (const LayoutParagraph &paragraph : page.footer)
        addParagraphBox(paragraph, place, 0, 0, true);
    slides.last().rels = currentRelationships;
}

QByteArray PptxWriter::slideXml(const Slide &slide) const
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sld")
        + kNamespaces + "><p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/>"
          "<p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
          "<a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm>"
          "</p:grpSpPr>"
        + slide.tree
        + "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>";
}

QByteArray PptxWriter::masterXml() const
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sldMaster")
        + kNamespaces + "><p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/>"
          "<p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
          "<a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm>"
          "</p:grpSpPr></p:spTree></p:cSld>"
          "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
          "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" "
          "accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
          "<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
          "<p:txStyles><p:titleStyle><a:lvl1pPr algn=\"l\" defTabSz=\"914400\" rtl=\"0\" "
          "eaLnBrk=\"1\" latinLnBrk=\"0\" hangingPunct=\"1\"><a:lnSpc><a:spcPct val=\"90000\"/>"
          "</a:lnSpc><a:spcBef><a:spcPct val=\"0\"/></a:spcBef><a:buNone/></a:lvl1pPr></p:titleStyle>"
          "<p:bodyStyle><a:lvl1pPr marL=\"0\" indent=\"0\"><a:lnSpc><a:spcPct val=\"100000\"/>"
          "</a:lnSpc><a:spcBef><a:spcPct val=\"0\"/></a:spcBef><a:buNone/></a:lvl1pPr></p:bodyStyle>"
          "<p:otherStyle><a:defPPr><a:defRPr lang=\"en-US\"/></a:defPPr><a:lvl1pPr marL=\"0\" "
          "indent=\"0\"><a:defRPr sz=\"1800\" kern=\"1200\"><a:solidFill><a:schemeClr val=\"tx1\"/>"
          "</a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl1pPr></p:otherStyle>"
          "</p:txStyles></p:sldMaster>";
}

QByteArray PptxWriter::layoutXml() const
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sldLayout")
        + kNamespaces + " type=\"blank\" preserve=\"1\"><p:cSld name=\"Blank\"><p:spTree>"
          "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
          "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
          "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
          "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
}

QByteArray PptxWriter::themeXml() const
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<a:theme")
        + kNamespaces + " name=\"XYZPDF\"><a:themeElements><a:clrScheme name=\"Office\">"
          "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>"
          "<a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>"
          "<a:dk2><a:srgbClr val=\"44546A\"/></a:dk2><a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>"
          "<a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1>"
          "<a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>"
          "<a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3>"
          "<a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>"
          "<a:accent5><a:srgbClr val=\"5B9BD5\"/></a:accent5>"
          "<a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>"
          "<a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink>"
          "<a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink></a:clrScheme>"
          "<a:fontScheme name=\"Office\"><a:majorFont><a:latin typeface=\"Calibri Light\"/>"
          "<a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont>"
          "<a:latin typeface=\"Calibri\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
          "</a:fontScheme><a:fmtScheme name=\"Office\"><a:fillStyleLst>"
          "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
          "<a:solidFill><a:schemeClr val=\"phClr\"><a:tint val=\"50000\"/><a:satMod val=\"300000\"/>"
          "</a:schemeClr></a:solidFill>"
          "<a:solidFill><a:schemeClr val=\"phClr\"><a:shade val=\"15000\"/><a:satMod val=\"200000\"/>"
          "</a:schemeClr></a:solidFill></a:fillStyleLst><a:lnStyleLst>"
          "<a:ln w=\"6350\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr "
          "val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/><a:miter lim=\"800000\"/></a:ln>"
          "<a:ln w=\"12700\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr "
          "val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/><a:miter lim=\"800000\"/></a:ln>"
          "<a:ln w=\"19050\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr "
          "val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/><a:miter lim=\"800000\"/></a:ln>"
          "</a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle>"
          "<a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle>"
          "</a:effectStyleLst><a:bgFillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
          "<a:solidFill><a:schemeClr val=\"phClr\"><a:tint val=\"95000\"/><a:satMod val=\"170000\"/>"
          "</a:schemeClr></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"><a:shade val=\"90000\"/>"
          "<a:satMod val=\"120000\"/></a:schemeClr></a:solidFill></a:bgFillStyleLst></a:fmtScheme>"
          "</a:themeElements></a:theme>";
}

QByteArray PptxWriter::presentationXml() const
{
    const qint64 cx = emu(slide.width());
    const qint64 cy = emu(slide.height());
    QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:presentation";
    xml += kNamespaces;
    xml += "><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
           "<p:sldIdLst>";
    for (const Slide &item : std::as_const(slides))
        xml += "<p:sldId id=\"" + number(255 + item.number) + "\" r:id=\"rId"
            + number(1 + item.number) + "\"/>";
    xml += "</p:sldIdLst><p:sldSz cx=\"" + number(cx) + "\" cy=\"" + number(cy)
        + "\" type=\"custom\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/>";
    xml += "<p:defaultTextStyle><a:defPPr><a:defRPr lang=\"en-US\"/></a:defPPr>"
           "<a:lvl1pPr marL=\"0\" indent=\"0\"><a:defRPr sz=\"1800\"/></a:lvl1pPr></p:defaultTextStyle>";
    xml += "</p:presentation>";
    return xml;
}

QHash<QString, QByteArray> PptxWriter::parts() const
{
    QHash<QString, QByteArray> out;
    QByteArray types = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                       "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                       "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                       "<Default Extension=\"png\" ContentType=\"image/png\"/>"
                       "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
                       "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-"
                       "officedocument.presentationml.presentation.main+xml\"/>"
                       "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/"
                       "vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
                       "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/"
                       "vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
                       "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/"
                       "vnd.openxmlformats-officedocument.theme+xml\"/>";
    for (const Slide &slide : slides) {
        types += "<Override PartName=\"/ppt/slides/slide" + number(slide.number)
            + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
    }
    types += "</Types>";
    out.insert(QStringLiteral("[Content_Types].xml"), types);
    out.insert(QStringLiteral("_rels/.rels"),
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
               "relationships/officeDocument\" Target=\"ppt/presentation.xml\"/></Relationships>");
    out.insert(QStringLiteral("ppt/presentation.xml"), presentationXml());

    QByteArray presentationRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>";
    for (const Slide &slide : slides) {
        presentationRels += "<Relationship Id=\"rId" + number(1 + slide.number)
            + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
              "Target=\"slides/slide" + number(slide.number) + ".xml\"/>";
    }
    presentationRels += "</Relationships>";
    out.insert(QStringLiteral("ppt/_rels/presentation.xml.rels"), presentationRels);

    for (int i = 0; i < slides.size(); ++i) {
        const Slide &slide = slides.at(i);
        out.insert(QStringLiteral("ppt/slides/slide%1.xml").arg(slide.number), slideXml(slide));
        QByteArray rels =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
            "relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>";
        // Pictures and links were registered while the slide was being written;
        // each slide keeps its own list, because rId numbering starts over at every
        // part.
        for (const Relationship &rel : std::as_const(slides.at(i).rels)) {
            rels += "<Relationship Id=\"" + rel.id.toLatin1() + "\" Type=\"" + rel.type.toLatin1()
                + "\" Target=\"" + xmlText(rel.target) + "\""
                + (rel.external ? " TargetMode=\"External\"" : "") + "/>";
        }
        rels += "</Relationships>";
        out.insert(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(slide.number), rels);
    }

    out.insert(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), masterXml());
    out.insert(QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"),
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
               "relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
               "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
               "relationships/theme\" Target=\"../theme/theme1.xml\"/></Relationships>");
    out.insert(QStringLiteral("ppt/slideLayouts/slideLayout1.xml"), layoutXml());
    out.insert(QStringLiteral("ppt/slideLayouts/_rels/slideLayout1.xml.rels"),
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
               "relationships/slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/></Relationships>");
    out.insert(QStringLiteral("ppt/theme/theme1.xml"), themeXml());
    for (const Media &item : std::as_const(media))
        out.insert(QStringLiteral("ppt/") + item.partName, item.bytes);
    return out;
}

void PptxWriter::addParts(ZipWriter &zip) const
{
    const QHash<QString, QByteArray> all = parts();
    QStringList order = {QStringLiteral("[Content_Types].xml"), QStringLiteral("_rels/.rels"),
                         QStringLiteral("ppt/presentation.xml"),
                         QStringLiteral("ppt/_rels/presentation.xml.rels"),
                         QStringLiteral("ppt/slideMasters/slideMaster1.xml"),
                         QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"),
                         QStringLiteral("ppt/slideLayouts/slideLayout1.xml"),
                         QStringLiteral("ppt/slideLayouts/_rels/slideLayout1.xml.rels"),
                         QStringLiteral("ppt/theme/theme1.xml")};
    for (const Slide &slide : slides) {
        order.append(QStringLiteral("ppt/slides/slide%1.xml").arg(slide.number));
        order.append(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(slide.number));
    }
    for (const QString &name : std::as_const(order)) {
        if (all.contains(name))
            zip.addFile(name, all.value(name));
    }
    QStringList rest;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!order.contains(it.key()))
            rest.append(it.key());
    }
    rest.sort();
    for (const QString &name : std::as_const(rest))
        zip.addFile(name, all.value(name));
}

QByteArray PptxWriter::toBytes() const
{
    ZipWriter zip;
    addParts(zip);
    return zip.toBytes();
}

bool PptxWriter::save(const QString &path, QString *error) const
{
    ZipWriter zip;
    addParts(zip);
    return zip.write(path, error);
}
