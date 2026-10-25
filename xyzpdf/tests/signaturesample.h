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
// The same half of the encoder for the structure the timestamp token is built with: without it
// CMSG_SIGNER_ENCODE_INFO carries no HashEncryptionAlgorithm, and a token whose SignerInfo
// declares one digest while the signature block was made with another is rejected by
// CryptVerifyTimeStampSignature the same way a PDF signature would be.
#ifndef CMSG_SIGNER_ENCODE_INFO_HAS_CMS_FIELDS
#define CMSG_SIGNER_ENCODE_INFO_HAS_CMS_FIELDS
#endif

#include "formsample.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QRandomGenerator>
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
    // The key identifier (SHA-1 of the DER RSAPublicKey) and the serial number, kept because the
    // two structures that point at this certificate -- a leaf's authority-key-id and a CRL's list
    // of revoked serials -- have to name the very numbers this certificate carries.
    QByteArray keyId;
    QByteArray serial;
};

inline QString windowsError(const char *call)
{
    return QStringLiteral("%1: 0x%2")
        .arg(QLatin1String(call))
        .arg(quint32(GetLastError()), 8, 16, QLatin1Char('0'));
}

// ---- DER, by hand only where Windows has no encoder ----
//
// TSTInfo (RFC 3161 2.4.2) and the RSAPublicKey inside a SubjectPublicKeyInfo are the two
// structures this file writes itself: wincrypt.h can decode a timestamp token but has no encoder
// for TSTInfo, and a CNG key hands its public half back as a BCRYPT_RSAKEY_BLOB rather than as
// DER. Everything else -- names, extensions, certificates and CRLs -- goes through
// CryptEncodeObject or one of the sign-and-encode calls. Both of these structures are fixed
// shapes with no optional fields left out, so plain DER is enough and no BER handling is needed.

inline QByteArray derLength(int length)
{
    if (length < 0x80)
        return QByteArray(1, char(length));
    QByteArray bytes;
    while (length > 0) {
        bytes.prepend(char(length & 0xff));
        length >>= 8;
    }
    bytes.prepend(char(0x80 | bytes.size()));
    return bytes;
}

inline QByteArray derTlv(unsigned char tag, const QByteArray &content)
{
    return QByteArray(1, char(tag)) + derLength(content.size()) + content;
}

inline QByteArray derSequence(const QByteArray &content) { return derTlv(0x30, content); }
inline QByteArray derSet(const QByteArray &content) { return derTlv(0x31, content); }
inline QByteArray derNull() { return QByteArray::fromHex("0500"); }
inline QByteArray derOctetString(const QByteArray &value) { return derTlv(0x04, value); }

// Two's-complement INTEGER from a big-endian magnitude: drop leading zeros, and add the sign
// byte DER requires when the top bit is set. The serial numbers here are positive by
// construction, and this is also where the modulus from BCRYPT_RSAPUBLIC_BLOB arrives.
inline QByteArray derInteger(const QByteArray &magnitude)
{
    QByteArray value = magnitude;
    while (value.size() > 1 && value.at(0) == '\0')
        value.remove(0, 1);
    if (value.isEmpty())
        value = QByteArray(1, '\0');
    if (value.at(0) & 0x80)
        value.prepend('\0');
    return derTlv(0x02, value);
}

inline QByteArray derArcBody(quint64 value)
{
    QByteArray body(1, char(value & 0x7f));
    value >>= 7;
    while (value > 0) {
        body.prepend(char(0x80 | (value & 0x7f)));
        value >>= 7;
    }
    return body;
}

inline QByteArray derOid(const char *dotted)
{
    const QList<QByteArray> arcs = QByteArray(dotted).split('.');
    if (arcs.size() < 2)
        return {};
    QByteArray body = derArcBody(quint64(arcs.at(0).toULongLong()) * 40
                                 + quint64(arcs.at(1).toULongLong()));
    for (int index = 2; index < arcs.size(); ++index)
        body += derArcBody(quint64(arcs.at(index).toULongLong()));
    return derTlv(0x06, body);
}

inline QByteArray derGeneralizedTime(const QDateTime &moment)
{
    return derTlv(0x18, moment.toUTC().toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1()
                           + QByteArray("Z"));
}

// ---- the pieces a certificate and a CRL are built from ----

// A CNG key created with no name is ephemeral: it lives in this process and nowhere on disk.
// This is the one extraction every identity builder below shares, so there is a single place
// where "no key ever reaches the user's key directory" is true.
inline bool createEphemeralKey(Identity *out, QString *why)
{
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
    if (NCryptSetProperty(out->key, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&bits),
                          sizeof bits, 0)
            != ERROR_SUCCESS
        || NCryptFinalizeKey(out->key, 0) != ERROR_SUCCESS) {
        *why = QStringLiteral("NCryptFinalizeKey failed");
        return false;
    }
    return true;
}

inline void toSystemTime(const QDateTime &moment, SYSTEMTIME *target)
{
    const QDate date = moment.date();
    const QTime time = moment.time();
    target->wYear = date.year();
    target->wMonth = date.month();
    target->wDay = date.day();
    target->wHour = time.hour();
    target->wMinute = time.minute();
    target->wSecond = time.second();
}

// FILETIME is UTC; QDateTime::toMSecsSinceEpoch is UTC too, so no local-zone component is
// involved. The certificates and the CRL are written with this, which is why their validity
// windows mean the same thing to the chain engine as they do to this file.
inline FILETIME toFileTime(const QDateTime &moment)
{
    FILETIME result{};
    const qint64 ms = moment.toUTC().toMSecsSinceEpoch();
    if (ms <= 0)
        return result;
    const quint64 ticks
        = quint64(ms / 1000) * Q_INT64_C(10000000) + Q_INT64_C(116444736000000000);
    result.dwLowDateTime = DWORD(ticks & 0xffffffff);
    result.dwHighDateTime = DWORD(ticks >> 32);
    return result;
}

