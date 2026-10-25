// What the signature checker answers about files whose signatures were made for this test.
//
// Every sample here is signed during the run, by a nameless CNG key that never reaches the disk and
// a certificate that never reaches a store -- see the header of tests/signaturesample.h. The
// point of that construction is that the four verdicts below cannot be produced by this
// machine's own trust list: the "有效" row is only reachable through a chain engine whose root
// store the test injected, and the very next row asks the same question about the same file with
// the injection off, which is the measurement that says the machine trusted nothing.
//
// The revocation question is asked of a CRL distribution point that exists only for the length
// of this test: a QTcpServer on 127.0.0.1, named by an http:// URL inside the very certificate it
// answers about, serving a CRL signed by the CA that issued that certificate. The default mode is
// checked to make no request at all, and the online mode is checked to visit exactly that address
// -- the request targets the service received are printed, because they are the answer to "which
// addresses did it actually visit". No other address is reachable from these samples: the
// certificates carry no OCSP address and no authority-information address.
//
// A real CA-signed PDF and a real timestamping authority are still manual measurements (that is
// what handoff-08 J2 says they are), but the token here is a real RFC 3161 TimeStampToken built
// by this test's own authority, so the product's token path is exercised end to end.

#include "../src/document/signatureverify.h"

#include "signaturesample.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QHostAddress>
#include <QMutex>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUuid>

#include <atomic>
#include <iostream>
#include <memory>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

// A reading that is printed as well as asserted. Used for the numbers this test is the first to
// ask about -- the exact /SubFilter spelling PDFium hands back, the blob length -- because a
// test that only says "not equal" leaves the next person to guess what it was equal to.
void note(const QString &label, const QString &value)
{
    std::cout << "  " << label.toStdString() << " = " << value.toStdString() << '\n';
}

struct Scratch
{
    QDir directory;
    bool ok = false;
    // XYZPDF_SIGNATURE_SAMPLE_DIR keeps the samples for a look on the screen (the sidebar, the
    // status bar), the way XYZPDF_FORM_SAMPLE_DIR does for the form samples: the folder must not
    // exist yet, so nothing that is already there is overwritten.
    bool keep = false;
    Scratch()
    {
        QString path = qEnvironmentVariable("XYZPDF_SIGNATURE_SAMPLE_DIR");
        keep = !path.isEmpty();
        if (keep && QFileInfo::exists(path)) {
            std::cerr << "SETUP: XYZPDF_SIGNATURE_SAMPLE_DIR already exists, refusing to write into it\n";
            return;
        }
        if (!keep)
            path = QDir::tempPath() + QStringLiteral("/xyzpdf-signature-smoke-")
                + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        QDir().mkpath(path);
        directory.setPath(path);
        ok = directory.exists();
    }
    ~Scratch()
    {
        if (ok && !keep)
            directory.removeRecursively();
    }
    QString file(const QString &name) const { return directory.filePath(name); }
};

// One sample, written and then asked about.
struct Sample
{
    QString path;
    QByteArray bytes;
    // The two moments the generator encoded, so a reading of "the time came from the token" can
    // be compared against the token's own claim instead of against a guess.
    SignedSample::Written times;
    QVector<SignatureVerify::Signature> results;
    const SignatureVerify::Signature *first() const
    {
        return results.isEmpty() ? nullptr : &results.first();
    }
};

Sample sign(const Scratch &scratch, const QString &name,
            const SignedSample::Identity &identity, const SignedSample::Shape &shape)
{
    Sample sample;
    sample.path = scratch.file(name);
    QString why;
    if (!SignedSample::write(sample.path, identity, shape, &sample.bytes, &why, &sample.times)) {
        std::cerr << "SETUP: could not write " << name.toStdString() << ": "
                  << why.toStdString() << '\n';
        ++failures;
        return sample;
    }
    // Read once immediately, so that a caller that only wants the file's own statements -- the
    // /ByteRange numbers a tamper has to be aimed by -- has them without repeating the call.
    sample.results = SignatureVerify::verifyDocument(sample.path, QString(),
                                                     SignatureVerify::Options());
    return sample;
}

QVector<SignatureVerify::Signature> verify(const Sample &sample,
                                           const SignatureVerify::Options &options)
{
    return SignatureVerify::verifyDocument(sample.path, QString(), options);
}

