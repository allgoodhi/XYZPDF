// Signed PDF samples, signed in memory, for the tests that have to know what a signature is.
//
// ---- why the signatures are made here rather than committed ----
//
// A real signature needs a real certificate, and a certificate a test can rely on has two
// properties nobody wants on this machine: its private key sits somewhere on disk, and its root
// sits in somebody's store. Neither is true of these samples. The key is a CNG key created with
// no name, which CNG keeps in the process and never writes to the user's key directory; the
// certificate exists only as the context `CertCreateSelfSignCertificate` hands back, and nothing
// in this file names a store or calls `CertAddCertificateContextToStore` on anything but a
// `CERT_STORE_PROV_MEMORY` handle that dies with the test. See createIdentity() for how the key
// reaches the signer.
//
// That is also why tests/signaturecheck_smoke.cpp verifies the same sample twice, with and
// without the injected root store: the difference between those two runs is the evidence that the
// trusted verdict came from the test and not from something already installed here.
//
// ---- the byte layout, and why the file is built more than once ----
//
// `writeSigned()` in formsample.h already had to place /ByteRange by searching the assembled
// buffer for the /Contents hole rather than counting, for the same reason: the offsets are
// features of the finished bytes. What is new here is that the hole cannot be a generous fixed
// width, because the PKCS#7 has to fit it exactly -- real signers zero-pad the leftover, and a
// decoder handed trailing garbage after a self-delimiting DER is being asked to be tolerant
// rather than to be right.
//
// So: assemble with a big hole, sign the ranges that hole implies, look at how long the blob
// came out, rebuild with a hole of exactly that width, and sign again. The second signature is
// taken over different /ByteRange digits and comes out the same length, because the blob's size
// depends on the certificate and on a fixed-length digest and not on the bytes that were hashed.
// If it ever stopped converging the file says so instead of writing a signature that does not
// match its own ranges.
//
// ---- the shapes ----
//
// PDF 32000-1 sec.12.8, table 252: a signature dictionary is /Type /Sig, /Filter /Adobe.PPKLite,
// /SubFilter, /ByteRange, /Contents, /M, optionally /Reason and /Location, and for a
// certification signature the /Reference entry of sec.12.8.2.2. The field is a /Sig widget whose
// /V names that dictionary. /Location is written because no PDFium getter answers it, which is
// the fact that makes the qpdf half of src/document/signatureverify.cpp necessary at all.

#ifndef XYZPDF_SIGNATURESAMPLE_H
#define XYZPDF_SIGNATURESAMPLE_H

// Before every include, including formsample.h's <windows.h>: wincrypt.h keeps the CMS half of
// CRYPT_SIGN_MESSAGE_PARA -- HashEncryptionAlgorithm among them -- behind this define, and
// without it a signer cannot say which algorithm signs, so the message declares one digest and
// the signature block is made with another. That arrives as CRYPT_E_HASH_VALUE from the
// verifier, with nothing to point at the mismatch.
#ifndef CRYPT_SIGN_MESSAGE_PARA_HAS_CMS_FIELDS
#define CRYPT_SIGN_MESSAGE_PARA_HAS_CMS_FIELDS
#endif

#include "formsample.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#undef small

