// The Excel export of one open document: every page read for tables off the UI
// thread, with progress and a cancel.
//
// The arrangement is WordExportSession's, and for the same reasons: the export
// reads the same document the renderer reads, so it is cancelled and waited for
// before the document underneath is replaced; it runs off the UI thread because
// every page is a text and graphics call under the same lock the renderer takes;
// and one export at a time, because two would be writing the same file.
#pragma once

#include "../export/excelexport.h"

#include <QFuture>
#include <QObject>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;
class QAtomicInt;

class ExcelExportSession : public QObject
{
    Q_OBJECT

public:
    // `document` answers with the document to read, or null when the tab has none.
    explicit ExcelExportSession(std::function<Document *()> document, QObject *parent = nullptr);
    ~ExcelExportSession() override;

    // Writes the tables of the whole document out as a workbook. Does nothing
    // without a document, without a name, or while one is already running.
    void exportToExcel(const QString &path, bool exportText);
    bool isRunning() const;
    void cancel();

    // ---- the wait before the document is replaced ----
    // See DocumentTab::drainRenders.
    void cancelPending();
    void waitUntilFinished();

Q_SIGNALS:
    // How far the export has got: pages read of pages in the document. Sent from
    // the worker thread, once per page.
    void progress(int done, int total);
    // What an export wrote, or why it did not.
    void reportFinished(const ExcelExportReport &report);

private:
    void notifyProgress(int done, int total);

    std::function<Document *()> documentProvider;
    QFuture<ExcelExportReport> excelExportTask;
    QSharedPointer<QAtomicInt> exportCancel;
};