// Write the same bytes back: the tamper controls, which are done to a file in the scratch
// directory and never to a fixture.
bool rewrite(const Sample &sample, const QByteArray &bytes)
{
    QFile file(sample.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(bytes);
    file.close();
    return true;
}

// A CRL distribution point on this machine and nowhere else.
//
// A certificate under test names `http://127.0.0.1:<port>/<random>.crl` in its cRLDistributionPoints
// extension, so when the chain engine is allowed to go to the network it has exactly one address
// to visit -- this thread -- and every target it asks for is recorded. The body is a CRL signed by
// the same CA that issued the certificate, so a "not revoked" answer is the engine's own reading
// of a CRL it verified, not the shape of a failed download.
//
// The port and the path are random per round on purpose: Windows caches downloaded CRLs by URL
// (CryptnetUrlCache), so a second round that reused an address could be answered from the first
// round's bytes and would print no request at all.
class LocalCrlServer : public QThread
{
public:
    ~LocalCrlServer() override { stop(); }

    bool startServer(const QByteArray &initial)
    {
        {
            QMutexLocker locker(&mutex_);
            crl_ = initial;
            path_ = QStringLiteral("/xyzpdf-crl-%1.crl")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        }
        start();
        ready_.acquire();
        return listening_;
    }

    void stop()
    {
        if (!isRunning())
            return;
        stop_.store(true);
        wait();
    }

    void setCrl(const QByteArray &bytes)
    {
        QMutexLocker locker(&mutex_);
        crl_ = bytes;
    }

    QString url() const
    {
        QMutexLocker locker(&mutex_);
        return QStringLiteral("http://127.0.0.1:%1%2").arg(port_).arg(path_);
    }

    // The targets exactly as received, first line second token: "/x.crl" for a direct request,
    // "http://host/x.crl" if it ever went through a proxy. Recorded verbatim because that is the
    // measurement, not a normalised version of it.
    QStringList requests() const
    {
        QMutexLocker locker(&mutex_);
        return requests_;
    }

    QString servedPath() const
    {
        QMutexLocker locker(&mutex_);
        return path_;
    }

protected:
    void run() override
    {
        QTcpServer server;
        {
            QMutexLocker locker(&mutex_);
            listening_ = server.listen(QHostAddress::LocalHost, 0);
            port_ = server.serverPort();
        }
        ready_.release();
        if (!listening_)
            return;
        while (!stop_.load()) {
            // Nothing here runs an event loop: the sockets are served one at a time in this
            // thread, which is what makes a blocking chain call in the test thread and this
            // thread able to meet.
            if (!server.waitForNewConnection(200))
                continue;
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                serve(socket);
                delete socket;
            }
        }
    }

private:
    void serve(QTcpSocket *socket)
    {
        QByteArray request;
        while (!request.contains("\r\n\r\n") && request.size() < 65536) {
            if (!socket->waitForReadyRead(2000))
                break;
            request += socket->readAll();
        }
        const QByteArray firstLine = request.left(request.indexOf("\r\n"));
        const QList<QByteArray> parts = firstLine.split(' ');
        const QString target = parts.size() >= 2 ? QString::fromLatin1(parts.at(1)) : QString();
        QString path = target;
        const int scheme = path.indexOf(QStringLiteral("://"));
        if (scheme >= 0) {
            const int slash = path.indexOf(QLatin1Char('/'), scheme + 3);
            path = slash >= 0 ? path.mid(slash) : QStringLiteral("/");
        }
        QByteArray body;
        {
            QMutexLocker locker(&mutex_);
            requests_ << target;
            if (path == path_)
                body = crl_;
        }
        const QByteArray response
            = body.isEmpty()
            ? QByteArray("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            : QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/pkix-crl\r\n"
                         "Content-Length: ")
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        socket->write(response);
        socket->flush();
        socket->waitForBytesWritten(2000);
        socket->disconnectFromHost();
    }

    mutable QMutex mutex_;
    QByteArray crl_;
    QString path_;
    QStringList requests_;
    quint16 port_ = 0;
    bool listening_ = false;
    QSemaphore ready_;
    std::atomic<bool> stop_{false};
};

// A CA, a leaf it issued, and the local service that leaf's CRL distribution point names. A fresh
// round per question, because the address lives inside the certificate and the engine's cache
// lives by that address.
struct RevocationRound
{
    LocalCrlServer server;
    SignedSample::Identity ca;
    SignedSample::Identity leaf;
    Sample sample;
    bool ready = false;

    ~RevocationRound()
    {
        SignedSample::releaseIdentity(&leaf);
        SignedSample::releaseIdentity(&ca);
    }

