// A PDF read out as a PowerPoint presentation: the slide's numbers, the package,
// and the fixtures from end to end.
//
// ---- what is tested, and with what ----
//
// The layout analysis is pageanalysis.cpp's and is tested by wordexport_smoke; the
// subject here is the step after it -- a LayoutPage turned into slides. The pages
// below are built by hand so that a box's offset can be compared with arithmetic,
// and the fixtures at the end run the whole path through PDFium.
//
// The file is unpacked by this test's own reader (the end record, the central
// directory, the local headers), because the writer and an in-repository reader
// would agree with each other about a package PowerPoint refuses. Whether it
// *looks* like the PDF in PowerPoint is a question only PowerPoint or WPS can
// answer; that loop is run by hand -- export, print the .pptx to PDF, compare the
// pages, and `tools/check_pptx.py` for the package itself.

#include "../src/document/pagecontent.h"
#include "../src/document/pdfiumbackend.h"
#include "../src/export/pptexport.h"
#include "../src/export/pptxwriter.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QAtomicInt>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QSharedPointer>
#include <QRegularExpression>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// ---- a ZIP, read the way an unpacker reads one ----

quint16 u16(const QByteArray &data, int at)
{
    return quint16(quint8(data.at(at))) | quint16(quint8(data.at(at + 1))) << 8;
}

quint32 u32(const QByteArray &data, int at)
{
    return quint32(u16(data, at)) | (quint32(u16(data, at + 2)) << 16);
}

QHash<QString, QByteArray> unpackZip(const QByteArray &file)
{
    QHash<QString, QByteArray> parts;
    const int end = file.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (end < 0)
        return parts;
    const int count = u16(file, end + 10);
    int at = int(u32(file, end + 16));
    for (int index = 0; index < count; ++index) {
        if (at + 46 > file.size() || file.mid(at, 4) != QByteArray("PK\x01\x02", 4))
            return {};
        const quint32 compressed = u32(file, at + 20);
        const quint32 plain = u32(file, at + 24);
        const int nameLength = u16(file, at + 28);
        const int extraLength = u16(file, at + 30);
        const int commentLength = u16(file, at + 32);
        const int localAt = int(u32(file, at + 42));
        const QString name = QString::fromUtf8(file.mid(at + 46, nameLength));
        // The writer stores; anything else would need an inflater this test does
        // not have, on purpose.
        if (compressed != plain)
            return {};
        const int localNameLength = u16(file, localAt + 26);
        const int localExtraLength = u16(file, localAt + 28);
        const int dataAt = localAt + 30 + localNameLength + localExtraLength;
        parts.insert(name, file.mid(dataAt, int(compressed)));
        at += 46 + nameLength + extraLength + commentLength;
    }
    return parts;
}

bool wellFormed(const QByteArray &xml, QString *error)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
        reader.readNext();
    if (reader.hasError() && error)
        *error = reader.errorString();
    return !reader.hasError();
}

QByteArray png(const QSize &size, const QColor &colour)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString fixture(const QString &directory, const QString &name)
{
    const QString path = directory + QLatin1Char('/') + name;
    if (!QFileInfo::exists(path)) {
        std::cerr << "FAIL: " << path.toStdString() << " is missing; run tools/make_fixtures.py\n";
        ++failures;
        return {};
    }
    return path;
}

// The part a relationship's Target names, resolved against the part that owns the
// relationships file. `ppt/slides/_rels/slide1.xml.rels` lives in `ppt/slides`.
QString resolveTarget(const QString &relsPart, const QString &target)
{
    if (target.startsWith(QLatin1Char('/')))
        return target.mid(1);
    if (relsPart.startsWith(QLatin1String("_rels/")))
        return QDir::cleanPath(target); // the package's own relationships, based at the root
    QString base = relsPart;
    const int cut = base.indexOf(QStringLiteral("/_rels/"));
    if (cut >= 0)
        base = base.left(cut);
    return QDir::cleanPath(base + QLatin1Char('/') + target);
}