inline QByteArray encodedDirectoryName(const QString &commonName, QString *why)
{
    const QString subject = QStringLiteral("CN=%1").arg(commonName);
    DWORD size = 0;
    if (!CertStrToNameW(X509_ASN_ENCODING, reinterpret_cast<LPCWSTR>(subject.utf16()),
                        CERT_X500_NAME_STR, nullptr, nullptr, &size, nullptr)) {
        *why = windowsError("CertStrToNameW(size)");
        return {};
    }
    QByteArray encoded(int(size), '\0');
    if (!CertStrToNameW(X509_ASN_ENCODING, reinterpret_cast<LPCWSTR>(subject.utf16()),
                        CERT_X500_NAME_STR, nullptr,
                        reinterpret_cast<BYTE *>(encoded.data()), &size, nullptr)) {
        *why = windowsError("CertStrToNameW");
        return {};
    }
    encoded.resize(int(size));
    return encoded;
}

// The certificate holds the key handle as a property without owning it: releaseIdentity()
// frees the handle itself.
inline bool attachKeyProperty(Identity *out, QString *why)
{
    CERT_KEY_CONTEXT keyContext{};
    keyContext.cbSize = sizeof keyContext;
    keyContext.hNCryptKey = out->key;
    keyContext.dwKeySpec = CERT_NCRYPT_KEY_SPEC;
    if (!CertSetCertificateContextProperty(out->context, CERT_KEY_CONTEXT_PROP_ID,
                                           CERT_SET_PROPERTY_INHIBIT_PERSIST_FLAG, &keyContext)) {
        *why = windowsError("CertSetCertificateContextProperty(CERT_KEY_CONTEXT_PROP_ID)");
        return false;
    }
    return true;
}

// The public half of a CNG RSA key, as the DER RSAPublicKey a SubjectPublicKeyInfo carries, and
// its SHA-1 key identifier. BCRYPT_RSAPUBLIC_BLOB is a header followed by the exponent and the
// modulus, both big-endian; RFC 3279 puts the modulus first, and derInteger() adds the sign
// byte when the top bit is set.
inline bool rsaPublicKeyInfo(NCRYPT_KEY_HANDLE key, QByteArray *rsaDer, QByteArray *keyId,
                             QString *why)
{
    DWORD size = 0;
    if (NCryptExportKey(key, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, nullptr, 0, &size, 0)
            != ERROR_SUCCESS
        || size < 24) {
        *why = QStringLiteral("NCryptExportKey(BCRYPT_RSAPUBLIC_BLOB, size) failed");
        return false;
    }
    QByteArray blob(int(size), '\0');
    if (NCryptExportKey(key, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr,
                        reinterpret_cast<PBYTE>(blob.data()), size, &size, 0)
        != ERROR_SUCCESS) {
        *why = QStringLiteral("NCryptExportKey(BCRYPT_RSAPUBLIC_BLOB) failed");
        return false;
    }
    blob.resize(int(size));
    const auto number = [](const BYTE *at) {
        return quint32(at[0]) | (quint32(at[1]) << 8) | (quint32(at[2]) << 16)
            | (quint32(at[3]) << 24);
    };
    const BYTE *raw = reinterpret_cast<const BYTE *>(blob.constData());
    const quint32 magic = number(raw);
    const quint32 exponentLength = number(raw + 8);
    const quint32 modulusLength = number(raw + 12);
    if (magic != BCRYPT_RSAPUBLIC_MAGIC
        || quint64(24) + exponentLength + modulusLength > quint64(blob.size())) {
        *why = QStringLiteral("the exported RSA public key has an unexpected layout");
        return false;
    }
    const QByteArray exponent(reinterpret_cast<const char *>(raw + 24), int(exponentLength));
    const QByteArray modulus(reinterpret_cast<const char *>(raw + 24 + exponentLength),
                             int(modulusLength));
    *rsaDer = derSequence(derInteger(modulus) + derInteger(exponent));
    *keyId = QCryptographicHash::hash(*rsaDer, QCryptographicHash::Sha1);
    return true;
}

inline bool encodeObject(LPCSTR structType, const void *info, QByteArray *encoded, QString *why)
{
    DWORD size = 0;
    if (!CryptEncodeObject(X509_ASN_ENCODING, structType, info, nullptr, &size) || size == 0) {
        *why = windowsError("CryptEncodeObject(size)");
        return false;
    }
    encoded->resize(int(size));
    if (!CryptEncodeObject(X509_ASN_ENCODING, structType, info,
                           reinterpret_cast<BYTE *>(encoded->data()), &size)) {
        *why = windowsError("CryptEncodeObject");
        return false;
    }
    encoded->resize(int(size));
    return true;
}

// An extension whose DER must outlive the structure pointing at it. The array below is built
// only after every value is in place, because the pointers would dangle if the vector moved.
struct OwnedExtension
{
    QByteArray oid;
    bool critical = false;
    QByteArray value;
};

inline QVector<CERT_EXTENSION> extensionPointers(const QVector<OwnedExtension> &owned)
{
    QVector<CERT_EXTENSION> extensions;
    extensions.reserve(owned.size());
    for (const OwnedExtension &one : owned) {
        CERT_EXTENSION extension{};
        extension.pszObjId = const_cast<LPSTR>(one.oid.constData());
        extension.fCritical = one.critical ? TRUE : FALSE;
        extension.Value.cbData = DWORD(one.value.size());
        extension.Value.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(one.value.constData()));
        extensions.append(extension);
    }
    return extensions;
}

