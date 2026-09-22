#include "qtpdfbackend.h"

#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#ifndef XYZPDF_SHELL_HANDLER
#include <QPdfLink>
#include <QPdfLinkModel>
#endif
#include <QPdfSelection>

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>

#include <limits>

namespace {

QPdfDocumentRenderOptions::Rotation rotationFromDegrees(int degrees)
{
    int normalized = degrees % 360;
    if (normalized < 0)
        normalized += 360;
    switch (normalized) {
    case 90:
        return QPdfDocumentRenderOptions::Rotation::Clockwise90;
    case 180:
        return QPdfDocumentRenderOptions::Rotation::Clockwise180;
    case 270:
        return QPdfDocumentRenderOptions::Rotation::Clockwise270;
    default:
        return QPdfDocumentRenderOptions::Rotation::None;
    }
}

QPdfDocumentRenderOptions renderOptions(const RenderOptions &options)
{
    QPdfDocumentRenderOptions result;
    result.setRotation(rotationFromDegrees(options.rotationDegrees));

    QPdfDocumentRenderOptions::RenderFlags flags = QPdfDocumentRenderOptions::RenderFlag::None;
    // Annotations are drawn only when asked for. The default in RenderOptions
    // is on, so this branch is the one that runs for an ordinary page view.
    if (options.annotations)
        flags |= QPdfDocumentRenderOptions::RenderFlag::Annotations;
    if (options.optimizedForLcd)
        flags |= QPdfDocumentRenderOptions::RenderFlag::OptimizedForLcd;
    result.setRenderFlags(flags);

    // Only when it is a band rather than the page: setting the clip rectangle
    // to the whole page as well happens to work, and hides the case where the
    // two disagree about the size.
    if (!options.clip.isNull()) {
        if (!options.scaledSize.isEmpty())
            result.setScaledSize(options.scaledSize);
        result.setScaledClipRect(options.clip);
    }
    return result;
}

// A PDF's dates are strings in a format of their own, and Qt PDF hands back
// whatever it managed to parse. Both shapes are accepted rather than assuming
// one: a missing date is better than a wrong one, but a date that came through
// as text and was thrown away is a Properties dialog with a blank field for
// no reason.
QDateTime dateOf(const QVariant &value)
{
    if (!value.isValid())
        return {};
    if (value.metaType().id() == QMetaType::QDateTime)
        return value.toDateTime();
    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return {};
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODate);
    return parsed.isValid() ? parsed : QDateTime{};
}

QString stringOf(const QVariant &value)
{
    return value.isValid() ? value.toString() : QString();
}

// The box of one character, in the page's own coordinates (points, top-left).
//
// One character at a time is what getSelectionAtIndex() can address exactly, and
// it is the only thing here that can: see characterIndexAt() for why Qt's
// point-based query is not usable.
QRectF characterBox(QPdfDocument *document, int page, int index)
{
    const QPdfSelection one = document->getSelectionAtIndex(page, index, 1);
    if (!one.isValid())
        return {};
    const QList<QPolygonF> bounds = one.bounds();
    if (bounds.isEmpty())
        return {};
    return bounds.first().boundingRect();
}

// How far outside a glyph the pointer still counts as being over the line, in
// points. About the width of a space at ordinary text sizes, so that a pointer
// in the gap between two words is over the line while one in the margin is not
// -- and a two-point tolerance would make the cursor flicker back to an arrow
// in every word space, which reads as the program being unsure.
constexpr double kTextHitTolerance = 4.0;

} // namespace

QtPdfBackend::QtPdfBackend()
    : document(std::make_unique<QPdfDocument>())
    , bookmarks(std::make_unique<QPdfBookmarkModel>())
{
}

QtPdfBackend::~QtPdfBackend() = default;

void QtPdfBackend::attachModels()
{
    bookmarks->setDocument(document.get());
}

