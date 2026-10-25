#include "excelexportsession.h"

#include "../document/document.h"

#include <QAtomicInt>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

ExcelExportSession::ExcelExportSession(std::function<Document *()> document, QObject *parent)
    : QObject(parent)
    , documentProvider(std::move(document))
{
}

ExcelExportSession::~ExcelExportSession() = default;

void ExcelExportSession::exportToExcel(const QString &path, bool exportText)
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document || path.isEmpty() || excelExportTask.isRunning())
        return;

    // A flag of its own, handed to the worker: set when the document is about to
    // be replaced (the tab's open(), and its destructor), so the export stops at
    // the page it is on rather than reading a document being torn down.
    exportCancel = QSharedPointer<QAtomicInt>::create(0);
    const QSharedPointer<QAtomicInt> cancel = exportCancel;

    auto *watcher = new QFutureWatcher<ExcelExportReport>(this);
    connect(watcher, &QFutureWatcher<ExcelExportReport>::finished, this, [this, watcher] {
        const ExcelExportReport report = watcher->result();
        watcher->deleteLater();
        Q_EMIT reportFinished(report);
    });

    ExcelExportOptions options;
    options.cancel = cancel;
    options.exportText = exportText;
    // Progress arrives from the worker thread, so it is handed to this object's
    // thread rather than run where it is called. Queued through a QPointer,
    // because by the time the message arrives the tab -- and this session -- may
    // be gone.
    QPointer<ExcelExportSession> alive(this);
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

    // Qualified: this class has a method of nearly the same name, and the free
    // function is the one that does the reading.
    excelExportTask = QtConcurrent::run(
        [document, path, options] { return ::exportToExcel(*document, path, options); });
    watcher->setFuture(excelExportTask);
}

void ExcelExportSession::notifyProgress(int done, int total)
{
    Q_EMIT progress(done, total);
}

bool ExcelExportSession::isRunning() const
{
    return excelExportTask.isRunning();
}

void ExcelExportSession::cancel()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void ExcelExportSession::cancelPending()
{
    if (exportCancel)
        exportCancel->storeRelaxed(1);
}

void ExcelExportSession::waitUntilFinished()
{
    // The export reads the same document the view renders, by the same means, so
    // it is cancelled and waited for before that document goes away -- and it is
    // the one that would take longest to notice, because a document is a lot of
    // pages to read.
    if (excelExportTask.isRunning())
        excelExportTask.waitForFinished();
}
