#include "documenteditor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <QDebug>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// A namespace rather than a class member, and named the way QObject's is: these
// are sentences to show the reader, so they go through the catalogue. Going
// through QObject::tr() instead would park every one of them in the "QObject"
// context, which is the thing src/windows/shellintegration.cpp warns about.
QString tr(const char *text)
{
    return QCoreApplication::translate("DocumentEditor", text);
}

// The plural shape of the same arrangement, and it takes three arguments for a reason
// that is not visible from Qt's own documentation: lupdate reads a `tr()` call by
// position -- source, disambiguation, count -- so a helper written as tr(text, n) hands
// the count to the disambiguation slot. The string still lands in the catalogue, but
// flagged non-plural, and the next update run rewrites the row down to a single
// <numerusform> ("Removed plural forms as the target language has less forms") and the
// English starts saying "Delete 1 pages" again. Measured: 34 plural rows became 29, and
// the five lost were exactly this context's, because every other file's tr() is
// QObject's and its callers already pass the nullptr.
//
// The Chinese needs no second form -- it does not inflect -- so the two forms differ
// only in the translation, which is the whole reason this is not an "s" bolted onto the
// English: a screen reader says "Delete 1 pages" out loud, and that is the reader's
// problem, not a typographical one.
QString tr(const char *text, const char *disambiguation, int n)
{
    return QCoreApplication::translate("DocumentEditor", text, disambiguation, n);
}

// The comparison half of sameFile(): the same path written two ways is still one
// file, and the two ways a save dialog produces are a different drive letter case
// and a slash rather than a backslash.
QString comparablePath(const QString &path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower();
}

// The document as it was opened: every page present, in its own order, all of them
// named by the number the reader is looking at.
QVector<DocumentEditor::PageRef> openedDocumentInOrder(int pageCount)
{
    QVector<DocumentEditor::PageRef> pages;
    pages.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        pages.append({0, page});
    return pages;
}

// Can this name still be replaced, right now? The last step of a save is one MoveFileExW per file
// with MOVEFILE_REPLACE_EXISTING, and Windows lets that through only while the file's current
// reader allows the delete share -- so a program holding the file open for plain reading makes the
// name unreplaceable even though every byte of it is readable. Measured: with a tab of this window
// reading the name, the move came back as error 5 after the whole document had already been
// rebuilt and written to a temporary name (tests/documenttab_smoke.cpp pins that, and §66's
// "应用自己开 PDF 仍锁文件" is the same fact seen from the other side).
//
// Asking for the DELETE right is the same question the move itself will ask, and it touches no
// contents. The alternative tried first was to rename the file and rename it back, which really
// moves the reader's document and leaves it renamed if anything goes wrong in between. And only a
// refusal to share the delete counts here: any other failure -- a folder this user may not open,
// a reparse point, an antivirus saying no -- is left for the move itself to report, because turning
// an unknown into a refusal would stop saves that work today.
bool replaceableDespiteBeingRead(const QString &path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const HANDLE probe = CreateFileW(native.c_str(), DELETE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        return true;
    }
    const DWORD refused = GetLastError();
    return refused != ERROR_ACCESS_DENIED && refused != ERROR_SHARING_VIOLATION;
}

// What one bookmark destination said, in the file's own words.
//
// §75 measured that a save takes this away: `writeOutlineItem` used to emit /Fit for every row it was
// handed, because the read path handed the tree over as a title and a page number and nothing else.
// Asking Qt's model for one more role is not a fix -- Qt reports no position for /FitH, and a real
// book's bookmarks were 184 /FitH out of 184 -- so the shape has to be read off the file. This is
// that reading: the kind plus its numbers, with nulls kept, because `/XYZ 0 700 null` means "keep the
// zoom" and a 0.0 in that slot would mean something else.
//
// Returns `None` for a destination that names only a page, and for one this parser does not know: an
// unknown kind is left flattened exactly as it was before, rather than written back as a guess.
BookmarkDest parseBookmarkDest(const QPDFObjectHandle &dest)
{
    BookmarkDest out;
    if (!dest.isArray() || dest.getArrayNItems() < 2)
        return out;
    const QPDFObjectHandle kind = dest.getArrayItem(1);
    if (!kind.isName())
        return out;
    static const struct
    {
        const char *name;
        BookmarkDest::Kind kind;
    } table[] = {
        {"/Fit", BookmarkDest::Kind::Fit},   {"/XYZ", BookmarkDest::Kind::XYZ},
        {"/FitH", BookmarkDest::Kind::FitH}, {"/FitV", BookmarkDest::Kind::FitV},
        {"/FitR", BookmarkDest::Kind::FitR}, {"/FitB", BookmarkDest::Kind::FitB},
        {"/FitBH", BookmarkDest::Kind::FitBH}, {"/FitBV", BookmarkDest::Kind::FitBV},
    };
    const std::string asked = kind.getName();
    for (const auto &entry : table) {
        if (asked == entry.name) {
            out.kind = entry.kind;
            break;
        }
    }
    if (out.kind == BookmarkDest::Kind::None)
        return out;
    for (int i = 2; i < dest.getArrayNItems(); ++i) {
        const QPDFObjectHandle value = dest.getArrayItem(i);
        if (value.isNumber())
            out.numbers.append(std::optional<double>(value.getNumericValue()));
        else
            out.numbers.append(std::optional<double>());
    }
    return out;
}

// Which file row, if any, each requested row is. Matched on what the row *is* -- title and the page
// it names -- taking the file's rows in walk order and at most once each, and NOT on the index. An
// index join shifts every bookmark after an inserted row, so "add a bookmark at the top" would cost
// three other bookmarks their places: §99's first version matched that way and the insert-a-row test
// reported the whole tree at risk.
//
// The price of matching on content is the duplicate: adding a second 「Xyz spot」@1 leaves one of the
// two without the file's destination, because only one row may consume it, and that one comes back
// at page top. Recorded here because the test that covers it is a one-row insert with a repeated
// title, and because "which of the two copies keeps the place" has no right answer to give.
struct MatchedRows
{
    QVector<int> fileIndex;   // one per requested row; -1 when the file has no counterpart
    QVector<bool> consumed;   // one per file row
};

// The writer's view of the shared join: which file row each requested row is, plus the file rows
// nobody claimed. Both the destinations a write carries and the number the box warns about go
// through here, and here goes through matchOutlineRows() -- the sidebar lands a click by the same
// rule, and two copies of it is how one of them ends up wrong.
MatchedRows matchRowsToSource(const QVector<OutlineRow> &file, const QVector<OutlineRow> &rows)
{
    MatchedRows out;
    out.fileIndex = matchOutlineRows(file, rows);
    out.consumed = QVector<bool>(file.size(), false);
    for (const int index : std::as_const(out.fileIndex)) {
        if (index >= 0)
            out.consumed[index] = true;
    }
    return out;
}

// The file's own bookmark tree: title, level, page (a position in the document as opened) and the
// destination as written. Read at open, and what setOutline() attaches destinations from.
//
// Why this is not the tree the box shows: that one comes from Qt's model, which is the right source
// for a title and a page and the wrong one for a place inside a page (§75). So the program now holds
// two readings of the same tree, and they are joined by *position in the walk* plus a check that the
// title and page agree -- a row that moved, was renamed or was made here is left without a
// destination rather than being given a neighbour's.
//
// Walked by hand rather than with the helper's walk() callback because the recursion has to carry the
// depth as it goes and assemble rows in the order a sidebar draws them -- the same order the dialog
// lists them, which is what makes the join by index meaningful at all.
// The destination of one outline item, read from the item's own keys: /Dest, or the /D of a GoTo
// action, and a name looked up in the older root /Dest dictionary. A destination that cannot be
// resolved this way answers null, and the caller treats that as "nothing to keep" -- which is what
// §80 already did for the count, and what the writer already did for the row: flattened, not guessed.
QPDFObjectHandle itemDestination(QPDF &qpdf, const QPDFObjectHandle &item)
{
    QPDFObjectHandle asked = item.getKey("/Dest");
    if (asked.isNull()) {
        const QPDFObjectHandle action = item.getKey("/A");
        if (action.isDictionary() && action.getKey("/S").isName()
            && action.getKey("/S").getName() == "/GoTo")
            asked = action.getKey("/D");
    }
    if (asked.isName() || asked.isString()) {
        // A named destination. The Names tree is the current place for these; the root /Dest
        // dictionary is the older one. Looked up in the second, and an unresolved name is left
        // alone -- resolving the tree would mean reaching the document's name table here, and the
        // honest answer for a bookmark we cannot place is the same one the writer already gives.
        const QPDFObjectHandle table = qpdf.getRoot().getKey("/Dest");
        if (table.isDictionary()) {
            const std::string key = asked.isName() ? asked.getName() : asked.getStringValue();
            const QPDFObjectHandle found = table.getKey(asked.isName() ? key : "/" + key);
            if (!found.isNull())
                return found;
        }
        return QPDFObjectHandle::newNull();
    }
    return asked;
}

// The file's own bookmark tree: title, level, page (a position in the document as opened) and the
// destination as written. Read at open, and what setOutline() attaches destinations from.
//
// Why this is not the tree the box shows: that one comes from Qt's model, which is the right source
// for a title and a page and the wrong one for a place inside a page (§75). So the program now holds
// two readings of the same tree, and they are joined by *position in the walk* plus a check that the
// title and page agree -- a row that moved, was renamed or was made here is left without a
// destination rather than being given a neighbour's.
//
// Walked through the object chain -- /First (backed up along /Prev), then /Next, recursing into
// /First -- instead of with QPDFOutlineDocumentHelper. Two reasons, one measured and one not.
// Measured: with the helper, an index-join over a FLAT four-bookmark file reported two bookmarks at
// risk for a request that renamed one, which is what an order different from the sidebar's looks
// like from here. Not measured: what that order actually was -- printing the two lists side by side
// later showed them agreeing, so the honest sentence is "the helper's order is not documented to be
// the order a sidebar draws, and one run behaved as if it were not". Walking the chain removes the
// question, and it is the same chain this program's own writer builds, so what is read and what is
// written cannot disagree about the shape of the tree. The helper's constructors are private, so
// resolving one item's destination by hand through this chain is the only way to have both.
QVector<OutlineRow> outlineFromSource(QPDF &qpdf)
{
    QVector<OutlineRow> rows;
    const QPDFObjectHandle root = qpdf.getRoot().getKey("/Outlines");
    if (!root.isDictionary())
        return rows;
    const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();

    std::function<void(QPDFObjectHandle, int)> visitSiblings = [&](QPDFObjectHandle item, int level) {
        while (item.isDictionary()) {
            OutlineRow row;
            const QPDFObjectHandle title = item.getKey("/Title");
            if (title.isString())
                row.title = QString::fromUtf8(title.getUTF8Value().c_str());
            row.level = level;
            const QPDFObjectHandle dest = itemDestination(qpdf, item);
            row.dest = parseBookmarkDest(dest);
            // The page the destination names, by identity within this document. §78's rule that
            // object numbers are not identity is about comparing across two writes; both sides here
            // are the open file, so handle identity is the right question to ask.
            const QPDFObjectHandle wanted = dest.isArray() ? dest.getArrayItem(0)
                                                           : QPDFObjectHandle::newNull();
            if (wanted.isDictionary()) {
                for (size_t i = 0; i < pages.size(); ++i) {
                    if (pages[i].getObjectHandle().isSameObjectAs(wanted)) {
                        row.page = int(i);
                        break;
                    }
                }
            }
            rows.append(row);
            if (qEnvironmentVariableIsSet("XYZPDF_DBG_WALK"))
                qWarning("walk L%d title=%s page=%d kind=%d", level,
                         row.title.toUtf8().constData(), row.page, int(row.dest.kind));
            const QPDFObjectHandle kids = item.getKey("/First");
            if (kids.isDictionary())
                visitSiblings(kids, level + 1);
            item = item.getKey("/Next");
        }
    };
    QPDFObjectHandle first = root.getKey("/First");
    // /First should be the leftmost item; walking back along /Prev first is what §63's controls
    // taught this program to do when reading a tree somebody else wrote.
    while (first.isDictionary() && first.getKey("/Prev").isDictionary())
        first = first.getKey("/Prev");
    visitSiblings(first, 1);
    return rows;
}

// How many of the file's own bookmarks name a place inside a page rather than the page -- answered
// off the reading above, so the number the box says out loud (§80) and the destinations a save can
// carry cannot drift apart. /Fit and a page-only destination have nothing to lose; anything the
// parser did not recognise counts as something to lose, which is the honest direction to be wrong in:
// it warns rather than quietly flattening.
int countPositionalBookmarks(const QVector<OutlineRow> &rows)
{
    int total = 0;
    for (const OutlineRow &row : rows) {
        if (row.dest.kind == BookmarkDest::Kind::None) {
            // A bookmark whose destination never resolved has no position that could be taken away
            // from it -- that was §80's rule and it is kept here rather than in the parser.
            continue;
        }
        if (row.dest.isPositional() || !row.dest.numbers.isEmpty())
            ++total;
    }
    return total;
}

// Whether a page of a file a merge drew from is one this request names. Repeats
// count once and the result is ordered by page number, so that two requests for the
// same pages produce the same operation -- which is what lets a test compare one
// against the other, and what makes the block arrive in the order the file has it.
QVector<DocumentEditor::PageRef> refsFrom(const QSet<int> &pages, int source)
{
    QVector<DocumentEditor::PageRef> out;
    out.reserve(pages.size());
    for (int page : pages)
        out.append({source, page});
    std::sort(out.begin(), out.end(), [](const DocumentEditor::PageRef &a,
                                        const DocumentEditor::PageRef &b) {
        return a.page < b.page;
    });
    return out;
}