    bool build(const Scratch &scratch, const QString &name, const QDateTime &now,
               bool revokeTheLeaf, QString *why)
    {
        if (!SignedSample::createCaIdentity(&ca, QStringLiteral("XYZPDF test revocation CA"),
                                            now.addDays(-30), now.addDays(365), why))
            return false;
        // The service has to be listening before the certificate is issued: its address is a
        // field of that certificate. It starts on an empty CRL and is given the real one as soon
        // as the leaf's serial exists.
        if (!server.startServer(SignedSample::makeCrl(ca, {}, now.addSecs(-60), now.addDays(1),
                                                      why)))
            return false;
        SignedSample::LeafOptions leafOptions;
        leafOptions.crlUrl = server.url();
        if (!SignedSample::createLeaf(&leaf, ca, QStringLiteral("XYZPDF revocable signer"),
                                      now.addDays(-30), now.addDays(365), leafOptions, why))
            return false;
        QVector<QByteArray> revoked;
        if (revokeTheLeaf)
            revoked << leaf.serial;
        const QByteArray crl
            = SignedSample::makeCrl(ca, revoked, now.addSecs(-60), now.addDays(1), why);
        if (crl.isEmpty())
            return false;
        server.setCrl(crl);
        sample = sign(scratch, name, leaf, SignedSample::Shape());
        ready = sample.first() != nullptr;
        return ready;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // The engine has to be initialised before any document is opened, and this test calls PDFium
    // through signatureverify.cpp rather than through the backend, so the reference count starts
    // here exactly as the backend's would.
    Scratch scratch;
    if (!scratch.ok) {
        std::cerr << "SETUP: no temporary directory to write samples into\n";
        return 1;
    }

    SignedSample::Identity signer;
    QString why;
    const QDateTime now = QDateTime::currentDateTime();
    if (!SignedSample::createIdentity(&signer, QStringLiteral("XYZPDF test signer"),
                                      now.addDays(-30), now.addDays(365), &why)) {
        std::cerr << "SETUP: no in-memory certificate: " << why.toStdString() << '\n';
        return 1;
    }
    SignedSample::Identity expired;
    if (!SignedSample::createIdentity(&expired, QStringLiteral("XYZPDF expired signer"),
                                      now.addYears(-6), now.addYears(-5), &why)) {
        std::cerr << "SETUP: no expired in-memory certificate: " << why.toStdString() << '\n';
        return 1;
    }

    // ------------------------------------------------------------------
    // the file states what the signature covers, and the two reads agree
    // ------------------------------------------------------------------
    {
        SignedSample::Shape shape;
        shape.reason = QStringLiteral("approved for the test");
        shape.location = QStringLiteral("Somewhere, Testland");
        Sample sample = sign(scratch, QStringLiteral("valid.pdf"), signer, shape);
        const SignatureVerify::Signature *one = sample.first();
        check(one != nullptr, "the signed sample produced one signature record");
        if (one) {
            note("subFilter", one->subFilter);
            note("digest algorithm", one->digestAlgorithm);
            QStringList rangeText;
            for (const int number : one->byteRange)
                rangeText << QString::number(number);
            note("byte range", rangeText.join(QLatin1Char(',')));
            note("file size", QString::number(one->fileSize));
            note("signer", one->signerName);
            note("issuer", one->issuerName);
            note("time", one->timeText());
            note("technical note", one->technicalNote);
            check(one->subFilter == QLatin1String("adbe.pkcs7.detached"),
                  "the sub-filter PDFium hands back is the name the file wrote, without the slash");
            check(one->byteRange.size() == 4, "and the byte range is two pairs");
            check(one->fileSize == sample.bytes.size(), "the record carries the file's own length");
            check(one->reason == QStringLiteral("approved for the test"),
                  "the reason the signer wrote comes back");
            // The join: /Location and the field's name and page are qpdf's answers, and they are
            // attached to the record PDFium produced because both reads state the same
            // /ByteRange numbers. A join by index would survive this file; it is the reason the
            // two lists exist at all that is being asserted here, not the order of either.
            check(one->location == QStringLiteral("Somewhere, Testland"),
                  "the location comes from the qpdf half, which is the only one that can read it");
            check(one->fieldName == QStringLiteral("sig1"),
                  "and it lands on the signature field's own name");
            check(one->page == 0, "on the page the widget is annotated on");
            check(one->docMdpPermission == 0, "an approval signature certifies nothing");
            check(one->signerName == QStringLiteral("XYZPDF test signer"),
                  "the name shown is the certificate subject's CN, read off the blob");
            // A self-signed certificate's issuer is itself, and that is the check that the two
            // CertGetNameStringW arguments were not swapped: CERT_NAME_ISSUER_FLAG is a dwFlags
            // value, and passing it as the dwType answers an empty string, which the first run of
            // this row measured.
            check(one->issuerName == one->signerName,
                  "and the issuer comes from the flags argument, not the type one");
            check(one->revisionsAfter == 0, "with nothing appended after it");
            check(!one->coverageGap, "and no byte of its own revision left uncovered");
        }

        // ---- the same file, with the injected root store and without it ----
        SignatureVerify::Options offline;
        const QVector<SignatureVerify::Signature> untrusted = verify(sample, offline);
        check(untrusted.size() == 1, "one signature, read once more");
        if (!untrusted.isEmpty()) {
            note("default-engine verdict", untrusted.first().verdictLabel());
            note("default-engine note", untrusted.first().technicalNote);
            // This is the row that answers the report's question about this machine: a root the
            // system trusts would make the injected and uninjected runs agree, and there would be
            // no evidence that the trusted verdict came from the test.
            check(untrusted.first().verdict == SignatureVerify::Verdict::CertificateProblem,
                  "without the injection the same signature's chain is not trusted");
            check(!untrusted.first().chainTrusted, "and the trust flag says so on its own");
            check(!untrusted.first().chainUsedInjectedRootStore,
                  "with no store passed in, no store was injected");
            check(untrusted.first().digestMatches,
                  "but the covered bytes still match: the problem is the certificate, not the file");
            // The status Windows answered with, not just the bool derived from it. Measured on
            // this machine for a self-signed root that is in no store: CERT_TRUST_IS_UNTRUSTED_ROOT
            // (0x20) in the chain status and CERT_E_UNTRUSTEDROOT (0x800b0109) from the base
            // policy. Naming them separates "this root is not in the store" from the other ways a
            // chain can fail, and the whole point of the row is that the machine's own root store
            // supplied no trust path. (0x20 is the untrusted-root bit, not the offline-revocation
            // one -- 0x01000000 is offline revocation, and it may or may not be set here.)
            note("chain error status", QString::number(untrusted.first().chainErrorStatus, 16));
            note("policy error", QString::number(untrusted.first().policyError, 16));
            check(untrusted.first().policyError == 0x800b0109u,
                  "and the base policy names an untrusted root");
            check(untrusted.first().chainErrorStatus & 0x20u,
                  "and the chain status names an untrusted root too");
        }

        SignatureVerify::Options injected;
        const HCERTSTORE store = SignedSample::memoryStoreWith(signer);
        check(store != nullptr, "a memory root store could be made for the injection");
        injected.exclusiveRootStore = store;
        const QVector<SignatureVerify::Signature> trusted = verify(sample, injected);
        if (store)
            CertCloseStore(store, 0);
        if (!trusted.isEmpty()) {
            note("injected verdict", trusted.first().verdictLabel());
            check(trusted.first().verdict == SignatureVerify::Verdict::Valid,
                  "with the test's own root injected the signature reads as valid");
            check(trusted.first().chainUsedInjectedRootStore,
                  "and the record says the trust came from the injected store");
            check(trusted.first().digestMatches, "the digest over the covered spans matches");
            check(trusted.first().signatureBlockVerifies, "the signature block verifies");
            check(trusted.first().chainBuilt, "and a chain was built");
            // Two time sources are claimed to exist; this is the one that is reachable without a
            // timestamping authority, because CryptSignMessage writes a signing-time attribute.
            check(trusted.first().timeSource == SignatureVerify::TimeSource::DeclaredBySigner,
                  "the time is the CMS signing-time attribute, which is the signer's own word");
            check(trusted.first().time.isValid(), "and it is a moment rather than nothing");
            check(trusted.first().timeText().contains(QStringLiteral("签名者自己声明的时间")),
                  "and the sentence says whose claim it is");
            check(!trusted.first().revocationCheckedOnline,
                  "by default the revocation question was not taken to the network");
            check(trusted.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("未联网检查吊销状态")),
                  "and the detail line says that nobody asked");
        }

        // ---- the online switch, on a certificate with nowhere to go ----
        // This sample's certificate carries no CRL distribution point and no OCSP address, so
        // turning the switch on asks a question Windows cannot take to any host. What is being
        // pinned is that the switch changes the *way* the chain is asked and the note, and does
        // not quietly become a network call the test did not agree to.
        SignatureVerify::Options online = injected;
        const HCERTSTORE onlineStore = SignedSample::memoryStoreWith(signer);
        online.exclusiveRootStore = onlineStore;
        online.checkRevocationOnline = true;
        const QVector<SignatureVerify::Signature> onlineRun = verify(sample, online);
        if (onlineStore)
            CertCloseStore(onlineStore, 0);
        if (!onlineRun.isEmpty()) {
            note("online-mode verdict", onlineRun.first().verdictLabel());
            note("online-mode note", onlineRun.first().technicalNote);
            check(onlineRun.first().revocationCheckedOnline,
                  "the online mode says it was the online mode");
            check(onlineRun.first().verdict == SignatureVerify::Verdict::Valid,
                  "and a certificate with no revocation address is not made to look revoked");
        }
    }