Document::Status QtPdfBackend::load(const QString &path, const QString &password)
{
    loadedPath.clear();
    errorText.clear();
    neededPassword = false;

    // The page text of the previous document: page numbers and character
    // indices both mean something else now, and lines from the file that was
    // open would place a click in this one.
    pageTexts.clear();
    pageTextsOrder.clear();
    charBoxes.clear();
    charBoxesOrder.clear();

    // The password goes in before the load, not after a failure: Qt PDF reads
    // it once while opening the file, and setting it afterwards reloads the
    // whole document a second time.
    document->setPassword(password);

    const QPdfDocument::Error error = document->load(path);
    if (error != QPdfDocument::Error::None) {
        // A lambda named `tr`, not QObject::tr. This class has no Q_OBJECT
        // macro and must not gain one: qtpdfbackend.cpp is compiled into the
        // preview and thumbnail surrogates too, and a moc-generated
        // staticMetaObject there is the same delay-load trouble QPdfLink is (see
        // links() below). lupdate warns about the missing macro anyway and files
        // these strings under "QtPdfBackend" regardless, which is the context
        // the literal below asks for -- so the warning is noise, not a to-do.
        const auto tr = [](const char *text) {
            return QCoreApplication::translate("QtPdfBackend", text);
        };
        document->close();
        attachModels();

        switch (error) {
        case QPdfDocument::Error::IncorrectPassword:
            // The document is encrypted: it either has no password yet or the
            // one given was wrong. Either way the caller asks, and calling
            // load() again with the answer is the whole retry path.
            neededPassword = true;
            errorText = tr("此文档已加密，需要密码。");
            return Status::NeedsPassword;
        case QPdfDocument::Error::InvalidFileFormat:
            errorText = tr("这个文件不是有效的 PDF，或者已经损坏。");
            return Status::InvalidFormat;
        case QPdfDocument::Error::UnsupportedSecurityScheme:
            // Not a format problem and not a missing file, but nothing this
            // program can do with it either way: the caller has two sentences
            // to choose between -- "this is not a PDF" and "this PDF cannot be
            // read" -- and this is the second one.
            errorText = tr("这个 PDF 使用了不支持的加密方式，无法打开。");
            return Status::InvalidFormat;
        case QPdfDocument::Error::FileNotFound:
            errorText = tr("找不到文件：%1").arg(path);
            return Status::FileError;
        case QPdfDocument::Error::DataNotYetAvailable:
            errorText = tr("文件还没有读完，无法打开。");
            return Status::FileError;
        default:
            errorText = tr("打开 %1 时出错：%2")
                            .arg(path, QFileInfo(path).exists() ? tr("文件无法解析")
                                                                : tr("文件不存在"));
            return Status::FileError;
        }
    }

    if (document->status() != QPdfDocument::Status::Ready) {
        // load() reporting success and the document not being ready happens for
        // a file that is still being read; there is no asynchronous load path
        // here, so this is a failure rather than something to wait for.
        errorText = QCoreApplication::translate("QtPdfBackend", "%1 没有读完，无法打开。").arg(path);
        document->close();
        attachModels();
        return Status::FileError;
    }

    neededPassword = !password.isEmpty();
    loadedPath = path;
    attachModels();
    return Status::Ready;
}

QString QtPdfBackend::lastError() const
{
    return errorText;
}

QString QtPdfBackend::path() const
{
    return loadedPath;
}

int QtPdfBackend::pageCount() const
{
    return loadedPath.isEmpty() ? 0 : document->pageCount();
}

QSizeF QtPdfBackend::pageSize(int page) const
{
    if (page < 0 || page >= pageCount())
        return {};
    // Qt PDF already folds the page's own /Rotate in: the fixture in
    // tests/fixtures/rotated.pdf is stored 300x400 and marked 90, and this
    // returns 400x300 -- measured, and pinned by document_smoke. Applying the
    // rotation here as well would turn a page marked 90 into a page marked 180,
    // which is invisible until somebody opens a scan that was fed in sideways.
    //
    // Rotation the *reader* applies is a different thing and does not come
    // through here at all: the view swaps the size and the renderer is told
    // separately.
    return document->pagePointSize(page);
}

QImage QtPdfBackend::render(int page, QSize pixels, const RenderOptions &options)
{
    if (page < 0 || page >= pageCount() || pixels.isEmpty())
        return {};
    // The image comes back the size of the clip when there is one, and the size
    // of the page when there is not -- Qt's own behaviour, and what the caller
    // has to expect.
    return document->render(page, pixels, renderOptions(options));
}

QString QtPdfBackend::text(int page) const
{
    if (page < 0 || page >= pageCount())
        return {};
    return document->getAllText(page).text();
}