QStringList relationshipTargets(const QByteArray &xml)
{
    QStringList targets;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("Relationship")) {
            const auto attributes = reader.attributes();
            if (attributes.value(QLatin1String("TargetMode")) == QLatin1String("External"))
                continue;
            targets.append(attributes.value(QLatin1String("Target")).toString());
        }
    }
    return targets;
}

// The package read back, however it was obtained: from the writer's own parts or
// from the bytes on disk. `where` names the file when more than one is checked, so
// a FAIL says which package broke.
void checkPackage(const QHash<QString, QByteArray> &parts, const QString &where)
{
    bool targetsPresent = true;
    QString missing;
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (!it.key().endsWith(QLatin1String(".rels")))
            continue;
        for (const QString &target : relationshipTargets(it.value())) {
            const QString resolved = resolveTarget(it.key(), target);
            if (!parts.contains(resolved)) {
                targetsPresent = false;
                missing = it.key() + QLatin1String(" -> ") + resolved;
            }
        }
    }
    check(targetsPresent, QStringLiteral("every relationship target is a part (%1%2)")
                              .arg(where, missing));

    bool allWellFormed = true;
    QString broken;
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (!it.key().endsWith(QLatin1String(".xml")) && !it.key().endsWith(QLatin1String(".rels")))
            continue;
        QString error;
        if (!wellFormed(it.value(), &error)) {
            allWellFormed = false;
            broken = it.key() + QLatin1String(": ") + error;
        }
    }
    check(allWellFormed, QStringLiteral("every XML part parses (%1%2)").arg(where, broken));
}

LayoutParagraph paragraphOf(const QString &text, const QRectF &box, double lineSpacing = 14)
{
    LayoutParagraph paragraph;
    LayoutRun run;
    run.text = text;
    run.style.font = QStringLiteral("Arial");
    run.style.size = 12;
    paragraph.runs.append(run);
    paragraph.box = box;
    paragraph.lineSpacing = lineSpacing;
    paragraph.lineCount = qMax(1, int(std::lround(box.height() / lineSpacing)));
    return paragraph;
}

LayoutBlock paragraphBlock(const LayoutParagraph &paragraph)
{
    LayoutBlock block;
    block.kind = LayoutBlock::Kind::Paragraph;
    block.paragraph = paragraph;
    block.top = paragraph.box.top();
    block.bottom = paragraph.box.bottom();
    return block;
}

qint64 emuOf(double points)
{
    return qint64(std::llround(points * 12700.0));
}

// Where the fixture exports are written. By default a temporary directory takes
// them away with itself; a second argument names a directory to keep them in,
// because the hand-run visual loop (export, WPS prints it to PDF, compare the
// pages) and tools/check_pptx.py need a file to look at after this has exited.
//
// A name that is already there is removed before it is handed out: two of the
// checks below are "this export wrote nothing", and a file left by the run before
// -- or, once, by a control run that ignored the stop flag -- makes them red while
// the export in front of them behaved.
class OutputDirectory
{
public:
    explicit OutputDirectory(const QString &keep)
        : fixed(keep)
        , keeps(!keep.isEmpty())
    {
        if (keeps)
            QDir().mkpath(keep);
    }

    QString filePath(const QString &name) const
    {
        if (!keeps)
            return temporary.filePath(name);
        const QString path = fixed.filePath(name);
        QFile::remove(path);
        return path;
    }

    // Without this, a temporary directory that could not be made hands out a bare
    // file name and the export lands beside the repository instead -- quietly.
    bool isValid() const
    {
        return keeps || temporary.isValid();
    }

private:
    QTemporaryDir temporary;
    QDir fixed;
    // QDir("").path() is ".", not "", so "was a directory named?" needs its own
    // answer.
    bool keeps = false;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("tests/fixtures");
    const QString keep = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    OutputDirectory output(keep);
    check(output.isValid(), QStringLiteral("there is a directory to write the exports into (%1)")
                                .arg(keep.isEmpty() ? QStringLiteral("temporary") : keep));