inline QByteArray encodedCertificate(const Identity &identity)
{
    if (!identity.context)
        return {};
    return QByteArray(reinterpret_cast<const char *>(identity.context->pbCertEncoded),
                      int(identity.context->cbCertEncoded));
}

// A certificate that is its own CA, so it can be the root of the chain an engine is pointed at.
// The extension is supplied rather than left out because CertCreateSelfSignCertificate's default
// marks the subject as an end entity, and an end entity cannot end a chain.
inline bool createIdentity(Identity *out, const QString &commonName, const QDateTime &notBefore,
                           const QDateTime &notAfter, QString *why)
{
    out->commonName = commonName;
    if (!createEphemeralKey(out, why))
        return false;
    const QByteArray subject = encodedDirectoryName(commonName, why);
    if (subject.isEmpty())
        return false;
    QByteArray rsaDer;
    if (!rsaPublicKeyInfo(out->key, &rsaDer, &out->keyId, why))
        return false;

    QVector<OwnedExtension> owned;
    {
        CERT_BASIC_CONSTRAINTS2_INFO constraints{};
        constraints.fCA = TRUE;
        constraints.fPathLenConstraint = FALSE;
        constraints.dwPathLenConstraint = 0;
        OwnedExtension basic;
        basic.oid = QByteArray(szOID_BASIC_CONSTRAINTS2);
        basic.critical = true;
        if (!encodeObject(X509_BASIC_CONSTRAINTS2, &constraints, &basic.value, why))
            return false;
        owned.append(basic);
    }
    QVector<CERT_EXTENSION> extensions = extensionPointers(owned);
    CERT_EXTENSIONS extensionList{};
    extensionList.cExtension = DWORD(extensions.size());
    extensionList.rgExtension = extensions.data();

    CERT_NAME_BLOB subjectBlob{};
    subjectBlob.cbData = DWORD(subject.size());
    subjectBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(subject.constData()));
    SYSTEMTIME start{};
    SYSTEMTIME end{};
    toSystemTime(notBefore, &start);
    toSystemTime(notAfter, &end);

    // No key-provider info: the key has no name to be found by. It travels with the certificate
    // as the live handle instead, which is what the signing path asks the certificate for first.
    out->context = CertCreateSelfSignCertificate(
        out->key, &subjectBlob, CERT_CREATE_SELFSIGN_NO_KEY_INFO, nullptr, nullptr, &start, &end,
        &extensionList);
    if (!out->context) {
        *why = windowsError("CertCreateSelfSignCertificate");
        return false;
    }
    out->serial = QByteArray(
        reinterpret_cast<const char *>(out->context->pCertInfo->SerialNumber.pbData),
        int(out->context->pCertInfo->SerialNumber.cbData));
    return attachKeyProperty(out, why);
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

// ---- a CA, a leaf it signs, and the CRL that leaf points at ----

// A CA that signs the end-entity certificates below: the timestamp authority's certificate and
// the one whose revocation status the local CRL server answers for. It is its own root, and the
// only place it exists is a CERT_STORE_PROV_MEMORY handle a test injects for one call.
inline bool createCaIdentity(Identity *out, const QString &commonName,
                             const QDateTime &notBefore, const QDateTime &notAfter, QString *why)
{
    out->commonName = commonName;
    if (!createEphemeralKey(out, why))
        return false;
    const QByteArray subject = encodedDirectoryName(commonName, why);
    if (subject.isEmpty())
        return false;
    QByteArray rsaDer;
    if (!rsaPublicKeyInfo(out->key, &rsaDer, &out->keyId, why))
        return false;

    QVector<OwnedExtension> owned;
    {
        CERT_BASIC_CONSTRAINTS2_INFO constraints{};
        constraints.fCA = TRUE;
        constraints.fPathLenConstraint = FALSE;
        constraints.dwPathLenConstraint = 0;
        OwnedExtension basic;
        basic.oid = QByteArray(szOID_BASIC_CONSTRAINTS2);
        basic.critical = true;
        if (!encodeObject(X509_BASIC_CONSTRAINTS2, &constraints, &basic.value, why))
            return false;
        owned.append(basic);
    }
    {
        // keyCertSign | cRLSign, the two bits RFC 5280 4.2.1.3 makes a CA need before it can
        // sign a certificate or a CRL.
        BYTE usage = 0x06;
        CRYPT_BIT_BLOB bits{};
        bits.cbData = 1;
        bits.pbData = &usage;
        bits.cUnusedBits = 1;
        OwnedExtension keyUsage;
        keyUsage.oid = QByteArray(szOID_KEY_USAGE);
        keyUsage.critical = true;
        if (!encodeObject(X509_KEY_USAGE, &bits, &keyUsage.value, why))
            return false;
        owned.append(keyUsage);
    }
    {
        // SubjectKeyIdentifier ::= OCTET STRING, so the extension's value is the key id wrapped in
        // an OCTET STRING; there is no CryptEncodeObject structure for this one.
        OwnedExtension keyId;
        keyId.oid = QByteArray(szOID_SUBJECT_KEY_IDENTIFIER);
        keyId.value = derOctetString(out->keyId);
        owned.append(keyId);
    }
    QVector<CERT_EXTENSION> extensions = extensionPointers(owned);
    CERT_EXTENSIONS extensionList{};
    extensionList.cExtension = DWORD(extensions.size());
    extensionList.rgExtension = extensions.data();

    CERT_NAME_BLOB subjectBlob{};
    subjectBlob.cbData = DWORD(subject.size());
    subjectBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(subject.constData()));
    SYSTEMTIME start{};
    SYSTEMTIME end{};
    toSystemTime(notBefore, &start);
    toSystemTime(notAfter, &end);
    out->context = CertCreateSelfSignCertificate(out->key, &subjectBlob,
                                                 CERT_CREATE_SELFSIGN_NO_KEY_INFO, nullptr,
                                                 nullptr, &start, &end, &extensionList);
    if (!out->context) {
        *why = windowsError("CertCreateSelfSignCertificate(CA)");
        return false;
    }
    out->serial = QByteArray(
        reinterpret_cast<const char *>(out->context->pCertInfo->SerialNumber.pbData),
        int(out->context->pCertInfo->SerialNumber.cbData));
    return attachKeyProperty(out, why);
}