// The pages a request names, as the list an operation holds.
QVector<int> sortedFrom(const QSet<int> &pages)
{
    QVector<int> out;
    out.reserve(pages.size());
    for (int page : pages)
        out.append(page);
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------- 水印, as pixels

// The three numbers that decide how big the marks are, none of which is a question the reader
// is asked. They are here rather than spread through the code because together they are the
// answer to "what does a watermark of this program's look like", and the placement function
// beside them has to be told the same fit the painting was cut to.
//
// 200 pixels for the em, and the canvas is only as big as the turned string needs: the two
// planes cost 3 bytes and 1 byte per pixel of *every file this writes*, so slack in the canvas
// is slack paid for once per save.
constexpr int kWatermarkFontPixels = 200;
// Counter-clockwise, so it reads the way a stamp does: up to the right.
constexpr double kWatermarkAngle = 30.0;
// The marks stop short of the page's edge. A watermark that touches the trim reads as a
// printing fault rather than as a mark, and a scanned page's own edge is rarely the paper's.
constexpr double kWatermarkFit = 0.9;
// Room around the turned string's measuring box, in pixels of em. Glyphs overhang the box the
// metrics report (an italic f, a comma's tail), and the box is of the *unrotated* string turned
// -- which is larger than the turned string's own box, never smaller, so this is slack rather
// than a clip allowance. It is still there because clipping a watermark cuts letters off.
constexpr double kWatermarkPadding = 12.0;

// Cosine and sine of each quarter turn, indexed by /Rotate/90. A table rather than std::cos
// because the numbers go into a content stream: std::cos(M_PI / 2.0) is 6.12e-17, and a matrix
// written with dust in it is a watermark a millimetre off its page for no reason anybody could
// give the reader.
struct QuarterTurn
{
    double cosine;
    double sine;
};
const QuarterTurn kQuarterTurns[4] = { { 1.0, 0.0 }, { 0.0, 1.0 }, { -1.0, 0.0 }, { 0.0, -1.0 } };

// The watermark as Qt paints it: any script the system has a font for, at any angle, for free.
// That freedom is the whole reason this route exists -- the operator route needs a font per
// script and puts its string into the page's extractable text (tests/watermarkprobe.cpp
// measured both halves of that sentence).
//
// The result is transparent everywhere nothing was painted, which is what makes the route
// affordable: the colour plane comes out flat and the mask plane is zero across most of the
// canvas, and a flat plane of bytes compresses to almost nothing.
QImage paintWatermark(const QString &text)
{
    QFont font;
    font.setPixelSize(kWatermarkFontPixels);
    font.setBold(true);

    // The box the string needs before it turns, then that box turned. Measuring the box rather
    // than the turned glyphs is deliberate: the rotated ink is always inside a rotated metric
    // box, and the arithmetic is one line instead of a scan of every pixel for the first and
    // last one that carries colour.
    const QRectF laidOut = QFontMetricsF(font).boundingRect(text);
    QTransform turned;
    turned.rotate(-kWatermarkAngle);
    const QRectF needed = turned.mapRect(QRectF(QPointF(0.0, 0.0), laidOut.size()));

    const int canvasWidth = int(std::ceil(needed.width() + 2.0 * kWatermarkPadding));
    const int canvasHeight = int(std::ceil(needed.height() + 2.0 * kWatermarkPadding));

    QImage image(canvasWidth, canvasHeight, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(font);
    QPen pen(Qt::black);
    painter.setPen(pen);
    painter.translate(canvasWidth / 2.0, canvasHeight / 2.0);
    painter.rotate(-kWatermarkAngle);
    // centred in a rect the size of the canvas, so the canvas' centre is the marks' centre,
    // which is the point watermarkPlacement() turns the page around.
    painter.drawText(QRectF(-canvasWidth / 2.0, -canvasHeight / 2.0, double(canvasWidth),
                            double(canvasHeight)),
                     Qt::AlignCenter | Qt::TextDontClip,
                     text);
    painter.end();
    return image;
}

// The two planes an image XObject wants: colour and, separately, coverage.
struct Planes
{
    QByteArray rgb;
    QByteArray alpha;
};

Planes splitPlanes(const QImage &argb)
{
    Planes planes;
    const int width = argb.width();
    const int height = argb.height();
    planes.rgb.resize(width * height * 3);
    planes.alpha.resize(width * height);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const int offset = y * width + x;
            planes.rgb[3 * offset] = char(qRed(line[x]));
            planes.rgb[3 * offset + 1] = char(qGreen(line[x]));
            planes.rgb[3 * offset + 2] = char(qBlue(line[x]));
            // Where nothing was painted the colour is 0/0/0 and the alpha is 0, so both planes
            // are flat over most of the page and compress to almost nothing. That is the only
            // reason this route is affordable at all.
            planes.alpha[offset] = char(qAlpha(line[x]));
        }
    }
    return planes;
}

// One image, as a stream object ready to be referenced. Used for both planes, which differ only
// in colour space and size.
QPDFObjectHandle imageStream(QPDF &qpdf, const QByteArray &raw, int width, int height,
                             const char *colorSpace)
{
    QPDFObjectHandle stream = qpdf.newStream();
    // getDict(), not replaceKey on the stream itself: a stream object answers "operation for
    // dictionary attempted on object of type stream" and *ignores* each request -- measured in
    // tests/watermarkprobe.cpp, six warnings for the six keys below, and the image came out with
    // no /Subtype at all.
    QPDFObjectHandle dict = stream.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
    dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName(colorSpace));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    // The pixels as they were painted, and *no* filter: every comment on `replaceStreamData` is
    // that the buffer it is given is "the data read from the file, after any decryption filters
    // have been applied", i.e. already encoded by whatever name is passed as /Filter. Handing over
    // raw bytes and labelling them /FlateDecode produced exactly what that reads as on the way
    // back in (also measured): "stream inflate: inflate: data: unknown compression method".
    // Leaving the filter out instead makes the writer the one that decides, which is the same
    // decision every other save this class makes already makes.
    stream.replaceStreamData(std::string(raw.constData(), size_t(raw.size())),
                             QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    return qpdf.makeIndirectObject(stream);
}

// How solid the marks are, as one ExtGState shared by the whole document.
//
// `/ca` rather than paler pixels: PDF multiplies it into the image's own soft mask, so the
// number the dialog showed is the number that reaches the file, and moving the slider redraws
// nothing. Only /ca and not also /CA -- /CA is the *stroking* alpha, and drawing an image is a
// non-stroking operation.
QPDFObjectHandle watermarkState(QPDF &qpdf, int opacityPercent)
{
    QPDFObjectHandle state = QPDFObjectHandle::newDictionary();
    state.replaceKey("/Type", QPDFObjectHandle::newName("/ExtGState"));
    state.replaceKey("/ca", QPDFObjectHandle::newReal(opacityPercent / 100.0));
    return qpdf.makeIndirectObject(state);
}

// A page's /Resources, ready to write into.
//
// Inheritance-aware and shallow-copied when shared: /Resources may live on a parent node of the
// page tree, and adding a key there would put this page's watermark resources on every page that
// inherits it -- including the pages this request did not name.
QPDFObjectHandle resourcesForWrite(QPDFPageObjectHelper &page)
{
    QPDFObjectHandle dict = page.getObjectHandle();
    QPDFObjectHandle resources = page.getAttribute("/Resources", /* copy_if_shared = */ true);
    if (!resources.isDictionary()) {
        dict.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
        resources = dict.getKey("/Resources");
    }
    return resources;
}

// One sub-dictionary of a page's /Resources, ready to write into: /ExtGState, /XObject.
//
// Two reasons this is a function and not a replaceKey on /Resources:
//
//   * A resource name is looked up inside the *subtype* dictionary -- /Resources /XObject /Imwm1,
//     not /Resources /Imwm1. Putting the entry at the top level is not where a reader looks, and a
//     lenient reader that draws something anyway draws it with its own defaults: measured in
//     tests/watermarkprobe.cpp, the first version of that probe drew its watermark completely
//     black because the /ca state was never found, and the picture looked right while the claim
//     about alpha had never been tested.
//   * The sub-dictionary is usually an indirect object shared by every page that inherits the
//     same resources, so writing into it writes into the other pages' too. Copied when indirect,
//     which is the precondition for being shared; `unsafeShallowCopy` is the right one because
//     only top-level keys get replaced, never anything below them.
QPDFObjectHandle resourceDictForWrite(QPDF &qpdf, QPDFObjectHandle &resources, const char *key)
{
    QPDFObjectHandle sub = resources.getKey(key);
    if (!sub.isDictionary()) {
        resources.replaceKey(key, QPDFObjectHandle::newDictionary());
        return resources.getKey(key);
    }
    if (sub.isIndirect()) {
        sub = qpdf.makeIndirectObject(sub.unsafeShallowCopy());
        resources.replaceKey(key, sub);
    }
    return sub;
}

// One page's content streams, with a new one added at the end -- which is where ink that should
// cover the page's own text has to go.
//
// Not qpdf's addPageContents(): what its copy-if-shared covers is *inheritable* attributes, as
// its own header says outright, and /Contents is not one of those. So on a page whose /Contents
// is an indirect array that a second page also references, addPageContents() appends into the
// array both pages read, and puts this watermark on a page nobody named. Hence the copy below,
// by the same rule resourceDictForWrite() follows.
//
// The sentence it refuses with is handed in rather than said here, and the reason is the
// catalogue: this is a free function, so a `tr()` call here has no class around it for lupdate
// to read as a context, and the string is then not an unfinished entry but no entry at all --
// it would reach an English interface straight out of the source. The caller is a member, so
// the same call there does get a context. This is §22's hole about the third-party table's
// four cells, in the other direction: not a missing translation but a missing question.
void appendWatermarkContent(QPDF &qpdf, QPDFPageObjectHelper &page, const std::string &ops,
                            const QString &refusal)
{
    QPDFObjectHandle pageDict = page.getObjectHandle();
    const QPDFObjectHandle added = qpdf.newStream(ops);
    QPDFObjectHandle contents = pageDict.getKey("/Contents");

    if (contents.isNull()) {
        pageDict.replaceKey("/Contents", added);
        return;
    }
    if (contents.isStream()) {
        // One stream of its own: this page's key is what is being replaced, and nothing below the
        // stream is written to, so a second page that points at the same stream keeps drawing
        // exactly what it drew before.
        QPDFObjectHandle both = QPDFObjectHandle::newArray();
        both.appendItem(contents);
        both.appendItem(added);
        pageDict.replaceKey("/Contents", both);
        return;
    }
    if (contents.isArray()) {
        // An array is written *into*, so a shared one has to be copied first: two pages whose
        // /Contents is the same indirect array would otherwise both gain these marks, and only
        // one of them was named. Copied when indirect, which is the precondition for being shared.
        if (contents.isIndirect()) {
            contents = qpdf.makeIndirectObject(contents.unsafeShallowCopy());
            pageDict.replaceKey("/Contents", contents);
        }
        contents.appendItem(added);
        return;
    }
    // /Contents is neither. Damaged, in other words, and this refuses rather than writing a page
    // whose marks a reader was promised: see the two refusals in the merge replay below, which
    // are the same judgement about the same kind of quiet.
    throw std::runtime_error(refusal.toUtf8().constData());
}

} // namespace

// The words a reader asked for, because these show up in a menu and a menu that
// says "undo operation 2" is not a menu. A member rather than something in the
// namespace above, for the sake of the catalogue -- see the declaration.
QString DocumentEditor::describeOperation(const Operation &op) const
{
    const int count = op.pages.size();
    // Every row below that names a number of pages goes through tr(): the count is `%n` so
    // the English can agree with it ("Delete 1 page" / "Delete 3 pages"), while the Chinese --
    // the source language, and the same text in both forms -- renders exactly as it always did.
    if (op.kind == Operation::Kind::RotatePages) {
        switch (op.quarterTurns) {
        case 2:
            return tr("旋转 180°（%n 页）", nullptr, count);
        case 3:
            return tr("向左转 90°（%n 页）", nullptr, count);
        default:
            return tr("向右转 90°（%n 页）", nullptr, count);
        }
    }
    if (op.kind == Operation::Kind::MovePages) {
        // The anchor number in the middle of it is the page as it reads on the
        // screen, which is the only numbering a reader has.
        if (op.moveTo == MoveTarget::Front)
            return tr("移到最前（%n 页）", nullptr, count);
        if (op.moveTo == MoveTarget::AfterPage)
            return tr("移到第 %1 页之后（%n 页）", nullptr, count).arg(op.anchor + 1);
        return tr("移到最后（%n 页）", nullptr, count);
    }
    if (op.kind == Operation::Kind::MergeFile) {
        // Which file, not just how many: a reader who merged three documents and
        // sees "撤销 合入 5 页" has no way to know which 5 pages are about to come
        // back. The name rather than the path, because this is a menu row.
        const Import &import = imports.value(op.importIndex);
        return tr("合入 %n 页（%1）", nullptr, count)
            .arg(QFileInfo(import.path).fileName().isEmpty()
                     ? tr("另一份文件")
                     : QFileInfo(import.path).fileName());
    }
    if (op.kind == Operation::Kind::SplitBySize) {
        // The size the reader chose rather than the number of files it makes: the file
        // count is whatever is left after the deletes, and a menu row that says 拆成 8 份
        // stops being true the moment a page is taken out, while 每 5 页一份 does not.
        return tr("拆成每 %n 页一份", nullptr, op.pieceSize);
    }
    if (op.kind == Operation::Kind::Protect) {
        // Not a word of the password, not even masked. This row is drawn in a menu that can
        // be open with other people standing behind the reader, and the same string is
        // joined into the discard prompt and put on the status bar -- so anything here is
        // text that leaves the dialog that asked for it. What the row has to carry is which
        // way round the request was, because that is the thing being taken back.
        if (op.protection.what == Protection::What::Decrypt)
            return tr("去掉口令");
        if (op.protection.what == Protection::What::Encrypt) {
            return op.protection.password.isEmpty()
                       ? tr("只改权限（不设打开口令）")
                       : tr("加上打开口令（AES-256）");
        }
        return tr("按原来的口令写出去");
    }
    if (op.kind == Operation::Kind::Watermark) {
        // What the marks say, in the row that names the thing being taken back. The text is put
        // here because, unlike a password, it is something the reader typed *to be seen*: it goes
        // across every named page of the file. A row that said only 加水印（12 页） would also be
        // two rows apart in a list of three watermarks and indistinguishable.
        // The count is the watermark's own page list rather than the shared `count` above: a
        // Watermark operation records no structural pages, so `op.pages` is empty and this row
        // read 加水印「DRAFT」（0 页） under a dialog that had just said 盖在 40 页上 -- measured in
        // the real window. `addWatermark()` has already dropped the pages an earlier unsaved
        // delete took out, so this size is the same number the summary showed at the press.
        return tr("加水印「%1」（%n 页）", nullptr, op.watermark.pages.size()).arg(op.watermark.text);
    }
    if (op.kind == Operation::Kind::SetOutline) {
        // The size of the tree, not the shared `count`: like 水印 and 加密 this operation records
        // no structural pages, so `op.pages` is empty and the row would read 设置书签（0 页） next
        // to a dialog that had just said 12 条书签.
        //
        // Taking them all away is its own row rather than "设置书签（0 条）", because a count of
        // zero does not read as something a reader asked for -- and this row names the thing that
        // comes back when the row is undone, which for an empty tree is every bookmark.
        if (op.outline.isEmpty())
            return tr("删掉全部书签");
        return tr("设置书签（%n 条）", nullptr, op.outline.size());
    }
    // The row that first proved the mechanism (docs/store-submission.md sec.32), and still
    // the simplest one: one number, one noun for the English to agree with it about.
    return tr("删除 %n 页", nullptr, count);
}

DocumentEditor::DocumentEditor() = default;

DocumentEditor::~DocumentEditor() = default;

DocumentEditor::DeletePlan DocumentEditor::planDelete(const QVector<PageRef> &present,
                                                     const QVector<int> &pages)
{
    DeletePlan plan;
    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    // Every number in `named` is an original page number, so the two questions it
    // raises are separate ones: is it still there at all, and how many go if this
    // request is granted. Numbers past the end of the document never reach here --
    // the range parser the dialog uses clamps them -- so anything missing from
    // `present` is a page an earlier pending operation removed. Pages that came in
    // from another file cannot be named by these numbers at all, which is what keeps
    // them out of `removable` and into `pagesRemaining`.
    int removable = 0;
    for (int page : std::as_const(named)) {
        if (present.contains({0, page}))
            ++removable;
    }
    plan.pagesToRemove = removable;
    plan.alreadyGone = named.size() - removable;
    plan.pagesRemaining = present.size() - removable;
    // Named at least one page and nothing is left: the two ways to have no pages
    // after a delete are different, and "you asked for nothing" is not "you would
    // have destroyed the document".
    plan.leavesNothing = plan.pagesToRemove > 0 && plan.pagesRemaining <= 0;
    return plan;
}

DocumentEditor::DeletePlan DocumentEditor::planDelete(const QVector<int> &pages) const
{
    return planDelete(live, pages);
}