    // ---- one page, one slide, every shape at its own place ----
    //
    // The numbers are the whole point of the writer: a text box's offset and extent
    // are the paragraph's box in points times 12700, so the slide is the page.
    // Character spacing comes across in hundredths of a point, which is the unit
    // a:rPr spc uses and the one the Word writer measured.
    {
        PptxWriter writer;
        LayoutPage page;
        page.size = QSizeF(595, 842);
        page.marginLeft = 72;
        page.marginRight = 72;

        LayoutParagraph paragraph;
        LayoutRun run;
        run.text = QStringLiteral("Hello\nWorld");
        run.style.font = QStringLiteral("Arial");
        run.style.size = 12;
        run.style.bold = true;
        run.style.color = QColor(255, 0, 0);
        run.style.spacing = 0.4;
        paragraph.runs.append(run);
        paragraph.box = QRectF(72, 100, 300, 40);
        paragraph.lineSpacing = 14;
        paragraph.lineCount = 2;
        // The first line starts further in than the second, which is what the box's
        // left is not.
        paragraph.firstLine = 21;
        page.blocks.append(paragraphBlock(paragraph));

        // A running header: placed at its own box, not in any flow.
        LayoutParagraph header = paragraphOf(QStringLiteral("Chapter 1"), QRectF(72, 30, 120, 14));
        header.framed = true;
        page.header.append(header);

        writer.addSlide(page);

        check(writer.slideCount() == 1, QStringLiteral("a page is a slide (%1)").arg(writer.slideCount()));
        check(writer.textBoxCount() == 2,
              QStringLiteral("each paragraph is a text box of its own, header included (%1)")
                  .arg(writer.textBoxCount()));
        check(writer.slideSize() == QSizeF(595, 842),
              QStringLiteral("the slide takes the page's size (%1 x %2)")
                  .arg(writer.slideSize().width()).arg(writer.slideSize().height()));

        const QHash<QString, QByteArray> parts = writer.parts();
        for (const QString &name : {QStringLiteral("[Content_Types].xml"), QStringLiteral("_rels/.rels"),
                                    QStringLiteral("ppt/presentation.xml"),
                                    QStringLiteral("ppt/_rels/presentation.xml.rels"),
                                    QStringLiteral("ppt/slideMasters/slideMaster1.xml"),
                                    QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"),
                                    QStringLiteral("ppt/slideLayouts/slideLayout1.xml"),
                                    QStringLiteral("ppt/slideLayouts/_rels/slideLayout1.xml.rels"),
                                    QStringLiteral("ppt/theme/theme1.xml"),
                                    QStringLiteral("ppt/slides/slide1.xml"),
                                    QStringLiteral("ppt/slides/_rels/slide1.xml.rels")}) {
            check(parts.contains(name), QStringLiteral("the package has %1").arg(name));
        }
        checkPackage(parts, QString());

        const QByteArray presentation = parts.value(QStringLiteral("ppt/presentation.xml"));
        check(presentation.contains("<p:sldSz cx=\"7556500\" cy=\"10693400\""),
              QStringLiteral("595 x 842 points is %1 x %2 EMU")
                  .arg(emuOf(595)).arg(emuOf(842)));
        check(presentation.contains("<p:sldIdLst><p:sldId id=\"256\" r:id=\"rId2\"/>"),
              "and it lists the slide");

        const QByteArray slide = parts.value(QStringLiteral("ppt/slides/slide1.xml"));
        check(slide.contains("<a:off x=\"914400\" y=\"1270000\"/>"),
              QStringLiteral("the paragraph's box starts at 72,100 points = 914400,1270000 EMU: %1")
                  .arg(QString::fromUtf8(slide.mid(slide.indexOf("<a:off"), 48))));
        check(slide.contains("<a:ext cx=\"3810000\" cy=\"514350\"/>"),
              "and is 300 points wide");
        check(slide.contains("sz=\"1200\"") && slide.contains("b=\"1\""),
              "the run keeps its size and weight");
        check(slide.contains("spc=\"40\""), "and its character spacing in hundredths of a point");
        check(slide.contains("<a:srgbClr val=\"FF0000\"/>"), "and its colour");
        check(slide.contains("<a:br/>") && slide.contains("Hello") && slide.contains("World"),
              "the PDF's own line break is a:br, not a wrap the reader chooses");
        // The shape, not just the presence: a:br inside an a:r is dropped by the
        // renderer (measured in WPS, where every line of a paragraph came back as
        // one), and an a:r holds exactly one a:t.
        check(slide.contains("</a:t></a:r><a:br/><a:r>"),
              "the break is a sibling of the runs, not folded inside one");
        check(slide.contains("<a:tabLst><a:tab pos=\"266700\" algn=\"l\"/></a:tabLst>"),
              QStringLiteral("the first line's 21-point indent is a tab stop of %1 EMU").arg(emuOf(21)));
        check(slide.contains("<a:t xml:space=\"preserve\">\t</a:t>"),
              "and that line starts with the tab that reaches the stop");
        check(slide.contains("<a:noAutofit/>") && slide.contains("wrap=\"none\"")
                  && slide.contains("lIns=\"0\" tIns=\"0\""),
              "the box does not autofit or inset, so the text stays where it was put");
        check(parts.value(QStringLiteral("ppt/slides/_rels/slide1.xml.rels"))
                  .contains("../slideLayouts/slideLayout1.xml"),
              "the slide is laid out on the blank layout");
    }

