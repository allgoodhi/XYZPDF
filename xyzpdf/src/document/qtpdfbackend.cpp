#include "qtpdfbackend.h"

#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfLink>
#include <QPdfLinkModel>
#include <QPdfSelection>

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>

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

// Whether the character at `index` comes before `point` in reading order.
//
// Monotone in `index`, which is what makes the bisection in characterIndexAt()
// valid: an earlier line is before the point, a later one is after it, and on
// the same line it is the horizontal position that decides.
bool characterIsBefore(QPdfDocument *document, int page, int index, QPointF point)
{
    const QRectF box = characterBox(document, page, index);
    if (box.isNull())
        return true; // nothing there, so nothing after the point either
    if (box.bottom() < point.y())
        return true;
    if (box.top() > point.y())
        return false;
    return box.center().x() < point.x();
}

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

    // The password goes in before the load, not after a failure: Qt PDF reads
    // it once while opening the file, and setting it afterwards reloads the
    // whole document a second time.
    document->setPassword(password);

    const QPdfDocument::Error error = document->load(path);
    if (error != QPdfDocument::Error::None) {
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

int QtPdfBackend::characterIndexAt(int page, QPointF point) const
{
    if (page < 0 || page >= pageCount())
        return -1;

    const QString pageText = text(page);
    if (pageText.isEmpty())
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
    // So the index is found here, by bisection over the characters' own boxes,
    // which are exact and always available. Twelve queries on a page of three
    // thousand characters, each one a lookup in a text page PDFium already has
    // in memory.
    int low = 0;
    int high = pageText.length();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (characterIsBefore(document.get(), page, middle, point))
            low = middle + 1;
        else
            high = middle;
    }
    // One past the last character is a legitimate answer, and the useful one: it
    // is where a selection dragged off the end of the text stops, and where a
    // click below the last line puts the cursor. Only a page with no text at
    // all has no answer.
    return low;
}

bool QtPdfBackend::hasTextAt(int page, QPointF point) const
{
    const QString pageText = text(page);
    if (pageText.isEmpty())
        return false;

    const int index = characterIndexAt(page, point);
    if (index < 0)
        return false;

    // The index is a boundary, not a glyph: the search returns the first
    // character that starts after the point, so the point is between that one
    // and the one before it. Which of the two it is inside depends on where in
    // the glyph it fell -- a click on the right half of a letter is a click
    // before the next one -- so both are asked, and that is also what covers
    // the gap between two words, where neither box contains the point but both
    // are within the tolerance.
    //
    // Past the end of the page's text there is nothing after it, and the
    // character before is the line the pointer is just off the end of.
    for (const int candidate : {index, index - 1}) {
        if (candidate < 0 || candidate >= pageText.length())
            continue;
        const QRectF box = characterBox(document.get(), page, candidate);
        if (box.isNull())
            continue; // a space: no ink, no box, and the character beside it answers
        if (box.adjusted(-kTextHitTolerance, -kTextHitTolerance, kTextHitTolerance,
                         kTextHitTolerance)
                .contains(point)) {
            return true;
        }
    }
    return false;
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