namespace SignedSample {

// An ephemeral key and the self-signed certificate on it. Neither ever reaches the disk.
//
// ---- how the key reaches the signer, measured ----
//
// CAPI's never-persisted container (`CRYPT_VERIFYCONTEXT`) attached as `CERT_KEY_CONTEXT_PROP_ID`:
// `CryptSignMessage` answered `0x8009200B`, CRYPT_E_NO_KEY_PROPERTY. A *named* CAPI container
// signs, but it is a file under the user's key directory for the length of the run, and a test
// that crashes leaves it there (the first version of this file did that, deleting it at the end).
// A nameless CNG key attached as `CERT_NCRYPT_KEY_HANDLE_PROP_ID`: `0x80090027`, NTE_INVALID_PARAMETER.
// A nameless CNG key attached as `CERT_KEY_CONTEXT_PROP_ID` with `dwKeySpec = CERT_NCRYPT_KEY_SPEC`
// signs -- that is the one used. The trust the "有效" verdict needs is still injected per call
// rather than installed.
struct Identity
{
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE key = 0;
    PCCERT_CONTEXT context = nullptr;
    QString commonName;
};

inline QString windowsError(const char *call)
{
    return QStringLiteral("%1: 0x%2")
        .arg(QLatin1String(call))
        .arg(quint32(GetLastError()), 8, 16, QLatin1Char('0'));
}

// A certificate that is its own CA, so it can be the root of the chain an engine is pointed at.
// The extension is supplied rather than left out because CertCreateSelfSignCertificate's default
// marks the subject as an end entity, and an end entity cannot end a chain.
inline bool createIdentity(Identity *out, const QString &commonName, const QDateTime &notBefore,
                           const QDateTime &notAfter, QString *why)
{
    out->commonName = commonName;
    // A CNG key created with no name is ephemeral: it lives in this process and nowhere on disk.
    if (NCryptOpenStorageProvider(&out->provider, MS_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS) {
        *why = QStringLiteral("NCryptOpenStorageProvider failed");
        return false;
    }
    if (NCryptCreatePersistedKey(out->provider, &out->key, BCRYPT_RSA_ALGORITHM, nullptr, 0, 0)
        != ERROR_SUCCESS) {
        *why = QStringLiteral("NCryptCreatePersistedKey failed");
        return false;
    }
    DWORD bits = 2048;
    if (NCryptSetProperty(out->key, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&bits), sizeof bits, 0)
            != ERROR_SUCCESS
        || NCryptFinalizeKey(out->key, 0) != ERROR_SUCCESS) {
        *why = QStringLiteral("NCryptFinalizeKey failed");
        return false;
    }

    const QString subject = QStringLiteral("CN=%1").arg(commonName);
    DWORD size = 0;
    if (!CertStrToNameW(X509_ASN_ENCODING, reinterpret_cast<LPCWSTR>(subject.utf16()),
                        CERT_X500_NAME_STR, nullptr, nullptr, &size, nullptr)) {
        *why = windowsError("CertStrToNameW(size)");
        return false;
    }
    QByteArray encoded(int(size), '\0');
    if (!CertStrToNameW(X509_ASN_ENCODING, reinterpret_cast<LPCWSTR>(subject.utf16()),
                        CERT_X500_NAME_STR, nullptr,
                        reinterpret_cast<BYTE *>(encoded.data()), &size, nullptr)) {
        *why = windowsError("CertStrToNameW");
        return false;
    }
    CERT_NAME_BLOB subjectBlob{};
    subjectBlob.cbData = size;
    subjectBlob.pbData = reinterpret_cast<BYTE *>(encoded.data());

    CERT_BASIC_CONSTRAINTS2_INFO constraints{};
    constraints.fCA = TRUE;
    constraints.fPathLenConstraint = FALSE;
    constraints.dwPathLenConstraint = 0;
    QByteArray constraintDer;
    DWORD derSize = 0;
    if (CryptEncodeObject(X509_ASN_ENCODING, X509_BASIC_CONSTRAINTS2, &constraints, nullptr,
                          &derSize) && derSize > 0) {
        constraintDer.resize(int(derSize));
        if (!CryptEncodeObject(X509_ASN_ENCODING, X509_BASIC_CONSTRAINTS2, &constraints,
                               reinterpret_cast<BYTE *>(constraintDer.data()), &derSize))
            constraintDer.clear();
        else
            constraintDer.resize(int(derSize));
    }
    CERT_EXTENSION constraintExtension{};
    constraintExtension.pszObjId = const_cast<LPSTR>(szOID_BASIC_CONSTRAINTS2);
    constraintExtension.fCritical = TRUE;
    constraintExtension.Value.cbData = DWORD(constraintDer.size());
    constraintExtension.Value.pbData = reinterpret_cast<BYTE *>(constraintDer.data());
    CERT_EXTENSIONS extensions{};
    extensions.cExtension = 1;
    extensions.rgExtension = &constraintExtension;

    const auto toSystemTime = [](const QDateTime &moment, SYSTEMTIME *target) {
        const QDate date = moment.date();
        const QTime time = moment.time();
        target->wYear = date.year();
        target->wMonth = date.month();
        target->wDay = date.day();
        target->wHour = time.hour();
        target->wMinute = time.minute();
        target->wSecond = time.second();
    };
    SYSTEMTIME start{};
    SYSTEMTIME end{};
    toSystemTime(notBefore, &start);
    toSystemTime(notAfter, &end);

    // No key-provider info: the key has no name to be found by. It travels with the certificate
    // as the live handle instead, which is what the signing path asks the certificate for first.
    out->context = CertCreateSelfSignCertificate(
        out->key, &subjectBlob, CERT_CREATE_SELFSIGN_NO_KEY_INFO, nullptr, nullptr, &start, &end,
        constraintDer.isEmpty() ? nullptr : &extensions);
    if (!out->context) {
        *why = windowsError("CertCreateSelfSignCertificate");
        return false;
    }
    CERT_KEY_CONTEXT keyContext{};
    keyContext.cbSize = sizeof keyContext;
    keyContext.hNCryptKey = out->key;
    keyContext.dwKeySpec = CERT_NCRYPT_KEY_SPEC;
    // CERT_SET_PROPERTY_INHIBIT_PERSIST_FLAG: the certificate does not take ownership of the handle,
    // which releaseIdentity() frees itself.
    if (!CertSetCertificateContextProperty(out->context, CERT_KEY_CONTEXT_PROP_ID,
                                           CERT_SET_PROPERTY_INHIBIT_PERSIST_FLAG, &keyContext)) {
        *why = windowsError("CertSetCertificateContextProperty(CERT_KEY_CONTEXT_PROP_ID)");
        return false;
    }
    return true;
}

inline void releaseIdentity(Identity *identity)
{
    // The certificate holds the key handle as a property without owning it, so it goes first.
    if (identity->context)
        CertFreeCertificateContext(identity->context);
    if (identity->key)
        NCryptFreeObject(identity->key);
    if (identity->provider)
        NCryptFreeObject(identity->provider);
    *identity = Identity();
}

// The detached PKCS#7 over two non-contiguous buffers -- which is the shape a PDF signature
// needs, and why CryptSignMessage takes an array of them instead of one blob.
//
// `bogusTimestampToken` asks for an unauthenticated counter-signature attribute whose bytes are
// not a token. That is the shape a reader has to answer honestly about: notice the token is
// there, say it did not read, and keep using the declared time.
inline QByteArray signDetached(const Identity &identity, const QVector<QByteArray> &spans,
                               bool bogusTimestampToken, QString *why)
{
    if (spans.isEmpty() || identity.context == nullptr) {
        *why = QStringLiteral("nothing to sign");
        return {};
    }
    CRYPT_SIGN_MESSAGE_PARA para{};
    para.cbSize = sizeof(para);
    para.dwMsgEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    para.pSigningCert = identity.context;
    para.HashAlgorithm.pszObjId = const_cast<LPSTR>(szOID_NIST_sha256);
    // The signature algorithm alongside the digest algorithm. Left zeroed, the encoder picks its
    // own default, and the message then declares one digest while the signature block was made
    // with another -- which the decoder reports as CRYPT_E_HASH_VALUE (0x80091007) rather than as
    // "the signer used something else".
    para.HashEncryptionAlgorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_RSA);
    para.dwFlags = 0;