    // ---- a table, a picture, and a panel ----
    //
    // Tables are per-cell text boxes with the grid drawn as rectangles, not an
    // a:tbl: a slide has no flow, and a:tbl rows grow to fit their text. A picture
    // stays a picture; a panel that is one flat colour becomes a rectangle, so it
    // stays sharp and it is the shape the PDF drew.
    {
        PptxWriter writer;
        LayoutPage page;
        page.size = QSizeF(595, 842);

        auto table = std::make_shared<LayoutTable>();
        table->box = QRectF(72, 100, 200, 60);
        table->columns = {72, 172, 272};
        table->rows = {100, 130, 160};
        LayoutCell left;
        left.row = 0;
        left.column = 0;
        left.box = QRectF(72, 100, 100, 60);
        left.shading = QColor(0xEE, 0xEE, 0xEE);
        left.top = {true, 1.0, Qt::black};
        left.left = {true, 1.0, Qt::black};
        left.bottom = {true, 1.0, Qt::black};
        left.right = {true, 1.0, Qt::black};
        left.blocks.append(paragraphBlock(paragraphOf(QStringLiteral("Left cell"), QRectF(76, 104, 90, 14))));
        LayoutCell right;
        right.row = 0;
        right.column = 1;
        right.box = QRectF(172, 100, 100, 60);
        right.top = {true, 1.0, Qt::black};
        right.right = {true, 1.0, Qt::black};
        right.bottom = {true, 1.0, Qt::black};
        right.blocks.append(paragraphBlock(paragraphOf(QStringLiteral("Right cell"), QRectF(176, 104, 90, 14))));
        table->cells = {left, right};
        LayoutBlock tableBlock;
        tableBlock.kind = LayoutBlock::Kind::Table;
        tableBlock.table = table;
        tableBlock.top = 100;
        tableBlock.bottom = 160;
        page.blocks.append(tableBlock);

        LayoutBlock pictureBlock;
        pictureBlock.kind = LayoutBlock::Kind::Image;
        pictureBlock.image.box = QRectF(100, 300, 40, 30);
        pictureBlock.image.bytes = png(QSize(20, 15), QColor(10, 120, 200));
        pictureBlock.image.extension = QStringLiteral("png");
        page.blocks.append(pictureBlock);

        // The same bytes at another place on the same page -- a logo, a repeated
        // rule -- so that the package's one copy of it can be counted.
        LayoutBlock repeated = pictureBlock;
        repeated.image.box = QRectF(300, 300, 40, 30);
        page.blocks.append(repeated);

        LayoutAnchor panel;
        panel.behindText = true;
        panel.image.box = QRectF(50, 600, 120, 10);
        panel.image.bytes = png(QSize(12, 1), QColor(20, 40, 200));
        panel.image.extension = QStringLiteral("png");
        page.anchors.append(panel);

        writer.addSlide(page);

        check(writer.tableCount() == 1 && writer.tableCellCount() == 2,
              QStringLiteral("the grid is a table of two cells (%1 / %2)")
                  .arg(writer.tableCount()).arg(writer.tableCellCount()));
        check(writer.pictureCount() == 2,
              QStringLiteral("both pictures are pictures on the slide (%1)").arg(writer.pictureCount()));
        check(writer.shapeCount() >= 1, QStringLiteral("the panel became a shape (%1)").arg(writer.shapeCount()));

        const QHash<QString, QByteArray> parts = writer.parts();
        checkPackage(parts, QStringLiteral("the picture slide: "));
        int media = 0;
        for (const QString &name : parts.keys())
            media += name.startsWith(QStringLiteral("ppt/media/")) ? 1 : 0;
        check(media == 1,
              QStringLiteral("a picture that appears twice is one file in the package (%1)").arg(media));

        const QByteArray slide = parts.value(QStringLiteral("ppt/slides/slide1.xml"));
        check(slide.contains("Left cell") && slide.contains("Right cell"),
              "both cells' words are on the slide");
        check(slide.contains("<a:srgbClr val=\"EEEEEE\"/>"), "the shaded cell is a filled rectangle");
        check(slide.contains("<a:srgbClr val=\"1428C8\"/>"), "and the panel is a rectangle of its colour");
        check(slide.contains("<a:blip r:embed="), "the picture refers to its media part");

        const QByteArray rels = parts.value(QStringLiteral("ppt/slides/_rels/slide1.xml.rels"));
        const QString target = [&rels] {
            const QRegularExpression pattern(QStringLiteral("Type=\"[^\"]*/image\" Target=\"([^\"]+)\""));
            const auto match = pattern.match(QString::fromUtf8(rels));
            return match.hasMatch() ? match.captured(1) : QString();
        }();
        check(target.startsWith(QStringLiteral("../media/image")),
              QStringLiteral("whose relationship names a media part (%1)").arg(target));
    }