QVector<TextRun> QtPdfBackend::textRuns(int page) const
{
    QVector<TextRun> runs;
    if (page < 0 || page >= pageCount())
        return runs;
    // One box per polygon Qt PDF reports. The polygons are in points, with the
    // origin at the page's top-left corner, which is the same space pageSize()
    // reports -- so a caller can place a box on a rendered image by scaling.
    const QList<QPolygonF> bounds = document->getAllText(page).bounds();
    runs.reserve(bounds.size());
    for (const QPolygonF &polygon : bounds)
        runs.append(TextRun{polygon.boundingRect()});
    return runs;
}

const QtPdfBackend::PageText &QtPdfBackend::pageTextOf(int page) const
{
    const auto held = pageTexts.constFind(page);
    if (held != pageTexts.constEnd())
        return held.value();

    PageText built;
    const QPdfSelection all = document->getAllText(page);
    built.text = all.text();
    const QList<QPolygonF> bounds = all.bounds();
    built.lines.reserve(bounds.size());
    for (const QPolygonF &polygon : std::as_const(bounds))
        built.lines.append(polygon.boundingRect());

    // A few pages at a time, oldest let go: a drag across a page boundary
    // alternates between two of them.
    constexpr int kPagesHeld = 4;
    while (pageTextsOrder.size() >= kPagesHeld)
        pageTexts.remove(pageTextsOrder.takeFirst());
    pageTextsOrder.append(page);
    return *pageTexts.insert(page, built);
}

int QtPdfBackend::nearestLine(const QVector<QRectF> &lines, double y)
{
    int nearest = -1;
    double best = std::numeric_limits<double>::max();
    for (int line = 0; line < lines.size(); ++line) {
        const QRectF &box = lines.at(line);
        // Zero inside the line's own band, so a point that is on a line always
        // beats one that is merely near it.
        const double distance =
            y < box.top() ? box.top() - y : (y > box.bottom() ? y - box.bottom() : 0.0);
        if (distance < best) {
            best = distance;
            nearest = line;
        }
    }
    return nearest;
}

QRectF QtPdfBackend::characterBoxAt(int page, const PageText &pageContent, int index) const
{
    if (index < 0 || index >= pageContent.text.length())
        return {};

    auto held = charBoxes.find(page);
    if (held == charBoxes.end()) {
        CharBoxes fresh;
        fresh.boxes.resize(pageContent.text.length());
        fresh.computed = QBitArray(pageContent.text.length(), false);
        // A few pages at a time, oldest let go -- the same shape as the line-box
        // cache, because a document being exported touches one page at a time.
        constexpr int kPagesHeld = 4;
        while (charBoxesOrder.size() >= kPagesHeld)
            charBoxes.remove(charBoxesOrder.takeFirst());
        charBoxesOrder.append(page);
        held = charBoxes.insert(page, fresh);
    }
    CharBoxes &cache = *held;
    if (cache.computed.testBit(index))
        return cache.boxes.at(index);

    const QRectF box = characterBox(document.get(), page, index);
    cache.boxes[index] = box;
    cache.computed.setBit(index);
    return box;
}

bool QtPdfBackend::characterIsBefore(int page, const PageText &pageContent, int index,
                                     QPointF point) const
{
    if (index < 0)
        return true;
    if (index >= pageContent.text.length())
        return false;

    const QRectF box = characterBoxAt(page, pageContent, index);
    if (box.isNull()) {
        // ---- the character with no geometry ----
        //
        // A line break. PDFium reports it as a zero-size rectangle in the
        // page's bottom-left corner, which is *above* every line of text, so
        // comparing it with the point put it before the point wherever the
        // point was -- a "true" in the middle of the search's "false" region,
        // and one of those is enough for a bisection to land somewhere
        // arbitrary. It is answered instead by the character before it, which is
        // the last character of the line it ends: a line break is before the
        // point exactly when the end of its line is.
        int previous = index - 1;
        while (previous >= 0 && characterBoxAt(page, pageContent, previous).isNull())
            --previous;
        return characterIsBefore(page, pageContent, previous, point);
    }

    // ---- which line, by the line boxes rather than by the character ----
    //
    // The vertical position alone does not say which line a character is on:
    // a space has a zero-height box sitting *on* the baseline, so it compares as
    // being below the point whenever the point is above the baseline -- inside
    // the same line of text. The line boxes, which are free, say it properly:
    // both the character and the point are put on their nearest line, and only
    // when they are the same line does the horizontal position decide.
    const int characterLine = nearestLine(pageContent.lines, box.center().y());
    const int pointLine = nearestLine(pageContent.lines, point.y());
    if (characterLine >= 0 && pointLine >= 0 && characterLine != pointLine)
        return characterLine < pointLine;
    return box.center().x() < point.x();
}

