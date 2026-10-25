// A scan put back as a file a reader can search: what the layer says, where it sits, and
// what must not have moved.
//
// ---- the sample ----
//
// Built here rather than kept in tests/fixtures, because the claim is about a page that is
// a photograph of words: a QImage drawn with QPainter and placed on a page as one image is
// exactly that, and it comes out with the fonts this machine has instead of pinning a file
// somebody made years ago. Two pages of it are scans -- Latin on one, Chinese on the other,
// because the recognizer is chosen per page and a page that needs the fallback for every
// word tests nothing -- and the third is real vector text, which is the page the export must
// leave alone.
//
// ---- what is asserted, and how each row can go red ----
//
// Every one of the four things the feature promises is checked by a row that has a twin:
//   the words are readable and near where they were drawn  -- the same look in the source
//     file finds nothing, so the row cannot pass on the sample's own text;
//   the page's picture does not change one pixel            -- reddened by writing the text
//     visibly instead of with `3 Tr` (see the report for the control run);
//   the file that was read is untouched                     -- reddened by the same hash
//     check against a copy that was deliberately appended to;
//   a page with a text layer keeps its own bytes            -- reddened by the same
//     comparison applied to a page that was written to, which is a built-in row here: the
//     scan page's content stream must have gained a stream and its first half must be the
//     page's own bytes.
// Plus the shapes the file has to have: one font object shared by the pages, /Identity-H
// with /CIDToGIDMap /Identity and a /ToUnicode, an embedded TrueType whose own checksums
// add up, and a refusal instead of a write when the destination is the source.

#include "../src/document/pagecontent.h"
#include "../src/document/pdfiumbackend.h"
#include "../src/edit/glyphlessfont.h"
#include "../src/edit/textlayerwriter.h"
#include "../src/export/searchablepdf.h"
#include "../src/ocr/ocrengine.h"

#include <QByteArray>
#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

void measure(const QString &what)
{
    // Flushed, because a row that stays in the buffer is a row nobody read: this
    // test's first crash took its whole console output with it, and the run looked
    // like a test that had not been started.
    std::cout << "MEASURE " << what.toStdString() << std::endl;
}

constexpr int kImageWidth = 1190;
constexpr int kImageHeight = 1684;
constexpr double kPageWidth = 595.0;
constexpr double kPageHeight = 842.0;
// The scan image is placed on the page at its full size, so one page point is exactly two
// of its pixels. Every expected box below is a pixel box halved.
constexpr double kPixelsPerPoint = 2.0;

// Where the test drew a word, and what it will look for in the finished file.
struct Placed
{
    int page = 0;
    QString needle;
    // In page points, top-left origin: the ink the recognizer was supposed to find.
    QRectF ink;
    // The font's em, in points. The tolerances below are fractions of it, because the
    // distance a selection box may be off by is a distance in the text's own size.
    double em = 0;
};

void paintLine(QImage &page, const QString &text, qreal pixelSize, qreal x, qreal baselineY,
               QVector<Placed> *placed, int pageIndex, const QStringList &needles)
{
    QPainter painter(&page);
    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPixelSize(int(pixelSize));
    painter.setFont(font);
    painter.drawText(QPointF(x, baselineY), text);
    painter.end();

    const QFontMetricsF metrics(font);
    for (const QString &needle : needles) {
        const int at = text.indexOf(needle);
        if (at < 0)
            continue;
        Placed entry;
        entry.page = pageIndex;
        entry.needle = needle;
        entry.em = pixelSize / kPixelsPerPoint;
        const QRectF ink = metrics.tightBoundingRect(needle);
        const qreal start = x + metrics.horizontalAdvance(text.left(at));
        entry.ink = QRectF((start + ink.left()) / kPixelsPerPoint,
                           (baselineY + ink.top()) / kPixelsPerPoint,
                           ink.width() / kPixelsPerPoint, ink.height() / kPixelsPerPoint);
        placed->append(entry);
    }
}