    // ---- two columns ----
    //
    // The analysis hands a two-column stretch over as one block carrying each
    // column's own width; a slide has no columns, so every paragraph becomes a box of
    // its own -- and a centred line has to be centred in the column it was set in,
    // not in the page.
    {
        PptxWriter writer;
        LayoutPage page;
        page.size = QSizeF(595, 842);
        page.marginLeft = 72;
        page.marginRight = 72;

        auto columns = std::make_shared<LayoutColumns>();
        columns->leftWidth = 210;
        columns->gap = 31;
        columns->rightWidth = 210;
        const auto centred = [](double top) {
            LayoutParagraph paragraph = paragraphOf(QStringLiteral("A line"), QRectF(0, top, 60, 14));
            paragraph.align = LayoutAlign::Center;
            paragraph.left = 20;
            paragraph.right = 20;
            return paragraph;
        };
        columns->left.append(paragraphBlock(centred(100)));
        columns->right.append(paragraphBlock(centred(100)));

        LayoutBlock block;
        block.kind = LayoutBlock::Kind::Columns;
        block.columns = columns;
        block.top = 100;
        block.bottom = 114;
        page.blocks.append(block);
        writer.addSlide(page);

        check(writer.textBoxCount() == 2,
              QStringLiteral("each column's paragraph is a box of its own (%1)")
                  .arg(writer.textBoxCount()));
        const QHash<QString, QByteArray> parts = writer.parts();
        const QByteArray slide = parts.value(QStringLiteral("ppt/slides/slide1.xml"));
        // 72 + 20 = 92 points, and 72 + 210 + 31 + 20 = 333 points; each box is the
        // column's own width (210 - 20 - 20 = 170 points), not the page's.
        check(slide.contains("<a:off x=\"1168400\" y=\"1270000\"/><a:ext cx=\"2159000\""),
              "the left one sits inside the left column, as wide as that column");
        check(slide.contains("<a:off x=\"4229100\" y=\"1270000\"/><a:ext cx=\"2159000\""),
              "the right one inside the right column, past the gutter, the same width");
    }