    // The certificate goes in the bag as well, which is what makes it possible to find the
    // signer's key without consulting a store.
    PCCERT_CONTEXT certificateList[] = { identity.context };
    para.cMsgCert = 1;
    para.rgpMsgCert = certificateList;

    // ---- the authenticated attributes, supplied by the caller ----
    //
    // Measured on this machine, `CryptSignMessage` hands back a SignerInfo that carries no
    // authenticated attributes at all for a detached signature: not with dwFlags 0, not with
    // CMSG_DETACHED_FLAG (which also took the certificates out of the bag), not with
    // CMSG_AUTHENTICATED_ATTRIBUTES_FLAG. In that shape the signature is over the content hash
    // directly, so a verifier has no message digest to compare against. It is a legal PKCS#7 and
    // it is not what PDF 32000-1 table 253's detached signature is: the shape the specification
    // describes, and the one Adobe and every other producer of `/adbe.pkcs7.detached` writes,
    // carries at least a message digest and usually a signing time.
    //
    // So both are built here and passed in, which is what a real PDF signer does: the digest is
    // over the same buffers the signature covers, and the encoder signs the attribute set that
    // contains it. The DER comes from Windows' own encoders, not from a hand-written one.
    QByteArray concatenated;
    for (const QByteArray &span : spans)
        concatenated += span;
    const QByteArray digest
        = QCryptographicHash::hash(concatenated, QCryptographicHash::Sha256);

