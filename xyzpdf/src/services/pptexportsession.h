// The PowerPoint export of one open document: the whole document read page by page
// off the UI thread, with progress and a cancel.
//
// The arrangement is WordExportSession's, and for the same reasons: a flag set
// first so the run stops at the next page, then the wait; progress handed back to
// this object's thread because every page image is a render; one export at a time,
// because a second would be writing the same file.
#pragma once

#include "../export/pptexport.h"

#include <QFuture>
#include <QObject>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;
class QAtomicInt;

class PptExportSession : public QObject
{
    Q_OBJECT

public:
    explicit PptExportSession(std::function<Document *()> document, QObject *parent = nullptr);
    ~PptExportSession() override;

    // Writes the document out as a presentation. Does nothing without a document,
    // without a name, or while one is already running.
    void exportToPowerPoint(const QString &path);
    bool isRunning() const;
    // The reader asked it to stop. The file is written only at the very end, so an
    // export stopped this way leaves nothing behind.
    void cancel();

    // The wait before the document is replaced; see DocumentTab::drainRenders.
    void cancelPending();
    void waitUntilFinished();

Q_SIGNALS:
    // Pages written of pages in the document. Sent from the worker thread, once
    // per page.
    void progress(int done, int total);
    void reportFinished(const PptExportReport &report);

private:
    void notifyProgress(int done, int total);

    std::function<Document *()> documentProvider;
    QFuture<PptExportReport> powerPointExportTask;
    QSharedPointer<QAtomicInt> exportCancel;
};