int DocumentEditor::pagesStillThere(const QVector<int> &pages) const
{
    QSet<int> named;
    for (int page : pages) {
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    return named.size();
}

bool DocumentEditor::pageStillExists(int page) const
{
    // The same address every other reader-typed page number uses (§70), asked of one row rather
    // than of a set: pagesStillThere() counts distinct pages, which is what a range caller wants
    // and what a bookmark list does not -- two bookmarks on one page are two bookmarks.
    return page >= 0 && live.contains({0, page});
}

int DocumentEditor::importedPageCount() const
{
    int imported = 0;
    for (const PageRef &ref : live) {
        if (ref.source != 0)
            ++imported;
    }
    return imported;
}

int DocumentEditor::pendingQuarterTurns(int page) const
{
    int turns = 0;
    for (const Operation &op : pending) {
        if (op.kind == Operation::Kind::RotatePages && op.pages.contains(page))
            turns += op.quarterTurns;
    }
    return ((turns % 4) + 4) % 4;
}

QString DocumentEditor::importPathFor(int source) const
{
    if (source <= 0)
        return {};
    for (const Operation &op : pending) {
        if (op.kind == Operation::Kind::MergeFile && op.mergeId == source && op.importIndex >= 0
            && op.importIndex < imports.size())
            return imports.at(op.importIndex).path;
    }
    return {};
}

bool DocumentEditor::hasFormFields() const
{
    if (sourceHasFormFields)
        return true;
    // A merge can bring a form into a document that had none, and the pages that came with it are
    // the ones 删除页面 / 拆分文件 can take out -- so the answer has to count the imports too.
    for (const Import &one : imports) {
        if (one.hasFormFields)
            return true;
    }
    return false;
}

int DocumentEditor::repliesAtRiskForDelete(const QVector<int> &pages) const
{
    // Only the links this request cuts. A thread whose target page an earlier unsaved delete
    // already took out is not counted again here (the box that asked for that delete said it, and
    // the save's repair handles it either way), and a reply on a page that goes with its target is
    // not counted either -- nothing survives that write to be standalone about.
    QSet<int> removing;
    for (int page : pages) {
        if (page >= 0 && live.contains({0, page}))
            removing.insert(page);
    }
    int atRisk = 0;
    for (const ReplyLink &link : sourceReplyLinks) {
        if (!removing.contains(link.targetPage))
            continue;
        if (removing.contains(link.replyPage))
            continue;
        if (!live.contains({0, link.replyPage}))
            continue;
        ++atRisk;
    }
    return atRisk;
}

int DocumentEditor::repliesAtRiskForSplit(int pagesPerPiece) const
{
    if (pagesPerPiece <= 0)
        return 0;
    // Which file each of the document's own pages would be written into. The position in `live`
    // rather than the original number, because that is what a piece is cut from -- and a page an
    // earlier delete took out has no piece to be in, so its threads are not this request's to cut.
    // A split that is reached has no merged pages in it (splitInto() and mergeFile() each refuse
    // the pairing), but the lookup answers -1 for those anyway rather than counting a page that is
    // in no piece at all.
    auto pieceOf = [this, pagesPerPiece](int page) {
        const int at = live.indexOf({0, page});
        return at < 0 ? -1 : at / pagesPerPiece;
    };
    int atRisk = 0;
    for (const ReplyLink &link : sourceReplyLinks) {
        const int reply = pieceOf(link.replyPage);
        const int target = pieceOf(link.targetPage);
        if (reply < 0 || target < 0)
            continue;
        if (reply != target)
            ++atRisk;
    }
    return atRisk;
}

int DocumentEditor::repliesAtRiskForImport(const QVector<ReplyLink> &links,
                                           const QVector<int> &pages)
{
    // Every reply on a page that comes over. The target's page is not consulted: qpdf copies the
    // annotation and leaves /IRT naming an object of the file it came out of, which is what §78's
    // merged-twice sample measured (two copies of a note, its popup and its reply, each coming out
    // with threads=0 -- including the reply that was on the very same page as its target).
    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    int atRisk = 0;
    for (const ReplyLink &link : links) {
        if (named.contains(link.replyPage))
            ++atRisk;
    }
    return atRisk;
}

bool DocumentEditor::changesPageCollection() const
{
    // Which of the eight kinds say what a save writes: 删除页面, 移动页面, 合并文件进来 and
    // 拆分文件. A split changes no page's contents and moves none -- it changes how many files the
    // pages are written into -- and that is enough: each piece holds part of the collection, so a
    // packet that described the whole of it describes neither piece.
    //
    // 移动页面 is here with the other three even though nothing is added or taken away, because
    // the packet names its template fields by page (P1, P2 … in order) and a reordered document
    // makes those names point somewhere else.
    //
    // The four that are not -- 旋转, 加密 / 去掉口令, 水印, 书签 -- leave the collection alone, and
    // their saves write the XFA exactly as the source had it.
    for (const Operation &op : pending) {
        switch (op.kind) {
        case Operation::Kind::DeletePages:
        case Operation::Kind::MovePages:
        case Operation::Kind::MergeFile:
        case Operation::Kind::SplitBySize:
            return true;
        case Operation::Kind::RotatePages:
        case Operation::Kind::Protect:
        case Operation::Kind::Watermark:
        case Operation::Kind::SetOutline:
            break;
        }
    }
    return false;
}

DocumentEditor::Landing DocumentEditor::landingSpot(const QVector<PageRef> &without, MoveTarget to,
                                                   int anchor)
{
    Landing spot;
    spot.at = without.size();
    if (to == MoveTarget::Front) {
        spot.at = 0;
        return spot;
    }
    if (to != MoveTarget::AfterPage)
        return spot;

    const int behind = without.indexOf({0, anchor});
    if (behind >= 0) {
        spot.at = behind + 1;
        spot.anchorUsed = anchor;
        return spot;
    }
    // The page named is gone. Where it was is still a place, and the pages that were
    // behind it stayed behind it, so the gap is in front of the first page whose
    // number is larger -- which is what the reader is standing behind now that the
    // one they named is not there. Pages that came in from another file are skipped in
    // that search: their numbers belong to their own document, and a landing spot
    // reasoned from one of them would be a spot the reader cannot find on the screen.
    for (int index = 0; index < without.size(); ++index) {
        const PageRef &ref = without.at(index);
        if (ref.source == 0 && ref.page > anchor) {
            spot.at = index;
            spot.anchorUsed = ref.page;
            break;
        }
    }
    spot.anchorWasGone = true;
    return spot;
}

DocumentEditor::MovePlan DocumentEditor::planMove(const QVector<PageRef> &present,
                                                 const QVector<int> &pages, MoveTarget to,
                                                 int anchor)
{
    MovePlan plan;
    plan.order = present;

    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    // The block in the order it has where it stands, not the order the numbers were
    // typed in: "第 2、4 页移到最后" moves those two pages as a pair keeping 2 before
    // 4, and a reader who wrote "4,2" did not ask for them swapped.
    QVector<PageRef> moved;
    QVector<PageRef> kept;
    kept.reserve(present.size());
    for (const PageRef &ref : present) {
        if (ref.source == 0 && named.contains(ref.page))
            moved.append(ref);
        else
            kept.append(ref);
    }
    plan.pagesMoved = moved.size();
    plan.alreadyGone = named.size() - moved.size();
    if (moved.isEmpty())
        return plan;
    if (to == MoveTarget::AfterPage && moved.contains({0, anchor})) {
        // The anchor is inside the block, so "behind it" is where the block already
        // is. Nothing here can be said about where it should go instead -- a reader
        // who means that types a different page.
        plan.anchorIsMoved = true;
        return plan;
    }

    const Landing spot = landingSpot(kept, to, anchor);

    QVector<PageRef> order;
    order.reserve(present.size());
    for (int index = 0; index < spot.at; ++index)
        order.append(kept.at(index));
    for (const PageRef &ref : moved)
        order.append(ref);
    for (int index = spot.at; index < kept.size(); ++index)
        order.append(kept.at(index));

    plan.order = order;
    plan.landsAt = spot.at;
    plan.anchorUsed = spot.anchorUsed;
    plan.anchorWasGone = spot.anchorWasGone;
    plan.changesOrder = order != present;
    return plan;
}

DocumentEditor::MovePlan DocumentEditor::planMove(const QVector<int> &pages, MoveTarget to,
                                                 int anchor) const
{
    return planMove(live, pages, to, anchor);
}

DocumentEditor::MergePlan DocumentEditor::planMerge(const QVector<PageRef> &present,
                                                    const QVector<int> &pages, int mergeId,
                                                    MoveTarget to, int anchor)
{
    MergePlan plan;
    plan.order = present;

    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    // The block as the file it comes from orders it, so that "取第 2、4 页" arrives as
    // 2 then 4 and the sentence below about where they land is true. `mergeId` rather
    // than the file index is what keeps two merges of one page from producing one page
    // twice over and an identity nobody can resolve.
    const QVector<PageRef> block = refsFrom(named, mergeId);
    plan.pagesAdded = block.size();
    if (block.isEmpty())
        return plan;

    // The same function a move uses, run over the whole list rather than over what is
    // left of it: nothing is taken out by a merge, so the landing spot is the one
    // place the two operations genuinely differ, and sharing it is what makes
    // "第 12 页之后" mean one thing in this program.
    const Landing spot = landingSpot(present, to, anchor);

    QVector<PageRef> order;
    order.reserve(present.size() + block.size());
    for (int index = 0; index < spot.at; ++index)
        order.append(present.at(index));
    for (const PageRef &ref : block)
        order.append(ref);
    for (int index = spot.at; index < present.size(); ++index)
        order.append(present.at(index));

    plan.order = order;
    plan.landsAt = spot.at;
    plan.anchorUsed = spot.anchorUsed;
    plan.anchorWasGone = spot.anchorWasGone;
    return plan;
}

QVector<DocumentEditor::PieceRange> DocumentEditor::pieceRanges(int total, int pieceSize)
{
    QVector<PieceRange> pieces;
    if (total <= 0)
        return pieces;
    // A piece of 0 is "nothing was split", and that comes out as one piece holding the
    // whole document rather than an empty list: the save then has one loop for every case,
    // and the case where nothing was split is not a special case that can be forgotten.
    // Anything from 1 up to total-1 is a real split; `splitInto()` refuses the rest.
    const int size = (pieceSize >= 1 && pieceSize < total) ? pieceSize : total;
    for (int first = 0; first < total; first += size) {
        PieceRange piece;
        piece.first = first;
        piece.last = qMin(total, first + size) - 1;
        pieces.append(piece);
    }
    return pieces;
}

namespace {

// How many items hang below this one, not counting it. /Count is exactly that number, and it is
// computed rather than carried because of what tests/outlineprobe.cpp measured: a /Count that
// disagrees with the chain is invisible to all three readers, so nothing would ever tell us we
// wrote a wrong one -- the only defence is not to write one.
int outlineDescendants(const OutlineNode &node)
{
    int below = 0;
    for (const OutlineNode &child : node.children)
        below += 1 + outlineDescendants(child);
    return below;
}

// The PDF name a destination kind is written back as, or nullptr when there is nothing to keep and
// the caller falls back to /Fit. Kept beside parseBookmarkDest's table rather than open-coded in the
// caller: a kind read in one place and unrecognised in the other would flatten silently, which is
// exactly the failure this path replaces.
const char *outlineDestKind(const BookmarkDest &dest)
{
    switch (dest.kind) {
    case BookmarkDest::Kind::XYZ:
        return "/XYZ";
    case BookmarkDest::Kind::Fit:
        return "/Fit";
    case BookmarkDest::Kind::FitH:
        return "/FitH";
    case BookmarkDest::Kind::FitV:
        return "/FitV";
    case BookmarkDest::Kind::FitR:
        return "/FitR";
    case BookmarkDest::Kind::FitB:
        return "/FitB";
    case BookmarkDest::Kind::FitBH:
        return "/FitBH";
    case BookmarkDest::Kind::FitBV:
        return "/FitBV";
    case BookmarkDest::Kind::None:
        return nullptr;
    }
    return nullptr;
}

// One item, then its children, then the links that make the row readable as a row. A null return
// means `*error` already holds the sentence: the only way this fails is a page number that is not
// in the document, which the caller has refused before getting here.
QPDFObjectHandle writeOutlineItem(QPDF &qpdf, const OutlineNode &node, QPDFObjectHandle parent,
                                  const std::vector<QPDFPageObjectHelper> &pages, QString *error)
{
    QPDFObjectHandle item = QPDFObjectHandle::newDictionary();
    item.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(node.title.toUtf8().constData()));
    if (!parent.isNull())
        item.replaceKey("/Parent", parent);
    if (node.page >= 0) {
        if (node.page >= int(pages.size())) {
            *error = QStringLiteral("bookmark page %1 is not in the document").arg(node.page);
            return QPDFObjectHandle::newNull();
        }
        // Where to look. A row that came from a file whose own /Dest said something keeps that
        // saying (§75's route (a), taken in §99): the kind and its numbers go back as they were
        // read, nulls included, because `/XYZ 0 700 null` instructs "keep the zoom" while a 0 in
        // that slot instructs something else. A row this program made -- or one the reader renamed,
        // re-nested or moved, so no honest counterpart in the file could be found -- says "the top
        // of this page", which is what this writer did for every bookmark before §99 and what the
        // sidebar's own click does with it.
        QPDFObjectHandle dest = QPDFObjectHandle::newArray();
        dest.appendItem(pages.at(size_t(node.page)).getObjectHandle());
        if (const char *kind = outlineDestKind(node.dest); kind != nullptr) {
            dest.appendItem(QPDFObjectHandle::newName(kind));
            for (const std::optional<double> &one : node.dest.numbers) {
                if (!one.has_value())
                    dest.appendItem(QPDFObjectHandle::newNull());
                else if (*one == double(int(*one)))
                    dest.appendItem(QPDFObjectHandle::newInteger(int(*one)));
                else
                    dest.appendItem(QPDFObjectHandle::newReal(*one));
            }
        } else {
            dest.appendItem(QPDFObjectHandle::newName("/Fit"));
        }
        item.replaceKey("/Dest", dest);
    }
    QPDFObjectHandle handle = qpdf.makeIndirectObject(item);

    std::vector<QPDFObjectHandle> children;
    for (const OutlineNode &child : node.children) {
        QPDFObjectHandle written = writeOutlineItem(qpdf, child, handle, pages, error);
        if (written.isNull())
            return written;
        children.push_back(written);
    }
    if (!children.empty()) {
        // The chain both ways, and the parent's two ends. These four keys are the load-bearing
        // ones: §63's controls showed that a missing /Next on the first item loses every item
        // after it in all three readers, and a parent with no /First /Last loses its whole subtree.
        for (size_t i = 0; i < children.size(); ++i) {
            if (i > 0)
                children[i].replaceKey("/Prev", children[i - 1]);
            if (i + 1 < children.size())
                children[i].replaceKey("/Next", children[i + 1]);
        }
        handle.replaceKey("/First", children.front());
        handle.replaceKey("/Last", children.back());
        handle.replaceKey("/Count", QPDFObjectHandle::newInteger(outlineDescendants(node)));
    }
    return handle;
}

// What one pass over the form tree did, so the caller can say it. Both counts are zero for the
// common case of a document with no form at all.
struct FormTidy
{
    int boxesDropped = 0;
    int valuesCleared = 0;
};

// Make this document's form describe *this* document: take out the boxes no page here carries, and
// with them the answers that belonged to pages which are not in this file.
//
// Why this is one step at save time rather than something the page removal does: a split copies
// pages, and qpdf copies the field's whole /Kids list along with them, so each piece ends up naming
// a widget whose page only the other piece has. §51 measured the answer travelling with it; §52
// measured the shape that rules the obvious repair out -- a field whose two boxes sit on two pages
// which the split pulls apart, so the field must stay in *both* pieces. What may go is the orphan
// box, never the name. docs/store-submission.md §51, §52, §76.
//
// And a value goes only where it is provably about a page this file does not have. §77 built the
// case that stops the weaker rule: PDF 2.0 §12.7.5.2 allows a field to be registered with a value
// and no widget at all (a default, or something a script reads), and after a page is deleted both
// that field and a boxed field whose page went away have "no box on any page here". Measured on
// tests/documenteditor_smoke.cpp's boxed-and-value-only sample, the two are told apart by /Rect --
// present means it was drawn somewhere and lost its page here; absent means it was never on a page
// at all, and its value is not ours to delete. So the rule is: **drawn somewhere, drawn nowhere
// here -> the /V goes**. /DV is never touched; that is the form author's default, not an answer
// anybody typed.
FormTidy tidyFormForThisDocument(QPDF &qpdf)
{
    FormTidy done;
    QPDFAcroFormDocumentHelper acro(qpdf);
    if (!acro.hasAcroForm())
        return done;

    // Which boxes a page here actually carries. Asked of the pages, not of each widget's /P,
    // because /P is the very thing that can lie or dangle -- §51's reading was built on it, and a
    // widget with no /P at all is legal.
    std::set<QPDFObjGen> liveBoxes;
    const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
    for (const QPDFPageObjectHelper &page : pages) {
        for (const QPDFAnnotationObjectHelper &box : acro.getWidgetAnnotationsForPage(page))
            liveBoxes.insert(box.getObjectHandle().getObjGen());
    }

    // How many objects hang below this array of /Kids, which is what a field's /Count says.
    // The handles are not const anywhere aitems() is called: qpdf's array accessor is non-const,
    // which has stopped every helper in this family the moment it was written (the sixth time).
    auto descendants = [](QPDFObjectHandle kids) {
        int total = 0;
        std::function<void(QPDFObjectHandle)> walk = [&](QPDFObjectHandle node) {
            QPDFObjectHandle children = node.getKey("/Kids");
            if (!children.isArray())
                return;
            for (QPDFObjectHandle child : children.aitems()) {
                ++total;
                walk(child);
            }
        };
        for (QPDFObjectHandle child : kids.aitems())
            walk(child);
        return total;
    };

    // Returns {drawn somewhere, drawn in this file} for one field, after its own box list has been
    // pruned. Both are needed at the parent: a spread field is drawn where *any* of its kids is,
    // and it is drawn here where any kid survives on a page here.
    std::function<std::pair<bool, bool>(QPDFObjectHandle)> tidy = [&](QPDFObjectHandle node) {
        bool drawnSomewhere = node.getKey("/Rect").isArray();
        bool drawnHere = liveBoxes.count(node.getObjGen()) > 0;

        QPDFObjectHandle kids = node.getKey("/Kids");
        if (kids.isArray()) {
            std::vector<QPDFObjectHandle> kept;
            for (QPDFObjectHandle kid : kids.aitems()) {
                const QPDFObjectHandle subtype = kid.getKey("/Subtype");
                const bool boxOnly = kid.getKey("/T").isNull() && subtype.isName()
                    && subtype.getName() == "/Widget";
                if (boxOnly) {
                    drawnSomewhere = true;
                    if (liveBoxes.count(kid.getObjGen()) == 0) {
                        ++done.boxesDropped;
                        continue;
                    }
                    drawnHere = true;
                    kept.push_back(kid);
                    continue;
                }
                const std::pair<bool, bool> below = tidy(kid);
                drawnSomewhere = drawnSomewhere || below.first;
                drawnHere = drawnHere || below.second;
                kept.push_back(kid);
            }
            if (kept.size() != size_t(kids.getArrayNItems())) {
                QPDFObjectHandle replacement = QPDFObjectHandle::newArray();
                for (QPDFObjectHandle &one : kept)
                    replacement.appendItem(one);
                node.replaceKey("/Kids", replacement);
                // Only rewritten when it was there: /Count is optional, and inventing one where the
                // source had none would be this program adding structure nobody wrote.
                if (node.getKey("/Count").isInteger()) {
                    node.replaceKey("/Count",
                                    QPDFObjectHandle::newInteger(descendants(replacement)));
                }
            }
        }

        if (drawnSomewhere && !drawnHere && !node.getKey("/V").isNull()) {
            node.removeKey("/V");
            ++done.valuesCleared;
        }
        return std::make_pair(drawnSomewhere, drawnHere);
    };

    QPDFObjectHandle fields = qpdf.getRoot().getKey("/AcroForm").getKey("/Fields");
    if (fields.isArray()) {
        for (QPDFObjectHandle top : fields.aitems())
            tidy(top);
    }
    if (done.boxesDropped > 0 || done.valuesCleared > 0)
        acro.invalidateCache();
    return done;
}

// What one pass over the references *between* annotations did, so the caller can say it.
struct LinkTidy
{
    int popupsMadePrivate = 0;
    int threadsDropped = 0;
};

// Put the annotation-to-annotation references right for *this* document, the same way the pass
// above puts the form right for it.
//
// Two kinds of reference exist, and neither is in the page's /Annots array, which is why a page
// copy can leave them pointing somewhere else. A popup annotation is named only by its owner's
// /Popup and answers with /Parent; PDF 2.0 §12.5.2.17 gives it exactly one owner. A reply names
// the annotation it answers with /IRT. §47 measured what a merge does to them: the copies each got
// their own note, but both notes still named one shared popup whose /Parent was the donor's note,
// and both replies threaded to that same donor's note -- an object no page of this document shows.
//
// The popup half is not a judgement call: the /Parent that comes back is wrong for at least one
// owner either way, so each owner gets a popup of its own, and the copy answers to the annot that
// carries it. The keys are copied by hand rather than the whole object because /Parent is the one
// key that must not come along; everything else, including any appearance the popup carries, is
// shared read-only, which is the same trade §76 made about a widget's appearance stream.
//
// The reply half only reaches as far as the file can decide. A thread that lands on an annotation
// some page here shows is left exactly as it is -- replies across pages are legal inside one
// document, and §70's lesson is that "not where I expected it" is not the same as "wrong". What is
// taken off is a /IRT naming something no page of this file shows: no reader can resolve it, the
// target may not even be in the file any more (that is what 删除页面 leaves), and what goes is the
// link, never the words -- the reply keeps its own /Contents, so this is a dead reference being
// removed rather than content being tidied away.
//
// Deliberately not done: re-pointing a stray thread at "the one note on this page that looks like
// what the reply meant". It reads as provable when the page holds exactly one such note, and the
// merged-twice sample above is full of those, but the candidate is picked by /Contents matching,
// which two notes in one document can share; a wrong guess attaches a reply to a annotation it was
// never about, which is worse than the dead link this branch leaves alone.
LinkTidy tidyAnnotationLinksForThisDocument(QPDF &qpdf)
{
    LinkTidy done;
    const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();

    // Which annotations this document can show, and on which page. From the pages, not from each
    // annot's /P -- the same reason §76 does not read /P: it is the thing that can dangle, and a
    // popup is legal without one.
    std::map<QPDFObjGen, size_t> shownOn;
    for (size_t index = 0; index < pages.size(); ++index) {
        // Not const anywhere aitems() is called: qpdf's array accessor is non-const, which has
        // stopped every helper in this family the moment it was written (the eighth time).
        QPDFObjectHandle annots = pages.at(index).getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            continue;
        for (QPDFObjectHandle item : annots.aitems())
            shownOn.insert(std::make_pair(item.getObjGen(), index));
    }

    for (size_t index = 0; index < pages.size(); ++index) {
        QPDFObjectHandle annots = pages.at(index).getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            continue;
        for (QPDFObjectHandle owner : annots.aitems()) {
            QPDFObjectHandle popup = owner.getKey("/Popup");
            if (popup.isDictionary()
                && popup.getKey("/Parent").getObjGen() != owner.getObjGen()) {
                QPDFObjectHandle fresh = QPDFObjectHandle::newDictionary();
                for (const std::string &key : popup.getKeys()) {
                    if (key != "/Parent")
                        fresh.replaceKey(key, popup.getKey(key));
                }
                fresh.replaceKey("/Parent", owner);
                owner.replaceKey("/Popup", qpdf.makeIndirectObject(fresh));
                ++done.popupsMadePrivate;
            }

            if (!owner.hasKey("/IRT"))
                continue;
            const QPDFObjectHandle target = owner.getKey("/IRT");
            if (shownOn.count(target.getObjGen()) > 0)
                continue; // shown by some page here: legal, same page or not
            owner.removeKey("/IRT");
            owner.removeKey("/RT");
            ++done.threadsDropped;
        }
    }
    return done;
}

// The same map the pass above builds, read as page numbers instead of object identities -- which
// is all of it that survives the trip, since this class keeps no QPDF open after open() and a
// QPDFObjGen means nothing in a document read again later (qpdf renumbers on every write, §78).
//
// Deliberately the *same criterion* as tidyAnnotationLinksForThisDocument() uses, down to asking
// which pages show an annotation rather than reading each annotation's /P: this is what a dialog
// promises before the press, and that pass is what the save does. If the two disagreed about
// which threads count as resolvable, the sentence would be about a cost the write path never
// charges. A reply naming something no page shows is left out for the reason §78 gives -- that
// thread is already dead, and nothing this request does can kill it again.
QVector<DocumentEditor::ReplyLink> replyLinksOf(QPDF &qpdf)
{
    QVector<DocumentEditor::ReplyLink> links;
    const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
    std::map<QPDFObjGen, size_t> shownOn;
    for (size_t index = 0; index < pages.size(); ++index) {
        QPDFObjectHandle annots = pages.at(index).getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            continue;
        for (QPDFObjectHandle item : annots.aitems())
            shownOn.insert(std::make_pair(item.getObjGen(), index));
    }
    for (size_t index = 0; index < pages.size(); ++index) {
        QPDFObjectHandle annots = pages.at(index).getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            continue;
        for (QPDFObjectHandle owner : annots.aitems()) {
            if (!owner.hasKey("/IRT"))
                continue;
            const auto target = shownOn.find(owner.getKey("/IRT").getObjGen());
            if (target == shownOn.end())
                continue;
            DocumentEditor::ReplyLink link;
            link.replyPage = int(index);
            link.targetPage = int(target->second);
            links.append(link);
        }
    }
    return links;
}

// Take the XFA packet out of the document about to be written.
//
// Why this is the write path's job rather than something done to the file when it is opened: the
// packet describes the whole form of a document -- which pages exist, which template fields sit on
// them, and every answer typed into it -- and nothing in qpdf reads it. §55 measured all four
// directions: a 删除页面 carried the packet through byte for byte and left it describing a page the
// output no longer had, a 拆分 gave both halves the entire form of the original, and a field merged
// in grew /AcroForm /Fields while the XML stayed exactly the size it was. A reader with an
// XFA-capable viewer then gets a form that is not this document's; a reader without one gets the
// AcroForm fields the packet was standing in front of. With the packet gone both of them see the
// same thing, which is the fallback PDF 2.0 settled on when it removed XFA altogether.
//
// Only the packet goes. /NeedsRendering stays where the source wrote it, because it is a statement
// about generating field appearances rather than about XFA, and this program cannot know whether
// the fields that survive still need it.
void dropXfaForThisDocument(QPDF &qpdf)
{
    QPDFObjectHandle form = qpdf.getRoot().getKey("/AcroForm");
    if (!form.isDictionary() || !form.hasKey("/XFA"))
        return; // no form, or a form that never had a packet: nothing to say and nothing to write
    form.removeKey("/XFA");
}

} // namespace