    QByteArray digestDer;
    {
        CRYPT_DATA_BLOB value{};
        value.cbData = DWORD(digest.size());
        value.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(digest.constData()));
        DWORD size = 0;
        if (!CryptEncodeObject(X509_ASN_ENCODING, X509_OCTET_STRING, &value, nullptr, &size)) {
            *why = windowsError("CryptEncodeObject(X509_OCTET_STRING size)");
            return {};
        }
        digestDer.resize(int(size));
        if (!CryptEncodeObject(X509_ASN_ENCODING, X509_OCTET_STRING, &value,
                               reinterpret_cast<BYTE *>(digestDer.data()), &size)) {
            *why = windowsError("CryptEncodeObject(X509_OCTET_STRING)");
            return {};
        }
        digestDer.resize(int(size));
    }

    const QDateTime when = QDateTime::currentDateTimeUtc();
    FILETIME whenTime{};
    const quint64 whenTicks = quint64(when.toMSecsSinceEpoch() / 1000) * Q_INT64_C(10000000)
        + Q_INT64_C(116444736000000000);
    whenTime.dwLowDateTime = DWORD(whenTicks & 0xffffffff);
    whenTime.dwHighDateTime = DWORD(whenTicks >> 32);
    QByteArray timeDer;
    {
        // The UTCTime form, which is what a 2026 signing time is written as (RFC 5280's two-digit
        // year window). A failure here is not fatal: the time is the second attribute, and a
        // signature without it is still a signature -- the reader then falls back to /M and says
        // so, which is a shape the tests ask about separately.
        DWORD size = 0;
        if (CryptEncodeObject(X509_ASN_ENCODING, PKCS_UTC_TIME, &whenTime, nullptr, &size)
            && size > 0) {
            timeDer.resize(int(size));
            if (!CryptEncodeObject(X509_ASN_ENCODING, PKCS_UTC_TIME, &whenTime,
                                   reinterpret_cast<BYTE *>(timeDer.data()), &size))
                timeDer.clear();
            else
                timeDer.resize(int(size));
        }
    }

    CRYPT_ATTR_BLOB digestValue;
    digestValue.cbData = DWORD(digestDer.size());
    digestValue.pbData = reinterpret_cast<BYTE *>(digestDer.data());
    CRYPT_ATTRIBUTE digestAttribute;
    digestAttribute.pszObjId = const_cast<LPSTR>(szOID_PKCS_9_MESSAGE_DIGEST);
    digestAttribute.cValue = 1;
    digestAttribute.rgValue = &digestValue;

    CRYPT_ATTR_BLOB timeValue;
    timeValue.cbData = DWORD(timeDer.size());
    timeValue.pbData = reinterpret_cast<BYTE *>(timeDer.data());
    CRYPT_ATTRIBUTE timeAttribute;
    timeAttribute.pszObjId = const_cast<LPSTR>(szOID_RSA_signingTime);
    timeAttribute.cValue = 1;
    timeAttribute.rgValue = &timeValue;

    // Both stay in scope until CryptSignMessage returns: the parameter block holds pointers into
    // them. The two-element array is a copy of each attribute, and the copy carries the same
    // rgValue pointers, so the blobs above are what the encoder reads.
    CRYPT_ATTRIBUTE bothAttributes[2] = { digestAttribute, timeAttribute };
    if (timeDer.isEmpty()) {
        para.cAuthAttr = 1;
        para.rgAuthAttr = &digestAttribute;
    } else {
        para.cAuthAttr = 2;
        para.rgAuthAttr = bothAttributes;
    }

    // Everything below has to outlive the call: the attribute array holds pointers into it.
    //
    // The fake token is a well-formed empty SEQUENCE rather than prose: measured, a value that is
    // not decodable ASN.1 at all makes the *whole* authenticated/unauthenticated attribute set
    // unreadable (CryptMsgGetParam answers CRYPT_E_ASN1_EOD), which would test the decoder's
    // tolerance instead of the question being asked -- that a token which is present but is not a
    // token Windows can verify does not become the reported time.
    QByteArray tokenBytes = bogusTimestampToken ? QByteArray::fromHex("3000") : QByteArray();
    CRYPT_DATA_BLOB tokenValue{};
    CRYPT_ATTRIBUTE tokenAttribute{};
    if (bogusTimestampToken) {
        tokenValue.cbData = DWORD(tokenBytes.size());
        tokenValue.pbData = reinterpret_cast<BYTE *>(tokenBytes.data());
        tokenAttribute.pszObjId = const_cast<LPSTR>(szOID_RFC3161_counterSign);
        tokenAttribute.cValue = 1;
        tokenAttribute.rgValue = &tokenValue;
        para.cUnauthAttr = 1;
        para.rgUnauthAttr = &tokenAttribute;
    }

    QVector<const BYTE *> pointers;
    QVector<DWORD> lengths;
    for (const QByteArray &span : spans) {
        pointers.push_back(reinterpret_cast<const BYTE *>(span.constData()));
        lengths.push_back(DWORD(span.size()));
    }

    DWORD needed = 0;
    if (!CryptSignMessage(&para, TRUE, DWORD(spans.size()), pointers.data(), lengths.data(),
                          nullptr, &needed)) {
        *why = windowsError("CryptSignMessage(size)");
        return {};
    }
    QByteArray blob(int(needed), '\0');
    if (!CryptSignMessage(&para, TRUE, DWORD(spans.size()), pointers.data(), lengths.data(),
                          reinterpret_cast<BYTE *>(blob.data()), &needed)) {
        *why = windowsError("CryptSignMessage");
        return {};
    }
    blob.resize(int(needed));
    return blob;
}

