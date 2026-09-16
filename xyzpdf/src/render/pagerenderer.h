// Rendering off the UI thread, through Qt PDF's own page renderer.
//
// ---- why not just call QPdfDocument::render() ----
//
// It is synchronous. On a 500-page scan, one page is tens of milliseconds on a
// fast machine and hundreds on a slow one, and it happens on the thread that
// draws the window. That thread being busy is a window that does not repaint, a
// scrollbar that does not move, and a title bar that Windows draws grey.
//
// ---- what MultiThreaded actually buys, and what it does not ----
//
// QPdfDocument is not thread safe, and Qt PDF does not fix that by giving each
// thread its own document. It fixes it with one global recursive mutex:
// QPdfMutexLocker, in QtPdf/private/qpdfdocument_p.h, is a
// std::unique_lock<QRecursiveMutex> that every FPDF call goes through. There
// is one document object and one lock, not a copy per thread.
//
// Two consequences worth having written down, because both are easy to assume
// away:
//
//   * MultiThreaded does not run two renders at once. It moves rendering off
//     the thread that draws the window, which is the point -- a busy UI thread
//     is a window that does not repaint -- but inside PDFium one call runs at
//     a time, so there is no multi-core speedup to be had here. Do not promise
//     one.
//
//   * The UI thread can be blocked by a render in progress. pageSize(), text(),
//     textRuns(), links() and metadata() all take that same lock. A 300 ms
//     scanned page being rendered means a 300 ms stall for anything on the UI
//     thread that asks the document a question -- which is why M2's text layer
//     and search have to move off it (see the note in document.h) rather than
//     trusting the lock to keep things responsive.
//
// Doing it by hand -- a worker thread calling into the same QPdfDocument -- is
// what this class exists to avoid: it is not a crash, it is a race that shows
// up under load and not before.
#pragma once

#include "rendertoken.h"

#include <QHash>
#include <QImage>
#include <QObject>

class QPdfDocument;
class QPdfDocumentRenderOptions;
class QPdfPageRenderer;

class PageRenderer : public QObject
{
    Q_OBJECT

public:
    explicit PageRenderer(QObject *parent = nullptr);
    ~PageRenderer() override;

    // The document to render from. Not owned: the caller keeps it, and must
    // keep it alive for as long as this object is asked to render. In
    // DocumentTab that is arranged by declaration order -- the backend is
    // declared first and so destroyed last.
    void setDocument(QPdfDocument *document);
    QPdfDocument *document() const;

    // Starts a new generation and returns it.
    //
    // Every request carries the generation it was made in, and results from
    // anything older are reported as discarded rather than rendered. Called on
    // every change that makes work in flight pointless: a new document, a new
    // zoom, a rotation.
    quint64 beginGeneration();
    quint64 generation() const { return currentGeneration; }

    // Queues one page. False when the token asks for nothing renderable, or
    // when no document has been set.
    bool request(const RenderToken &token);

    // How many results are still on their way. The view uses it to know whether
    // it is waiting for something, and the tests use it to know that a busy
    // question has been answered.
    int inFlight() const { return pending.size(); }

Q_SIGNALS:
    // A page that is still wanted. The receiver checks the token against its
    // own idea of the current generation before drawing -- the renderer drops
    // what it can tell is stale, but the last word belongs to whoever is about
    // to paint.
    void rendered(const RenderToken &token, const QImage &image);

    // A page that arrived too late, emitted so that the drop is observable
    // rather than invisible: render_smoke asserts on it, and without it the
    // only evidence that the rule works is that nothing bad is on screen.
    void discarded(const RenderToken &token);

private:
    void onPageRendered(int page, QSize imageSize, const QImage &image,
                        const QPdfDocumentRenderOptions &options, quint64 requestId);

    QPdfPageRenderer *renderer = nullptr;
    QPdfDocument *currentDocument = nullptr;
    quint64 currentGeneration = 1;
    // Which token each outstanding request was for, so that what comes back can
    // be told apart. Qt hands back a request id that says nothing about the
    // generation it was asked in.
    QHash<quint64, RenderToken> pending;
};
