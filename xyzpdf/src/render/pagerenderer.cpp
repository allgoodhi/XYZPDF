#include "pagerenderer.h"

#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfPageRenderer>

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

} // namespace

PageRenderer::PageRenderer(QObject *parent)
    : QObject(parent)
    , renderer(new QPdfPageRenderer(this))
{
    // MultiThreaded is Qt's own thread pool. It does not render two pages at
    // once -- every PDFium call goes through one global lock, see the note in
    // the header -- but it does move the work off the thread that draws the
    // window, which is the whole point. SingleThreaded renders on this object's
    // thread, which is the UI thread, and would make this class pointless.
    renderer->setRenderMode(QPdfPageRenderer::RenderMode::MultiThreaded);

    connect(renderer, &QPdfPageRenderer::pageRendered, this, &PageRenderer::onPageRendered);
}

PageRenderer::~PageRenderer() = default;

void PageRenderer::setDocument(QPdfDocument *document)
{
    currentDocument = document;
    renderer->setDocument(document);
    // Everything asked for against the previous document is now an answer to a
    // question nobody asked. Bumping here rather than leaving it to the caller
    // means a caller cannot forget.
    beginGeneration();
}

QPdfDocument *PageRenderer::document() const
{
    return currentDocument;
}

quint64 PageRenderer::beginGeneration()
{
    ++currentGeneration;
    // Requests already handed to Qt are not cancelled -- there is no cancel.
    // They are dropped when they arrive, which costs the render itself and
    // nothing else; the tokens are kept so that each one can be reported as
    // discarded rather than being forgotten about.
    return currentGeneration;
}

bool PageRenderer::request(const RenderToken &token)
{
    if (!currentDocument || !token.isValid())
        return false;
    if (token.generation != currentGeneration)
        return false;

    QPdfDocumentRenderOptions options;
    options.setRotation(rotationFromDegrees(token.rotationDegrees));
    QPdfDocumentRenderOptions::RenderFlags flags = QPdfDocumentRenderOptions::RenderFlag::None;
    if (token.annotations)
        flags |= QPdfDocumentRenderOptions::RenderFlag::Annotations;
    options.setRenderFlags(flags);
    // A band of the page, when the whole of it is more pixels than anyone
    // wants. The three sizes have to agree and they are not the same number:
    // the page is scaled to token.pixels, the band is named in that scale, and
    // the image that comes back is the band's size. Passing the page's size as
    // the image size too would return a whole page -- the exact memory the
    // banding exists to avoid.
    if (!token.clip.isNull()) {
        options.setScaledSize(token.pixels);
        options.setScaledClipRect(token.clip);
    }

    const quint64 requestId = renderer->requestPage(token.page, token.imageSize(), options);
    pending.insert(requestId, token);
    return true;
}

void PageRenderer::onPageRendered(int page, QSize imageSize, const QImage &image,
                                  const QPdfDocumentRenderOptions &options, quint64 requestId)
{
    Q_UNUSED(options);

    const auto found = pending.find(requestId);
    if (found == pending.end()) {
        // Nothing was asked for under this id. Reported rather than ignored, so
        // that a bookkeeping mistake shows up as a discard instead of as a page
        // that silently never appears.
        RenderToken orphan;
        orphan.page = page;
        orphan.pixels = imageSize;
        Q_EMIT discarded(orphan);
        return;
    }

    const RenderToken token = found.value();
    pending.erase(found);

    if (!token.isCurrent(currentGeneration)) {
        Q_EMIT discarded(token);
        return;
    }
    Q_EMIT rendered(token, image);
}