    // ------------------------------------------------------------------
    // 内容被改过: one byte inside the covered ranges
    // ------------------------------------------------------------------
    {
        SignedSample::Shape shape;
        Sample sample = sign(scratch, QStringLiteral("tampered.pdf"), signer, shape);
        // The byte has to be one the digest covers, and the file has to stay the same length so
        // /ByteRange keeps meaning what it said. Inside the first range, the field's own /V is
        // "(before)": changing a letter is a change the signature was made against.
        QByteArray bytes = sample.bytes;
        const int at = bytes.indexOf("(before)");
        check(at > 0, "the sample carries the text field's value where the test expects it");
        if (at > 0 && !sample.results.isEmpty()) {
            // The ranges the record states, so the tamper is aimed by the file's own claim
            // rather than by a number copied from a comment.
            const SignatureVerify::Signature &before = sample.results.first();
            const int holeStart = before.byteRange.at(0) + before.byteRange.at(1);
            check(at < holeStart, "the byte being changed is inside the first covered span");
            bytes.replace(at + 1, 1, "x");
            check(rewrite(sample, bytes), "the copy could be written back");
            const QVector<SignatureVerify::Signature> after
                = SignatureVerify::verifyDocument(sample.path, QString(),
                                                  SignatureVerify::Options());
            check(after.size() == 1, "and the file still states one signature");
            if (!after.isEmpty()) {
                note("tampered verdict", after.first().verdictLabel());
                check(after.first().verdict == SignatureVerify::Verdict::ContentModified,
                      "one changed byte inside the covered ranges is 内容被改过");
                check(!after.first().digestMatches, "and the digest is what says so");
                // The signature block is not asked about once the bytes have moved: the detached
                // verifier hashes the content itself, so its answer would be the same finding
                // wearing a second name. What is pinned here is that the panel does not say it.
                check(!after.first().signatureBlockVerifies,
                      "and no claim is made that the signature block verified");
                check(!after.first().detailLines().join(QLatin1Char('\n'))
                          .contains(QStringLiteral("签名块")),
                      "and the detail line leaves the second half out rather than reporting it "
                      "as a failure");
                check(after.first().detailLines().join(QLatin1Char('\n'))
                          .contains(QStringLiteral("摘要不一致")),
                      "and the detail line names the failing half");
            }
        }
    }