struct Built
{
    QByteArray bytes;
    QVector<int> byteRange;
    int holeStart = 0; // where the '<' of /Contents sits
    int holeEnd = 0;   // one past its '>'
};

// One pass: assemble a file whose /Contents hole is `holeBytes` wide, then state the /ByteRange
// the hole actually implies.
//
// `signSecondPage` puts the widget on a second page rather than on the first. It exists for the
// sidebar's jump: a row that goes to page 1 of a one-page document cannot be told apart from a row
// that never moved the page at all, and the point of the row is the move.
inline Built assemble(int holeBytes, const QString &subFilterName, const QString &reason,
                      const QString &location, int docMdp, bool gapAtFront, bool signSecondPage)
{
    // `holeBytes` is the width of `<...>` including both brackets, so the hex digits inside are
    // two fewer than that. Getting this backwards by two is what made the first version of this
    // generator report "the signature is 2286 hex digits and the hole holds 2288".
    const QByteArray marker = QByteArray("AB").repeated((holeBytes - 2) / 2);
    const QByteArray rangePlaceholder = "[0000000000 0000000000 0000000000 0000000000]";
    const FormSampleFile::SignedGeometry sample;
    // Object 12 is the second page when there is one, and the incremental update appendRevision()
    // adds uses 13 so that the two shapes number the same way.
    QVector<QByteArray> objects(signSecondPage ? 14 : 12);
    for (int unused = 7; unused < objects.size(); ++unused)
        objects[unused] = "null";
    QByteArray catalog = "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R";
    if (docMdp > 0)
        catalog += " /Perms << /DocMDP 10 0 R >>";
    objects[1] = catalog + " >>";
    objects[2] = signSecondPage
        ? "<< /Type /Pages /Kids [4 0 R 12 0 R] /Count 2 >>"
        : "<< /Type /Pages /Kids [4 0 R] /Count 1 >>";
    objects[3] = "<< /Type /AcroForm /Fields [5 0 R 6 0 R] /DA (/Helv 9 Tf 0 g ) /DR << /Font "
                 "<< /Helv 8 0 R >> >> >>";
    objects[4] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << "
                 "/Helv 8 0 R >> >> /Annots "
        + (signSecondPage ? QByteArray("[5 0 R]") : QByteArray("[5 0 R 6 0 R]")) + " >>";
    objects[5] = FormSampleFile::widgetBody(QStringLiteral("name"), "/Tx", sample.textField,
                                            " /V (before) /DA (/Helv 10 Tf 0 g )");
    // A widget's /P names the page it belongs to (PDF 32000-1 table 189), so the second-page shape
    // has to say so -- the qpdf half walks /Annots to find the page, and the form layer resolves
    // /P. Both are set here so the two cannot disagree about where the signature is.
    const QByteArray widgetExtra
        = signSecondPage ? " /V 10 0 R /P 12 0 R" : " /V 10 0 R /P 4 0 R";
    objects[6] = FormSampleFile::widgetBody(QStringLiteral("sig1"), "/Sig", sample.signature,
                                            widgetExtra);
    objects[8] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    if (signSecondPage)
        objects[12] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font "
                      "<< /Helv 8 0 R >> >> /Annots [6 0 R] >>";

    const auto escape = [](QString text) {
        text.replace(QLatin1Char('('), QStringLiteral("\\("));
        text.replace(QLatin1Char(')'), QStringLiteral("\\)"));
        return text;
    };
    QByteArray body = "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /"
        + subFilterName.toUtf8() + " /ByteRange " + rangePlaceholder + " /Contents <" + marker
        + "> /M (D:20261024120000+08'00')";
    if (!reason.isEmpty())
        body += " /Reason (" + escape(reason).toUtf8() + ")";
    if (!location.isEmpty())
        body += " /Location (" + escape(location).toUtf8() + ")";
    if (docMdp > 0)
        body += " /Reference [ << /Type /SigRef /TransformMethod /DocMDP /TransformParams << "
                "/Type /TransformParams /V /1.2 /P "
            + QByteArray::number(docMdp) + " >> >> ]";
    body += " >>";
    objects[10] = body;
    objects[11] = "null";

    Built built;
    built.bytes = FormSampleFile::assemblePdf(objects, 1);
    const QByteArray hole = "<" + marker + ">";
    built.holeStart = built.bytes.indexOf(hole);
    if (built.holeStart < 0)
        return built;
    built.holeEnd = built.holeStart + hole.size();
    const int rangeStart = built.bytes.lastIndexOf(rangePlaceholder, built.holeStart);
    if (rangeStart < 0) {
        built.holeStart = -1;
        return built;
    }
    const int fileSize = built.bytes.size();
    const int firstStart = gapAtFront ? 8 : 0;
    const int firstLength = built.holeStart - firstStart;
    const int secondLength = fileSize - built.holeEnd;
    const QByteArray range = "[" + QByteArray::number(firstStart).rightJustified(10, '0') + " "
        + QByteArray::number(firstLength).rightJustified(10, '0') + " "
        + QByteArray::number(built.holeEnd).rightJustified(10, '0') + " "
        + QByteArray::number(secondLength).rightJustified(10, '0') + "]";
    built.bytes.replace(rangeStart, rangePlaceholder.size(), range);
    built.byteRange = { firstStart, firstLength, built.holeEnd, secondLength };
    return built;
}