bool DocumentEditor::writeOutline(QPDF &qpdf, const QVector<OutlineRow> &rows, QString *error)
{
    const Outline tree = outlineFromRows(rows);

    // An empty list is "no bookmarks", and the honest file for that has no /Outlines at all --
    // an empty root would be a thing the readers have to guess the meaning of.
    if (tree.nodes.isEmpty()) {
        qpdf.getRoot().removeKey("/Outlines");
        return true;
    }

    const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();

    // The rows are assembled by the same function that builds the sidebar's tree, so what this
    // writes and what the reader shows cannot disagree about where a bookmark sits -- and a row
    // naming a page the document does not have is refused here rather than written and dropped.
    for (const OutlineRow &row : rows) {
        if (row.page >= int(pages.size())) {
            *error = tr("%1 指向第 %2 页，而这份文档只有 %3 页。")
                         .arg(row.title.isEmpty() ? tr("一条书签") : row.title)
                         .arg(row.page + 1)
                         .arg(int(pages.size()));
            return false;
        }
    }

    QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
    root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
    root = qpdf.makeIndirectObject(root);

    std::vector<QPDFObjectHandle> tops;
    for (const OutlineNode &node : tree.nodes) {
        QPDFObjectHandle written = writeOutlineItem(qpdf, node, root, pages, error);
        if (written.isNull())
            return false;
        tops.push_back(written);
    }
    for (size_t i = 0; i < tops.size(); ++i) {
        if (i > 0)
            tops[i].replaceKey("/Prev", tops[i - 1]);
        if (i + 1 < tops.size())
            tops[i].replaceKey("/Next", tops[i + 1]);
    }
    root.replaceKey("/First", tops.front());
    root.replaceKey("/Last", tops.back());
    int total = 0;
    for (const OutlineNode &node : tree.nodes)
        total += 1 + outlineDescendants(node);
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(total));

    // Replacing the key is the whole of "remove the old outline": the previous tree becomes
    // unreachable from the catalog, and qpdf does not write what nothing points at.
    qpdf.getRoot().replaceKey("/Outlines", root);
    return true;
}

QString DocumentEditor::pageSpan(int first, int last)
{
    // One page is a number, not a range. See the declaration for why this has one place to
    // be said from; the dash below is the whole bug when a caller writes it unconditionally.
    return first == last ? QString::number(first + 1)
                         : QStringLiteral("%1-%2").arg(first + 1).arg(last + 1);
}

QString DocumentEditor::piecePath(const QString &destination, int index, int total,
                                  const PieceRange &range)
{
    const QFileInfo wanted(destination);
    // Wide enough for the last piece, and never narrower than two: the folder sorts by
    // name, and pieces that read 1, 10, 11, 2 in Explorer are a page order nobody can
    // check by eye. The page numbers beside it are 1-based because this name is the one a
    // reader sees in a folder and types into a mail message; the ones inside `range` are
    // the 0-based numbers this class uses everywhere else.
    const int width = qMax(2, QString::number(total).size());
    const QString number = QStringLiteral("%1").arg(index + 1).rightJustified(width, QLatin1Char('0'));
    const QString pages = pageSpan(range.first, range.last);
    const QString stem = QStringLiteral("%1 %2 (%3)")
                             .arg(wanted.completeBaseName(), number, pages);
    const QString suffix = wanted.suffix();
    return QDir(wanted.absolutePath())
        .filePath(suffix.isEmpty() ? stem : stem + QLatin1Char('.') + suffix);
}

QString DocumentEditor::stagedPath(const QDir &directory, qint64 stamp, int piece)
{
    // The piece number is in the name, and that is not decoration: every piece is staged
    // before any of them is renamed, so a name that two pieces could share would have the
    // second writer truncate the first one's finished file.
    //
    // The counter has no ceiling, because the only way out of this loop is a name with no
    // file behind it. Stopping at 99 exits by returning the last candidate *without having
    // tested it*, which turns the promise above into "unless it took 98 steps" -- and the
    // whole point of a staged name is that it never needs that caveat. Whether the cap was
    // reachable through a real save is a separate question, and one this function cannot
    // answer: it is handed a millisecond clock by its caller, and the same folder is where a
    // refused save deliberately leaves its staged file for the message to name. Walking past
    // 99 costs a filename nobody reads. Truncating the reader's document costs the document.
    QString staged
        = directory.filePath(QStringLiteral("xyzpdf-save-%1-%2.pdf").arg(stamp).arg(piece));
    for (int attempt = 2; QFileInfo::exists(staged); ++attempt) {
        staged = directory.filePath(
            QStringLiteral("xyzpdf-save-%1-%2-%3.pdf").arg(stamp).arg(piece).arg(attempt));
    }
    return staged;
}

int DocumentEditor::splitPieceSize() const
{
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (it->kind == Operation::Kind::SplitBySize)
            return it->pieceSize;
    }
    return 0;
}

QVector<DocumentEditor::PieceRange> DocumentEditor::pieces() const
{
    return pieceRanges(pageCount(), splitPieceSize());
}

DocumentEditor::Protection DocumentEditor::protectionToWrite() const
{
    // The last one asked for, which is the same rule splitPieceSize() follows: a second
    // 加密 is the reader changing their mind about the first, not a second thing for the
    // save to carry out. Whatever is in between them -- a delete, a merge -- stays put,
    // because a Protect operation changes no page.
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (it->kind == Operation::Kind::Protect)
            return it->protection;
    }
    // Nothing pending, and the default-constructed answer is the one every save has done
    // since §11: hand the source's own /Encrypt to the writer.
    return Protection();
}

