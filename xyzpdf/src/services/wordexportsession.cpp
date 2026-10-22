#include "wordexportsession.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

WordExportSession::WordExportSession(std::function<Document *()> document, QObject *parent)
    : QObject(parent)
    , documentProvider(std::move(document))
{
}

WordExportSession::~WordExportSession() = default;

void WordExportSession::exportToWord(const QString &path)
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document || path.isEmpty() || wordExportTask.isRunning())
        return;

    // A flag of its own, handed to the worker. When the document is about
    // to be replaced -- the tab's open(), and its destructor -- it is set, and
    // the export stops at the page it is on rather than reading a document that
    // is being torn down under it. The same arrangement the search uses, for the
    // same reason; see DocumentTab::drainRenders().
    exportCancel = QSharedPointer<QAtomicInt>::create(0);
    const QSharedPointer<QAtomicInt> cancel = exportCancel;

    auto *watcher = new QFutureWatcher<WordExportReport>(this);
    connect(watcher, &QFutureWatcher<WordExportReport>::finished, this, [this, watcher] {
        const WordExportReport report = watcher->result();
        watcher->deleteLater();
        Q_EMIT reportFinished(report);
    });

    WordExportOptions options;
    options.cancel = cancel;
    // Progress comes from the worker thread, so it is handed to this object's
    // thread rather than run where it is called: a page image is a render, a
    // scanned document is minutes of them, and the window has to keep painting
    // while they happen. Queued through a QPointer, because by the time the
    // message arrives the tab -- and with it this session -- may be gone.
    QPointer<WordExportSession> alive(this);
    options.progress = [alive](int done, int total) {
        if (!alive)
            return;
        QMetaObject::invokeMethod(
            alive.data(),
            [alive, done, total] {
                if (alive)
                    alive->notifyProgress(done, total);
            },
            Qt::QueuedConnection);
    };

    // Qualified: this class has a method of the same name, which is the one the
    // window calls, and the free function is the one that does the reading.
    wordExportTask = QtConcurrent::run(
        [document, path, options] { return ::exportToWord(*document, path, options); });
    watcher->setFuture(wordExportTask);
}

void WordExportSession::notifyProgress(int done, int total)
{
    Q_EMIT progress(done, total);
}

bool WordExportSession::isRunning() const
{
    return wordExportTask.isRunning();
}

void WordExportSession::cancel()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void WordExportSession::cancelPending()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void WordExportSession::waitUntilFinished()
{
    // The export reads the same document the view is rendering, by the same
    // means, so it is cancelled and waited for before that document goes away
    // for the same reason as a scan -- and it is the one that would take longest
    // to notice, because a document is a lot of pages to read.
    if (wordExportTask.isRunning())
        wordExportTask.waitForFinished();
}