// A real incremental update, appended after the signing: one new object, an xref subsection for
// it, a trailer that links back to the revision that was signed, and its own startxref. It
// changes no byte the signature covers -- which is the whole property the sample exists to have.
//
// The /Prev number is read off the base file's own last `startxref`, so appending two revisions
// works as well as appending one.
inline QByteArray appendRevision(const QByteArray &file)
{
    int previousStartXref = -1;
    static const QByteArray token("startxref");
    const int marker = file.lastIndexOf(token);
    if (marker >= 0) {
        // The bytes right after the keyword: a newline, the number, a newline, %%EOF. Read the
        // leading digits and nothing else, because the tail of the file is not a place where a
        // second number could be assumed to mean what the first one does.
        const QByteArray tail = file.mid(marker + token.size()).trimmed();
        int end = 0;
        while (end < tail.size() && tail.at(end) >= '0' && tail.at(end) <= '9')
            ++end;
        if (end > 0)
            previousStartXref = tail.left(end).toInt();
    }
    if (previousStartXref < 0)
        return file;

    QByteArray added = file;
    const int objectAt = added.size();
    // Object 13 is the next number neither shape of the base file uses; /Size below has to stay
    // one greater than the highest number in the file.
    added += "13 0 obj\n<< /Note (an incremental update after the signature) >>\nendobj\n";
    const int xrefAt = added.size();
    added += "xref\n13 1\n" + QByteArray::number(objectAt).rightJustified(10, '0')
        + " 00000 n \n";
    added += "trailer\n<< /Size 14 /Root 1 0 R /Prev " + QByteArray::number(previousStartXref)
        + " >>\nstartxref\n" + QByteArray::number(xrefAt) + "\n%%EOF\n";
    return added;
}

