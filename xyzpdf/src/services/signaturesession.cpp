#include "signaturesession.h"

#include <QFutureWatcher>
#include <QtConcurrent>

SignatureSession::SignatureSession(QObject *parent)
    : QObject(parent)
{
}

void SignatureSession::start(const QString &path, const QString &password)
{
    if (path.isEmpty())
        return;
    expectedPath = path;
    verificationAnswered = false;

    // Read the setting here, on the thread that owns the question, so that the worker never
    // touches QSettings and an answer cannot change halfway through a verification.
    SignatureVerify::Options options;
    options.checkRevocationOnline = revocationCheckWanted ? revocationCheckWanted() : false;

    const QString capturedPath = path;
    const QString capturedPassword = password;
    auto *watcher = new QFutureWatcher<QVector<SignatureVerify::Signature>>(this);
    connect(watcher, &QFutureWatcher<QVector<SignatureVerify::Signature>>::finished, this,
            [this, watcher, capturedPath] {
                const QVector<SignatureVerify::Signature> found = watcher->result();
                watcher->deleteLater();
                // An answer to a question the tab has stopped asking: a reload during a
                // verification means this result describes a file the reader is no longer looking
                // at, and putting it in the panel would be a claim about the wrong document.
                if (capturedPath != expectedPath)
                    return;
                results = found;
                verificationAnswered = true;
                Q_EMIT verified();
            });

    QFuture<QVector<SignatureVerify::Signature>> task = QtConcurrent::run(
        [capturedPath, capturedPassword, options] {
            return SignatureVerify::verifyDocument(capturedPath, capturedPassword, options);
        });
    watcher->setFuture(task);
    tasks.append(task);
}

void SignatureSession::reset()
{
    results.clear();
    verificationAnswered = false;
}

bool SignatureSession::running() const
{
    for (const QFuture<QVector<SignatureVerify::Signature>> &task : tasks) {
        if (!task.isFinished())
            return true;
    }
    return false;
}

int SignatureSession::countWithVerdict(SignatureVerify::Verdict verdict) const
{
    return SignatureVerify::countWithVerdict(results, verdict);
}

void SignatureSession::cancelPending()
{
    // The path is what makes a late answer recognisable as late, so clearing it is the whole of
    // "do not accept what is coming back". The work itself still runs to completion, and
    // waitUntilFinished() is what makes that safe.
    expectedPath.clear();
}

void SignatureSession::waitUntilFinished()
{
    for (QFuture<QVector<SignatureVerify::Signature>> &task : tasks)
        task.waitForFinished();
}