    // ------------------------------------------------------------------
    // the /ByteRange numbers themselves being narrowed, which is an attack
    // and not a defect
    // ------------------------------------------------------------------
    {
        SignedSample::Shape shape;
        Sample sample = sign(scratch, QStringLiteral("narrowed.pdf"), signer, shape);
        const QVector<SignatureVerify::Signature> before = verify(
            sample, SignatureVerify::Options());
        if (!before.isEmpty() && before.first().byteRange.at(0) == 0) {
            QByteArray bytes = sample.bytes;
            // The first pair's offset, written as ten digits by the generator: 0 padded.
            const QByteArray needle = "[0000000000 ";
            const int at = bytes.indexOf(needle);
            check(at > 0, "the byte range starts where the generator put it");
            if (at > 0) {
                bytes.replace(at + 1, 10, QByteArray::number(8).rightJustified(10, '0'));
                rewrite(sample, bytes);
                const QVector<SignatureVerify::Signature> after
                    = SignatureVerify::verifyDocument(sample.path, QString(),
                                                      SignatureVerify::Options());
                if (!after.isEmpty()) {
                    note("narrowed verdict", after.first().verdictLabel());
                    // Dropping the first eight bytes out of the claimed coverage does not make
                    // the signature look narrower-and-fine: the digest the signer attested to is
                    // of the whole original span, so the recomputed one differs. This is the row
                    // that says a shrunken /ByteRange cannot be used to smuggle in a change.
                    check(after.first().verdict == SignatureVerify::Verdict::ContentModified,
                          "a signature whose ranges were narrowed after the fact reads as changed");
                    check(after.first().uncoveredBytesBefore == 8,
                          "and the gap the file now states is counted too");
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 证书不受信任或已过期: a certificate that had already expired
    // ------------------------------------------------------------------
    {
        SignedSample::Shape shape;
        Sample sample = sign(scratch, QStringLiteral("expired.pdf"), expired, shape);
        SignatureVerify::Options injected;
        const HCERTSTORE store = SignedSample::memoryStoreWith(expired);
        injected.exclusiveRootStore = store;
        const QVector<SignatureVerify::Signature> results = verify(sample, injected);
        if (store)
            CertCloseStore(store, 0);
        check(!results.isEmpty(), "the expired sample still states a signature");
        if (!results.isEmpty()) {
            note("expired verdict", results.first().verdictLabel());
            note("expired note", results.first().technicalNote);
            check(results.first().digestMatches,
                  "an expired certificate does not move the bytes: the digest still matches");
            check(results.first().signatureBlockVerifies,
                  "and the signature block still verifies");
            check(results.first().verdict == SignatureVerify::Verdict::CertificateProblem,
                  "what fails is the certificate, and it is reported as the certificate row");
            check(!results.first().chainTrusted, "the chain is not trusted");
            // The chain is asked about the signing moment, so an expiry that arrived afterwards
            // cannot be what produced this answer -- which is what makes the next row possible.
            check(results.first().chainTimeUsed.isValid(),
                  "and the moment the chain was asked about is recorded");
        }
    }

    // ------------------------------------------------------------------
    // 签名之后又有修订: appended incremental updates
    // ------------------------------------------------------------------
    {
        SignedSample::Shape one;
        one.revisionsAfter = 1;
        Sample single = sign(scratch, QStringLiteral("appended-one.pdf"), signer, one);
        SignatureVerify::Options injected;
        const HCERTSTORE store = SignedSample::memoryStoreWith(signer);
        injected.exclusiveRootStore = store;
        const QVector<SignatureVerify::Signature> singleRun = verify(single, injected);
        if (!singleRun.isEmpty()) {
            note("appended once: revisions", QString::number(singleRun.first().revisionsAfter));
            note("appended once: bytes after", QString::number(singleRun.first().bytesAfterRanges));
            check(singleRun.first().revisionsAfter == 1,
                  "one update appended after the signature is counted as one");
            check(singleRun.first().bytesAfterRanges > 0,
                  "and the bytes past the ranges are the ones that carried it");
            check(singleRun.first().digestMatches,
                  "the appended revision is outside the covered ranges, so the digest still "
                  "matches -- which is the fact and not a verdict");
            check(singleRun.first().verdict == SignatureVerify::Verdict::Valid,
                  "so with the test's root injected the signature is still valid");
            check(singleRun.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("签名之后还有")),
                  "and the detail line says there were updates after it");
        }

        SignedSample::Shape two;
        two.revisionsAfter = 2;
        Sample doubleOne = sign(scratch, QStringLiteral("appended-two.pdf"), signer, two);
        const QVector<SignatureVerify::Signature> doubleRun = verify(doubleOne, injected);
        if (!doubleRun.isEmpty()) {
            note("appended twice: revisions", QString::number(doubleRun.first().revisionsAfter));
            // The control the handoff asks for is a reading that changes when the thing it counts
            // changes: one update says 1, two say 2, and a file that was never appended to says 0.
            check(doubleRun.first().revisionsAfter == 2,
                  "two appended updates are counted as two, not as one revision");
        }
        if (store)
            CertCloseStore(store, 0);
    }

    // ------------------------------------------------------------------
    // 无法验证: an unsupported format, and a blob that is not PKCS#7
    // ------------------------------------------------------------------
    {
        SignedSample::Shape unsupported;
        unsupported.subFilter = QStringLiteral("adbe.x509.rsa_sha1");
        Sample sample = sign(scratch, QStringLiteral("x509-rsa-sha1.pdf"), signer, unsupported);
        const QVector<SignatureVerify::Signature> results = verify(
            sample, SignatureVerify::Options());
        check(!results.isEmpty(), "the x509.rsa_sha1 sample states a signature");
        if (!results.isEmpty()) {
            note("unsupported verdict", results.first().verdictLabel());
            note("unsupported note", results.first().technicalNote);
            check(results.first().verdict == SignatureVerify::Verdict::Unverifiable,
                  "a format this cannot check is 无法验证 rather than a guess");
            check(results.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("不支持的签名格式")),
                  "and the sentence names the format");
            check(!results.first().digestMatches,
                  "and nothing was checked, so nothing is claimed to have matched");
        }

        // A /Contents of the right length whose bytes are not a decodable CMS. Same file, same
        // ranges -- only the blob is garbage -- so the answer has to be "无法验证" and not
        // "内容被改过": the covered bytes did not move at all.
        SignedSample::Shape brokenShape;
        Sample broken = sign(scratch, QStringLiteral("broken-blob.pdf"), signer, brokenShape);
        const QVector<SignatureVerify::Signature> before = verify(
            broken, SignatureVerify::Options());
        if (!before.isEmpty() && before.first().byteRange.size() == 4) {
            QByteArray bytes = broken.bytes;
            const int holeStart = before.first().byteRange.at(0) + before.first().byteRange.at(1);
            const int holeEnd = before.first().byteRange.at(2);
            // Fill the hex string with digits that are valid hex but not a DER message.
            for (int at = holeStart + 1; at + 1 < holeEnd; ++at)
                bytes[at] = 'F';
            rewrite(broken, bytes);
            const QVector<SignatureVerify::Signature> after
                = SignatureVerify::verifyDocument(broken.path, QString(),
                                                 SignatureVerify::Options());
            if (!after.isEmpty()) {
                note("broken blob verdict", after.first().verdictLabel());
                note("broken blob note", after.first().technicalNote);
                check(after.first().verdict == SignatureVerify::Verdict::Unverifiable,
                      "a blob that does not decode is 无法验证");
                check(!after.first().digestMatches, "and no digest claim is made");
            }
        }
    }

    // ------------------------------------------------------------------
    // the coverage gap is a fact beside the verdict, not a verdict
    // ------------------------------------------------------------------
    {
        SignedSample::Shape shape;
        shape.gapAtFront = true;
        Sample sample = sign(scratch, QStringLiteral("gap-at-front.pdf"), signer, shape);
        SignatureVerify::Options injected;
        const HCERTSTORE store = SignedSample::memoryStoreWith(signer);
        injected.exclusiveRootStore = store;
        const QVector<SignatureVerify::Signature> results = verify(sample, injected);
        if (!results.isEmpty()) {
            note("gap verdict", results.first().verdictLabel());
            note("gap before", QString::number(results.first().uncoveredBytesBefore));
            check(results.first().coverageGap, "the byte range that skips the header is a gap");
            check(results.first().uncoveredBytesBefore == 8,
                  "and the gap is counted as the eight bytes it left before the first span");
            check(results.first().verdict == SignatureVerify::Verdict::Valid,
                  "the signature still verifies over what it claims to cover");
            check(results.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("没有覆盖它自己那一次修订")),
                  "and the detail line says so beside the verdict, not instead of it");
        }
        if (store)
            CertCloseStore(store, 0);
    }

    // ------------------------------------------------------------------
    // a timestamp token that is there and cannot be read
    // ------------------------------------------------------------------
    {
        SignedSample::Shape shape;
        shape.bogusTimestampToken = true;
        Sample sample = sign(scratch, QStringLiteral("token-unreadable.pdf"), signer, shape);
        SignatureVerify::Options injected;
        const HCERTSTORE store = SignedSample::memoryStoreWith(signer);
        injected.exclusiveRootStore = store;
        const QVector<SignatureVerify::Signature> results = verify(sample, injected);
        if (!results.isEmpty()) {
            note("token verdict", results.first().verdictLabel());
            note("token note", results.first().technicalNote);
            check(results.first().timestampTokenPresent,
                  "the token attribute is noticed even when nothing can be made of its bytes");
            check(results.first().timeSource != SignatureVerify::TimeSource::TimestampToken,
                  "and an unreadable token is not allowed to become the reported time");
            check(results.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("时间戳令牌")),
                  "the detail line says a token was there");
            check(results.first().technicalNote.contains(
                      QStringLiteral("CryptVerifyTimeStampSignature")),
                  "and names the call that said no, which is the difference between a reading "
                  "and an assertion");
        }
        if (store)
            CertCloseStore(store, 0);
    }

