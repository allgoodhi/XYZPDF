#include "pptexportsession.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

PptExportSession::PptExportSession(std::function<Document *()> document, QObject *parent)
    : QObject(parent)
    , documentProvider(std::move(document))
{
}

PptExportSession::~PptExportSession() = default;

void PptExportSession::exportToPowerPoint(const QString &path)
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document || path.isEmpty() || powerPointExportTask.isRunning())
        return;

    exportCancel = QSharedPointer<QAtomicInt>::create(0);
    const QSharedPointer<QAtomicInt> cancel = exportCancel;

    auto *watcher = new QFutureWatcher<PptExportReport>(this);
    connect(watcher, &QFutureWatcher<PptExportReport>::finished, this, [this, watcher] {
        const PptExportReport report = watcher->result();
        watcher->deleteLater();
        Q_EMIT reportFinished(report);
    });

    PptExportOptions options;
    options.cancel = cancel;
    QPointer<PptExportSession> alive(this);
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

    powerPointExportTask = QtConcurrent::run(
        [document, path, options] { return ::exportToPowerPoint(*document, path, options); });
    watcher->setFuture(powerPointExportTask);
}

void PptExportSession::notifyProgress(int done, int total)
{
    Q_EMIT progress(done, total);
}

bool PptExportSession::isRunning() const
{
    return powerPointExportTask.isRunning();
}

void PptExportSession::cancel()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void PptExportSession::cancelPending()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void PptExportSession::waitUntilFinished()
{
    if (powerPointExportTask.isRunning())
        powerPointExportTask.waitForFinished();
}