QString DocumentEditor::passwordOfWrittenFile() const
{
    // Not simply protectionToWrite().password: a Carry save hands the source's own
    // /Encrypt to the writer, so the copy that comes out is locked with the key this
    // session used to open the original. Whoever opens the copy has to be given that key,
    // which is why the tab that opens a saved result asks the editor rather than guessing
    // -- a wrong guess is a password prompt for a file that needs none, or a "cannot open"
    // for one that does.
    const Protection protection = protectionToWrite();
    if (protection.what == Protection::What::Encrypt)
        return protection.password;
    if (protection.what == Protection::What::Decrypt)
        return QString();
    return sourcePassword;
}

DocumentEditor::Placement DocumentEditor::watermarkPlacement(const QRectF &box,
                                                             double canvasWidth,
                                                             double canvasHeight,
                                                             int rotateDegrees)
{
    // A page with no area, or a canvas with none, cannot be scaled to: the answer here is a
    // matrix that draws nothing rather than a division that produces infinity and puts
    // "1.#INF" into a content stream. Both are reachable from a damaged file, and a page that
    // cannot be drawn on is not a reason to write a file that cannot be parsed.
    if (box.width() <= 0.0 || box.height() <= 0.0 || canvasWidth <= 0.0 || canvasHeight <= 0.0)
        return Placement();

    // Which quarter turn, and turned the normal way round: /Rotate is in degrees clockwise and
    // may be written as -90 or 270 or 450, so the only thing it means to this function is how
    // many times the paper has been turned.
    const int quarter = ((rotateDegrees / 90) % 4 + 4) % 4;
    const QuarterTurn &turn = kQuarterTurns[quarter];

    // The page as a reader sees it. /Rotate turns the paper, so a 90 or a 270 swaps which edge
    // is the wide one, and fitting the marks to the unrotated box would put them across the page
    // the wrong way up and too long for it.
    const bool swapped = (quarter % 2) == 1;
    const double displayWidth = swapped ? box.height() : box.width();
    const double displayHeight = swapped ? box.width() : box.height();

    // One scale for both axes, so the canvas keeps the shape it was painted with. Which of the
    // two edges is the binding one depends on the string: a long one is width-bound and a
    // two-word one may be height-bound, and neither can be assumed about a document.
    const double scale
        = qMin(kWatermarkFit * displayWidth / canvasWidth,
               kWatermarkFit * displayHeight / canvasHeight);
    const double along = scale * canvasWidth;   // the bitmap's own right edge, on the paper
    const double across = scale * canvasHeight; // and its top edge

    // Turning the marks counter-clockwise by the page's /Rotate is what makes them read upright
    // once the viewer has turned the paper clockwise by the same amount: a vector drawn along
    // (cos, sin) in the page's own space lands along the displayed horizontal. Which also means
    // a pending 旋转页面 is folded in rather than fought -- see the declaration for why the ink
    // follows the paper's text instead of the screen.
    Placement m;
    m.a = turn.cosine * along;
    m.b = turn.sine * along;
    m.c = -turn.sine * across;
    m.d = turn.cosine * across;
    // The four corners of the unit square land at (e,f), (a+e,b+f), (c+e,d+f) and
    // (a+c+e, b+d+f); their mean is the centre of what gets drawn, so that is the one set to the
    // centre of the page.
    const double cx = box.x() + box.width() / 2.0;
    const double cy = box.y() + box.height() / 2.0;
    m.e = cx - (m.a + m.c) / 2.0;
    m.f = cy - (m.b + m.d) / 2.0;
    return m;
}

bool DocumentEditor::sameFile(const QString &path, const QString &other)
{
    if (path.isEmpty() || other.isEmpty())
        return false;
    if (comparablePath(path) == comparablePath(other))
        return true;

    // The string compare above catches what a save dialog hands back. This catches
    // the rest -- an 8.3 short name typed over the shoulder of a long one, a
    // junction pointed at the same folder -- by asking the file system what each
    // path really is. canonicalFilePath() returns an empty string for a path that
    // does not exist, which is the normal case for a destination being created, so
    // this only says "same" and can never say "different".
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString otherCanonical = QFileInfo(other).canonicalFilePath();
    return !canonical.isEmpty() && !otherCanonical.isEmpty()
        && comparablePath(canonical) == comparablePath(otherCanonical);
}

bool DocumentEditor::open(const QString &path, const QString &password, QString *error)
{
    // Before anything else: a failed open must not leave the previous document's
    // edits behind, or an error message and a dirty tab arrive together. The imports
    // go with them because pending operations name them by index, and an index left
    // over from another document's list would point at an unrelated file.
    pending.clear();
    undone.clear();
    live.clear();
    imports.clear();
    // And the merge identities go with them for the same reason: a number left over
    // from another document's list would be a page two documents both claim.
    nextMergeId = 1;
    originalPages = 0;
    sourcePath.clear();
    sourcePassword.clear();
    sourceEncrypted = false;
    sourceProtect = Protection();
    sourceHasFormFields = false;
    sourceHasXfa = false;
    sourceDynamicXfa = false;
    sourceReplyLinks.clear();
    sourcePositionalBookmarkCount = 0;

    // Parsed, counted, and let go. Holding on to the QPDF is what the alternative
    // design needed and what this class decided not to do; see the header.
    std::unique_ptr<QPDF> document = std::make_unique<QPDF>();
    try {
        document->processFile(path.toUtf8().constData(),
                              password.isEmpty() ? nullptr : password.toUtf8().constData());
        originalPages = int(QPDFPageDocumentHelper(*document).getAllPages().size());
    } catch (const std::exception &e) {
        document.reset();
        // Worded for the one place this is reached from: the reader is looking at
        // this document right now, so "the file is unreadable" would be a sentence
        // the reader knows is false. What is actually going on is that the
        // structural editor's parser is stricter than the renderer's, and the
        // detail below is the only evidence of which of the two it is.
        *error = tr("这份文档正在阅读中，但结构编辑用的解析器打不开它。\n\n解析器说：%1")
                     .arg(QString::fromUtf8(e.what()));
        return false;
    }
    if (originalPages == 0) {
        // Not a complaint about the parser: a document with no pages is one this
        // class can do nothing to, and saying so here beats failing at the save.
        *error = tr("这份 PDF 一页也没有，没法在上面做任何页面修改。");
        return false;
    }

    live = openedDocumentInOrder(originalPages);
    sourcePath = path;
    sourcePassword = password;

    // What the file itself says about protection, read beside the page count while the
    // parser still holds the document. Nothing re-derives it later: by the time a dialog
    // opens, the reader may already have asked for a change, and "what it was" has to stay
    // answerable for the sentence that says what 去掉口令 is taking off.
    //
    // The bits only, never a password -- this class does not know the source's user
    // password (the string it was opened with is kept for one job: opening the file again at
    // the save), and QPDF answers "allowed" to every flag of an unencrypted document, so an
    // unencrypted source leaves these at their defaults rather than carrying a set of bits
    // the file does not actually write. `what` stays Carry because that field means what a
    // *save* should do; here it describes a file that is already on disk.
    sourceEncrypted = document->isEncrypted();
    if (sourceEncrypted) {
        sourceProtect.allowPrint = document->allowPrintHighRes();
        sourceProtect.allowExtract = document->allowExtractAll();
        sourceProtect.allowAssembly = document->allowModifyAssembly();
        sourceProtect.allowAnnotate = document->allowModifyAnnotation();
        sourceProtect.allowFormFilling = document->allowModifyForm();
        sourceProtect.allowModifyOther = document->allowModifyOther();
    }
    // Read once here, for the same reason `sourceEncrypted` is: the two boxes that can take a page
    // out ask this on every keystroke to decide whether to say anything about form answers, and
    // there is no QPDF kept open in this class to ask a second time.
    //
    // The type check is not decoration. Asking /Fields of the answer to /AcroForm when the file has
    // no form at all makes qpdf print a WARNING line per call, and a document without a form is the
    // ordinary case -- §58's "a second key asked of a null also reports a WARNING", met again in
    // the code §77 added, where it fired on every open and every import check.
    {
        const QPDFObjectHandle form = document->getRoot().getKey("/AcroForm");
        const QPDFObjectHandle fields = form.isDictionary() ? form.getKey("/Fields")
                                                            : QPDFObjectHandle::newNull();
        sourceHasFormFields = fields.isArray() && fields.getArrayNItems() > 0;

        // The same two keys, one dictionary further along: XFA is the form's second copy, kept as
        // a stream or as an array of (subtype, stream) pairs, and the flag beside it says whether
        // a reader has to build the pages themselves. Nothing here reads the packet -- qpdf has no
        // XFA facility, which is what §55 measured four directions of -- so what these two answers
        // are for is saying what this program is not showing, and dropping a packet that would
        // otherwise describe pages a save is taking out.
        //
        // /NeedsRendering is read even when there is no packet: a document that asks for the flag
        // and carries no XFA is not a dynamic one, and calling it one would put the stronger of
        // the two sentences on a file that only has an ordinary form.
        const QPDFObjectHandle xfa = form.isDictionary() ? form.getKey("/XFA")
                                                        : QPDFObjectHandle::newNull();
        sourceHasXfa = xfa.isStream() || (xfa.isArray() && xfa.getArrayNItems() > 0);
        const QPDFObjectHandle needsRendering = form.isDictionary()
            ? form.getKey("/NeedsRendering")
            : QPDFObjectHandle::newNull();
        sourceDynamicXfa = sourceHasXfa
            && ((needsRendering.isBool() && needsRendering.getBoolValue()) || !sourceHasFormFields);
    }
    // And the threads between annotations, while the parser still has them: 删除页面 and 拆分文件
    // ask what a request would cost them on every keystroke (§47), and this class keeps no QPDF
    // open afterwards to answer that. Page numbers, not objects -- see replyLinksOf().
    sourceReplyLinks = replyLinksOf(*document);
    // And the tree itself, while the parser still has the file: the destinations it carries are the
    // only place a position can come from (§75), and this class keeps no QPDF open afterwards, so a
    // save that wants them has to have been given them here. The count the 编辑书签 box says out loud
    // (§80) is answered off the same reading, not off a second walk.
    sourceOutline = outlineFromSource(*document);
    sourcePositionalBookmarkCount = countPositionalBookmarks(sourceOutline);
    return true;
}

bool DocumentEditor::inspectImport(const QString &path, int *pageCount, QString *error,
                                   bool *hasFormFields, QVector<ReplyLink> *replyLinks)
{
    if (pageCount)
        *pageCount = 0;
    if (hasFormFields)
        *hasFormFields = false;
    if (replyLinks)
        replyLinks->clear();
    QString reason;
    int pages = 0;
    try {
        // Opened with no password on purpose: a file that needs one is refused below
        // rather than asked for, and the reason is in the sentence, not in a box that
        // would have to explain itself.
        QPDF document;
        document.processFile(path.toUtf8().constData());
        pages = int(QPDFPageDocumentHelper(document).getAllPages().size());
        if (hasFormFields) {
            const QPDFObjectHandle form = document.getRoot().getKey("/AcroForm");
            const QPDFObjectHandle fields = form.isDictionary() ? form.getKey("/Fields")
                                                                : QPDFObjectHandle::newNull();
            *hasFormFields = fields.isArray() && fields.getArrayNItems() > 0;
        }
        // Read on this pass rather than by a second call, because the merge box wants it while the
        // reader is typing a range into that row -- and a file looked over twice is a file that can
        // answer differently between the two looks.
        if (replyLinks)
            *replyLinks = replyLinksOf(document);
    } catch (const QPDFExc &e) {
        // QPDF answers a wrong or missing user password the same way it answers a
        // broken file -- by throwing -- so which of the two this is has to be read off
        // the error code. Getting it wrong would tell a reader their readable file is
        // unreadable, or ask them for a password a file never had.
        const bool needsPassword = e.getErrorCode() == qpdf_e_password;
        reason = needsPassword
            ? tr("%1 要用口令才能打开，不知道口令的文档合不进来。\n\n想合并它，先把"
                 "它本身打开一次，或者去掉它的口令。")
                  .arg(QFileInfo(path).fileName())
            : tr("%1 打不开，所以合不进来。\n\n解析器说：%2")
                  .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        reason = tr("%1 打不开，所以合不进来。\n\n解析器说：%2")
                     .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
    }
    if (!reason.isEmpty()) {
        *error = reason;
        return false;
    }
    if (pages == 0) {
        *error = tr("%1 一页也没有，没什么可合并的。").arg(QFileInfo(path).fileName());
        return false;
    }
    if (pageCount)
        *pageCount = pages;
    return true;
}

