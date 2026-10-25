// One open document turned into a searchable copy, off the UI thread.
//
// The same arrangement as WordExportSession, for the same reasons: every page of it is
// a content-stream read and a render under the lock the view uses, so it cannot run on
// the thread that draws; it is cancelled with a flag between pages and then waited for,
// so a document that is about to be replaced cannot be freed under a page still being
// read; and a second run while one is going is refused rather than queued, because the
// two would be writing the same file.
//
// What it adds to that shape is the source name and the password: the writer reopens the
// file through qpdf by path, and an encrypted document has to be opened with the key the
// reader already typed.
#pragma once

#include "../export/searchablepdf.h"

#include <QFuture>
#include <QObject>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;
class QAtomicInt;

class SearchablePdfSession : public QObject
{
    Q_OBJECT

public:
    // `document` answers with the document to read, or null when the tab has none.
    // `source` and `password` answer for the file behind it at the moment the work
    // starts -- a tab's path and the key it was opened with.
    SearchablePdfSession(std::function<Document *()> document, std::function<QString()> source,
                         std::function<QString()> password, QObject *parent = nullptr);
    ~SearchablePdfSession() override;

    // Writes `destination`. Does nothing without a document, without a name, or while
    // one is already running.
    void makeSearchable(const QString &destination);
    bool isRunning() const;
    // The reader has asked for it to stop, through the window's progress box.
    void cancel();

    // ---- the wait before the document is replaced ----
    // See SearchSession's pair of the same names, and DocumentTab::drainRenders.
    void cancelPending();
    void waitUntilFinished();

Q_SIGNALS:
    // Pages done of pages in the document. Sent from the worker thread.
    void progress(int done, int total);
    // What was written, or why nothing was.
    void reportFinished(const SearchablePdfReport &report);

private:
    void notifyProgress(int done, int total);

    std::function<Document *()> documentProvider;
    std::function<QString()> sourceProvider;
    std::function<QString()> passwordProvider;
    QFuture<SearchablePdfReport> makeTask;
    QSharedPointer<QAtomicInt> cancelFlag;
};