struct LeafOptions
{
    // RFC 3161 2.3: a TSA's certificate must carry id-kp-timeStamping as a *critical* EKU.
    bool timestamping = false;
    // Where the CRL that answers about this certificate lives, written into the certificate as a
    // cRLDistributionPoints extension (RFC 5280 4.2.1.13). Empty leaves the extension out.
    QString crlUrl;
};

// An end-entity certificate issued by `issuer` and signed with the issuer's key through
// CryptSignAndEncodeCertificate. There is no self-signed shortcut for a leaf, and the extensions a
// leaf needs -- authority key id, optional critical EKU, a CRL distribution point -- are exactly
// the ones the self-sign call cannot add.
inline bool createLeaf(Identity *out, const Identity &issuer, const QString &commonName,
                       const QDateTime &notBefore, const QDateTime &notAfter,
                       const LeafOptions &leafOptions, QString *why)
{
    if (!issuer.context || issuer.key == 0) {
        *why = QStringLiteral("the issuing CA has no certificate or key");
        return false;
    }
    out->commonName = commonName;
    if (!createEphemeralKey(out, why))
        return false;
    const QByteArray subject = encodedDirectoryName(commonName, why);
    if (subject.isEmpty())
        return false;
    QByteArray rsaDer;
    if (!rsaPublicKeyInfo(out->key, &rsaDer, &out->keyId, why))
        return false;

    // A serial the test can name: a CRL entry carries this certificate's own bytes, and a value
    // whose top bit is clear needs no sign byte, so the two encodings cannot disagree.
    out->serial = QByteArray(8, '\0');
    for (int index = 0; index < out->serial.size(); ++index)
        out->serial[index] = char(QRandomGenerator::global()->bounded(256));
    out->serial[0] = char(uchar(out->serial.at(0)) & 0x7f);
    if (out->serial.at(0) == '\0')
        out->serial[0] = '\x01';

    QVector<OwnedExtension> owned;
    {
        CERT_BASIC_CONSTRAINTS2_INFO constraints{};
        constraints.fCA = FALSE;
        constraints.fPathLenConstraint = FALSE;
        constraints.dwPathLenConstraint = 0;
        OwnedExtension basic;
        basic.oid = QByteArray(szOID_BASIC_CONSTRAINTS2);
        basic.critical = true;
        if (!encodeObject(X509_BASIC_CONSTRAINTS2, &constraints, &basic.value, why))
            return false;
        owned.append(basic);
    }
    {
        // digitalSignature alone for an ordinary leaf; digitalSignature | nonRepudiation for a
        // timestamp authority, which signs on its own authority rather than on a request.
        BYTE usage = leafOptions.timestamping ? 0xc0 : 0x80;
        CRYPT_BIT_BLOB bits{};
        bits.cbData = 1;
        bits.pbData = &usage;
        bits.cUnusedBits = leafOptions.timestamping ? 6 : 7;
        OwnedExtension keyUsage;
        keyUsage.oid = QByteArray(szOID_KEY_USAGE);
        keyUsage.critical = true;
        if (!encodeObject(X509_KEY_USAGE, &bits, &keyUsage.value, why))
            return false;
        owned.append(keyUsage);
    }
    {
        OwnedExtension keyId;
        keyId.oid = QByteArray(szOID_SUBJECT_KEY_IDENTIFIER);
        keyId.value = derOctetString(out->keyId);
        owned.append(keyId);
    }
    {
        CERT_AUTHORITY_KEY_ID2_INFO authority{};
        authority.KeyId.cbData = DWORD(issuer.keyId.size());
        authority.KeyId.pbData = reinterpret_cast<BYTE *>(
            const_cast<char *>(issuer.keyId.constData()));
        OwnedExtension keyId;
        keyId.oid = QByteArray(szOID_AUTHORITY_KEY_IDENTIFIER2);
        if (!encodeObject(X509_AUTHORITY_KEY_ID2, &authority, &keyId.value, why))
            return false;
        owned.append(keyId);
    }
    if (leafOptions.timestamping) {
        LPSTR usage = const_cast<LPSTR>(szOID_PKIX_KP_TIMESTAMP_SIGNING);
        CERT_ENHKEY_USAGE extended{};
        extended.cUsageIdentifier = 1;
        extended.rgpszUsageIdentifier = &usage;
        OwnedExtension eku;
        eku.oid = QByteArray(szOID_ENHANCED_KEY_USAGE);
        eku.critical = true;
        if (!encodeObject(X509_ENHANCED_KEY_USAGE, &extended, &eku.value, why))
            return false;
        owned.append(eku);
    }
    if (!leafOptions.crlUrl.isEmpty()) {
        const QString url = leafOptions.crlUrl;
        CERT_ALT_NAME_ENTRY entry{};
        entry.dwAltNameChoice = CERT_ALT_NAME_URL;
        entry.pwszURL = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(url.utf16()));
        CERT_ALT_NAME_INFO names{};
        names.cAltEntry = 1;
        names.rgAltEntry = &entry;
        CRL_DIST_POINT point{};
        point.DistPointName.dwDistPointNameChoice = CRL_DIST_POINT_FULL_NAME;
        point.DistPointName.FullName = names;
        CRL_DIST_POINTS_INFO points{};
        points.cDistPoint = 1;
        points.rgDistPoint = &point;
        OwnedExtension distribution;
        distribution.oid = QByteArray(szOID_CRL_DIST_POINTS);
        if (!encodeObject(X509_CRL_DIST_POINTS, &points, &distribution.value, why))
            return false;
        owned.append(distribution);
    }
    QVector<CERT_EXTENSION> extensions = extensionPointers(owned);

    BYTE nullParameters[] = { 0x05, 0x00 };
    CERT_PUBLIC_KEY_INFO publicKey{};
    publicKey.Algorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_RSA);
    publicKey.Algorithm.Parameters.cbData = 2;
    publicKey.Algorithm.Parameters.pbData = nullParameters;
    publicKey.PublicKey.cbData = DWORD(rsaDer.size());
    publicKey.PublicKey.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(rsaDer.constData()));
    publicKey.PublicKey.cUnusedBits = 0;

    CERT_INFO info{};
    info.dwVersion = CERT_V3;
    info.SerialNumber.cbData = DWORD(out->serial.size());
    info.SerialNumber.pbData = reinterpret_cast<BYTE *>(
        const_cast<char *>(out->serial.constData()));
    info.SignatureAlgorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    info.Issuer = issuer.context->pCertInfo->Subject;
    info.NotBefore = toFileTime(notBefore);
    info.NotAfter = toFileTime(notAfter);
    info.Subject.cbData = DWORD(subject.size());
    info.Subject.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(subject.constData()));
    info.SubjectPublicKeyInfo = publicKey;
    info.cExtension = DWORD(extensions.size());
    info.rgExtension = extensions.data();

    CRYPT_ALGORITHM_IDENTIFIER algorithm{};
    algorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    DWORD size = 0;
    if (!CryptSignAndEncodeCertificate(issuer.key, CERT_NCRYPT_KEY_SPEC, X509_ASN_ENCODING,
                                       X509_CERT_TO_BE_SIGNED, &info, &algorithm, nullptr, nullptr,
                                       &size)
        || size == 0) {
        *why = windowsError("CryptSignAndEncodeCertificate(leaf size)");
        return false;
    }
    QByteArray encoded(int(size), '\0');
    if (!CryptSignAndEncodeCertificate(issuer.key, CERT_NCRYPT_KEY_SPEC, X509_ASN_ENCODING,
                                       X509_CERT_TO_BE_SIGNED, &info, &algorithm, nullptr,
                                       reinterpret_cast<BYTE *>(encoded.data()), &size)) {
        *why = windowsError("CryptSignAndEncodeCertificate(leaf)");
        return false;
    }
    encoded.resize(int(size));
    out->context = CertCreateCertificateContext(
        X509_ASN_ENCODING, reinterpret_cast<const BYTE *>(encoded.constData()), size);
    if (!out->context) {
        *why = windowsError("CertCreateCertificateContext(leaf)");
        return false;
    }
    return attachKeyProperty(out, why);
}