    // ------------------------------------------------------------------
    // 时间戳: a real RFC 3161 token, adopted only when it verifies and chains
    // ------------------------------------------------------------------
    {
        // RFC 3161 2.3: a timestamp authority carries id-kp-timeStamping as a critical EKU, which
        // makes its certificate an end entity -- so the root it chains to is a separate CA, and
        // both go into the store the call injects.
        SignedSample::Identity timestampCa;
        check(SignedSample::createCaIdentity(&timestampCa,
                                             QStringLiteral("XYZPDF test timestamp CA"),
                                             now.addDays(-30), now.addDays(365), &why),
              "a CA for the timestamp authority could be made");
        SignedSample::LeafOptions tsaOptions;
        tsaOptions.timestamping = true;
        SignedSample::Identity tsa;
        check(SignedSample::createLeaf(&tsa, timestampCa, QStringLiteral("XYZPDF test TSA"),
                                       now.addDays(-30), now.addDays(365), tsaOptions, &why),
              "a timestamp authority certificate with the critical timeStamping EKU could be "
              "issued");

        SignedSample::Shape shape;
        shape.timestampAuthority = &tsa;
        Sample sample = sign(scratch, QStringLiteral("timestamped.pdf"), signer, shape);

        SignatureVerify::Options both;
        const HCERTSTORE bothStore = SignedSample::memoryStoreWith(signer, timestampCa);
        both.exclusiveRootStore = bothStore;
        const QVector<SignatureVerify::Signature> results = verify(sample, both);
        if (bothStore)
            CertCloseStore(bothStore, 0);
        check(!results.isEmpty(), "the timestamped sample states a signature");
        if (!results.isEmpty()) {
            note("token time (written by the test)",
                 sample.times.tokenTime.toString(Qt::ISODate));
            note("signing time (written by the test)",
                 sample.times.signingTime.toString(Qt::ISODate));
            note("timestamp verdict", results.first().verdictLabel());
            note("timestamp time", results.first().timeText());
            note("timestamp note", results.first().technicalNote);
            check(results.first().timestampTokenPresent, "the token is found in the blob");
            check(results.first().timeSource == SignatureVerify::TimeSource::TimestampToken,
                  "a token that verifies and whose authority chains to the injected root is the "
                  "reported time");
            // The token's moment, not the signer's: the two are two minutes apart by construction,
            // so a fallback to the declared time cannot satisfy this row.
            check(results.first().time.toSecsSinceEpoch()
                      == sample.times.tokenTime.toSecsSinceEpoch(),
                  "and it is the moment the token names");
            check(results.first().time.toSecsSinceEpoch()
                      != sample.times.signingTime.toSecsSinceEpoch(),
                  "which is not the moment the signer declared");
            check(results.first().timeText().contains(QStringLiteral("（时间戳）")),
                  "and the sentence says the time is a timestamp");
            check(results.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("时间取的是令牌里的")),
                  "and the detail line says the time came from the token");
            check(results.first().verdict == SignatureVerify::Verdict::Valid,
                  "while the signature itself still verifies");
        }