struct Shape
{
    QString subFilter = QStringLiteral("adbe.pkcs7.detached");
    QString reason;
    QString location;
    int docMdp = 0;
    // Start the first span at 8 rather than at 0, so the file header is covered by nothing: the
    // shape whose gap the reader has to be told about.
    bool gapAtFront = false;
    // Append N incremental updates after signing, so the signed ranges stop short of the end.
    int revisionsAfter = 0;
    bool bogusTimestampToken = false;
    bool signSecondPage = false;
};

inline bool write(const QString &path, const Identity &identity, const Shape &shape,
                  QByteArray *bytesOut, QString *why)
{
    int holeBytes = 8192;
    QByteArray blob;
    Built built;
    for (int attempt = 0; attempt < 4; ++attempt) {
        built = assemble(holeBytes, shape.subFilter, shape.reason, shape.location, shape.docMdp,
                         shape.gapAtFront, shape.signSecondPage);
        if (built.holeStart < 0 || built.byteRange.size() != 4) {
            *why = QStringLiteral("the sample could not be assembled");
            return false;
        }
        const QVector<QByteArray> spans = {
            built.bytes.mid(built.byteRange.at(0), built.byteRange.at(1)),
            built.bytes.mid(built.byteRange.at(2), built.byteRange.at(3))
        };
        blob = signDetached(identity, spans, shape.bogusTimestampToken, why);
        if (blob.isEmpty())
            return false;
        const int needed = 2 * blob.size() + 2;
        if (needed == holeBytes)
            break; // the file in hand is already exactly the right size
        if (attempt == 3) {
            *why = QStringLiteral("the /Contents hole never settled at %1 bytes").arg(needed);
            return false;
        }
        holeBytes = needed;
    }

    QByteArray file = built.bytes;
    const QByteArray hex = blob.toHex().toUpper();
    if (hex.size() + 2 != built.holeEnd - built.holeStart) {
        *why = QStringLiteral("the signature is %1 hex digits and the hole holds %2")
                   .arg(hex.size()).arg(built.holeEnd - built.holeStart - 2);
        return false;
    }
    file.replace(built.holeStart + 1, hex.size(), hex);
    for (int i = 0; i < shape.revisionsAfter; ++i)
        file = appendRevision(file);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *why = QStringLiteral("could not write %1").arg(path);
        return false;
    }
    out.write(file);
    out.close();
    if (bytesOut)
        *bytesOut = file;
    return true;
}

// A memory store holding one certificate, for SignatureVerify::Options::exclusiveRootStore.
inline HCERTSTORE memoryStoreWith(const Identity &identity)
{
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!store)
        return nullptr;
    if (!CertAddCertificateContextToStore(store, identity.context, CERT_STORE_ADD_ALWAYS,
                                          nullptr)) {
        CertCloseStore(store, 0);
        return nullptr;
    }
    return store;
}

} // namespace SignedSample

#endif // XYZPDF_SIGNATURESAMPLE_H