// The three-page sample: two photographs of words and one page of real text. Returns false
// when nothing could be written, which is the one failure a caller can do nothing about.
bool writeSample(const QString &path, QVector<Placed> *placed)
{
    QImage latin(kImageWidth, kImageHeight, QImage::Format_ARGB32);
    latin.fill(Qt::white);
    paintLine(latin, QStringLiteral("SEARCHABLE 123"), 80, 200, 420, placed, 0,
              { QStringLiteral("SEARCHABLE"), QStringLiteral("123") });
    // A second line, lower down, so that a layer placed with the wrong vertical scale is
    // caught by the gap between the two rather than only by each line's own position.
    paintLine(latin, QStringLiteral("SECOND LINE"), 80, 200, 700, placed, 0,
              { QStringLiteral("SECOND") });

    QImage chinese(kImageWidth, kImageHeight, QImage::Format_ARGB32);
    chinese.fill(Qt::white);
    paintLine(chinese, QStringLiteral("可搜索的扫描页"), 60, 220, 600, placed, 1,
              { QStringLiteral("可搜索"), QStringLiteral("扫描") });

    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageLayout(
        QPageLayout(QPageSize(QSizeF(kPageWidth, kPageHeight), QPageSize::Point),
                    QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    QPainter painter(&writer);
    painter.drawImage(QRectF(0, 0, kPageWidth, kPageHeight), latin);
    writer.newPage();
    painter.drawImage(QRectF(0, 0, kPageWidth, kPageHeight), chinese);
    writer.newPage();
    // Real text, in more than the forty characters the scan rule stops at: this page has a
    // text layer already and must come out of the export exactly as it went in.
    QFont font = painter.font();
    font.setPixelSize(14);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(QPointF(40, 60),
                     QStringLiteral("Printed text on the third page is not a picture of "
                                    "anything, and this export leaves it alone."));
    painter.end();
    return QFileInfo::exists(path);
}

QByteArray hashOfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

// One page's content streams, decoded, the way an unpacker reads them rather than the way
// this program wrote them: `pageStreams(source, 0).first() == pageStreams(result, 0).first()`
// is the statement "the page's own drawing is still its own bytes".
std::vector<QByteArray> pageStreams(const QString &path, int pageIndex)
{
    std::vector<QByteArray> streams;
    QPDF pdf;
    try {
        pdf.processFile(path.toUtf8().constData());
        QPDFPageDocumentHelper helper(pdf);
        std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
        if (pageIndex < 0 || pageIndex >= int(pages.size()))
            return streams;
        for (QPDFObjectHandle stream : pages.at(size_t(pageIndex)).getPageContents()) {
            const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_all);
            if (data)
                streams.emplace_back(reinterpret_cast<const char *>(data->getBuffer()),
                                     int(data->size()));
        }
    } catch (const std::exception &) {
        // Left empty; the caller's row then goes red with "no streams", which is the
        // honest reading of a file qpdf cannot open.
    }
    return streams;
}

QByteArray joined(const std::vector<QByteArray> &streams)
{
    QByteArray all;
    for (const QByteArray &stream : streams)
        all.append(stream);
    return all;
}

// The font this program adds, by the name it gives it. Empty when the page does not have
// one -- which is what the text page must answer.
QPDFObjectHandle layerFont(QPDF &pdf, int pageIndex)
{
    QPDFPageDocumentHelper helper(pdf);
    std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
    if (pageIndex < 0 || pageIndex >= int(pages.size()))
        return QPDFObjectHandle::newNull();
    QPDFObjectHandle fonts = pages.at(size_t(pageIndex))
                                 .getObjectHandle()
                                 .getKey("/Resources")
                                 .getKey("/Font");
    if (!fonts.isDictionary())
        return QPDFObjectHandle::newNull();
    for (const std::string &key : fonts.getKeys()) {
        if (key.find("XYZPDFScan") != std::string::npos)
            return fonts.getKey(key.c_str());
    }
    return QPDFObjectHandle::newNull();
}

// The TrueType checksum: the file read as big-endian 32-bit words, and the number the
// format says the whole file must add up to once head's checkSumAdjustment is in place.
quint32 wordSum(const QByteArray &data)
{
    quint32 sum = 0;
    for (int i = 0; i < data.size(); i += 4) {
        quint32 word = 0;
        for (int j = 0; j < 4; ++j)
            word = (word << 8) | (i + j < data.size() ? quint8(data.at(i + j)) : 0u);
        sum += word;
    }
    return sum;
}

quint16 u16(const QByteArray &data, int at)
{
    return quint16((quint8(data.at(at)) << 8) | quint8(data.at(at + 1)));
}

int differingPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return -1;
    const QImage first = a.convertToFormat(QImage::Format_ARGB32);
    const QImage second = b.convertToFormat(QImage::Format_ARGB32);
    int different = 0;
    for (int y = 0; y < first.height(); ++y) {
        const QRgb *one = reinterpret_cast<const QRgb *>(first.constScanLine(y));
        const QRgb *two = reinterpret_cast<const QRgb *>(second.constScanLine(y));
        for (int x = 0; x < first.width(); ++x)
            different += one[x] == two[x] ? 0 : 1;
    }
    return different;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);

    if (!Ocr::isAvailable()) {
        std::cout << "no OCR language packs on this machine; the searchable-PDF test is a skip\n";
        return 0;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "FAIL: no temporary directory to work in\n";
        return 1;
    }
    const QString sample = directory.filePath(QStringLiteral("sample.pdf"));
    const QString output = directory.filePath(QStringLiteral("searchable.pdf"));

    QVector<Placed> placed;
    if (!writeSample(sample, &placed)) {
        std::cerr << "FAIL: the sample could not be written\n";
        return 1;
    }
    check(placed.size() == 5,
          QStringLiteral("the sample carries five measured words, not %1").arg(placed.size()));

    // ---- the sample is the shape the feature is for ----
    PdfiumBackend source;
    check(source.load(sample, QString()) == Document::Status::Ready, "the sample opens");
    check(source.pageCount() == 3, QStringLiteral("it has three pages"));

    const PageContent first = pageContentOf(source, 0, true);
    check(first.ok && first.chars.isEmpty() && first.invisibleChars == 0,
          QStringLiteral("the Latin page has no text of its own (%1 chars, %2 invisible)")
              .arg(first.chars.size())
              .arg(first.invisibleChars));
    check(first.images.size() == 1
              && std::fabs(first.images.at(0).box.width() * first.images.at(0).box.height()
                           - kPageWidth * kPageHeight)
              < 4.0,
          "and one picture the size of the page");
    const PageContent third = pageContentOf(source, 2, true);
    check(third.chars.size() > 40 && third.invisibleChars == 0,
          QStringLiteral("the third page is printed text (%1 characters)").arg(third.chars.size()));

    const QByteArray sourceHashBefore = hashOfFile(sample);
    check(!sourceHashBefore.isEmpty(), "the sample has a hash to compare against");

    // ---- the export ----
    PdfiumBackend document;
    check(document.load(sample, QString()) == Document::Status::Ready,
          "the sample opens for the export");
    const SearchablePdfReport report = makeSearchablePdf(document, sample, QString(), output);
    measure(QStringLiteral("error: %1").arg(report.error));
    measure(QStringLiteral("recognized=%1 skipped=%2 empty=%3 words=%4 writtenPages=%5")
                .arg(report.recognized)
                .arg(report.skipped)
                .arg(report.empty)
                .arg(report.words)
                .arg(report.writtenPages));
    check(report.ok, QStringLiteral("the export wrote a file (%1)").arg(report.error));
    check(report.recognized == 2, QStringLiteral("two pages went to the recognizer"));
    check(report.skipped == 1, QStringLiteral("one page was left alone as printed text"));
    check(report.empty == 0, QStringLiteral("no recognized page came back empty"));
    check(report.writtenPages == 2 && report.words >= 5,
          QStringLiteral("both pages gained words (%1 pages, %2 words)")
              .arg(report.writtenPages)
              .arg(report.words));

    // ---- (c) the file that was read ----
    check(hashOfFile(sample) == sourceHashBefore, "the sample's bytes did not change");
    // The same comparison must be able to say "changed", or the row above is reading a hash
    // of nothing. Appending one byte to a copy is that proof, and it is checked here rather
    // than in a control run because it costs a file and no source edit.
    {
        const QString scratch = directory.filePath(QStringLiteral("mutated.pdf"));
        QFile::copy(sample, scratch);
        QFile file(scratch);
        if (file.open(QIODevice::Append))
            file.putChar('\n');
        file.close();
        check(hashOfFile(scratch) != sourceHashBefore,
              "and the hash comparison does notice one byte added to a copy");
    }

    // ---- (a) the words, and where they are ----
    PdfiumBackend result;
    check(result.load(output, QString()) == Document::Status::Ready, "the new file opens");
    check(result.pageCount() == 3, QStringLiteral("and still has three pages"));

    const PageContent nowFirst = pageContentOf(result, 0, true);
    const PageContent nowSecond = pageContentOf(result, 1, true);
    check(nowFirst.invisibleChars > 0,
          QStringLiteral("the Latin page now carries invisible text (%1 characters)")
              .arg(nowFirst.invisibleChars));
    check(nowSecond.invisibleChars > 0,
          QStringLiteral("the Chinese page carries its words as invisible text too (%1)")
              .arg(nowSecond.invisibleChars));
    // What the layer adds to the page a reader can see. PDFium counts a character as
    // visible even when the file draws it in render mode 3 if the space between two show-texts
    // is one it put there itself -- so the check is "nothing but spaces", not "nothing".
    const auto onlySpaces = [](const PageContent &content) {
        for (const ContentChar &ch : content.chars) {
            if (ch.code > 0x20)
                return false;
        }
        return true;
    };
    check(onlySpaces(nowFirst) && onlySpaces(nowSecond),
          QStringLiteral("and no visible characters were added to either page (%1, %2)")
              .arg(nowFirst.chars.size())
              .arg(nowSecond.chars.size()));
    check(!source.text(0).contains(QStringLiteral("SEARCHABLE")),
          "the source page said nothing at all before the export");

    for (const Placed &entry : std::as_const(placed)) {
        const QString pageText = result.text(entry.page);
        const int at = pageText.indexOf(entry.needle, 0, Qt::CaseInsensitive);
        check(at >= 0, QStringLiteral("page %1 reads back the word \"%2\" (page text: \"%3\")")
                           .arg(entry.page + 1)
                           .arg(entry.needle, pageText.simplified()));
        if (at < 0)
            continue;

        const PageSelection selection = result.pageSelection(entry.page, at, at + entry.needle.length());
        QRectF box;
        for (const QRectF &run : selection.boxes)
            box = box.isNull() ? run : box.united(run);
        measure(QStringLiteral("word \"%1\": drawn [%2] selected [%3]")
                    .arg(entry.needle)
                    .arg(QStringLiteral("%1,%2 %3x%4")
                             .arg(entry.ink.left())
                             .arg(entry.ink.top())
                             .arg(entry.ink.width())
                             .arg(entry.ink.height()),
                         QStringLiteral("%1,%2 %3x%4")
                             .arg(box.left())
                             .arg(box.top())
                             .arg(box.width())
                             .arg(box.height())));
        const double centreError = std::fabs((box.center().x() - entry.ink.center().x()))
            > 0.25 * entry.em
            || std::fabs((box.center().y() - entry.ink.center().y())) > 0.35 * entry.em;
        check(!centreError,
              QStringLiteral("\"%1\" is placed where it was drawn (em %2 pt)").arg(entry.needle)
                  .arg(entry.em));
        check(std::fabs(box.width() - entry.ink.width()) <= 0.3 * entry.ink.width(),
              QStringLiteral("\"%1\" is as wide as the ink it stands for (%2 pt against %3)")
                  .arg(entry.needle)
                  .arg(box.width())
                  .arg(entry.ink.width()));
    }

    // ---- (b) the picture does not move ----
    for (int page = 0; page < 3; ++page) {
        const RenderOptions options;
        const QImage before = source.render(page, QSize(595, 842), options);
        const QImage after = result.render(page, QSize(595, 842), options);
        check(!before.isNull() && !after.isNull(),
              QStringLiteral("page %1 renders on both sides of the export").arg(page + 1));
        const int different = differingPixels(before, after);
        measure(QStringLiteral("page %1: %2 pixels of 501290 differ").arg(page + 1).arg(different));
        check(different == 0, QStringLiteral("page %1 is pixel for pixel what it was").arg(page + 1));
    }

    // ---- (d) the page that already had text keeps its own bytes ----
    const std::vector<QByteArray> sourceThirdStreams = pageStreams(sample, 2);
    const std::vector<QByteArray> resultThirdStreams = pageStreams(output, 2);
    check(sourceThirdStreams.size() == resultThirdStreams.size()
              && joined(sourceThirdStreams) == joined(resultThirdStreams),
          QStringLiteral("the printed page's content streams are the same bytes (%1 against %2)")
              .arg(sourceThirdStreams.size())
              .arg(resultThirdStreams.size()));
    // The twin of that row, on a page that was written to: it must have gained streams, and
    // the streams it already had must still be its own bytes. Without this, the row above
    // could be comparing two files that are equal because nothing was ever written.
    //
    // Two streams are added, not one: `q` before the page's own content and `Q` with the
    // text layer after it. The page's own bytes are not touched -- they are still the same
    // objects -- and what changes is the state they run in, which the layer's coordinates
    // have to be able to count on (see the note in textlayerwriter.cpp).
    {
        const std::vector<QByteArray> sourceFirstStreams = pageStreams(sample, 0);
        const std::vector<QByteArray> resultFirstStreams = pageStreams(output, 0);
        check(resultFirstStreams.size() == sourceFirstStreams.size() + 2,
              QStringLiteral("the scan page gained the wrap and the layer (%1 against %2)")
                  .arg(sourceFirstStreams.size())
                  .arg(resultFirstStreams.size()));
        bool ownBytesIntact = resultFirstStreams.size() == sourceFirstStreams.size() + 2;
        if (ownBytesIntact) {
            ownBytesIntact = resultFirstStreams.front() == "q\n";
            for (size_t i = 0; i < sourceFirstStreams.size() && ownBytesIntact; ++i)
                ownBytesIntact = sourceFirstStreams.at(i) == resultFirstStreams.at(i + 1);
        }
        check(ownBytesIntact, "the page's own streams are unchanged, byte for byte, inside the wrap");
        const QByteArray added = resultFirstStreams.size() > 2 ? resultFirstStreams.back()
                                                              : QByteArray();
        check(added.startsWith("Q\n") && added.contains("3 Tr") && added.contains(" Tz")
                  && added.contains(" Tm"),
              "the stream that was added draws invisible text, stretched and placed");
        check(added.contains("q\nBT") && added.trimmed().endsWith("Q"),
              "and the layer itself is wrapped too, so it leaves nothing set behind");
    }

    // ---- the file's shape ----
    {
        QPDF pdf;
        pdf.processFile(output.toUtf8().constData());
        const QPDFObjectHandle one = layerFont(pdf, 0);
        const QPDFObjectHandle two = layerFont(pdf, 1);
        check(!one.isNull() && !two.isNull(), "both scan pages name a font");
        check(one.isSameObjectAs(two),
              "and it is one object shared by the file, not two copies of it");
        check(layerFont(pdf, 2).isNull(), "and the printed page was given none");
        if (!one.isNull()) {
            check(one.getKey("/Subtype").getName() == "/Type0"
                      && one.getKey("/Encoding").getName() == "/Identity-H",
                  "a Type0 font encoded Identity-H");
            const QPDFObjectHandle descendant = one.getKey("/DescendantFonts").getArrayItem(0);
            check(descendant.getKey("/Subtype").getName() == "/CIDFontType2"
                      && descendant.getKey("/CIDToGIDMap").getName() == "/Identity",
                  "with a CIDFontType2 descendant whose CIDs are glyph indices");
            // /ToUnicode belongs to the Type0 font, not to its descendant (PDF 32000-1
            // sec.9.10.2), and it is the only thing that says what these codes are: the
            // embedded program has no cmap a reader could use for that.
            check(one.getKey("/ToUnicode").isStream(),
                  "and a /ToUnicode on the composite font, which is what makes the words readable");
            const QPDFObjectHandle descriptor = descendant.getKey("/FontDescriptor");
            QPDFObjectHandle program = descriptor.getKey("/FontFile2");
            const std::shared_ptr<Buffer> bytes = program.getStreamData(qpdf_dl_all);
            check(bytes && bytes->size() > 12, "with an embedded TrueType program");
            if (bytes) {
                const QByteArray font(reinterpret_cast<const char *>(bytes->getBuffer()),
                                      int(bytes->size()));
                check(u16(font, 0) == 0x0001 && u16(font, 4) == 9,
                      QStringLiteral("it is a TrueType face of nine tables (%1)")
                          .arg(u16(font, 4)));
                // The directory record is tag, checksum, offset, length. A face whose
                // records are written in any other order has no table names a parser can
                // find, and that is a defect no count of characters and no pixel compare
                // notices: the layer still reads back, from the engine's own fallback.
                QStringList tags;
                for (int record = 0; record < 9; ++record)
                    tags.append(QString::fromLatin1(font.mid(12 + 16 * record, 4)));
                check(tags == QStringList{QStringLiteral("cmap"), QStringLiteral("glyf"),
                                          QStringLiteral("head"), QStringLiteral("hhea"),
                                          QStringLiteral("hmtx"), QStringLiteral("loca"),
                                          QStringLiteral("maxp"), QStringLiteral("name"),
                                          QStringLiteral("post")},
                      QStringLiteral("and names its tables where the format says they are (%1)")
                          .arg(tags.join(QLatin1Char(','))));
                check(wordSum(font) == 0xB1B0AFBAu,
                      QStringLiteral("and its own checksum adds up (got %1)")
                          .arg(wordSum(font), 0, 16));
            }
        }
    }

    // ---- the mapping on a page that is turned ----
    //
    // The layer's coordinates are handed to the writer in the page's own user space, and
    // the only reason the conversion is asked of PDFium rather than worked out here is that
    // a page marked /Rotate shows the reader one thing and stores another. Nothing in the
    // rest of this file can see that: the sample is unrotated, so the mapping it exercises
    // is the identity. So the same page is turned ninety degrees here -- a test copy in a
    // temporary folder, never a reader's file -- and the three points that build the layer
    // transform are asked for directly.
    {
        QTemporaryDir turned;
        const QString path = turned.filePath(QStringLiteral("rotated.pdf"));
        try {
            // Read the sample, write the turned copy somewhere else: qpdf will not write to
            // the file it has open, and an exception escaping here takes the process down
            // before anything it had printed reaches the console.
            QPDF pdf;
            pdf.processFile(sample.toUtf8().constData());
            QPDFPageDocumentHelper helper(pdf);
            std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
            pages.at(0).getObjectHandle().replaceKey("/Rotate",
                                                     QPDFObjectHandle::newInteger(90));
            QPDFWriter writer(pdf, path.toUtf8().constData());
            writer.write();
        } catch (const std::exception &e) {
            check(false, QStringLiteral("the page could be turned: %1")
                             .arg(QString::fromUtf8(e.what())));
        }
        PdfiumBackend turn;
        check(turn.load(path, QString()) == Document::Status::Ready,
              "the turned page opens");
        check(turn.pageSize(0) == QSizeF(kPageHeight, kPageWidth),
              QStringLiteral("and is shown landscape (%1 × %2)")
                  .arg(turn.pageSize(0).width())
                  .arg(turn.pageSize(0).height()));
        QPointF corner;
        QPointF right;
        QPointF down;
        const bool asked = userSpacePoint(turn, 0, QPointF(0, 0), &corner)
            && userSpacePoint(turn, 0, QPointF(1, 0), &right)
            && userSpacePoint(turn, 0, QPointF(0, 1), &down);
        measure(QStringLiteral("turned page: (0,0)->(%1,%2) right step (%3,%4) down step (%5,%6)")
                    .arg(corner.x())
                    .arg(corner.y())
                    .arg(right.x() - corner.x())
                    .arg(right.y() - corner.y())
                    .arg(down.x() - corner.x())
                    .arg(down.y() - corner.y()));
        check(asked, "and the mapping answers for a rotated page too");
        // A quarter turn: the reader's right is the stored page's up or down, the reader's
        // down is the other one, and each step is still one point. Which corner the page
        // lands on is the file's own business -- what a layer written against these two
        // vectors needs is that they are perpendicular and one point long, because anything
        // else is a shear or a scale, and a shear is what a hand-rolled inverse gives.
        const QPointF across = right - corner;
        const QPointF under = down - corner;
        check(std::fabs(std::hypot(across.x(), across.y()) - 1.0) < 0.01
                  && std::fabs(std::hypot(under.x(), under.y()) - 1.0) < 0.01
                  && std::fabs(across.x() * under.x() + across.y() * under.y()) < 0.01,
              QStringLiteral("the turned page's two directions are perpendicular one-point steps "
                             "(%1,%2 and %3,%4)")
                  .arg(across.x())
                  .arg(across.y())
                  .arg(under.x())
                  .arg(under.y()));
    }

    // ---- the refusals ----
    check(hashOfFile(sample) == sourceHashBefore, "reading the file never changed it");
    {
        const SearchablePdfReport ontoItself
            = makeSearchablePdf(document, sample, QString(), sample);
        check(!ontoItself.ok && !ontoItself.error.isEmpty(),
              "and asking for the output under the source's own name is refused");
        check(hashOfFile(sample) == sourceHashBefore, "which leaves the source as it was");
    }
    {
        TextLayerWriteReport missingDirectory = writeTextLayer(
            sample, directory.filePath(QStringLiteral("nowhere/file.pdf")), QString(), {});
        check(!missingDirectory.ok && !missingDirectory.error.isEmpty(),
              "a destination in a folder that is not there is refused");
        QTemporaryDir elsewhere;
        const QString copy = elsewhere.filePath(QStringLiteral("copy.pdf"));
        QFile::copy(sample, copy);
        TextLayerPage page;
        page.page = 0;
        TextLayerWord word;
        word.text = QStringLiteral("XYZPDF");
        word.box = QRectF(100, 100, 200, 30);
        word.size = 30;
        page.words.append(word);
        // One character outside the Basic Multilingual Plane, so that the /ToUnicode has to
        // carry a surrogate pair and the row below has something to look for.
        TextLayerWord astral;
        astral.text = QString(QChar(0xD83D)) + QChar(0xDE00);
        astral.box = QRectF(100, 150, 60, 30);
        astral.size = 30;
        page.words.append(astral);
        const TextLayerWriteReport written
            = writeTextLayer(copy, elsewhere.filePath(QStringLiteral("layer.pdf")), QString(), {page});
        check(written.ok && written.words == 2,
              QStringLiteral("the writer works on a page named by hand (%1)").arg(written.error));
        QPDF pdf;
        pdf.processFile(elsewhere.filePath(QStringLiteral("layer.pdf")).toUtf8().constData());
        QPDFObjectHandle toUnicode
            = layerFont(pdf, 0).getKey("/ToUnicode");
        const std::shared_ptr<Buffer> cmap = toUnicode.getStreamData(qpdf_dl_all);
        const QByteArray cmapBytes = cmap
            ? QByteArray(reinterpret_cast<const char *>(cmap->getBuffer()), int(cmap->size()))
            : QByteArray();
        check(cmapBytes.contains("<0001> <0058>") && cmapBytes.contains("<0007> <D83DDE00>"),
              QStringLiteral("and the /ToUnicode maps each CID to its UTF-16BE, surrogate pair "
                             "and all (%1 bytes of CMap)").arg(cmapBytes.size()));
    }

    // ---- the invisible layer is a text layer the next run can see ----
    {
        PdfiumBackend again;
        check(again.load(output, QString()) == Document::Status::Ready,
              "the file this export wrote opens again");
        const SearchablePdfReport second
            = makeSearchablePdf(again, output, QString(), directory.filePath(QStringLiteral("twice.pdf")));
        check(!second.ok && second.recognized == 0 && second.skipped == 3,
              QStringLiteral("and every page is left alone the second time around (%1 recognized, "
                             "%2 skipped, %3 empty)")
                  .arg(second.recognized)
                  .arg(second.skipped)
                  .arg(second.empty));
        check(!QFileInfo::exists(directory.filePath(QStringLiteral("twice.pdf"))),
              "nothing was written when there was nothing to add");
    }

    // ---- the font generator's own edges ----
    check(glyphlessTrueType(0).isEmpty() && glyphlessTrueType(70000).isEmpty(),
          "a font is generated only for a number of glyphs a TrueType header can hold");
    for (const int glyphs : {1, 2, 300, 4096}) {
        const QByteArray font = glyphlessTrueType(glyphs);
        check(!font.isEmpty() && u16(font, 4) == 9,
              QStringLiteral("a %1-glyph font is built with nine tables").arg(glyphs));
        // maxp is the third table in this file's sorted directory; read numGlyphs from
        // wherever the directory says it is, which is the only way to know the directory
        // and the body agree.
        const int count = u16(font, 4);
        int maxpOffset = -1;
        for (int record = 0; record < count; ++record) {
            const int at = 12 + 16 * record;
            if (font.mid(at, 4) == "maxp")
                maxpOffset = int(quint32((quint8(font.at(at + 8)) << 24)
                                         | (quint8(font.at(at + 9)) << 16)
                                         | (quint8(font.at(at + 10)) << 8)
                                         | quint8(font.at(at + 11))));
        }
        check(maxpOffset >= 0 && u16(font, maxpOffset + 4) == glyphs,
              QStringLiteral("and says it has %n glyphs", nullptr, glyphs));
        check(wordSum(font) == 0xB1B0AFBAu, "and adds up to what the format promises");
    }

    if (failures == 0) {
        std::cout << "the scan came back searchable, and nothing else moved\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