        // ---- the same token with one byte of its messageImprint wrong ----
        // The token is the TSA's own: only the digest inside it does not describe this signature.
        // A reader that adopted its moment anyway would be reading a date off a document that
        // never had one.
        SignedSample::Shape flippedShape;
        flippedShape.timestampAuthority = &tsa;
        flippedShape.timestampImprintFlip = true;
        Sample flipped = sign(scratch, QStringLiteral("timestamp-imprint-wrong.pdf"), signer,
                              flippedShape);
        SignatureVerify::Options flippedOptions;
        const HCERTSTORE flippedStore = SignedSample::memoryStoreWith(signer, timestampCa);
        flippedOptions.exclusiveRootStore = flippedStore;
        const QVector<SignatureVerify::Signature> flippedRun = verify(flipped, flippedOptions);
        if (flippedStore)
            CertCloseStore(flippedStore, 0);
        check(!flippedRun.isEmpty(), "the sample with a wrong imprint states a signature");
        if (!flippedRun.isEmpty()) {
            note("wrong-imprint verdict", flippedRun.first().verdictLabel());
            note("wrong-imprint time", flippedRun.first().timeText());
            note("wrong-imprint note", flippedRun.first().technicalNote);
            check(flippedRun.first().timestampTokenPresent,
                  "the token is still noticed even though it does not describe this signature");
            check(flippedRun.first().timeSource == SignatureVerify::TimeSource::DeclaredBySigner,
                  "a token that fails its check is not allowed to become the reported time");
            check(flippedRun.first().time.toSecsSinceEpoch()
                      == flipped.times.signingTime.toSecsSinceEpoch(),
                  "the signer's own declared time is what is reported instead");
            check(flippedRun.first().technicalNote.contains(
                      QStringLiteral("CryptVerifyTimeStampSignature")),
                  "and the note names the call that refused it");
            // The number it refused with, measured: 0x80090002. Not a hash-value error and not an
            // ASN.1 one -- the token decodes, its own signature verifies, and the imprint simply
            // does not describe this signature.
            check(flippedRun.first().technicalNote.contains(QStringLiteral("0x80090002")),
                  "and the number Windows answered with");
            check(flippedRun.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("没能通过校验")),
                  "the detail line says the token did not verify");
            check(flippedRun.first().verdict == SignatureVerify::Verdict::Valid,
                  "the signature itself was not moved by the bad token");
        }

        // ---- the token verifies, but its authority is a stranger ----
        // Windows verifies the token's own signature and imprint; its documentation leaves the
        // question of whether the TSA is trusted to the caller. Asked with a store that holds
        // only the signer's root, the answer has to be "not adopted", not "the token's time".
        SignatureVerify::Options signerOnly;
        const HCERTSTORE signerStore = SignedSample::memoryStoreWith(signer);
        signerOnly.exclusiveRootStore = signerStore;
        const QVector<SignatureVerify::Signature> stranger = verify(sample, signerOnly);
        if (signerStore)
            CertCloseStore(signerStore, 0);
        check(!stranger.isEmpty(), "the same sample read with only the signer's root");
        if (!stranger.isEmpty()) {
            note("untrusted TSA time", stranger.first().timeText());
            note("untrusted TSA note", stranger.first().technicalNote);
            check(stranger.first().timeSource == SignatureVerify::TimeSource::DeclaredBySigner,
                  "a token whose authority chains nowhere this reader trusts is not adopted");
            check(stranger.first().technicalNote.contains(
                      QStringLiteral("the timestamp authority's certificate is not trusted")),
                  "and the note says which half of the question failed");
            // The note carries the two statuses it decided on. Measured here: the chain is partial
            // (CERT_TRUST_IS_PARTIAL_CHAIN 0x00010000 -- the authority's CA is in no store this call
            // was given) and the base policy answers CERT_E_CHAINING (0x800b010a). A note that said
            // only "not trusted" would leave the next person to re-run the sample to find out which
            // of several refusals it was.
            check(stranger.first().technicalNote.contains(QStringLiteral("0x800b010a")),
                  "and names the chaining error the policy answered, not a paraphrase");
            check(stranger.first().detailLines().join(QLatin1Char('\n'))
                      .contains(QStringLiteral("没能通过校验")),
                  "the detail line calls the token unverified as well");
            check(stranger.first().verdict == SignatureVerify::Verdict::Valid,
                  "the signer's own chain is still trusted, so the verdict does not move");
        }

        SignedSample::releaseIdentity(&tsa);
        SignedSample::releaseIdentity(&timestampCa);
    }

    // ------------------------------------------------------------------
    // 联网吊销: a CRL served by this test, and the switch that is allowed to look
    // ------------------------------------------------------------------
    {
        // The certificate names http://127.0.0.1:<port>/<random>.crl, which is this process and
        // nothing else. The CRL it serves does not name the certificate, so the answer has to be
        // the verdict the file already had -- with a note that the question went out.
        RevocationRound round;
        check(round.build(scratch, QStringLiteral("crl-not-revoked.pdf"), now, false, &why),
              "a certificate whose revocation answer lives on this machine could be made");
        if (round.ready) {
            note("CRL address", round.server.url());
            SignatureVerify::Options online;
            const HCERTSTORE store = SignedSample::memoryStoreWith(round.ca);
            online.exclusiveRootStore = store;
            online.checkRevocationOnline = true;
            const QVector<SignatureVerify::Signature> results = verify(round.sample, online);
            if (store)
                CertCloseStore(store, 0);
            const QStringList requests = round.server.requests();
            for (const QString &target : requests)
                note("MEASURE CRL request", target);
            check(!results.isEmpty(), "the sample states a signature");
            if (!results.isEmpty()) {
                note("not-revoked verdict", results.first().verdictLabel());
                note("not-revoked note", results.first().technicalNote);
                check(results.first().revocationCheckedOnline,
                      "the answer says the online mode is what was used");
                check(!requests.isEmpty(),
                      "and the chain engine really visited the address the certificate names");
                for (const QString &target : requests)
                    check(target.endsWith(round.server.servedPath()),
                          "the address it visited is the CRL distribution point");
                check(!results.first().certificateRevoked,
                      "a certificate the CRL does not name is not reported revoked");
                check(results.first().verdict == SignatureVerify::Verdict::Valid,
                      "and the verdict is the one it was before the question was asked");
                check(results.first().detailLines().join(QLatin1Char('\n'))
                          .contains(QStringLiteral("已联网检查过吊销状态")),
                      "the detail line says the question went to the network");
            }
        }
    }

    {
        // The same shape, with the CRL naming this certificate's serial. The two rounds get
        // different ports and paths because the address is part of the certificate -- the
        // engine's URL cache keys on it, and a reused address could answer this round from the
        // previous round's bytes.
        RevocationRound round;
        check(round.build(scratch, QStringLiteral("crl-revoked.pdf"), now, true, &why),
              "a certificate the local CRL names could be made");
        if (round.ready) {
            note("CRL address", round.server.url());
            note("revoked serial (written by the test)",
                 QString::fromLatin1(round.leaf.serial.toHex()));
            SignatureVerify::Options online;
            const HCERTSTORE store = SignedSample::memoryStoreWith(round.ca);
            online.exclusiveRootStore = store;
            online.checkRevocationOnline = true;
            const QVector<SignatureVerify::Signature> results = verify(round.sample, online);
            if (store)
                CertCloseStore(store, 0);
            const QStringList requests = round.server.requests();
            for (const QString &target : requests)
                note("MEASURE CRL request", target);
            check(!results.isEmpty(), "the revoked sample states a signature");
            if (!results.isEmpty()) {
                note("revoked verdict", results.first().verdictLabel());
                note("revoked chain status",
                     QString::number(results.first().chainErrorStatus, 16));
                note("revoked policy error", QString::number(results.first().policyError, 16));
                note("revoked note", results.first().technicalNote);
                check(!requests.isEmpty(),
                      "the answer came from a CRL this test actually served");
                check(results.first().certificateRevoked,
                      "a serial the CRL names is reported as a revoked certificate");
                check(!results.first().chainTrusted, "and the chain is not trusted");
                // The two numbers the machine answered with, as assertions rather than as printout:
                // the chain's status carries CERT_TRUST_IS_REVOKED (0x4) and the BASE policy answers
                // CRYPT_E_REVOKED (0x80092010). Not CERT_E_REVOKED (0x800b010c) -- measured, and the
                // reason the product reads the status bit.
                check((results.first().chainErrorStatus & CERT_TRUST_IS_REVOKED) != 0,
                      "the chain status carries the revoked bit itself");
                check(results.first().policyError == DWORD(CRYPT_E_REVOKED),
                      "and the policy names CRYPT_E_REVOKED, the revocation number, not the "
                      "untrusted-root one");
                check(results.first().verdict == SignatureVerify::Verdict::CertificateProblem,
                      "a revoked certificate is the certificate row, not 有效");
                check(results.first().detailLines().join(QLatin1Char('\n'))
                          .contains(QStringLiteral("证书已被吊销")),
                      "and the detail line says revoked rather than merely untrusted");
                check(results.first().detailLines().join(QLatin1Char('\n'))
                          .contains(QStringLiteral("已联网检查过吊销状态")),
                      "beside the note that the question went out");
            }
        }
    }

    {
        // The default mode must ask nobody. The zero is a reading only if the very same address
        // can be reached when the switch is on, so the second half of this block turns it on and
        // looks at the service again -- a test-side control rather than a product mutation.
        RevocationRound round;
        check(round.build(scratch, QStringLiteral("crl-switch-off.pdf"), now, false, &why),
              "a third certificate with a local revocation address could be made");
        if (round.ready) {
            SignatureVerify::Options offline;
            const HCERTSTORE store = SignedSample::memoryStoreWith(round.ca);
            offline.exclusiveRootStore = store;
            const QVector<SignatureVerify::Signature> results = verify(round.sample, offline);
            if (store)
                CertCloseStore(store, 0);
            note("default-mode CRL requests",
                 QString::number(round.server.requests().size()));
            check(round.server.requests().isEmpty(),
                  "with the switch off the default mode sent nothing to the distribution point");
            check(!results.isEmpty(), "and the sample still states a signature");
            if (!results.isEmpty()) {
                note("switch-off verdict", results.first().verdictLabel());
                note("switch-off chain status",
                     QString::number(results.first().chainErrorStatus, 16));
                note("switch-off policy error",
                     QString::number(results.first().policyError, 16));
                note("switch-off note", results.first().technicalNote);
                check(!results.first().revocationCheckedOnline,
                      "the record says the revocation question was not taken to the network");
                check(results.first().detailLines().join(QLatin1Char('\n'))
                          .contains(QStringLiteral("未联网检查吊销状态")),
                      "and the detail line says nobody asked");
                check(results.first().verdict == SignatureVerify::Verdict::Valid,
                      "the verdict is what it is without the network question");
                // Which two bits the machine actually answered with, so the row above means what it
                // says: the chain does report the revocation question as unanswered
                // (CERT_TRUST_IS_OFFLINE_REVOCATION 0x01000000 plus
                // CERT_TRUST_REVOCATION_STATUS_UNKNOWN 0x40), and with the switch off the policy is
                // asked to ignore exactly that, so it comes back 0 rather than
                // CRYPT_E_REVOCATION_OFFLINE (0x80092013) -- the number this row measured before the
                // ignore was added, and the reason 未联网 could otherwise read as 不受信任.
                check((results.first().chainErrorStatus & CERT_TRUST_IS_OFFLINE_REVOCATION) != 0
                          && (results.first().chainErrorStatus
                              & CERT_TRUST_REVOCATION_STATUS_UNKNOWN)
                              != 0,
                      "the chain still says nobody answered the revocation question");
                check(results.first().policyError == 0,
                      "and the policy was asked to ignore that answer rather than act on it");
                check(!results.first().certificateRevoked,
                      "an unanswered question is not reported as a revoked certificate");
            }

            SignatureVerify::Options online;
            const HCERTSTORE onlineStore = SignedSample::memoryStoreWith(round.ca);
            online.exclusiveRootStore = onlineStore;
            online.checkRevocationOnline = true;
            const QVector<SignatureVerify::Signature> reachable = verify(round.sample, online);
            if (onlineStore)
                CertCloseStore(onlineStore, 0);
            note("switch-on CRL requests",
                 QString::number(round.server.requests().size()));
            check(!round.server.requests().isEmpty(),
                  "the same address is reached once the switch is on, which is what makes the "
                  "zero above a reading rather than a dead service");
            check(!reachable.isEmpty() && reachable.first().verdict
                      == SignatureVerify::Verdict::Valid,
                  "and the answer is the same valid verdict");
        }
    }

    // ------------------------------------------------------------------
    // the sentences: the summary's order and its counts
    // ------------------------------------------------------------------
    {
        // Built here rather than read off a file, because what is being pinned is the wording
        // rule -- the changed-content row leads, and every number is its own %n sentence.
        auto make = [](SignatureVerify::Verdict verdict, int times) {
            QVector<SignatureVerify::Signature> list;
            for (int i = 0; i < times; ++i) {
                SignatureVerify::Signature one;
                one.verdict = verdict;
                list.append(one);
            }
            return list;
        };
        QVector<SignatureVerify::Signature> mixed;
        mixed << make(SignatureVerify::Verdict::Valid, 2)
              << make(SignatureVerify::Verdict::CertificateProblem, 1)
              << make(SignatureVerify::Verdict::ContentModified, 1);
        const QString text = SignatureVerify::summary(mixed);
        std::cout << "  summary = " << text.toStdString() << '\n';
        check(SignatureVerify::countWithVerdict(mixed, SignatureVerify::Verdict::Valid) == 2,
              "the valid count is its own question");
        check(text.contains(QStringLiteral("4")), "and the total the sentence opens with is four");
        check(text.indexOf(QStringLiteral("内容被改过")) < text.indexOf(QStringLiteral("有效")),
              "the row that says the document changed comes before the quiet one");
        check(text.indexOf(QStringLiteral("内容被改过")) < text.indexOf(QStringLiteral("证书不受信任")),
              "and before the certificate row too");
        check(SignatureVerify::summary({}).isEmpty(), "an unsigned document gets no sentence");

        // A one-count sentence and a forty-count sentence must not be the same string in
        // English; that is tests/i18n_smoke.cpp's job to check in the catalogue, and this is the
        // row that checks the code asks for the plural form at all.
        const QString forty = SignatureVerify::summary(
            make(SignatureVerify::Verdict::ContentModified, 40));
        check(forty.contains(QStringLiteral("40")), "the count travels with the sentence");
    }

    SignedSample::releaseIdentity(&signer);
    SignedSample::releaseIdentity(&expired);

    if (failures == 0) {
        std::cout << "the signature checker answers the four verdicts and the two facts\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