    // ---- a document whose pages differ in size ----
    //
    // The first page fixes the slide size; the rest are scaled to fit it and
    // centred. A slide deck has one size, and shrinking a page evenly is the only
    // way to keep its shape.
    {
        PptxWriter writer;
        LayoutPage first;
        first.size = QSizeF(595, 842);
        first.blocks.append(paragraphBlock(paragraphOf(QStringLiteral("First"), QRectF(72, 100, 100, 14))));
        writer.addSlide(first);

        LayoutPage second;
        second.size = QSizeF(300, 400);
        second.blocks.append(paragraphBlock(paragraphOf(QStringLiteral("Second"), QRectF(10, 20, 100, 14))));
        writer.addSlide(second);

        check(writer.slideCount() == 2, "both pages are slides");
        check(writer.scaledSlideCount() == 1,
              QStringLiteral("the second page had to be scaled (%1)").arg(writer.scaledSlideCount()));
        const double scale = 595.0 / 300.0;
        const double dy = (842.0 - 400.0 * scale) / 2.0;
        const QHash<QString, QByteArray> parts = writer.parts();
        const QByteArray slide2 = parts.value(QStringLiteral("ppt/slides/slide2.xml"));
        const QByteArray offset = "<a:off x=\"" + QByteArray::number(emuOf(10 * scale)) + "\" y=\""
            + QByteArray::number(emuOf(dy + 20 * scale)) + "\"/>";
        check(slide2.contains(offset),
              QStringLiteral("and its first box is scaled and centred (%1)")
                  .arg(QString::fromUtf8(offset)));
    }

