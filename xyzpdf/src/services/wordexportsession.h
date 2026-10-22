// The Word export of one open document: the whole document's text read page by
// page off the UI thread, with progress and a cancel.
//
// Cut out of DocumentTab (docs/review-03.md §1.2) because it is the same job the
// search session is -- take the document, spend minutes on it, stay cancellable,
// and above all never be still reading when the document underneath is replaced.
// The arrangement is identical for the same reason: a flag set first so the run
// stops at the next page, then the wait.
//
// It runs off the UI thread because every page of it is a text call and a
// selection under the same lock the renderer takes, so an export on the thread
// that draws is a window that stops repainting until it is done.
//
// One export at a time: a second while one is running is refused rather than
// queued, because they would be writing the same file.
#pragma once

#include "../export/wordexport.h"

#include <QFuture>
#include <QObject>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;
class QAtomicInt;

class WordExportSession : public QObject
{
    Q_OBJECT

public:
    // `document` answers with the document to read, or null when the tab has
    // none: an export started against a document that is not there has nothing
    // to write.
    explicit WordExportSession(std::function<Document *()> document, QObject *parent = nullptr);
    ~WordExportSession() override;

    // Writes the whole document's text out as a Word file. Does nothing without
    // a document, without a name, or while one is already running.
    void exportToWord(const QString &path);
    bool isRunning() const;
    // The reader has asked for it to stop, through the window's progress box.
    // The same flag the destructor sets, and for the same reason: the export
    // looks at it between pages, which is the only place it can look -- and
    // because the file is written at the very end, an export stopped this way
    // leaves nothing behind to explain.
    void cancel();

    // ---- the wait before the document is replaced ----
    // See SearchSession's pair of the same names, and DocumentTab::drainRenders.
    void cancelPending();
    void waitUntilFinished();

Q_SIGNALS:
    // How far the export has got: pages written of pages in the document. Sent
    // from the worker thread, once per page -- a page with a picture on it is a
    // render, and a scanned document is minutes of work nobody can see.
    void progress(int done, int total);
    // What an export wrote, or why it did not.
    void reportFinished(const WordExportReport &report);

private:
    // Emits the progress signal, and only ever runs on this object's thread: the
    // export reports from a worker, and this is how the report gets here.
    void notifyProgress(int done, int total);

    std::function<Document *()> documentProvider;
    // It reads the document page by page, and a reload under it is the crash
    // drainRenders() exists to prevent. Its flag is set first, so that the wait
    // is one page rather than a whole document.
    QFuture<WordExportReport> wordExportTask;
    QSharedPointer<QAtomicInt> exportCancel;
};