// A CRL signed by the CA, listing `revoked`. Built through the same sign-and-encode call as the
// certificates, so the bytes the local server hands out are a CRL the chain engine verifies with
// the CA it already has -- and any disagreement about the signature is the engine's to report.
inline QByteArray makeCrl(const Identity &issuer, const QVector<QByteArray> &revoked,
                          const QDateTime &thisUpdate, const QDateTime &nextUpdate, QString *why)
{
    if (!issuer.context || issuer.key == 0) {
        *why = QStringLiteral("the CRL issuer has no certificate or key");
        return {};
    }
    QVector<CRL_ENTRY> entries(revoked.size());
    for (int index = 0; index < revoked.size(); ++index) {
        entries[index].SerialNumber.cbData = DWORD(revoked.at(index).size());
        entries[index].SerialNumber.pbData = reinterpret_cast<BYTE *>(
            const_cast<char *>(revoked.at(index).constData()));
        entries[index].RevocationDate = toFileTime(thisUpdate.addSecs(-60));
    }

    CERT_AUTHORITY_KEY_ID2_INFO authority{};
    authority.KeyId.cbData = DWORD(issuer.keyId.size());
    authority.KeyId.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(issuer.keyId.constData()));
    QByteArray authorityDer;
    if (!encodeObject(X509_AUTHORITY_KEY_ID2, &authority, &authorityDer, why))
        return {};
    CERT_EXTENSION extension{};
    extension.pszObjId = const_cast<LPSTR>(szOID_AUTHORITY_KEY_IDENTIFIER2);
    extension.fCritical = FALSE;
    extension.Value.cbData = DWORD(authorityDer.size());
    extension.Value.pbData = reinterpret_cast<BYTE *>(authorityDer.data());

    CRL_INFO info{};
    info.dwVersion = CRL_V2;
    info.SignatureAlgorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    info.Issuer = issuer.context->pCertInfo->Subject;
    info.ThisUpdate = toFileTime(thisUpdate);
    info.NextUpdate = toFileTime(nextUpdate);
    info.cCRLEntry = DWORD(entries.size());
    info.rgCRLEntry = entries.isEmpty() ? nullptr : entries.data();
    info.cExtension = 1;
    info.rgExtension = &extension;

    CRYPT_ALGORITHM_IDENTIFIER algorithm{};
    algorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    DWORD size = 0;
    if (!CryptSignAndEncodeCertificate(issuer.key, CERT_NCRYPT_KEY_SPEC, X509_ASN_ENCODING,
                                       X509_CERT_CRL_TO_BE_SIGNED, &info, &algorithm, nullptr,
                                       nullptr, &size)
        || size == 0) {
        *why = windowsError("CryptSignAndEncodeCertificate(CRL size)");
        return {};
    }
    QByteArray encoded(int(size), '\0');
    if (!CryptSignAndEncodeCertificate(issuer.key, CERT_NCRYPT_KEY_SPEC, X509_ASN_ENCODING,
                                       X509_CERT_CRL_TO_BE_SIGNED, &info, &algorithm, nullptr,
                                       reinterpret_cast<BYTE *>(encoded.data()), &size)) {
        *why = windowsError("CryptSignAndEncodeCertificate(CRL)");
        return {};
    }
    encoded.resize(int(size));
    return encoded;
}

