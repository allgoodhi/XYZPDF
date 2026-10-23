// Rendering off the UI thread, one page at a time.
//
// ---- why not just call Document::render() ----
//
// It is synchronous. On a 500-page scan, one page is tens of milliseconds on a
// fast machine and hundreds on a slow one, and it happens on the thread that
// draws the window. That thread being busy is a window that does not repaint, a
// scrollbar that does not move, and a title bar that Windows draws grey.
//
// ---- what the worker buys, and what it does not ----
//
// The pool has one thread, so two renders do not run at once; they run off the
// thread that draws the window, which is the point. Moving work here does not
// make a backend safe to ask from two threads at once -- that promise belongs to
// the backend, and is made in its own header.
//
// The backend makes it in the shape PDFium's own header asks for: one global
// recursive mutex that every call into the PDF library goes through, one document
// object, not a copy per thread (pdfiumbackend.cpp, pdfiumMutex()). Two
// consequences worth having written down, because both are easy to assume away:
//
//   * One call runs at a time inside PDFium, so there is no multi-core speedup
//     to be had here. Do not promise one.
//
//   * The UI thread can be blocked by a render in progress. pageSize(), text(),
//     textRuns(), links() and metadata() all take that same lock. A 300 ms
//     scanned page being rendered means a 300 ms stall for anything on the UI
//     thread that asks the document a question -- which is why M2's text layer
//     and search have to move off it (see the note in document.h) rather than
//     trusting the lock to keep things responsive.
//
// The worker here is not a place to fix a backend that cannot be asked from two
// threads. A backend that cannot make the promise has to say so, not rely on
// this pool happening to have one thread.
#pragma once

#include "rendertoken.h"

#include <QHash>
#include <QImage>
#include <QObject>

class Document;
class QThreadPool;
class RenderRunnable;

class PageRenderer : public QObject
{
    Q_OBJECT

public:
    explicit PageRenderer(QObject *parent = nullptr);
    ~PageRenderer() override;

    // The document to render from. Not owned: the caller keeps it, and must
    // keep it alive for as long as this object is asked to render. In
    // DocumentTab that is arranged by declaration order -- the backend is
    // declared first and so destroyed last -- and the destructor below ends
    // "asked to render" by clearing queued work and waiting for the render that
    // is already running.
    void setDocument(Document *document);
    Document *document() const;

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
    friend class RenderRunnable;

    // Called on this object's thread when a render comes back. The runnable
    // posts it here rather than emitting from the worker, so that pending and
    // the generation are only ever touched by one thread.
    void finishRender(quint64 requestId, const RenderToken &token, const QImage &image);

    QThreadPool *pool = nullptr;
    Document *currentDocument = nullptr;
    quint64 currentGeneration = 1;
    quint64 nextRequestId = 1;
    // Which token each outstanding request was for, so that what comes back can
    // be told apart. Qt PDF used to hand back an id of its own; this class hands
    // out the number now, which is why it says nothing about the generation and
    // the token still has to be kept.
    QHash<quint64, RenderToken> pending;
};
