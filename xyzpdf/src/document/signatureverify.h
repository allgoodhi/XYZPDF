// What the program says about a signed document once it actually checks.
//
// ---- why this file exists, and why it is application-only ----
//
// H4 (handoff-06) taught the reader to *count* signatures and to protect the bytes a
// signature covers on the way out. It deliberately did not check anything: the sentence a
// reader got said so ("本版不验证签名本身"), because a count that reads like a judgement is
// worse than no count. This is the checking, and it is the application's half for the same
// reason the form input is (AGENTS.md, "表单代码分两半"): a preview pane and a thumbnail
// provider draw a document and answer nothing about who signed it, so the code that reaches
// for the Windows certificate machinery -- and everything behind it -- has no business being
// linked into those two DLLs. Nothing here is declared on Document or on PdfiumBackend, and
// a handler that called it would not link.
//
// ---- the four answers, and why they are not two ----
//
// PDF 32000-1 sec.12.8.1 makes a signature a claim about bytes, and the claim can fail in
// ways a reader has to tell apart:
//
//   * the digest of the covered bytes does not match the one inside the signed attributes:
//     the document was changed after it was signed. This is the heaviest one and the only
//     one that puts 内容被改过 ahead of everything else in the status line.
//   * the digest matches but the certificate cannot be trusted -- no chain, a root nobody
//     trusts, or a certificate that was already expired when it signed. The bytes are the
//     bytes the signer saw; who they came from is unresolved.
//   * the blob itself is not something this can check: an unsupported /SubFilter, a
//     /Contents that is not a decodable PKCS#7, a hash algorithm not on the table.
//   * none of the above: the signature holds and chains to something trusted.
//
// Two further facts are reported beside the verdict and are *not* verdicts, because the file
// states them and they ask nothing about cryptography:
//
//   * 签名之后又有修订 -- the covered ranges stop short of the end of the file, which is what
//     an incremental save after a signature looks like in the bytes (sec.12.8.2.4 says those
//     later revisions are only allowed in narrow cases; whether they are *permitted* is the
//     DocMDP question DocumentEditor already answers, and this file does not judge it).
//   * 覆盖有缺口 -- bytes before or between the ranges that neither a range nor the /Contents
//     hole accounts for: a signature whose own claim does not cover its whole revision.
//
// ---- what is deliberately not done ----
//
// No field-level (FieldMDP) verdict, no judgement of whether a later revision was allowed by
// DocMDP, no form-script execution, and no network unless the reader turned 联网检查吊销状态 on.
// By default the chain is built with CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY and the answer says
// "未联网检查吊销状态" out loud, because "not revoked" and "nobody asked" are different sentences.

#ifndef XYZPDF_SIGNATUREVERIFY_H
#define XYZPDF_SIGNATUREVERIFY_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace SignatureVerify {

// The four answers above. Kept apart rather than folded into "valid/invalid": the reader has
// to be told which of the two things they care about failed -- the document, or the identity.
enum class Verdict {
    Valid,
    ContentModified,
    CertificateProblem,
    Unverifiable
};

// Where the signing time came from, because the three sources are not equally trustworthy and
// the sentence has to say which one the reader is looking at.
enum class TimeSource {
    None,
    // An RFC 3161 / counter-signature token inside the CMS, and Windows verified it. A third
    // party's word for when the signing happened.
    TimestampToken,
    // The CMS signing-time authenticated attribute: the signer's own word.
    DeclaredBySigner,
    // The signature dictionary's /M, which PDFium hands out: also the signer's own word, and
    // the weakest of the three because it sits outside the signed blob.
    FileTimeField
};

struct Options
{
    // Off by default, and the product only ever turns it on with the reader's agreement
    // (src/settings: 联网检查证书是否被吊销). It selects
    // CERT_CHAIN_REVOCATION_CHECK_CHAIN instead of ..._CACHE_ONLY.
    bool checkRevocationOnline = false;

    // A HCERTSTORE the *chain engine* takes as its exclusive root store, which replaces the
    // system 受信任的根 store for this call. Nothing in the product sets it -- it exists so a
    // test can make one of its own in-memory self-signed roots trusted without writing to any
    // store on this machine (AGENTS.md: 测试绝不写这台机器的证书存储). `void *` so that this
    // header does not hand <windows.h> to every widget that shows a verdict; the caller that
    // sets it includes <wincrypt.h> and passes an HCERTSTORE.
    void *exclusiveRootStore = nullptr;

    // Ask the chain about this certificate as it stood at this moment. Empty means the
    // signature's own signing time, which is what "was this a valid signature when it was
    // made" asks for; a certificate that expired afterwards does not retroactively break the
    // document. The answer names the moment it used.
    QDateTime timeToCheck;
};

