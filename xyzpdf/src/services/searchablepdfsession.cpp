#include "searchablepdfsession.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

SearchablePdfSession::SearchablePdfSession(std::function<Document *()> document,
                                           std::function<QString()> source,
                                           std::function<QString()> password, QObject *parent)
    : QObject(parent)
    , documentProvider(std::move(document))
    , sourceProvider(std::move(source))
    , passwordProvider(std::move(password))
{
}

SearchablePdfSession::~SearchablePdfSession() = default;

void SearchablePdfSession::makeSearchable(const QString &destination)
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document || destination.isEmpty() || makeTask.isRunning())
        return;

    cancelFlag = QSharedPointer<QAtomicInt>::create(0);
    const QSharedPointer<QAtomicInt> cancel = cancelFlag;
    const QString source = sourceProvider ? sourceProvider() : QString();
    const QString password = passwordProvider ? passwordProvider() : QString();

    auto *watcher = new QFutureWatcher<SearchablePdfReport>(this);
    connect(watcher, &QFutureWatcher<SearchablePdfReport>::finished, this, [this, watcher] {
        const SearchablePdfReport report = watcher->result();
        watcher->deleteLater();
        Q_EMIT reportFinished(report);
    });

    SearchablePdfOptions options;
    options.cancel = cancel;
    // Progress comes from the worker thread, so it is handed to this object's thread
    // rather than run where it is called -- through a QPointer, because by the time the
    // message arrives the tab, and with it this session, may be gone.
    QPointer<SearchablePdfSession> alive(this);
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

    // Qualified: this class has a method of the same name, and the free function is the
    // one that does the reading.
    makeTask = QtConcurrent::run([document, source, password, destination, options] {
        return ::makeSearchablePdf(*document, source, password, destination, options);
    });
    watcher->setFuture(makeTask);
}

void SearchablePdfSession::notifyProgress(int done, int total)
{
    Q_EMIT progress(done, total);
}

bool SearchablePdfSession::isRunning() const
{
    return makeTask.isRunning();
}

void SearchablePdfSession::cancel()
{
    if (cancelFlag)
        cancelFlag->storeRelaxed(1);
}

void SearchablePdfSession::cancelPending()
{
    if (cancelFlag)
        cancelFlag->storeRelaxed(1);
}

void SearchablePdfSession::waitUntilFinished()
{
    if (makeTask.isRunning())
        makeTask.waitForFinished();
}