bool DocumentEditor::deletePages(const QVector<int> &pagesToDelete, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    const DeletePlan plan = planDelete(pagesToDelete);
    if (plan.pagesToRemove == 0) {
        // Two different reasons for the same zero, and the reader needs the right
        // one: naming nothing, versus naming pages that this same list of
        // unsaved changes has already taken out.
        *error = plan.alreadyGone > 0
            ? tr("那些页在还没保存的修改里已经删掉了。")
            : tr("页码范围里没有可删除的页面。");
        return false;
    }
    if (plan.leavesNothing) {
        *error = tr("不能把每一页都删掉：一个不剩的 PDF 不是 PDF，任何阅读器都打不开它。"
                    "想丢掉整份文档，就把文件本身删掉。");
        return false;
    }

    QSet<int> named;
    for (int page : pagesToDelete) {
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    Operation op;
    op.kind = Operation::Kind::DeletePages;
    op.pages = sortedFrom(named);
    record(op);
    return true;
}

bool DocumentEditor::rotatePages(const QVector<int> &pagesToRotate, int quarterTurns,
                                 QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (quarterTurns < 1 || quarterTurns > 3) {
        // PDF records rotation as /Rotate, and only in quarter turns. Refusing an
        // angle that cannot be stored beats storing the nearest one and letting
        // the reader find out from the page.
        *error = tr("旋转只能是 90° 的整倍数。");
        return false;
    }
    QSet<int> named;
    for (int page : pagesToRotate) {
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    if (named.isEmpty()) {
        *error = tr("页码范围里没有可旋转的页面。");
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::RotatePages;
    op.pages = sortedFrom(named);
    op.quarterTurns = quarterTurns;
    record(op);
    return true;
}

bool DocumentEditor::movePages(const QVector<int> &pagesToMove, MoveTarget to, int anchor,
                               QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (to == MoveTarget::AfterPage && (anchor < 0 || anchor >= originalPages)) {
        // Checked against the file rather than against `live`: a page an earlier
        // unsaved change deleted is a page the reader can point at, and planMove()
        // knows what to do with it. A number that was never a page is not a
        // position to move behind.
        *error = tr("要说移到第几页之后，而那个页码不在这份文档里。");
        return false;
    }
    const MovePlan plan = planMove(pagesToMove, to, anchor);
    if (plan.pagesMoved == 0) {
        *error = plan.alreadyGone > 0
            ? tr("那些页在还没保存的修改里已经删掉了。")
            : tr("页码范围里没有可移动的页面。");
        return false;
    }
    if (plan.anchorIsMoved) {
        *error = tr("第 %1 页就在要移动的页里，这样移等于没移。").arg(anchor + 1);
        return false;
    }
    if (!plan.changesOrder) {
        // Three ways to end up where you already are, and the one that fits says
        // what the other two would have left the reader to work out.
        *error = to == MoveTarget::Front
            ? tr("那些页已经在最前头了。")
            : (to == MoveTarget::Back ? tr("那些页已经在最后了。")
                                      : tr("那些页已经在第 %1 页后面了。").arg(anchor + 1));
        return false;
    }

    QSet<int> named;
    for (int page : pagesToMove) {
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    Operation op;
    op.kind = Operation::Kind::MovePages;
    op.pages = sortedFrom(named);
    op.moveTo = to;
    op.anchor = anchor;
    record(op);
    return true;
}

bool DocumentEditor::mergeFile(const QString &path, const QVector<int> &pages, MoveTarget to,
                               int anchor, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (const int splitting = splitPieceSize(); splitting > 0) {
        // The other end of the rule splitInto() states, and the reason it has to be in
        // both places is that the two requests come in no fixed order: 拆分 then 合并 is
        // as reachable as 合并 then 拆分. What it keeps out of the file is a save with
        // imported pages to place into pieces numbered against a document the reader
        // cannot see -- and with page handles that would have to stay open across every
        // piece's writer at once.
        *error = tr("这份文档已经说了要拆成每 %n 页一份。先撤销那一步，再把文件合并进来。", nullptr, splitting);
        return false;
    }
    if (to == MoveTarget::AfterPage && (anchor < 0 || anchor >= originalPages)) {
        // The same rule 移动页面 enforces, for the same reason: the anchor has to be a
        // page of the document on the screen, which is the only numbering a reader can
        // check. A page an earlier change deleted is allowed and lands in the gap.
        *error = tr("要说合并到第几页之后，而那个页码不在这份文档里。");
        return false;
    }

    // Looked over again rather than trusted from the dialog. The file can have been
    // replaced, shrunk or deleted in the time between choosing it and pressing 合并,
    // and a merge that quietly brought in fewer pages than the summary promised is the
    // silent kind of wrong -- the count would look right in the status bar and be wrong
    // in the file.
    int importPages = 0;
    bool importHasForm = false;
    if (!inspectImport(path, &importPages, error, &importHasForm))
        return false;

    QSet<int> named;
    for (int page : pages) {
        if (page < 0 || page >= importPages) {
            *error = tr("%1 一共 %n 页，没有第 %2 页。", nullptr, importPages)
                         .arg(QFileInfo(path).fileName())
                         .arg(page + 1);
            return false;
        }
        named.insert(page);
    }
    if (named.isEmpty()) {
        *error = tr("还没有说出要合并 %1 的哪几页。").arg(QFileInfo(path).fileName());
        return false;
    }

    // One file, one entry: operations point at files by index, so a second entry for a
    // path already in the list would make the same document look like two of them --
    // and would have the save open it twice.
    int index = -1;
    for (int candidate = 0; candidate < imports.size(); ++candidate) {
        if (sameFile(imports.at(candidate).path, path)) {
            index = candidate;
            break;
        }
    }
    const bool firstTime = index < 0;
    if (firstTime)
        index = imports.size();

    const QVector<int> chosen = sortedFrom(named);
    // This merge's own identity, taken before the plan is worked out because the plan
    // is what stamps it onto the pages. A refusal below burns a number, which costs
    // nothing: the only rule is that no two merges share one.
    const int mergeId = nextMergeId++;
    const MergePlan plan = planMerge(live, chosen, mergeId, to, anchor);
    if (!plan.usable()) {
        *error = tr("没有页可以合并进来。");
        return false;
    }
    if (firstTime)
        imports.append({path, importPages, importHasForm});

    Operation op;
    op.kind = Operation::Kind::MergeFile;
    op.pages = chosen;
    op.importIndex = index;
    op.mergeId = mergeId;
    op.moveTo = to;
    op.anchor = anchor;
    record(op);
    return true;
}

bool DocumentEditor::splitInto(int pagesPerPiece, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (const int imported = importedPageCount(); imported > 0) {
        // Not "拆分 is unavailable after a merge" but which pages make it unavailable:
        // the reader has to be able to find the merge and undo it, and after several of
        // them the count is the only thing this sentence can point at.
        *error = tr("这份要写出的文档里有 %n 页是合并进来的，而那些页没有你在屏幕上能看到的"
                    "页号，所以哪几页该进哪一份是说不出来的。先撤销那几次合并再拆。",
                    nullptr, imported);
        return false;
    }
    if (pagesPerPiece < 1) {
        // The dialog's spin box cannot send this. Checked because a piece of 0 pages is
        // not a file, and a save that wrote one would write it forever.
        *error = tr("每份至少要有 1 页。");
        return false;
    }
    if (pagesPerPiece >= live.size()) {
        // Refused rather than recorded: a split that comes out as one piece dirties the
        // tab, earns an undo row, and writes exactly the file the reader already had.
        // Saying so is cheaper than letting them find out at the save.
        *error = tr("这份要写出的文档一共 %1 页，每份 %2 页只会写出一份，那就不用拆了。")
                     .arg(live.size())
                     .arg(pagesPerPiece);
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::SplitBySize;
    op.pieceSize = pagesPerPiece;
    // record() and nothing else: no page is taken out, moved or turned, so `live` comes
    // back from rebuildLive() exactly as it went in. That is the whole point of the kind.
    record(op);
    return true;
}

bool DocumentEditor::protectWith(const Protection &protection, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::Protect;
    op.protection = protection;
    op.protection.what = Protection::What::Encrypt;

    if (op.protection == protectionToWrite()) {
        // Refused for the reason splitInto() refuses a one-piece split: this is not a
        // change. It would dirty the tab and earn an undo row that, when taken back,
        // leaves the file exactly as it would have been -- and a menu that offers "撤销
        // 加上打开口令" over a document that already had that口令 written into it is a
        // menu describing a thing that did not happen.
        *error = tr("这份要写出的文件已经是这个口令和这些权限了，再要一次不会多改什么。");
        return false;
    }
    record(op);
    return true;
}

bool DocumentEditor::removeProtection(QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (protectionToWrite().what == Protection::What::Decrypt) {
        // The same refusal as above, in the other direction: the request is what a save
        // would already write.
        *error = tr("这份要写出的文件已经说过要去掉口令了，再要一次不会多去掉什么。");
        return false;
    }
    if (!sourceEncrypted && protectionToWrite().what == Protection::What::Carry) {
        // Nothing on disk to take off and nothing in the list putting any on. Two answers
        // are needed for this refusal because the request can be about either: the file the
        // reader is looking at, or the file a save would write. A pending 加密 is undone by
        // 去掉口令 -- that symmetry is why the two directions share a dialog -- and on an
        // unopened-by-password document that is the only thing this method has to act on.
        *error = tr("正在打开的这份文件本身没有口令，也没有权限限制，所以没有东西可去掉。");
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::Protect;
    op.protection.what = Protection::What::Decrypt;
    // The six flags are left at their defaults on purpose: they say nothing once the
    // dictionary is gone, and filling them in from the source's bits would put numbers in
    // a pending operation that no code reads -- which is worse than leaving them alone,
    // because it looks like it means something.
    record(op);
    return true;
}

bool DocumentEditor::addWatermark(const Watermark &watermark, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (watermark.text.trimmed().isEmpty()) {
        // Refused rather than defaulted, and rather than treated as "no watermark": an empty
        // canvas is not an invisible one. Whatever the metrics report for a blank string is what
        // would be painted and stretched across every named page, so a reader who pressed 确定
        // with the box empty asked for a smudge, not for nothing.
        *error = tr("还没有说水印要写什么字。");
        return false;
    }
    if (watermark.opacityPercent < 1 || watermark.opacityPercent > 100) {
        // The dialog's spin box cannot send this. Checked because the number reaches a file as
        // PDF's `/ca`, and outside that range PDF says nothing anyone could rely on: 0.0 is
        // invisible, and a value above 1.0 is clamped by some readers and not by others. A
        // watermark whose strength depends on which reader the next person opens it with is
        // worse than a refusal.
        *error = tr("不透明度只能是 1% 到 100% 之间的整数。");
        return false;
    }

    QSet<int> named;
    for (int page : watermark.pages) {
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    if (named.isEmpty()) {
        // The two zeroes are different sentences, and 删除页面 and 旋转页面 already say them
        // apart: nothing was named, versus pages that an earlier unsaved change took out.
        *error = watermark.pages.isEmpty()
            ? tr("页码范围里没有可加水印的页面。")
            : tr("那些页在还没保存的修改里已经删掉了，没有可以加水印的。");
        return false;
    }

    Watermark asked = watermark;
    asked.pages = sortedFrom(named);
    if (asked == watermarkToWrite()) {
        // The same refusal splitInto() and protectWith() make: this is not a change. It would
        // dirty the tab and earn an undo row that, when taken back, leaves the file exactly as it
        // would have been.
        *error = tr("这份要写出的文件已经带着一模一样的水印了，再要一次不会多改什么。");
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::Watermark;
    op.watermark = asked;
    // record() and nothing else: no page is taken out, moved or turned, so `live` comes back from
    // rebuildLive() exactly as it went in. That is what makes 撤销 here one entry out of a list
    // rather than a page to be un-drawn -- nothing had been written yet.
    record(op);
    return true;
}

DocumentEditor::Watermark DocumentEditor::watermarkToWrite() const
{
    // The last one asked for, which is the rule splitPieceSize() and protectionToWrite() follow:
    // a second 水印 is the reader changing the wording, not a second layer for the save to draw.
    // The alternative is that taking one back leaves the other behind, which is not something any
    // summary in this program describes -- and it would make 撤销 twice the only way to take a
    // watermark off a document.
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (it->kind == Operation::Kind::Watermark)
            return it->watermark;
    }
    // Nothing pending. An empty text is what the dialog reads as "open on the defaults", and what
    // the save reads as "draw nothing".
    return Watermark();
}

bool DocumentEditor::setOutline(const QVector<OutlineRow> &rows, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }

    QVector<OutlineEdit> edits;
    edits.reserve(rows.size());
    // Which requested row is which file bookmark, and what that costs -- both read off the same
    // matcher, so the number the box warns about is the number this request actually carries.
    const MatchedRows matched = matchRowsToSource(sourceOutline, rows);
    positionalAtRiskCount = positionalAtRiskFor(rows);
    for (const OutlineRow &row : rows) {
        if (row.level < 1) {
            // Refused rather than clamped: a tree that quietly moved a bookmark a level would be a
            // different tree from the one that was arranged. The box greys 确定 for such a row before
            // the press (§74), so this is the backstop for a caller that is not the box -- and since
            // §74 the level is a text cell, so a 0 is something a reader can actually type.
            *error = tr("“%1” 的层级是 %2，而书签的层级从 1 开始。")
                         .arg(row.title.isEmpty() ? tr("一条书签") : row.title)
                         .arg(row.level);
            return false;
        }
        if (row.page < -1 || row.page >= originalPages) {
            // The bound is the file the reader opened, not the list the pending edits have shrunk:
            // an edit here is only recorded, never applied, so the page they can check a typed
            // number against is the one on their screen. Validating against `live` instead made the
            // last page of a 40-page document un-typeable as soon as one page was pending-deleted
            // (measured: `“Last page” 指向第 40 页，而现在这份文档只有 39 页。`).
            *error = tr("“%1” 指向第 %2 页，而这份文件打开时是 %3 页。")
                         .arg(row.title.isEmpty() ? tr("一条书签") : row.title)
                         .arg(row.page + 1)
                         .arg(originalPages);
            return false;
        }
        OutlineEdit edit;
        edit.title = row.title;
        edit.depth = row.level;
        // The number becomes the page's identity here and nowhere else afterwards: a delete
        // replayed later, or a split's pieces, each resolve the reference against the pages that
        // actually reach the file. Storing the position would store a number that the next
        // operation is free to change under it.
        //
        // `{0, n}` is the same address `deletePages()`/`rotatePages()` read a typed number through
        // (`live.contains({0, page})`) and `positionsOf()` resolves -- which is why it is *not*
        // `live.at(n)`: measured (§70), on a document with one page pending-deleted, `live.at(5)`
        // put a bookmark named for 「第 6 页」 on the page after it, and naming the page already
        // taken out borrowed its neighbour instead of losing its page. A number whose page is gone
        // still resolves, to nothing: `outlineRowsFor()` finds no such page in the output and the
        // bookmark comes back a heading, which is what §69 already pins for a page lost *after* the
        // ask. One answer for both orders.
        edit.page = row.page < 0 ? PageRef() : PageRef{0, row.page};
        // §75's route (a), taken here: the file's own destination rides along on a row the reader
        // left where it was. The join is by place in the walk *and* by title and page both still
        // agreeing, because a row that was renamed, re-nested or made in the box has no honest
        // counterpart in the file -- giving it the neighbour's `/FitH 650` would be worse than the
        // flattening this replaces, and §75's own reading is what says a neighbour's number is a
        // plausible-looking lie.
        // The file's own position for this row, when there is one to take: see matchRowsToSource()
        // for what that means and for the duplicate-title case it cannot resolve.
        const int from = matched.fileIndex.at(edits.size());
        if (from >= 0)
            edit.dest = sourceOutline.at(from).dest;
        edits.append(edit);
    }

    if (outlineAsked() && outlineToWrite() == edits) {
        // The same refusal addWatermark() and splitInto() make: asking twice for the identical
        // tree is not a change, and recording it would dirty the tab and hand the reader an undo
        // row that puts the file back exactly as it was.
        *error = tr("这份要写出的文件已经带着一模一样的书签了，再要一次不会多改什么。");
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::SetOutline;
    op.outline = edits;
    // record() only: no page is taken out, moved or turned, so this is one entry out of a list to
    // undo it -- and nothing has been written yet, which is the whole point of the replay design.
    record(op);
    return true;
}

QVector<DocumentEditor::OutlineEdit> DocumentEditor::outlineToWrite() const
{
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (it->kind == Operation::Kind::SetOutline)
            return it->outline;
    }
    return {};
}

bool DocumentEditor::outlineAsked() const
{
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (it->kind == Operation::Kind::SetOutline)
            return true;
    }
    return false;
}

int DocumentEditor::positionalAtRiskFor(const QVector<OutlineRow> &rows) const
{
    // The file's positional bookmarks that this request leaves behind: renamed away, deleted, or
    // duplicated past the one copy that could take the place. Wording matters here -- the sentence in
    // the box says these "won't be carried into what gets written", which is true of all three, and
    // the number stays 0 for a tree the reader only re-titled at another place in.
    const MatchedRows matched = matchRowsToSource(sourceOutline, rows);
    int atRisk = 0;
    for (int i = 0; i < sourceOutline.size(); ++i) {
        if (!matched.consumed.at(i) && sourceOutline.at(i).dest.isPositional())
            ++atRisk;
    }
    return atRisk;
}

QVector<OutlineRow> DocumentEditor::outlineRowsFor(const QVector<OutlineEdit> &edits,
                                                   const QVector<PageRef> &written)
{
    QVector<OutlineRow> rows;
    rows.reserve(edits.size());
    for (const OutlineEdit &edit : edits) {
        OutlineRow row;
        row.title = edit.title;
        row.level = edit.depth;
        // A heading stays a heading; everything else is looked up by *which* page it names, so a
        // bookmark follows its page through a reorder and a merge. Not found means the page is not
        // in this output -- the bookmark stays, without a target, which is what outline.h already
        // says to do with a bookmark that names no page rather than dropping it.
        row.page = edit.page.page < 0 ? -1 : written.indexOf(edit.page);
        // The destination is the one thing that does *not* depend on where the page ended up: it
        // describes a place on the page, and the page travels with the reference above. A destination
        // whose page is gone is dropped with it (row.page becomes -1, a heading), so nothing here can
        // write a place on a page that is not in the file.
        row.dest = edit.dest;
        rows.append(row);
    }
    return rows;
}

QVector<OutlineRow> DocumentEditor::outlineToEdit() const
{
    QVector<OutlineRow> rows;
    const QVector<OutlineEdit> edits = outlineToWrite();
    rows.reserve(edits.size());
    for (const OutlineEdit &edit : edits) {
        OutlineRow row;
        row.title = edit.title;
        row.level = edit.depth;
        // The reference goes back to the number that came in, for the reason its declaration gives:
        // `PageRef()` reads as -1 (a heading) and `{0, n}` reads as n. Nothing else can be in here,
        // so there is no third case to write a branch for -- if one ever is, this is the line that
        // would silently turn a merged page's bookmark into a heading, and the comment on
        // setOutline() is where the numbering rule lives.
        row.page = edit.page.page;
        rows.append(row);
    }
    return rows;
}

void DocumentEditor::record(const Operation &op)
{
    pending.append(op);
    // The branch this list took is gone: a new operation after an undo is the
    // reader moving forward again, not a second history waiting somewhere to be
    // picked back up.
    undone.clear();
    // And the page list is retaken from the operations rather than patched, so
    // that the count the dialog showed and the count a save writes cannot be two
    // different answers to the same question.
    rebuildLive();
}

bool DocumentEditor::undo()
{
    if (pending.isEmpty())
        return false;
    undone.append(pending.takeLast());
    rebuildLive();
    return true;
}

bool DocumentEditor::redo()
{
    if (undone.isEmpty())
        return false;
    pending.append(undone.takeLast());
    rebuildLive();
    return true;
}

void DocumentEditor::rebuildLive()
{
    live = openedDocumentInOrder(originalPages);
    for (const Operation &op : std::as_const(pending)) {
        if (op.kind == Operation::Kind::DeletePages) {
            for (int page : op.pages)
                live.removeOne({0, page});
        } else if (op.kind == Operation::Kind::MovePages) {
            // The same function the dialog called and the replay below uses, so
            // that the page list in memory and the page tree that gets written can
            // not drift apart by one position -- which, for a reorder, is the
            // difference between the document somebody asked for and one they did
            // not, with the same number of pages in both.
            live = planMove(live, op.pages, op.moveTo, op.anchor).order;
        } else if (op.kind == Operation::Kind::MergeFile) {
            // planMerge() again for the same reason, and note that the pages it adds
            // here are references to another file, not copies: this list is a promise
            // about where pages come from, and the bytes are only pulled in by the
            // save that says so.
            live = planMerge(live, op.pages, op.mergeId, op.moveTo, op.anchor).order;
        }
        // RotatePages needs no branch -- a turned page is still the same page in the same
        // place -- and neither do the three kinds that leave the tree alone: SplitBySize changes
        // how many files a save writes, Protect changes what each of them needs to open, and
        // Watermark changes what is drawn inside a page. None of the three touches which pages
        // exist. A new kind that does change the list has to appear
        // here, and rebuild-rather-than-patch is what makes forgetting it show up as a count
        // that contradicts the operations instead of a list that quietly drifts.
    }
}

QString DocumentEditor::undoDescription() const
{
    return pending.isEmpty() ? QString() : describeOperation(pending.last());
}

QString DocumentEditor::redoDescription() const
{
    return undone.isEmpty() ? QString() : describeOperation(undone.last());
}

QString DocumentEditor::pendingSummary() const
{
    QStringList parts;
    for (const Operation &op : pending)
        parts.append(describeOperation(op));
    // tr() for a single comma: this is the line a reader reads in the discard prompt and
    // on the status bar, and joining it with a literal ， put CJK punctuation into the
    // English interface.
    return parts.join(tr("，"));
}

QVector<int> DocumentEditor::positionsOf(const QVector<PageRef> &present,
                                         const QVector<int> &pages)
{
    QVector<int> positions;
    positions.reserve(pages.size());
    for (int page : pages) {
        const int at = present.indexOf({0, page});
        if (at >= 0)
            positions.append(at);
    }
    return positions;
}

void DocumentEditor::apply(const Operation &op, QPDF &document, QVector<PageRef> &present,
                           QPDF *mergeSource)
{
    if (op.kind == Operation::Kind::SplitBySize || op.kind == Operation::Kind::Protect
        || op.kind == Operation::Kind::SetOutline) {
        // A split says how many files the save writes; it takes nothing out of the page
        // tree, moves nothing in it and turns nothing. The pruning into pieces happens in
        // saveAs(), after this walk has agreed with `live` -- so that the page order every
        // piece is cut from is the one the dialogs described.
        //
        // A Protect op is inert the same way, and lands in the same place: it says what the
        // writer at the end of saveAs() is handed, and nothing about the page tree. Being
        // here rather than in a branch of its own is what lets a 删页 and a 加密 ride one
        // save -- the replay reaches the writer with the pages already rearranged, and the
        // protection is then a property of the bytes going out, not of the tree.
        //
        // A bookmark tree is the third of these, and it is inert for a reason the other two
        // do not have: it must be written *after* the piece is cut, against the pages that are
        // in this file, so resolving its page references here would answer the question too
        // early. Skipping the replay is what makes a bookmark survive a 删页 in the same save as
        // landing on the page the reader pointed at rather than on whatever sits where that
        // page used to be.
        return;
    }

    QPDFPageDocumentHelper helper(document);
    // One snapshot per operation: the handles below are what removePage() matches
    // against, and re-asking between changes would number the pages against a tree
    // that has already changed under them.
    //
    // Not const, and a copy of a handle is still the page itself: these are object
    // references, so rotatePage() on one of them writes into the document.
    std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
    // `present` and the snapshot above are index-aligned -- both run through the pages
    // in the order the tree holds them -- so any page, from either file, can be found
    // by what the reader asked for. No two entries of `present` are the same reference,
    // which is what makes indexOf() a safe way to ask: a page merged in twice carries
    // two identities, so the copy a move leans against is the one that was meant.
    auto handleOf = [&](const PageRef &ref) { return pages.at(size_t(present.indexOf(ref))); };

    if (op.kind == Operation::Kind::RotatePages) {
        for (int original : op.pages) {
            const int at = present.indexOf({0, original});
            if (at >= 0 && at < int(pages.size()))
                pages.at(size_t(at)).rotatePage(op.quarterTurns * 90, /* relative = */ true);
        }
        return;
    }

    if (op.kind == Operation::Kind::Watermark) {
        // One canvas, painted once, for the whole file. Every named page references the same two
        // image streams and the same graphics state, which is what makes the bitmap route the
        // *cheaper* of the two (tests/watermarkprobe.cpp, question 6: 0.24 KB per page against
        // the operator route's 0.42 over a 600-page file) -- a page gains a `cm` and a `Do`, and
        // the pixels are paid for once no matter how many pages carry them.
        const Watermark &mark = op.watermark;
        const QImage art = paintWatermark(mark.text);
        const Planes planes = splitPlanes(art);
        const QPDFObjectHandle image
            = imageStream(document, planes.rgb, art.width(), art.height(), "/DeviceRGB");
        image.getDict().replaceKey("/SMask", imageStream(document, planes.alpha, art.width(),
                                                        art.height(), "/DeviceGray"));
        const QPDFObjectHandle state = watermarkState(document, mark.opacityPercent);

        for (int original : mark.pages) {
            const int at = present.indexOf({0, original});
            if (at < 0)
                continue; // an earlier 删页 took this one out; see pagesStillThere()
            QPDFPageObjectHelper page = pages.at(size_t(at));

            // What the reader sees of the page: the CropBox (which is what a viewer clips to, and
            // falls back to the MediaBox without writing anything when the page has no /CropBox)
            // and the /Rotate that watermarkPlacement() folds in. Both read through getAttribute's
            // inheritance walk rather than getKey, since either may live on a parent node.
            QPDFObjectHandle box = page.getCropBox(/* copy_if_shared = */ false,
                                                   /* copy_if_fallback = */ false);
            if (!box.isArray())
                box = page.getMediaBox(/* copy_if_shared = */ false);
            QRectF area;
            if (box.isArray() && box.getArrayNItems() == 4) {
                area = QRectF(box.getArrayItem(0).getNumericValue(),
                              box.getArrayItem(1).getNumericValue(),
                              box.getArrayItem(2).getNumericValue()
                                  - box.getArrayItem(0).getNumericValue(),
                              box.getArrayItem(3).getNumericValue()
                                  - box.getArrayItem(1).getNumericValue());
            }
            const QPDFObjectHandle rotate = page.getAttribute("/Rotate", false);
            const int degrees = rotate.isInteger() ? rotate.getIntValue() : 0;
            const Placement m = watermarkPlacement(area, double(art.width()), double(art.height()),
                                                   degrees);

            // Names are made unique against the whole /Resources dictionary rather than against
            // the subtype sub-dictionary: qpdf's own guidance for getUniqueResourceName says so,
            // and answering from the sub-dictionary alone is what lets a page that already has an
            // `Imwm1` of its own get it overwritten -- the page would then draw the watermark as
            // its own form, and lose whatever it used to draw.
            QPDFObjectHandle resources = resourcesForWrite(page);
            int suffix = 1;
            const std::string imageName
                = resources.getUniqueResourceName("Imwm", suffix);
            const std::string stateName
                = resources.getUniqueResourceName("GSwm", suffix);
            resourceDictForWrite(document, resources, "/XObject")
                .replaceKey("/" + imageName, image);
            resourceDictForWrite(document, resources, "/ExtGState")
                .replaceKey("/" + stateName, state);

            char ops[512];
            std::snprintf(ops, sizeof(ops),
                          "q /%s gs %.4f %.4f %.4f %.4f %.4f %.4f cm /%s Do Q\n", stateName.c_str(),
                          m.a, m.b, m.c, m.d, m.e, m.f, imageName.c_str());
            appendWatermarkContent(document, page, ops,
                                   tr("这一页的内容流不是能往上加东西的样子，所以水印没有画上去。"));
        }
        return;
    }

    if (op.kind == Operation::Kind::MergeFile) {
        const MergePlan plan = planMerge(present, op.pages, op.mergeId, op.moveTo, op.anchor);
        // Pages taken out of another document are not object references here: they are
        // objects in a file that is open beside this one, so each has to be copied
        // across. Handing the foreign page straight to addPage()/addPageAt() is what
        // qpdf does for this -- it copies the page and its resources, after pushing any
        // attributes the other file inherited down into it.
        QPDFPageDocumentHelper fromHelper(*mergeSource);
        const std::vector<QPDFPageObjectHelper> fromPages = fromHelper.getAllPages();
        QPDFAcroFormDocumentHelper toForm(document);
        QPDFAcroFormDocumentHelper fromForm(*mergeSource);

        // Left to right, the same order planMove() re-inserts a block in, and for the
        // same reason: the page each insertion leans on is already in the tree. The
        // tree has changed by then, so the handle of the page before has to be re-asked
        // every time -- getAllPages() is a snapshot, and a stale one numbers the pages
        // against the tree as it was.
        for (int index = plan.landsAt; index < plan.landsAt + plan.pagesAdded; ++index) {
            const PageRef &ref = plan.order.at(index);
            // Both of these are refused loudly rather than skipped: the saveAs() above
            // re-opens each file and checks its page count before anything is staged, so
            // reaching here with a page that cannot be copied means the two lists have
            // stopped agreeing, and writing a document that is one page short of the
            // sentence the reader acted on is the failure this class exists to avoid.
            if (ref.source != op.mergeId)
                throw std::runtime_error(tr("要合并的那一页在这份文件里找不到。")
                                             .toUtf8()
                                             .constData());
            const int source = ref.page;
            if (source < 0 || source >= int(fromPages.size()))
                throw std::runtime_error(tr("要合并的那一页在这份文件里找不到。")
                                             .toUtf8()
                                             .constData());
            const QPDFPageObjectHelper foreign = fromPages.at(size_t(source));
            const QPDFObjectHandle fromHandle = foreign.getObjectHandle();
            if (index == 0)
                helper.addPage(foreign, /*first=*/true);
            else
                helper.addPageAt(foreign, /*before=*/false,
                                 helper.getAllPages().at(size_t(index - 1)));
            // A page copied as an object shares its /Annots array with the page it came
            // from, and its form fields go missing entirely because nothing in this
            // document's AcroForm points at them. qpdf's own repair for that is this
            // call, and it is worth making only of a page that has annotations: the
            // helper this needs is documented as expensive to build, and most pages of
            // most documents have none.
            if (fromHandle.hasKey("/Annots"))
                toForm.fixCopiedAnnotations(helper.getAllPages().at(size_t(index)).getObjectHandle(),
                                            fromHandle, fromForm);
        }
        present = plan.order;
        return;
    }

    if (op.kind == Operation::Kind::MovePages) {
        const MovePlan plan = planMove(present, op.pages, op.moveTo, op.anchor);
        QVector<PageRef> moved;
        for (const PageRef &ref : present) {
            if (op.pages.contains(ref.page) && ref.source == 0)
                moved.append(ref);
        }

        // Out of the tree first, with the handles held on to. This matters more for
        // a move than for anything else here: addPage() shallow-copies a page it
        // finds already in the tree so that it does not add it twice, so inserting
        // before removing would duplicate the page rather than move it -- a document
        // with the block in it twice and nothing to say so.
        for (const PageRef &ref : moved)
            helper.removePage(handleOf(ref));
        // Then walk the order that was asked for and put each moved page behind
        // whatever precedes it. The pages that were not named are still in their
        // relative order, and the moved ones are inserted left to right, so the page
        // before each insertion is already in the tree -- which makes the result
        // exactly `plan.order`, not merely a document with the same pages in it.
        for (int index = 0; index < plan.order.size(); ++index) {
            const PageRef &ref = plan.order.at(index);
            if (!moved.contains(ref))
                continue;
            if (index == 0)
                helper.addPage(handleOf(ref), /*first=*/true);
            else
                helper.addPageAt(handleOf(ref), /*before=*/false,
                                 handleOf(plan.order.at(index - 1)));
        }
        present = plan.order;
        return;
    }

    const QVector<int> positions = positionsOf(present, op.pages);
    // Highest first, so that the index of every page still to come is unaffected
    // by the one just taken out.
    QVector<int> ordered = positions;
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    for (int at : std::as_const(ordered)) {
        if (at < int(pages.size()))
            helper.removePage(pages.at(size_t(at)));
    }
    for (int original : op.pages)
        present.removeOne({0, original});
}

bool DocumentEditor::saveAs(const QString &path, QString *error, QStringList *written)
{
    if (live.isEmpty() && pending.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (live.isEmpty()) {
        // Cannot be reached through deletePages(), which refuses the request that
        // would empty the document. It is checked again because a save writes what
        // this list says, and an empty page tree is not a PDF -- an operation added
        // later must not be able to get one written by oversight.
        *error = tr("这些修改会把每一页都删掉，而一个不剩的 PDF 任何阅读器都打不开。");
        return false;
    }

    const QFileInfo wanted(path);
    const QDir directory = wanted.absoluteDir();
    if (!directory.exists()) {
        *error = tr("文件夹不存在：%1").arg(directory.absolutePath());
        return false;
    }

    // Where the bytes go. Without a split that is the one name the save dialog just asked
    // the reader about; with one it is a name per piece, cut from the same pieceRanges()
    // that fed the dialog's summary -- so the set of files that turns up in the folder is
    // the set that was described, and not a second answer to "拆成几份".
    const QVector<PieceRange> pieces = this->pieces();
    QStringList destinations;
    for (int index = 0; index < pieces.size(); ++index) {
        destinations << (pieces.size() > 1
                             ? QFileInfo(piecePath(path, index, pieces.size(), pieces.at(index)))
                                   .absoluteFilePath()
                             : wanted.absoluteFilePath());
    }

    // What the writer is handed, worked out once for the whole save rather than once per
    // piece: a split that put a password on the first file and not the rest would be eight
    // answers to the one question the dialog asked. This is the same protectionToWrite()
    // the dialog's summary reads, so the sentence about what the file will need and the file
    // that needs it cannot come apart.
    const Protection protection = protectionToWrite();

    for (const QString &one : std::as_const(destinations)) {
        if (sameFile(sourcePath, one)) {
            // The rule this enforces is AGENTS.md's 永不写用户的 PDF 文件, and the
            // save dialog is quite capable of handing back the name it was seeded
            // with. The reader asked for a new file; saying so out loud is better than
            // quietly writing a different one. Under a split the same collision is
            // reachable by accident rather than by pressing 用上一次的名字: a document
            // already called 报告 01 (1-3).pdf is exactly one piece's output name.
            *error = destinations.size() > 1
                         ? tr("拆出来的文件里有一个（%1）就是正在打开的这份。请换一个文件名。")
                               .arg(QFileInfo(one).fileName())
                         : tr("另存为不能覆盖正在打开的文件。请换一个文件名。");
            return false;
        }
        for (const Import &import : std::as_const(imports)) {
            if (sameFile(import.path, one)) {
                // The other half of the same rule. A merge keeps the file it draws from
                // open as an input for the length of this save, and the reader may well
                // have that file open in another tab as a document of its own -- so
                // pointing the destination at it would overwrite a file while this program
                // is reading pages out of it.
                *error = tr("另存为不能覆盖合并进来的文件（%1）。请换一个文件名。")
                             .arg(QFileInfo(import.path).fileName());
                return false;
            }
        }
        if (pieces.size() > 1 && QFileInfo::exists(one)) {
            // The dialog asked whether it was all right to overwrite *one* file -- the name
            // the reader typed -- and none of these. So a piece whose name is already taken
            // refuses the whole save instead of quietly replacing it, and refuses everything
            // rather than writing the pieces that were free: a folder holding six of eight
            // pieces is a folder that looks like the job was done.
            *error = tr("%1 那里已经有一个文件了。拆分不会盖掉已经存在的文件，请换一个文件名，"
                        "或者先把那个文件挪走。")
                         .arg(QFileInfo(one).fileName());
            return false;
        }

        // §79: the same name, readable but not replaceable. Nothing was wrong with this name when
        // the save dialog asked about overwriting it, and the dialog cannot know what this can:
        // replacing takes the delete right, which Windows withholds while a program holds the file
        // open. Measured on a name another tab of this very window was reading --
        // tests/documenttab_smoke.cpp pins it -- the last step of the save came back with error 5
        // *after* the whole document had been rebuilt and written under a temporary name, and the
        // reader was told the file might be held by "别的程序", when the program holding it was us.
        // Saying it before any byte is staged is the difference between a name to change now and a
        // finished new file to go and look for in the temporary folder.
        if (QFileInfo::exists(one) && !replaceableDespiteBeingRead(one)) {
            *error = tr("%1 现在盖不掉：它正被读着（可能是别的程序，也可能是这个程序的另一个窗口）。"
                        "请换一个文件名，或者先关掉正在读它的那一个。")
                         .arg(QFileInfo(one).fileName());
            return false;
        }
    }

    if (pieces.size() > 1) {
        for (const Operation &op : std::as_const(pending)) {
            if (op.kind == Operation::Kind::MergeFile) {
                // Both splitInto() and mergeFile() refuse this pairing, so arriving here
                // means one of them stopped checking. It is checked because the pieces are
                // numbered by pages the reader can see and an imported page is not one of
                // those: a save that went ahead would write a set of files whose contents
                // no summary in this program ever described.
                *error = tr("这份文档里既有合并进来的页，又说了要拆分，这两样凑不成一份读者能核对的"
                            "文件。请先撤销其中一步。");
                return false;
            }
        }
    }

    // A name chosen rather than a file created, and beside the destination rather
    // than in the temp folder: the last step has to be a rename within one volume,
    // since across volumes it becomes a copy -- slower and, more importantly, not
    // atomic. It is also why this is not a QTemporaryFile: that keeps the handle it
    // created the file with until the object is destroyed, and QPDFWriter then
    // answers "Permission denied" when pointed at a file this function made a
    // moment ago (measured, not guessed: it is what the first version of this code
    // did). Nothing opens the path before the writer does, so nothing holds it. The
    // stamp and the counter keep two saves in the same folder apart, and an existing
    // file is stepped over rather than truncated -- one left behind by an earlier
    // refused write stays readable. That name is stagedPath()'s to work out, so that the
    // rule above has one place to be broken in.
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();

    // The files the merges draw from, opened here and held open until the last byte is
    // on disk. They have to outlive the replay: a page copied out of another document
    // keeps pointing at that file for its stream data, and it is the writer, not the
    // copy, that reads it (QPDF.hh says so under copyForeignObject). Opening them all
    // up front rather than one at a time inside the replay also means a file that went
    // away or changed shape is answered before anything is staged, and that two merges
    // of one file copy from one handle -- which is what qpdf's copy cache expects.
    // A std::vector rather than a QVector: this holds unique_ptrs, and Qt's list asks
    // for a copyable element the moment a non-const operator[] or detach touches it.
    std::vector<std::unique_ptr<QPDF>> foreign(size_t(imports.size()));
    for (const Operation &op : std::as_const(pending)) {
        if (op.kind != Operation::Kind::MergeFile || op.importIndex < 0
            || size_t(op.importIndex) >= foreign.size())
            continue;
        if (foreign[size_t(op.importIndex)])
            continue;
        const Import &import = imports.at(op.importIndex);
        auto source = std::make_unique<QPDF>();
        try {
            source->processFile(import.path.toUtf8().constData());
        } catch (const std::exception &e) {
            *error = tr("%1 这时候打不开了，所以这些修改写不出来。原来的文件没有改动。\n\n"
                        "解析器说：%2")
                         .arg(QFileInfo(import.path).fileName(), QString::fromUtf8(e.what()));
            return false;
        }
        const int nowPages = int(QPDFPageDocumentHelper(*source).getAllPages().size());
        if (nowPages != import.pageCount) {
            // Not clamped and not carried on with: the summary the reader acted on
            // named pages of a file that is no longer that file, and writing a
            // document that has fewer of them than the sentence promised is the silent
            // kind of wrong this class keeps refusing.
            *error = tr("%1 在这些修改记下来之后变了：当时是 %2 页，现在只有 %3 页。"
                        "请撤销那次合并，重新选一遍页码。")
                         .arg(QFileInfo(import.path).fileName())
                         .arg(import.pageCount)
                         .arg(nowPages);
            return false;
        }
        foreign[size_t(op.importIndex)] = std::move(source);
    }

    // One piece's bytes, from the source read again off disk. A split runs this once per
    // piece, which is once per parse of the whole file -- docs/store-submission.md §20
    // measures what that costs rather than guessing. It is not done by taking one replayed
    // document and cutting its page tree down N times, because what a piece needs taken out
    // is exactly what removePage() already does for 删除页面, and a page tree cut by hand has
    // to push down the attributes that sit on the intermediate /Pages nodes -- /MediaBox
    // among them -- before any leaf can stand on its own. That is how a hand-cut split
    // opens on blank pages, and it is not a class of wrong this class writes on purpose.
    const auto writeOne = [&](const PieceRange &keep, const QString &staged) {
        try {
            // The replay: the source read again from disk, then every pending operation
            // in the order the reader asked for them. This is where an original page
            // number becomes a position in a document that has already shrunk, which is
            // the whole reason `present` is carried along.
            std::unique_ptr<QPDF> document = std::make_unique<QPDF>();
            document->processFile(sourcePath.toUtf8().constData(),
                                  sourcePassword.isEmpty() ? nullptr
                                                           : sourcePassword.toUtf8().constData());
            QVector<PageRef> present = openedDocumentInOrder(originalPages);
            for (const Operation &op : std::as_const(pending))
                apply(op, *document, present,
                      op.importIndex >= 0 && size_t(op.importIndex) < foreign.size()
                          ? foreign[size_t(op.importIndex)].get()
                          : nullptr);
            if (present != live) {
                // The two ways of working out what the saved file holds -- the in-memory
                // list and the replay over the real page tree -- have to agree before any
                // bytes are promised to the reader. They run the same planMerge()/planMove()
                // functions for that reason, and this is the check that says so: a
                // disagreement means one of them stopped reading the same list, and the
                // file about to be written would not be the document the dialogs described.
                *error = tr("这些修改在内存里是一种页序，写到文件时成了另一种，所以这一份没有写。"
                            "请把这种情况报上来。");
                return false;
            }

            if (keep.count() < present.size()) {
                // Everything outside this piece's run of pages. Highest position first and
                // one snapshot of the tree, for the two reasons 删除页面 already documents:
                // a removal shifts the index of everything after it, and re-asking
                // getAllPages() between removals numbers the pages against a tree that has
                // already changed under the handles.
                QPDFPageDocumentHelper helper(*document);
                const std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
                for (int at = int(pages.size()) - 1; at >= 0; --at) {
                    if (at < keep.first || at > keep.last)
                        helper.removePage(pages.at(size_t(at)));
                }
            }

            // §76/§77: a split copies a field's whole /Kids list along with the pages, so each piece
            // can be left naming a box that belongs to a page only the other piece has -- and with
            // it, an answer about a page this file does not contain. Both are put right here, for
            // every output file, because by now the page list in `document` is the list this file
            // will have: pages may have left through 删除页面 (in apply()) or through 拆分 (the cut
            // above). Doing it in two places is how one gets missed -- §68's reason for rebuilding
            // the outline tree rather than patching it, in a different key.
            tidyFormForThisDocument(*document);

            // §78: the same argument one key lower. A merged page's own annotations came over as
            // private copies, but the references *between* them -- a popup's /Parent, a reply's
            // /IRT -- were left naming the document the page came out of, and a page that loses its
            // note to 删除页面 leaves a reply threaded to nothing. Also here, also once per output
            // file, for the reason above: this is the first point at which the page list is the one
            // this file will have.
            tidyAnnotationLinksForThisDocument(*document);

            // §55's half of the same argument, one level up: an /AcroForm /XFA packet is the form's
            // second copy, and it describes the *whole* document -- which pages there are, which
            // template fields sit on them, and every answer typed into it. Nothing in qpdf reads
            // it, so until now a save replayed the pages and let the packet say whatever it had
            // said about a document that no longer exists. Here the page list is final, so this is
            // where "the packet no longer describes this file" can be answered -- and the answer is
            // to write no packet, which sends every reader to the AcroForm fields that are still
            // tidied above for this file's own pages.
            //
            // Said per output file rather than once for the save, because that is what the other two
            // tidies do and what a split makes true: each piece holds part of the collection.
            if (changesPageCollection())
                dropXfaForThisDocument(*document);

            // The bookmarks, after the cut and before any writer exists, because this is the one
            // place that knows both which pages made it into *this* file and in what order. Under
            // no split `keep` is the whole document, so `here` is `present` -- the same page order
            // the check above just agreed to. Under a split it is one piece, which is what makes
            // each piece carry the bookmarks whose pages came out in it and no others; a reference
            // that is not in `here` -- a page a 删除页面 took out, or one that went into a
            // different piece -- stays in the tree as a heading rather than vanishing, because
            // the reader asked for that bookmark and outline.h's own rule for a bookmark with no
            // page is to show it and not to pretend it is gone.
            //
            // The tree is written whole rather than patched onto whatever the source had, which
            // is what makes 撤销 of a bookmark operation mean "the previous tree, or none": the
            // pending list holds the last request and this line is the only place that reaches
            // the file.
            if (outlineAsked()) {
                const QVector<PageRef> here = present.mid(keep.first, keep.count());
                QString outlineError;
                if (!writeOutline(*document, outlineRowsFor(outlineToWrite(), here),
                                  &outlineError)) {
                    // No removal here, unlike the catch below: stagedPath() only chooses a name,
                    // and the writer that would have put a file behind it has not run yet.
                    *error = outlineError;
                    return false;
                }
            }

            // The one place this program talks to a writer about protection, which is why
            // the whole switch sits here rather than being split between "the bits" and "the
            // password": the /P integer PDF stores is a combination of these arguments, and
            // a mapping that lived in two places is how a file ends up saying something the
            // six boxes never said.
            QPDFWriter writer(*document, staged.toUtf8().constData());
            switch (protection.what) {
            case Protection::What::Carry:
                // Nothing was asked for, so nothing is set and the writer's default
                // (preserveEncryption = true) carries the original's /Encrypt dictionary --
                // including its /P permission bits -- into the copy. That default is off
                // whenever prefiltering, content normalization or QDF mode is selected, and
                // off the moment setR6EncryptionParameters is called, so a future line that
                // adds one of those quietly turns 删页 into a tool that hands out
                // unprotected copies of protected documents.
                // tests/documenteditor_smoke.cpp compares the bits on both files for exactly
                // that reason -- and under a split it compares every piece, since "the copy"
                // is now N of them.
                break;
            case Protection::What::Decrypt:
                // Write no /Encrypt at all. This needs the password that opened the source
                // only in the sense that the source had to be opened to be read again --
                // measurement says nothing more is asked for: an encrypted document opened
                // by its *user* password still writes out stripped, with different bits, and
                // without a further key (tests/encryptprobe.cpp). That is why the permission
                // bits this program puts on a file are described to the reader as advisory.
                writer.setPreserveEncryption(false);
                break;
            case Protection::What::Encrypt: {
                // R6 / AES-256 and nothing else offered, so there is no compatibility
                // dropdown here to map: this writes the newest revision the standard has,
                // which a reader of a PDF from before Acrobat X cannot open. The dialog says
                // so out loud; this line is where it becomes true.
                //
                // The same string for the user and the owner password, and that is not a
                // shortcut -- see Protection::password. Asking for a second one would promise
                // a distinction QPDF does not keep on the way in.
                const QByteArray password = protection.password.toUtf8();
                writer.setR6EncryptionParameters(
                    password.constData(), password.constData(),
                    /* allow_accessibility = */ true, // not a box: always allowed
                    protection.allowExtract, protection.allowAssembly,
                    protection.allowAnnotate, protection.allowFormFilling,
                    protection.allowModifyOther,
                    protection.allowPrint ? qpdf_r3p_full : qpdf_r3p_none,
                    /* encrypt_metadata_aes = */ true);
                break;
            }
            }
            writer.write();
        } catch (const std::exception &e) {
            // Nothing usable came of the attempt, so nothing of ours is left behind.
            QFile::remove(staged);
            *error = tr("写出文件时出错，原来的文件没有改动。\n\n解析器说：%1")
                         .arg(QString::fromUtf8(e.what()));
            return false;
        }
        return true;
    };

    // Every piece is built before the first one is moved into place: a piece that cannot be
    // written then stops the save with nothing renamed, so the reader is never left with
    // three of eight files in the folder and no sentence to say which three.
    struct Staged
    {
        QString from;
        QString to;
    };
    QVector<Staged> built;
    for (int index = 0; index < pieces.size(); ++index) {
        const QString staged = stagedPath(directory, stamp, index);
        if (!writeOne(pieces.at(index), staged)) {
            for (const Staged &one : std::as_const(built))
                QFile::remove(one.from);
            return false;
        }
        built.append({staged, destinations.at(index)});
    }

    // One call per file, not "remove then rename": removing first would leave a window in
    // which the reader has neither file, and a failure inside that window would
    // lose a document it already had. QFile::rename is not enough here -- it
    // refuses to step on a file that is already there, and the save dialog has just
    // asked the reader whether this name was meant to overwrite that file.
    QStringList leftBehind;
    for (const Staged &one : std::as_const(built)) {
        const std::wstring from = QDir::toNativeSeparators(one.from).toStdWString();
        const std::wstring to = QDir::toNativeSeparators(one.to).toStdWString();
        if (MoveFileExW(from.c_str(), to.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
            == 0) {
            // From here on a complete staged file is never thrown away by this
            // function: the failure that leaves one behind -- the destination locked by
            // another program -- outlasts any dialog this could put up, and the
            // alternative is deleting work that took real time to produce.
            leftBehind << tr("新文件已经写好，但没法放到 %1 那里去——它可能被别的程序占着，"
                             "或者这个名字不允许覆盖（Windows 错误 %2）。写好的文件留在 %3。")
                              .arg(one.to)
                              .arg(GetLastError())
                              .arg(one.from);
        }
    }
    if (leftBehind.isEmpty()) {
        if (written != nullptr) {
            written->clear();
            for (const Staged &one : std::as_const(built))
                *written << one.to;
        }
        return true;
    }
    // Named piece by piece rather than one summary: the reader has to be able to find each
    // orphaned staged file, and a count does not tell them which names are still free.
    *error = leftBehind.size() == 1 && built.size() == 1
                 ? leftBehind.first()
                 : tr("拆出来的 %1 个文件里有 %2 个没能放到位。放到位的那些已经在文件夹里了，"
                      "下面每一条说的是剩下的该去哪儿找。")
                       .arg(built.size())
                       .arg(leftBehind.size())
        + QLatin1Char('\n') + leftBehind.join(QLatin1Char('\n'));
    return false;
}