// Everything one signature is reported as: what the file states, what the cryptography
// answered, and the two extra facts beside the verdict.
struct Signature
{
    // ---- what the file states (nothing here depends on the check working) ----
    QString subFilter;      // /Sig dictionary's /SubFilter, e.g. "adbe.pkcs7.detached"
    QString fieldName;      // the /Sig field's fully qualified /T; empty when unlocated
    int page = -1;          // zero-based page the field sits on; -1 when unlocated
    QString reason;         // /Reason
    QString location;       // /Location
    int docMdpPermission = 0; // 1..3 for the certification signature, 0 for an ordinary one
    QVector<int> byteRange; // (offset, length) pairs, as the dictionary numbers them
    qint64 fileSize = 0;    // the file as it is now, for the two facts below

    // ---- what the check answered ----
    Verdict verdict = Verdict::Unverifiable;
    // The two halves of "the blob is sound", kept apart because they fail for different
    // reasons and the reader deserves to know which: the digest over the covered bytes, and
    // the signature block over the signed attributes.
    bool digestMatches = false;
    bool signatureBlockVerifies = false;
    bool chainBuilt = false;
    bool chainTrusted = false;
    // True when the chain was built against `Options::exclusiveRootStore` rather than the
    // system root store -- which is what a test does, and what lets the test say the verdict
    // did not come from something already installed on this machine.
    bool chainUsedInjectedRootStore = false;
    bool revocationCheckedOnline = false;
    bool timestampTokenPresent = false;
    QString signerName;     // the certificate subject's CN; empty when there is no certificate
    QString issuerName;
    QString digestAlgorithm; // the OID the signer used, or an empty string when unsupported
    QString technicalNote;  // a name and number a report can quote: "CertGetCertificateChain: 0x800..."
    // The two Windows statuses, kept as numbers so the detail line and a report can quote the
    // one the verdict was decided on rather than a paraphrase of it.
    quint32 chainErrorStatus = 0;
    quint32 policyError = 0;

    // ---- the signing time, and which of the three sources answered ----
    QDateTime time;
    TimeSource timeSource = TimeSource::None;
    QDateTime chainTimeUsed;

    // ---- the two facts beside the verdict ----
    // How many incremental updates were appended after this signature: the number of
    // `startxref` tokens in the bytes past the end of the covered ranges. Those bytes are, by
    // the definition of /ByteRange, all later revisions -- the range that follows the
    // /Contents hole runs to the end of the file as it was when the signing happened.
    int revisionsAfter = 0;
    // True when bytes exist that no range and not the /Contents hole accounts for: a first
    // range that does not start at 0, or a hole bigger than the /Contents string in it. Such a
    // signature's own claim does not cover its whole revision, so "the bytes have not moved"
    // is a weaker statement than the one it appears to make.
    bool coverageGap = false;
    qint64 uncoveredBytesBefore = 0;
    qint64 uncoveredBytesInHole = 0;
    qint64 uncoveredBytesAfter = 0;
    qint64 bytesAfterRanges = 0;

    // ---- the sentences the interface shows ----
    // Already translated; built here so the panel, the properties dialog and the status line
    // cannot word the same verdict three different ways.
    QString verdictLabel() const;
    // The signing time with the words that say whose claim it is: a verified timestamp token, or
    // the signer's own declared time. Empty when nothing answered.
    QString timeText() const;
    // One line per signature for the panel's expanded detail.
    QStringList detailLines() const;
};

// Runs the whole thing for one file: reads the signature dictionaries through the engine the
// reader is using, the field names and pages and /Location through qpdf (PDFium exposes
// neither), and the covered bytes from the file itself with our own handle.
//
// Blocking, and not cheap -- it parses the file a second time. Call it from a worker thread
// (src/services/signaturesession.cpp), which is also why it takes a path rather than a
// Document: it opens everything it needs and holds nothing of the tab's.
QVector<Signature> verifyDocument(const QString &path, const QString &password,
                                  const Options &options = Options());

// The counts the properties row and the status summary are made of.
int countWithVerdict(const QVector<Signature> &signatures, Verdict verdict);
// "带着 2 个数字签名：1 个有效，1 个证书不受信任" -- with 内容被改过 first, always, and each
// counted fragment its own `%n` sentence (AGENTS.md: 带计数的句子一律走 %n).
QString summary(const QVector<Signature> &signatures);

// The label on its own, for the panel rows and the tests.
QString verdictLabel(Verdict verdict);

} // namespace SignatureVerify

#endif // XYZPDF_SIGNATUREVERIFY_H