    // ---- the fixtures, end to end ----
    //
    // The whole path again, this time through PDFium: a document whose page is a
    // picture, a mixed one, and a ruled table.
    {
        if (const QString path = fixture(directory, QStringLiteral("picture.pdf")); !path.isEmpty()) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready, "the picture fixture opens");
            const QString file = output.filePath(QStringLiteral("picture.pptx"));
            const PptExportReport report = exportToPowerPoint(document, file);
            check(report.ok && report.pages == 1 && report.picturePages == 1,
                  QStringLiteral("one page, put in as one picture (%1 pages, %2 pictures, %3)")
                      .arg(report.pages).arg(report.picturePages).arg(report.error));
            const QSizeF pageSize = document.pageSize(0);
            check(std::fabs(report.slideSize.width() - pageSize.width()) < 0.01
                      && std::fabs(report.slideSize.height() - pageSize.height()) < 0.01,
                  QStringLiteral("the slide is the page's size (%1 x %2, page %3 x %4)")
                      .arg(report.slideSize.width()).arg(report.slideSize.height())
                      .arg(pageSize.width()).arg(pageSize.height()));
            const QHash<QString, QByteArray> parts = unpackZip(readAll(file));
            const QByteArray slide = parts.value(QStringLiteral("ppt/slides/slide1.xml"));
            check(slide.contains("<p:pic>"), "the page is a picture shape");
            check(slide.contains("<a:ext cx=\"" + QByteArray::number(emuOf(595)) + "\""),
                  "as large as the page");
            check(!parts.value(QStringLiteral("ppt/slides/_rels/slide1.xml.rels")).isEmpty(),
                  "with its media relationship");
            check(!parts.isEmpty(), "and the file on disk is a ZIP that can be read back");
            checkPackage(parts, QStringLiteral("picture.pptx: "));
        }

        if (const QString path = fixture(directory, QStringLiteral("mixed.pdf")); !path.isEmpty()) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready, "the mixed fixture opens");
            const QString file = output.filePath(QStringLiteral("mixed.pptx"));
            QVector<QPair<int, int>> reported;
            PptExportOptions options;
            options.progress = [&reported](int done, int total) { reported.append({done, total}); };
            const PptExportReport report = exportToPowerPoint(document, file, options);
            check(report.ok && report.pages == 2 && report.picturePages == 1 && report.textBoxes >= 1,
                  QStringLiteral("one page of words and one of a picture (%1 pages, %2 pictures, %3 boxes)")
                      .arg(report.pages).arg(report.picturePages).arg(report.textBoxes));
            check(!reported.isEmpty() && reported.first() == QPair<int, int>(0, 2)
                      && reported.last() == QPair<int, int>(2, 2),
                  QStringLiteral("it says how big the job is and when it is done (%1 reports)")
                      .arg(reported.size()));
            const QHash<QString, QByteArray> parts = unpackZip(readAll(file));
            checkPackage(parts, QStringLiteral("mixed.pptx: "));
            check(parts.contains(QStringLiteral("ppt/slides/slide2.xml")),
                  "and the second page is a slide of its own");
        }

        if (const QString path = fixture(directory, QStringLiteral("table.pdf")); !path.isEmpty()) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready, "the table fixture opens");
            const QString file = output.filePath(QStringLiteral("table.pptx"));
            const PptExportReport report = exportToPowerPoint(document, file);
            check(report.ok && report.tables == 1 && report.tableCells == 4,
                  QStringLiteral("its grid is one table of four cells (%1 tables, %2 cells)")
                      .arg(report.tables).arg(report.tableCells));
            const QHash<QString, QByteArray> parts = unpackZip(readAll(file));
            checkPackage(parts, QStringLiteral("table.pptx: "));
            check(parts.value(QStringLiteral("ppt/slides/slide1.xml")).contains("top left"),
                  "and the cell words are on the slide");
        }
    }

    // ---- a stop before the first page ----
    {
        if (const QString path = fixture(directory, QStringLiteral("dense.pdf")); !path.isEmpty()) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready, "the dense fixture opens");
            const QString file = output.filePath(QStringLiteral("stopped.pptx"));
            PptExportOptions options;
            options.cancel = QSharedPointer<QAtomicInt>::create(1);
            const PptExportReport refused = exportToPowerPoint(document, file, options);
            check(refused.cancelled && !QFileInfo::exists(file),
                  "an export stopped before it starts writes nothing");
        }
    }

    // ---- a stop that lands mid-export ----
    //
    // Asked for from the progress report after page 10, where a reader's click
    // lands. A stop noticed only at the end would still leave no file, so the
    // check is also how far it got -- and the file must not be there.
    {
        if (const QString path = fixture(directory, QStringLiteral("many-pages.pdf")); !path.isEmpty()) {
            PdfiumBackend document;
            check(document.load(path, QString()) == Document::Status::Ready, "the forty-page fixture opens");
            const QString file = output.filePath(QStringLiteral("stopped-midway.pptx"));
            auto flag = QSharedPointer<QAtomicInt>::create(0);
            int lastReported = 0;
            PptExportOptions options;
            options.cancel = flag;
            options.progress = [flag, &lastReported](int done, int) {
                lastReported = done;
                if (done == 10)
                    flag->storeRelaxed(1);
            };
            const PptExportReport report = exportToPowerPoint(document, file, options);
            std::cout << "MEASURE ppt stop: asked at page 10, last progress at page " << lastReported << "\n";
            check(report.cancelled && !QFileInfo::exists(file),
                  "a stop asked for mid-export is reported and writes nothing");
            check(lastReported >= 10 && lastReported <= 11,
                  QStringLiteral("and the export stopped at the next page (%1 of 40)").arg(lastReported));
        }
    }

    if (failures == 0) {
        std::cout << "a PDF comes out as slides that look like it\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