int QtPdfBackend::characterIndexAt(int page, QPointF point) const
{
    if (page < 0 || page >= pageCount())
        return -1;

    const PageText &pageContent = pageTextOf(page);
    if (pageContent.text.isEmpty())
        return -1;

    // ---- why this is a search rather than a query ----
    //
    // Qt PDF has a point query -- getSelection(page, start, end) -- and it
    // answers only when the point lands on the ink of a glyph. Measured on this
    // machine: a point in the gap between two letters of a line, or on the
    // corner of a line's bounding box, comes back as an empty selection with
    // isValid() false, and a point on a glyph comes back with the character.
    // That is no use for a click: a reader clicking a line expects the cursor
    // somewhere on that line, and half the line is not ink.
    //
    // So the index is found by bisection over the character boxes, which are
    // exact and always available -- twelve queries on a page of three thousand
    // characters. A table of every character's box was tried instead and thrown
    // away: a character box costs about a millisecond (measured: 20 of them take
    // 19 ms), so the table for a page of 756 characters took 753 ms to build,
    // on the thread that draws the window.
    //
    // Above the text is its beginning and below it is its end, whatever the
    // horizontal position: the contract in document.h, and where a drag that
    // starts in the margin lands.
    if (!pageContent.lines.isEmpty()) {
        double top = 0;
        double bottom = 0;
        for (int line = 0; line < pageContent.lines.size(); ++line) {
            const QRectF &box = pageContent.lines.at(line);
            if (line == 0 || box.top() < top)
                top = box.top();
            if (line == 0 || box.bottom() > bottom)
                bottom = box.bottom();
        }
        if (point.y() < top)
            return 0;
        if (point.y() > bottom)
            return pageContent.text.length();
    }

    int low = 0;
    int high = pageContent.text.length();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (characterIsBefore(page, pageContent, middle, point))
            low = middle + 1;
        else
            high = middle;
    }
    // One past the last character is a legitimate answer, and the useful one: it
    // is where a selection dragged off the end of the text stops, and where a
    // click below the last line puts the cursor. Only a page with no text at all
    // has no answer.
    return low;
}

bool QtPdfBackend::hasTextAt(int page, QPointF point) const
{
    if (page < 0 || page >= pageCount())
        return false;

    // ---- the line box, not the characters ----
    //
    // Every character's box would say this exactly, and costs a millisecond each
    // (see characterIndexAt). The line box says it well enough and is free with
    // the page's text: a pointer is over a line when it is beside that line's
    // ink, which is what "the pointer shows an I-beam over text" means -- in the
    // gap between two words, yes; in the margin, no.
    //
    // Only the nearest line, so that a point beside the last line of a page is
    // not over the first line of whatever comes next.
    const PageText &pageContent = pageTextOf(page);
    const int line = nearestLine(pageContent.lines, point.y());
    if (line < 0)
        return false;

    const QRectF box = pageContent.lines.at(line);
    return box.adjusted(-kTextHitTolerance, -kTextHitTolerance, kTextHitTolerance,
                        kTextHitTolerance)
        .contains(point);
}

PageSelection QtPdfBackend::pageSelection(int page, int from, int to) const
{
    PageSelection result;
    if (page < 0 || page >= pageCount())
        return result;

    // A backwards or empty range is not a range. Normalising it here would turn
    // a caller's mistake into one character selected at the end it did not
    // mean, which is worse than nothing: nothing is a selection that does not
    // appear, and a wrong character is a selection that appears to be right.
    // The one caller sorts its ends before it gets here, and sorts them because
    // dragging backwards is a real gesture.
    const int start = qMax(0, from);
    const int length = to - start;
    if (length <= 0)
        return result;

    const QPdfSelection selection = document->getSelectionAtIndex(page, start, length);
    if (!selection.isValid())
        return result;

    result.text = selection.text();
    const QList<QPolygonF> bounds = selection.bounds();
    result.boxes.reserve(bounds.size());
    for (const QPolygonF &polygon : bounds)
        result.boxes.append(polygon.boundingRect());
    return result;
}

