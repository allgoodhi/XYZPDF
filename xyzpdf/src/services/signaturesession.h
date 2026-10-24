// Verifying a document's digital signatures without stopping the window.
//
// The job is SignatureVerify::verifyDocument() and this is the thread it runs on plus the three
// questions the rest of the tab asks: is it still running, is it finished, and what did it answer.
//
// ---- why it is waited for ----
//
// It opens the file again -- once through PDFium for the signature dictionaries, once through
// qpdf for the field names and pages -- so it is a reader of the document, and AGENTS.md's rule
// for a new service is "凡是读文档的都算": DocumentTab's drainRenders() and its destructor both
// cancel and wait. The alternative is a tab whose document goes away under a parse that is
// halfway through it.
//
// The work itself holds no Document pointer: the path and the password are copied in, which is
// what OcrSession and TranslateSession do for the same reason. What it does take is the process's
// one PDFium lock, briefly, for the signature-dictionary read -- on a worker thread, so the UI
// thread never waits for it, and the render path waits for it no longer than one getter loop over
// a handful of objects.
//
// ---- the cost, stated ----
//
// Verifying a signature means holding the bytes it covers, which for a signed document is nearly
// the whole file: the detached verifier Windows provides has no streaming form (measured -- the
// CryptMsg route wants the encoded message, not the external content). A five-hundred-page scan
// therefore allocates once, on this thread, per signature. That is the price of being able to say
// 内容被改过 with a straight face.
#pragma once

#include "../document/signatureverify.h"

#include <QObject>
#include <QFuture>
#include <QString>
#include <QVector>

#include <functional>

class SignatureSession : public QObject
{
    Q_OBJECT

public:
    explicit SignatureSession(QObject *parent = nullptr);

    // Verifies one file. Called when the document that was just opened says it carries
    // signatures -- an unsigned document is not worth a thread or a second parse, and the caller
    // already has that answer from the backend.
    void start(const QString &path, const QString &password);

    bool running() const;
    // Forgets the previous document's answer. Called by every open(), because an answer about the
    // file that was there a moment ago is not an answer about this one -- and "no summary yet"
    // has to be the state a new document starts in, not the old document's last sentence.
    void reset();
    // Whether a verification has ever come back for this tab. It is what lets the sidebar decide
    // to show the 签名 page: a document that has not been answered about yet keeps the page it had,
    // and one that was answered with nothing does not grow the tab.
    bool answered() const { return verificationAnswered; }
    const QVector<SignatureVerify::Signature> &signatures() const { return results; }
    int countWithVerdict(SignatureVerify::Verdict verdict) const;

    // The reader's setting for the revocation question, asked at the moment a verification starts.
    // DocumentTab supplies it so that this class does not include the settings header and a test
    // can answer it without writing a preference.
    std::function<bool()> revocationCheckWanted;

    // The two-step wait, in this order, for drainRenders() and the destructor. A verification
    // cannot be cut short mid-blob -- it is one call into Windows -- so "cancel" here means "do
    // not accept a result that is coming back", and the wait is the half that matters.
    void cancelPending();
    void waitUntilFinished();

Q_SIGNALS:
    // The answer is in. Emitted for every finished verification, including the one that found
    // nothing checkable, so the tab can redraw the panel and replace the status line whether or
    // not there were signatures to report.
    void verified();

private:
    QVector<SignatureVerify::Signature> results;
    QVector<QFuture<QVector<SignatureVerify::Signature>>> tasks;
    bool verificationAnswered = false;
    // Which verification the incoming answer has to belong to. Compared by identity rather than by
    // a counter, because a reload hands the next answer to the tab that did not ask for it -- the
    // same guard SearchSession uses for a scan that was overtaken.
    QString expectedPath;
};