// The signature block's own bytes. RFC 3161 2.4.2's messageImprint is a digest of these -- the
// signature value in the SignerInfo, not the document the signature covers -- which is the fact
// that makes a token outlive a re-save and the reason it is read back here.
inline QByteArray signatureValueOfBlob(const QByteArray &blob, QString *why)
{
    if (blob.isEmpty()) {
        *why = QStringLiteral("the signature blob is empty");
        return {};
    }
    CRYPT_DATA_BLOB data{};
    data.cbData = DWORD(blob.size());
    data.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()));
    DWORD encodingType = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &data, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encodingType, &contentType,
                          &formatType, nullptr, nullptr, nullptr)) {
        *why = windowsError("CryptQueryObject(signature blob)");
        return {};
    }
    const HCRYPTMSG message = CryptMsgOpenToDecode(encodingType, 0, 0, 0, nullptr, nullptr);
    if (!message) {
        *why = windowsError("CryptMsgOpenToDecode(signature blob)");
        return {};
    }
    if (!CryptMsgUpdate(message, data.pbData, data.cbData, TRUE)) {
        *why = windowsError("CryptMsgUpdate(signature blob)");
        CryptMsgClose(message);
        return {};
    }
    DWORD needed = 0;
    if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &needed) || needed == 0) {
        *why = windowsError("CryptMsgGetParam(CMSG_SIGNER_INFO_PARAM size)");
        CryptMsgClose(message);
        return {};
    }
    QByteArray storage(int(needed), '\0');
    if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0,
                          reinterpret_cast<BYTE *>(storage.data()), &needed)) {
        *why = windowsError("CryptMsgGetParam(CMSG_SIGNER_INFO_PARAM)");
        CryptMsgClose(message);
        return {};
    }
    const auto *signerInfo = reinterpret_cast<const CMSG_SIGNER_INFO *>(storage.constData());
    const QByteArray value(
        reinterpret_cast<const char *>(signerInfo->EncryptedHash.pbData),
        int(signerInfo->EncryptedHash.cbData));
    CryptMsgClose(message);
    if (value.isEmpty())
        *why = QStringLiteral("the SignerInfo carries no signature value");
    return value;
}

// The policy a TSTInfo names. A real TSA's policy is its own OID; what RFC 3161 2.4.2 requires is
// that one be present, and a private arc keeps this from looking like somebody's real policy.
inline const char *timestampPolicyOid()
{
    return "1.3.6.1.4.1.99999.1";
}

