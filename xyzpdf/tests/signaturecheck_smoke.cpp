// What the signature checker answers about files whose signatures were made for this test.
//
// Every sample here is signed during the run, by a nameless CNG key that never reaches the disk and
// a certificate that never reaches a store -- see the header of tests/signaturesample.h. The
// point of that construction is that the four verdicts below cannot be produced by this
// machine's own trust list: the "有效" row is only reachable through a chain engine whose root
// store the test injected, and the very next row asks the same question about the same file with
// the injection off, which is the measurement that says the machine trusted nothing.
//
// Nothing here goes near the network. The revocation question is asked in cache-only mode for
// every row, and the one row that turns the online mode on uses a certificate with no CRL
// distribution point and no OCSP address in it -- so there is no address to connect to, and the
// answer is checked to be the same verdict with the note that says it was asked differently.
// (The online path's real behaviour -- which addresses a genuine certificate reaches, and how
// long it takes -- is a manual measurement, recorded in the commit report rather than in a test
// that would have to reach somebody's responder to prove anything.)

#include "../src/document/signatureverify.h"

#include "signaturesample.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QUuid>

#include <iostream>
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
    if (!SignedSample::write(sample.path, identity, shape, &sample.bytes, &why)) {
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
            // The status Windows answered with, not just the bool derived from it: 0x80000004 is
            // CERT_TRUST_IS_UNTRUSTED_ISSUER, and naming it is what separates "this root is not in
            // the store" from the other ways a chain can fail -- which matters because the test's
            // whole claim is that the machine's own root store did not supply a trust path.
            // The status Windows answered with, not just the bool derived from it. Measured on
            // this machine for a self-signed root that is in no store: the chain's own status says
            // CERT_TRUST_IS_OFFLINE_REVOCATION (0x20 -- which is also the evidence that the
            // cache-only mode did not go to the wire, since there was nothing cached to find), and
            // the base policy answers CERT_E_UNTRUSTEDROOT (0x800b0109). Naming them separates
            // "this root is not in the store" from the other ways a chain can fail, and the whole
            // point of the row is that the machine's own root store supplied no trust path.
            note("chain error status", QString::number(untrusted.first().chainErrorStatus, 16));
            note("policy error", QString::number(untrusted.first().policyError, 16));
            check(untrusted.first().policyError == 0x800b0109u,
                  "and the base policy names an untrusted root");
            check(untrusted.first().chainErrorStatus & 0x20u,
                  "and the chain status says the revocation check stayed offline");
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
