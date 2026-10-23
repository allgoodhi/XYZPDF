#include "pagerenderer.h"

#include "../document/document.h"

#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

namespace {

// The token in the terms Document::render takes them.
RenderOptions optionsFor(const RenderToken &token)
{
    RenderOptions options;
    options.rotationDegrees = token.rotationDegrees;
    options.annotations = token.annotations;
    // A band of the page, when the whole of it is more pixels than anyone
    // wants. The three sizes have to agree and they are not the same number:
    // the page is scaled to token.pixels, the band is named in that scale, and
    // the image that comes back is the band's size. Passing the page's size as
    // the image size too would return a whole page -- the exact memory the
    // banding exists to avoid.
    if (!token.clip.isNull()) {
        options.clip = token.clip;
        options.scaledSize = token.pixels;
    }
    return options;
}

} // namespace

class RenderRunnable : public QRunnable
{
public:
    RenderRunnable(PageRenderer *owner, Document *document, quint64 requestId,
                   const RenderToken &token)
        : owner(owner)
        , document(document)
        , requestId(requestId)
        , token(token)
    {
    }

    void run() override
    {
        // token.imageSize(), not token.pixels: Document::render's size argument
        // is the image that comes back, while a band's coordinates are in the
        // whole page's scale (RenderOptions::scaledSize).
        const QImage image =
            document->render(token.page, token.imageSize(), optionsFor(token));

        // Back to the thread that owns pending. The runnable deletes itself when
        // this returns, so the lambda captures copies rather than `this`.
        PageRenderer *const target = owner;
        const quint64 id = requestId;
        const RenderToken returned = token;
        QMetaObject::invokeMethod(
            target, [target, id, returned, image] { target->finishRender(id, returned, image); },
            Qt::QueuedConnection);
    }

private:
    PageRenderer *owner;
    Document *document;
    quint64 requestId;
    RenderToken token;
};

PageRenderer::PageRenderer(QObject *parent)
    : QObject(parent)
    , pool(new QThreadPool(this))
{
    // One render at a time. A second one would only wait on the backend's own
    // lock, and keeping the queue here makes inFlight() mean "pages not yet on
    // screen" rather than "pages queued behind a busy lock".
    pool->setMaxThreadCount(1);
}

PageRenderer::~PageRenderer()
{
    // A render already running cannot be cancelled -- the backend has no way to
    // stop one -- so wait for it. Queued jobs have not started, and clearing
    // them means no new work begins after this point.
    pool->clear();
    pool->waitForDone();
}

void PageRenderer::setDocument(Document *document)
{
    currentDocument = document;
    // Everything asked for against the previous document is now an answer to a
    // question nobody asked. Bumping here rather than leaving it to the caller
    // means a caller cannot forget.
    beginGeneration();
}

Document *PageRenderer::document() const
{
    return currentDocument;
}

quint64 PageRenderer::beginGeneration()
{
    ++currentGeneration;
    // Requests already running are not cancelled -- there is no cancel. They are
    // dropped when they arrive, which costs the render itself and nothing else;
    // the tokens are kept so that each one can be reported as discarded rather
    // than being forgotten about.
    return currentGeneration;
}

bool PageRenderer::request(const RenderToken &token)
{
    if (!currentDocument || !token.isValid())
        return false;
    if (token.generation != currentGeneration)
        return false;

    const quint64 requestId = nextRequestId++;
    // Inserted before the runnable starts, so the answer has somewhere to land
    // even if the render is instantaneous. The answer is posted to this thread,
    // which is still inside request() until it returns, so finishRender() cannot
    // run before this line either.
    pending.insert(requestId, token);
    pool->start(new RenderRunnable(this, currentDocument, requestId, token));
    return true;
}

void PageRenderer::finishRender(quint64 requestId, const RenderToken &token, const QImage &image)
{
    const auto found = pending.find(requestId);
    if (found == pending.end()) {
        // Nothing was asked for under this id. Reported rather than ignored, so
        // that a bookkeeping mistake shows up as a discard instead of as a page
        // that silently never appears.
        RenderToken orphan;
        orphan.page = token.page;
        orphan.pixels = token.pixels;
        Q_EMIT discarded(orphan);
        return;
    }

    pending.erase(found);

    if (!token.isCurrent(currentGeneration)) {
        Q_EMIT discarded(token);
        return;
    }
    Q_EMIT rendered(token, image);
}