// A real RFC 3161 TimeStampToken, built the way a TSA builds one: a TSTInfo (RFC 3161 2.4.2)
// carried as CMS id-ct-TSTInfo (RFC 5652 5.1) and signed by a certificate whose EKU says
// timeStamping.
//
// CryptSignMessage cannot produce this: its parameter block has no inner-content-type field, so
// everything it builds is id-data. CryptMsgOpenToEncode's pszInnerContentObjID is that field.
inline QByteArray makeTimestampToken(const Identity &tsa, const QByteArray &signatureValue,
                                     const QDateTime &genTime, bool flipImprint, QString *why)
{
    if (!tsa.context || tsa.key == 0 || signatureValue.isEmpty()) {
        *why = QStringLiteral("no timestamp authority or nothing to imprint");
        return {};
    }
    // The imprint is the *digest* of the signature value, not the value: RFC 3161 2.4.2's
    // MessageImprint is `SEQUENCE { HashAlgorithm, HASH OF (to-be-timestamped data) }`, and the data
    // here is the 256-byte signature value. Measured: a token that carries the raw value in that
    // OCTET STRING is rejected by CryptVerifyTimeStampSignature with 0x80090002, and Windows' own
    // decoder reads the field back 256 bytes long where the digest is 32 -- no pbData can ever
    // match it, so the token reads as un-verifiable rather than as "about another signature".
    //
    // One byte of that digest, wrong, is the red sample: the token is otherwise one the TSA really
    // signed, and what a reader does with a token that does not describe this signature is the
    // question the row asks.
    QByteArray imprint = QCryptographicHash::hash(signatureValue, QCryptographicHash::Sha256);
    if (flipImprint)
        imprint[0] = char(uchar(imprint.at(0)) ^ 0x01);

    QByteArray serial(4, '\0');
    for (int index = 0; index < serial.size(); ++index)
        serial[index] = char(QRandomGenerator::global()->bounded(256));
    serial[0] = char(uchar(serial.at(0)) & 0x7f);
    if (serial.at(0) == '\0')
        serial[0] = '\x01';

    const QByteArray messageImprint = derSequence(
        derSequence(derOid(szOID_NIST_sha256) + derNull()) + derOctetString(imprint));
    const QByteArray tstInfo
        = derSequence(derInteger(QByteArray(1, '\x01')) + derOid(timestampPolicyOid())
                      + messageImprint + derInteger(serial) + derGeneralizedTime(genTime));

    // RFC 3161 2.4.2 points at RFC 2634's SigningCertificate attribute as the way a token names
    // the certificate that signed it; this is that attribute with one ESSCertID -- the SHA-1 of
    // the TSA's DER certificate -- and no issuerSerial. wincrypt.h has no structure or constant
    // for it, so the three nested SEQUENCEs are written here; they are all fixed shapes.
    //
    // contentType and messageDigest are *not* written here, because the encoder adds them itself:
    // measured, a signer whose rgAuthAttr already names 1.2.840.113549.1.9.3 and .9.4 comes back
    // out of the decoder with five attributes -- both of those twice -- and the signature block no
    // longer verifies (CMSG_CTRL_VERIFY_SIGNATURE_EX: CRYPT_E_HASH_VALUE, and
    // CryptVerifyTimeStampSignature: the same number one layer up). A detached PDF signature made
    // with CryptSignMessage needs its digest attribute supplied (see signDetachedOnce); a token
    // made with CryptMsgOpenToEncode does not.
    const QByteArray certificate = encodedCertificate(tsa);
    const QByteArray signingCertificateValue = derSequence(derSequence(derSequence(
        derOctetString(QCryptographicHash::hash(certificate, QCryptographicHash::Sha1)))));

    QByteArray signingCertificateBytes = signingCertificateValue;
    CRYPT_ATTR_BLOB signingCertificateBlob{};
    signingCertificateBlob.cbData = DWORD(signingCertificateBytes.size());
    signingCertificateBlob.pbData = reinterpret_cast<BYTE *>(signingCertificateBytes.data());
    CRYPT_ATTRIBUTE attributes[1] = {};
    attributes[0].pszObjId = const_cast<LPSTR>("1.2.840.113549.1.9.16.2.12");
    attributes[0].cValue = 1;
    attributes[0].rgValue = &signingCertificateBlob;

    CERT_BLOB certificateBlob{};
    certificateBlob.cbData = DWORD(certificate.size());
    certificateBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(certificate.constData()));

    CMSG_SIGNER_ENCODE_INFO signer{};
    signer.cbSize = sizeof(signer);
    signer.pCertInfo = tsa.context->pCertInfo;
    signer.hNCryptKey = tsa.key;
    signer.HashAlgorithm.pszObjId = const_cast<LPSTR>(szOID_NIST_sha256);
    signer.cAuthAttr = 1;
    signer.rgAuthAttr = attributes;
    // RFC 5754: the signature algorithm names the digest alongside the key, so a SHA-256 token says
    // sha256WithRSAEncryption rather than bare rsaEncryption.
    signer.HashEncryptionAlgorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);

    CMSG_SIGNED_ENCODE_INFO signedInfo{};
    signedInfo.cbSize = sizeof(signedInfo);
    signedInfo.cSigners = 1;
    signedInfo.rgSigners = &signer;
    signedInfo.cCertEncoded = 1;
    signedInfo.rgCertEncoded = &certificateBlob;

    // The inner content type is the whole reason this is not CryptSignMessage: id-ct-TSTInfo is
    // what makes the blob a TimeStampToken rather than a signed blob that happens to be shaped
    // like one.
    const HCRYPTMSG message = CryptMsgOpenToEncode(
        PKCS_7_ASN_ENCODING | X509_ASN_ENCODING, 0, CMSG_SIGNED, &signedInfo,
        const_cast<LPSTR>(szOID_TIMESTAMP_TOKEN), nullptr);
    if (!message) {
        *why = windowsError("CryptMsgOpenToEncode(TSTInfo)");
        return {};
    }
    // No CMSG_DETACHED_FLAG: the TSTInfo is encapsulated in the token, which is what a token is.
    if (!CryptMsgUpdate(message, reinterpret_cast<const BYTE *>(tstInfo.constData()),
                        DWORD(tstInfo.size()), TRUE)) {
        *why = windowsError("CryptMsgUpdate(TSTInfo)");
        CryptMsgClose(message);
        return {};
    }
    // Measured on this machine: for a message opened with CryptMsgOpenToEncode, CMSG_ENCODED_MESSAGE
    // answers CRYPT_E_INVALID_MSG_TYPE (0x80091004) -- for a bare signed message with no signers as
    // well as for this one -- while CMSG_CONTENT_PARAM returns the complete PKCS#7 ContentInfo
    // (it starts with 30 .. 06 09 2a 86 48 86 f7 0d 01 07 02, and CryptQueryObject reads it as
    // PKCS7_SIGNED) and CMSG_BARE_CONTENT_PARAM returns the same bytes minus the 19-byte outer
    // wrapper. The complete one is what a `/Contents` has to hold, so that is the one asked for.
    DWORD needed = 0;
    if (!CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, nullptr, &needed) || needed == 0) {
        *why = windowsError("CryptMsgGetParam(CMSG_CONTENT_PARAM size)");
        CryptMsgClose(message);
        return {};
    }
    QByteArray token(int(needed), '\0');
    if (!CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0,
                          reinterpret_cast<BYTE *>(token.data()), &needed)) {
        *why = windowsError("CryptMsgGetParam(CMSG_CONTENT_PARAM)");
        CryptMsgClose(message);
        return {};
    }
    token.resize(int(needed));
    CryptMsgClose(message);
    return token;
}