Outline QtPdfBackend::outline() const
{
    Outline tree;
    if (loadedPath.isEmpty())
        return tree;

    // Qt's bookmark model is a flat list whose rows carry their nesting level,
    // which is what outlineFromRows turns back into a tree. The Level role is
    // read rather than the model's parent()/index() because the model is flat:
    // every row is a child of the root there, and reading it as a tree would
    // return the top level and nothing else.
    QVector<OutlineRow> rows;
    const int count = bookmarks->rowCount(QModelIndex());
    rows.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = bookmarks->index(row, 0, QModelIndex());
        if (!index.isValid())
            continue;
        OutlineRow entry;
        entry.title = bookmarks->data(index, static_cast<int>(QPdfBookmarkModel::Role::Title))
                          .toString();
        entry.level = bookmarks->data(index, static_cast<int>(QPdfBookmarkModel::Role::Level))
                          .toInt();
        entry.page = bookmarks->data(index, static_cast<int>(QPdfBookmarkModel::Role::Page))
                         .toInt();
        rows.append(entry);
    }
    return outlineFromRows(rows);
}

QVector<Link> QtPdfBackend::links(int page) const
{
#ifdef XYZPDF_SHELL_HANDLER
    // Built out of the shell handlers' copy of this file -- the preview pane and
    // the thumbnail icon -- and only there.
    //
    // QPdfLink's metatype is an imported *data* symbol, and a data symbol cannot
    // be delay-loaded, which is the one thing that keeps those DLLs loadable
    // inside DllHost -- see the note on the xyzpdfpreview target in
    // CMakeLists.txt. A handler draws a page and has nothing to click, so what is
    // left out is a capability the caller cannot use rather than one it expects.
    Q_UNUSED(page)
    return {};
#else
    QVector<Link> found;
    if (page < 0 || page >= pageCount())
        return found;

    // A model of its own for every query, rather than one kept as a member.
    //
    // QPdfLinkModel::setPage moves the model to a page, so a shared one makes
    // this method a lie: it is declared const, it would mutate shared state,
    // and two callers asking about different pages would each get whatever the
    // other asked for last -- an overlay drawing the previous page's hotspots
    // while the new page is being rendered.
    //
    // The cost is a load of this page's links, which is the work the shared
    // model would have done the first time the page was asked about. If M2's
    // overlay turns out to call this on every paint, the answer is a cache
    // keyed by page -- not one model pretending to be in several places.
    QPdfLinkModel model;
    model.setDocument(document.get());
    model.setPage(page);

    const int count = model.rowCount(QModelIndex());
    found.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = model.index(row, 0, QModelIndex());
        if (!index.isValid())
            continue;
        const QVariant linkValue = model.data(index, static_cast<int>(QPdfLinkModel::Role::Link));
        if (!linkValue.canConvert<QPdfLink>())
            continue;
        const QPdfLink link = linkValue.value<QPdfLink>();
        if (!link.isValid())
            continue;

        Link entry;
        entry.box = model.data(index, static_cast<int>(QPdfLinkModel::Role::Rectangle)).toRectF();
        entry.url = link.url();
        if (!entry.url.isValid()) {
            entry.targetPage = link.page();
            entry.target = link.location();
        }
        found.append(entry);
    }
    return found;
#endif
}

DocMetadata QtPdfBackend::metadata() const
{
    DocMetadata result;
    if (loadedPath.isEmpty())
        return result;
    result.title = stringOf(document->metaData(QPdfDocument::MetaDataField::Title));
    result.author = stringOf(document->metaData(QPdfDocument::MetaDataField::Author));
    result.subject = stringOf(document->metaData(QPdfDocument::MetaDataField::Subject));
    result.keywords = stringOf(document->metaData(QPdfDocument::MetaDataField::Keywords));
    result.producer = stringOf(document->metaData(QPdfDocument::MetaDataField::Producer));
    result.creator = stringOf(document->metaData(QPdfDocument::MetaDataField::Creator));
    result.creationDate = dateOf(document->metaData(QPdfDocument::MetaDataField::CreationDate));
    result.modificationDate =
        dateOf(document->metaData(QPdfDocument::MetaDataField::ModificationDate));
    result.encrypted = neededPassword;
    return result;
}

QPdfDocument *QtPdfBackend::pdfDocument() const
{
    return document.get();
}