// The detached PKCS#7 over two non-contiguous buffers -- which is the shape a PDF signature
// needs, and why CryptSignMessage takes an array of them instead of one blob.
//
// `bogusTimestampToken` asks for an unauthenticated counter-signature attribute whose bytes are
// not a token. That is the shape a reader has to answer honestly about: notice the token is
// there, say it did not read, and keep using the declared time.
inline QByteArray signDetachedOnce(const Identity &identity, const QVector<QByteArray> &spans,
                                   const QDateTime &signingTime, bool bogusTimestampToken,
                                   const QByteArray &timestampToken, QString *why)
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

    const QDateTime when = signingTime;
    const FILETIME whenTime = toFileTime(when);
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
    // A real token goes under id-aa-signatureTimeStampToken (1.2.840.113549.1.9.16.2.14), the CMS
    // attribute RFC 3161 2.4.2 says a signer attaches it as. The fake one goes under Microsoft's
    // counter-signature OID, and its bytes are a well-formed empty SEQUENCE rather than prose:
    // measured, a value that is not decodable ASN.1 at all makes the *whole* attribute set
    // unreadable (CRYPT_E_ASN1_EOD), which would test the decoder's tolerance instead of the
    // question being asked -- that a token which is present but is not a token Windows can verify
    // does not become the reported time.
    QByteArray tokenBytes = timestampToken;
    const char *tokenOid = "1.2.840.113549.1.9.16.2.14";
    if (tokenBytes.isEmpty() && bogusTimestampToken) {
        tokenBytes = QByteArray::fromHex("3000");
        tokenOid = szOID_RFC3161_counterSign;
    }
    CRYPT_DATA_BLOB tokenValue{};
    CRYPT_ATTRIBUTE tokenAttribute{};
    if (!tokenBytes.isEmpty()) {
        tokenValue.cbData = DWORD(tokenBytes.size());
        tokenValue.pbData = reinterpret_cast<BYTE *>(tokenBytes.data());
        tokenAttribute.pszObjId = const_cast<LPSTR>(tokenOid);
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

// The moment the token vouches for: two minutes after the signer's own attribute, far enough
// apart that "the time came from the token" cannot be satisfied by the fallback, and inside every
// validity window these samples use.
inline QDateTime timestampTimeFor(const QDateTime &signingTime)
{
    return signingTime.addSecs(120);
}

// The two passes a timestamp needs. The token's imprint is a digest of the signature value, so
// that value has to exist before the token can be built, and the token has to be attached before
// the blob is final -- signing once gives the first, signing again with the token attached gives
// the second. RSA PKCS#1 v1.5 over the same authenticated attributes is deterministic, so the two
// signature values are the same bytes; that equality is checked rather than assumed, because a
// token naming a different value would be a token about another signature.
inline QByteArray signDetached(const Identity &identity, const QVector<QByteArray> &spans,
                               const QDateTime &signingTime, bool bogusTimestampToken,
                               const Identity *timestampAuthority, bool imprintFlip, QString *why)
{
    const QByteArray plain
        = signDetachedOnce(identity, spans, signingTime, bogusTimestampToken, QByteArray(), why);
    if (plain.isEmpty() || timestampAuthority == nullptr)
        return plain;
    const QByteArray signatureValue = signatureValueOfBlob(plain, why);
    if (signatureValue.isEmpty())
        return {};
    const QByteArray token = makeTimestampToken(*timestampAuthority, signatureValue,
                                                timestampTimeFor(signingTime), imprintFlip, why);
    if (token.isEmpty())
        return {};
    const QByteArray stamped = signDetachedOnce(identity, spans, signingTime, false, token, why);
    if (stamped.isEmpty())
        return {};
    if (signatureValueOfBlob(stamped, why) != signatureValue) {
        *why = QStringLiteral("the attached token names a signature value the blob does not carry");
        return {};
    }
    return stamped;
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
    // A real RFC 3161 token from this TSA, attached as id-aa-signatureTimeStampToken. Null means
    // no token at all.
    const Identity *timestampAuthority = nullptr;
    // Built the same way but with one byte of the messageImprint wrong: the token is present and
    // is the TSA's, and it does not describe this signature.
    bool timestampImprintFlip = false;
    bool signSecondPage = false;
};

// The two moments a timestamped sample was signed with, reported so a test can say which one the
// product read without asking the product.
struct Written
{
    QDateTime signingTime;
    QDateTime tokenTime;
};

inline bool write(const QString &path, const Identity &identity, const Shape &shape,
                  QByteArray *bytesOut, QString *why, Written *written = nullptr)
{
    // Whole seconds: the CMS signing-time and the TSTInfo genTime are both written to the second,
    // so a caller comparing them with the product's answer must not be handed milliseconds that
    // were never encoded.
    const QDateTime signingTime
        = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), Qt::UTC);
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
        blob = signDetached(identity, spans, signingTime, shape.bogusTimestampToken,
                            shape.timestampAuthority, shape.timestampImprintFlip, why);
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
    if (written) {
        written->signingTime = signingTime;
        written->tokenTime
            = shape.timestampAuthority ? timestampTimeFor(signingTime) : QDateTime();
    }
    return true;
}

// A memory store holding the certificates a call is going to treat as roots, for
// SignatureVerify::Options::exclusiveRootStore. An Identity with no certificate is skipped, which
// is what lets a caller name one, two or three without a second overload.
inline HCERTSTORE memoryStoreWith(const Identity &first, const Identity &second = Identity(),
                                  const Identity &third = Identity())
{
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!store)
        return nullptr;
    for (const Identity *one : { &first, &second, &third }) {
        if (!one->context)
            continue;
        if (!CertAddCertificateContextToStore(store, one->context, CERT_STORE_ADD_ALWAYS,
                                              nullptr)) {
            CertCloseStore(store, 0);
            return nullptr;
        }
    }
    return store;
}

} // namespace SignedSample

#endif // XYZPDF_SIGNATURESAMPLE_H
