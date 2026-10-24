// Checking what a signed PDF claims, with Windows' own cryptography.
//
// ---- why crypt32 and not a library ----
//
// The two questions a signature answers -- "do these bytes still hash to the digest that was
// signed" and "does the certificate that signed them chain to something this machine trusts" --
// are answered on Windows by `CryptMsg*` and `CertGetCertificateChain`. Adding a third-party
// verifier would buy a second implementation of both plus a second trust store to explain, and
// the trust store is the part that has to agree with what the reader already installed. So: no
// new dependency, and everything here is Windows-only by construction.
//
// ---- the detached-signature shape decides the code ----
//
// `adbe.pkcs7.detached` (and `ETSI.CAdES.detached`) carry no content: the CMS says "the digest
// of the signer's chosen bytes is X", and X is an *authenticated attribute* that the signature
// block covers. Two things therefore have to be true and they are separate checks, which is why
// this file makes both and reports them apart:
//
//   1. the digest we compute over the file's own /ByteRange spans equals the message-digest
//      attribute (CryptMsgControl cannot do this itself -- a detached message has no content,
//      so there is nothing for the decoder to hash);
//   2. the signature block over the authenticated attributes verifies against the signer's key
//      (`CMSG_CTRL_VERIFY_SIGNATURE_EX`).
//
// A file whose covered byte changed fails 1 and keeps 2; a file whose /Contents was rewritten
// fails 2. Reporting only "the signature is bad" would call the first of those the same defect
// as the second, and they want different sentences.
//
// The same rule reaches `adbe.pkcs7.sha1`, whose blob encapsulates its content: neither check
// looks at the encapsulated bytes, so one path serves both -- stated as reasoning, and the row
// in tests/signatureverify_smoke.cpp says which accepted names have actually been put in front
// of this code.
//
// ---- which read answers which field ----
//
// PDFium's fpdf_signature.h is the source for what the cryptography consumes (/ByteRange,
// /Contents, /SubFilter, /Reason, /M, the DocMDP /P) because it is the engine the reader is
// already using, and because §122 made its count the number the rest of the program quotes.
// It exposes no field name, no page and no /Location for a signature dictionary -- there is no
// getter for any of the three -- so those come from qpdf, and the two halves are joined on the
// signature dictionary's own /ByteRange numbers rather than on the order either library
// happened to hand back (AGENTS.md, sec.99: 不要按下标连). Two signatures cannot share a
// /ByteRange: each one's /Contents occupies a different hole.

#include "signatureverify.h"

#include "pdfiumbackend_p.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QVector>

// NOMINMAX comes from pdfiumbackend_p.h, which has already pulled in windows.h; wincrypt.h and
// the qpdf headers go after it. rpcndr.h's `#define small char` is the trap AGENTS.md names for
// a .cpp that adds windows.h, and `status` is a name this file would otherwise have used for the
// chain policy result -- so the macro goes, and the two reads keep meaning different things.
#include <wincrypt.h>
#undef small

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <fpdf_signature.h>

#include <limits>

namespace {

using SignatureVerify::Options;

// Every user-visible sentence in this file is written as an explicit
// QCoreApplication::translate("SignatureVerify", "...") call rather than through a shorter
// helper, because lupdate matches on the name (AGENTS.md, sec.112): a helper called `label()`
// would put the sentence in the binary and out of the catalogue, and nothing in the build would
// notice.

// The /SubFilter names this can check. PDF 32000-1 table 253 plus the PAdES/BIES registry;
// anything else -- `adbe.x509.rsa_sha1` (a bare PKCS#1 block with no certificate in it), an
// unknown vendor name -- is answered as "无法验证" instead of being guessed at.
bool subFilterIsSupported(const QString &subFilter)
{
    static const QStringList supported = {
        QStringLiteral("adbe.pkcs7.detached"),
        QStringLiteral("adbe.pkcs7.sha1"),
        QStringLiteral("ETSI.CAdES.detached"),
        QStringLiteral("ETSI.CAdES.detached-smime"),
    };
    return supported.contains(subFilter, Qt::CaseInsensitive);
}

// The signer's own digest algorithm, as far as this table has been drawn: an OID Windows names
// an algorithm for, and the algorithm name to put in the detail line beside it. Anything else
// answers "无法验证" with the OID quoted, because handing CryptCreateHash a value it does not
// know is a failure with no name attached to it.
bool digestAlgorithmOf(const QString &oid, ALG_ID *algId, QString *name)
{
    struct Row { const char *oid; ALG_ID alg; const char *name; };
    static const Row rows[] = {
        { "1.2.840.113549.2.5", CALG_MD5, "MD5" },
        { "1.3.14.32.26", CALG_SHA1, "SHA-1" },
        { "1.2.840.113549.1.3.2", CALG_SHA1, "SHA-1" },
        { "2.16.840.1.101.3.4.2.1", CALG_SHA_256, "SHA-256" },
        { "2.16.840.1.101.3.4.2.2", CALG_SHA_384, "SHA-384" },
        { "2.16.840.1.101.3.4.2.3", CALG_SHA_512, "SHA-512" },
    };
    for (const Row &row : rows) {
        if (oid == QLatin1String(row.oid)) {
            *algId = row.alg;
            *name = QString::fromLatin1(row.name);
            return true;
        }
    }
    return false;
}

QString lastErrorText(const char *call)
{
    return QStringLiteral("%1: 0x%2")
        .arg(QLatin1String(call))
        .arg(quint32(GetLastError()), 8, 16, QLatin1Char('0'));
}

// An in-memory crypto provider, released on every way out of the function that took it. The
// decoder's own early returns are many and each one would otherwise need the release line.
struct EphemeralProvider
{
    HCRYPTPROV handle = 0;
    bool acquired = false;

    ~EphemeralProvider()
    {
        if (acquired)
            CryptReleaseContext(handle, 0);
    }

    // What CryptMsgOpenToDecode wants: a handle when there is one, zero when the acquisition
    // failed and the caller should fall back to whatever the system thinks is right.
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE forDecoder() const
    {
        return acquired ? handle : HCRYPTPROV(0);
    }
};

// ---- reading the file's own bytes ----

// Our own handle, with the sharing flags F7 chose for the backend: a document being checked must
// not stop its own file being replaced underneath it, and FILE_SHARE_DELETE is the flag that
// makes that true.
HANDLE openSharedReadOnly(const QString &path, DWORD *win32Code)
{
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        *win32Code = GetLastError();
    return handle;
}

// The spans /ByteRange names, read in file order. Two buffers rather than one, because the
// detached verifier CAPI provides takes an array -- the same shape the signer was given.
//
// This is a copy of the covered part of the file, which for a signed document is nearly the
// whole of it, and the reason is the verifier: `CryptVerifyDetachedMessageSignature` has no
// streaming form (wincrypt.h offers no CryptMsg route for detached content either --
// CryptMsgUpdate's content argument is the encoded message, not the external bytes). An earlier
// shape of this file hashed the spans straight off the handle and asked
// `CMSG_CTRL_VERIFY_SIGNATURE_EX` instead; that control answers CRYPT_E_HASH_VALUE on a signature
// this same OS produced with this same key, because the high-level signer hashes the content and
// the control hashes the authenticated attributes. Verification and signing therefore have to be
// CAPI's own matching pair, and the price is holding the covered bytes once, on a worker thread,
// for one signature at a time.
QVector<QByteArray> readCoveredSpans(HANDLE handle, const QVector<int> &byteRange, QString *why)
{
    QVector<QByteArray> spans;
    for (int pair = 0; pair + 1 < byteRange.size(); pair += 2) {
        const qint64 offset = byteRange.at(pair);
        const qint64 length = byteRange.at(pair + 1);
        if (offset < 0 || length < 0) {
            *why = QStringLiteral("the signature states a negative offset or length");
            return {};
        }
        LARGE_INTEGER position;
        position.QuadPart = offset;
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
            *why = lastErrorText("SetFilePointerEx");
            return {};
        }
        QByteArray span;
        span.resize(int(length));
        qint64 filled = 0;
        while (filled < length) {
            DWORD got = 0;
            if (!ReadFile(handle, span.data() + filled, DWORD(length - filled), &got, nullptr)
                || got == 0) {
                // The file is shorter than the range claims: the signed bytes are not there
                // anymore, which is reported as what it is rather than as a digest mismatch.
                *why = QStringLiteral("the file ends inside the signed range at %1")
                           .arg(offset + filled);
                return {};
            }
            filled += got;
        }
        spans.append(span);
    }
    return spans;
}

bool hashSpans(const QVector<QByteArray> &spans, ALG_ID algId, QByteArray *digest, QString *why)
{
    EphemeralProvider provider;
    provider.acquired = CryptAcquireContextW(&provider.handle, nullptr, MS_ENH_RSA_AES_PROV_W,
                                             PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    if (!provider.acquired) {
        *why = lastErrorText("CryptAcquireContextW");
        return false;
    }
    HCRYPTHASH hash = 0;
    if (!CryptCreateHash(provider.handle, algId, 0, 0, &hash)) {
        *why = lastErrorText("CryptCreateHash");
        return false;
    }
    for (const QByteArray &span : spans) {
        if (!CryptHashData(hash, reinterpret_cast<const BYTE *>(span.constData()),
                           DWORD(span.size()), 0)) {
            *why = lastErrorText("CryptHashData");
            CryptDestroyHash(hash);
            return false;
        }
    }
    DWORD size = 0;
    if (!CryptGetHashParam(hash, HP_HASHVAL, nullptr, &size, 0)) {
        *why = lastErrorText("CryptGetHashParam(size)");
        CryptDestroyHash(hash);
        return false;
    }
    QByteArray value(int(size), '\0');
    if (!CryptGetHashParam(hash, HP_HASHVAL, reinterpret_cast<BYTE *>(value.data()), &size, 0))
        *why = lastErrorText("CryptGetHashParam");
    else
        value.resize(int(size));
    CryptDestroyHash(hash);
    if (!why->isEmpty() && value.isEmpty())
        return false;
    *digest = value;
    return true;
}

// Check 2 of the two: the signature block, over the covered bytes, against the certificate the
// blob carries. Returns false with the OS error named, which is the difference between "the blob
// was rewritten" and "the bytes moved" -- the caller has already decided the second question by
// comparing the message-digest attribute itself.
bool verifySignatureBlock(const QByteArray &contents, const QVector<QByteArray> &spans,
                          QString *why)
{
    // A provider of our own, so the digest the verifier recomputes is the SHA family the signer
    // used rather than whatever the default mapping picks.
    EphemeralProvider provider;
    provider.acquired = CryptAcquireContextW(&provider.handle, nullptr, MS_ENH_RSA_AES_PROV_W,
                                             PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    if (!provider.acquired) {
        *why = lastErrorText("CryptAcquireContextW");
        return false;
    }

    CRYPT_VERIFY_MESSAGE_PARA para{};
    para.cbSize = sizeof(para);
    para.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    para.hCryptProv = provider.handle;

    QVector<const BYTE *> pointers;
    QVector<DWORD> lengths;
    for (const QByteArray &span : spans) {
        pointers.push_back(reinterpret_cast<const BYTE *>(span.constData()));
        lengths.push_back(DWORD(span.size()));
    }
    const CERT_CONTEXT *used = nullptr;
    const BOOL ok = CryptVerifyDetachedMessageSignature(
        &para, 0, reinterpret_cast<const BYTE *>(contents.constData()), DWORD(contents.size()),
        DWORD(spans.size()), pointers.data(), lengths.data(), &used);
    if (used)
        CertFreeCertificateContext(used);
    if (!ok)
        *why = lastErrorText("CryptVerifyDetachedMessageSignature");
    return ok == TRUE;
}

// ---- the PDFium half: what the signature dictionary states ----

struct StatedSignature
{
    QString subFilter;
    QVector<int> byteRange;
    QByteArray contents;
    QString reason;
    QString declaredTime; // /M, as PDFium hands it out
    int docMdpPermission = 0;
};

QString fromUtf16Getter(const QVector<FPDF_WCHAR> &buffer, unsigned long bytes)
{
    if (bytes <= 2)
        return {};
    const int units = qMin<int>(int(bytes / 2), int(buffer.size())) - 1;
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()), units);
}

// The ASCII getters count the trailing NUL in the length they return.
QString asciiGetterResult(const char *raw, unsigned long bytes)
{
    if (bytes == 0)
        return {};
    return QString::fromLatin1(raw, int(bytes) - 1);
}

// Opens the file the way the backend does -- our own handle, FPDF_LoadCustomDocument over it --
// and reads every signature dictionary. Its own FPDF_DOCUMENT, so it never touches the handle the
// view is rendering with; the one lock still serialises the calls, which is why this runs on a
// worker thread and not on the UI one (src/document/document.h at the top).
QVector<StatedSignature> readWithPdfium(const QString &path, const QString &password,
                                        QString *why)
{
    QVector<StatedSignature> records;

    DWORD openCode = 0;
    const HANDLE handle = openSharedReadOnly(path, &openCode);
    if (handle == INVALID_HANDLE_VALUE) {
        *why = lastErrorText("CreateFileW");
        return records;
    }
    LARGE_INTEGER length;
    if (!GetFileSizeEx(handle, &length)
        || quint64(length.QuadPart) > std::numeric_limits<unsigned long>::max()) {
        *why = QStringLiteral("the file is empty or larger than this API can address");
        CloseHandle(handle);
        return records;
    }

    PdfFileHandle fileHandle; // closes the handle on every way out
    fileHandle.handle = handle;
    FPDF_FILEACCESS access{};
    access.m_FileLen = unsigned(length.QuadPart);
    access.m_GetBlock = &pdfiumReadBlock;
    access.m_Param = &fileHandle;

    QMutexLocker locker(&pdfiumMutex());
    acquirePdfium();
    const FPDF_DOCUMENT document = FPDF_LoadCustomDocument(
        &access, password.isEmpty() ? nullptr : password.toUtf8().constData());
    if (!document) {
        const unsigned long code = FPDF_GetLastError();
        releasePdfium();
        *why = QStringLiteral("FPDF_LoadCustomDocument failed, error %1").arg(code);
        return records;
    }

    const int listed = qMax(0, FPDF_GetSignatureCount(document));
    for (int index = 0; index < listed; ++index) {
        const FPDF_SIGNATURE signature = FPDF_GetSignatureObject(document, index);
        if (!signature)
            continue;
        int range[8] = {0};
        const unsigned long rangeNumbers = FPDFSignatureObj_GetByteRange(signature, range, 8);
        const unsigned long contentsBytes = FPDFSignatureObj_GetContents(signature, nullptr, 0);
        // The same empty-field test §122 added for the count, repeated here rather than trusted
        // from it: a record with no ranges and no bytes has nothing to check, and answering
        // "无法验证" about a field that was never signed would put a defect claim on a blank
        // template. It is skipped silently, exactly as the count skips it.
        if (rangeNumbers == 0 && contentsBytes == 0)
            continue;

        StatedSignature record;
        for (unsigned long slot = 0; slot < rangeNumbers; ++slot)
            record.byteRange.append(range[slot]);

        QByteArray contents(int(contentsBytes), '\0');
        if (contentsBytes > 0
            && FPDFSignatureObj_GetContents(signature, contents.data(), contentsBytes)
                   == contentsBytes)
            record.contents = contents;

        char name[128] = {0};
        record.subFilter = asciiGetterResult(name, FPDFSignatureObj_GetSubFilter(signature, name, 128));
        {
            QVector<FPDF_WCHAR> buffer(512);
            record.reason = fromUtf16Getter(
                buffer, FPDFSignatureObj_GetReason(
                            signature, buffer.data(),
                            static_cast<unsigned long>(buffer.size() * sizeof(FPDF_WCHAR))));
        }
        char text[128] = {0};
        record.declaredTime = asciiGetterResult(text, FPDFSignatureObj_GetTime(signature, text, 128));
        const unsigned int permission = FPDFSignatureObj_GetDocMDPPermission(signature);
        if (permission >= 1 && permission <= 3)
            record.docMdpPermission = int(permission);
        records.append(record);
    }

    FPDF_CloseDocument(document);
    releasePdfium();
    return records;
}

// ---- the qpdf half: which field, on which page, and where ----

struct SignatureField
{
    QString name;     // fully qualified /T, spelled the way the field tree spells it
    QString location; // /Location, which no PDFium getter answers
    QString reason;
    QString declaredTime;
    QVector<int> byteRange;
    int page = -1;
};

void collectSignatureFields(const QPDFObjectHandle &field, const QString &parent,
                            const std::vector<QPDFPageObjectHelper> &pages, int depth,
                            int &remaining, QVector<SignatureField> *out)
{
    if (depth > 32 || remaining <= 0 || !field.isDictionary())
        return;
    --remaining;
    QString name = parent;
    const QPDFObjectHandle part = field.getKey("/T");
    if (part.isString()) {
        const QString text = QString::fromUtf8(part.getUTF8Value());
        if (!text.isEmpty())
            name = parent.isEmpty() ? text : parent + QLatin1Char('.') + text;
    }
    const QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isArray()) {
        for (int index = 0; index < kids.getArrayNItems(); ++index)
            collectSignatureFields(kids.getArrayItem(index), name, pages, depth + 1, remaining,
                                   out);
    }
    const QPDFObjectHandle type = field.getKey("/FT");
    const QPDFObjectHandle value = field.getKey("/V");
    if (!(type.isName() && type.getName() == "/Sig" && value.isDictionary()))
        return; // an unsigned field, or a group node: nothing to report

    SignatureField record;
    record.name = name;
    const QPDFObjectHandle location = value.getKey("/Location");
    if (location.isString())
        record.location = QString::fromUtf8(location.getUTF8Value());
    const QPDFObjectHandle reason = value.getKey("/Reason");
    if (reason.isString())
        record.reason = QString::fromUtf8(reason.getUTF8Value());
    const QPDFObjectHandle time = value.getKey("/M");
    if (time.isString())
        record.declaredTime = QString::fromUtf8(time.getUTF8Value());
    const QPDFObjectHandle range = value.getKey("/ByteRange");
    if (range.isArray()) {
        for (int index = 0; index < range.getArrayNItems(); ++index) {
            const QPDFObjectHandle item = range.getArrayItem(index);
            if (item.isInteger())
                record.byteRange.append(int(item.getIntValue()));
        }
    }
    // Which page carries this widget. Object identity inside one open parse, which is what
    // sec.78's rule allows: both sides are the same file in the same parser, not two writes.
    for (size_t page = 0; page < pages.size(); ++page) {
        const QPDFObjectHandle annots = pages[page].getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            continue;
        bool found = false;
        for (int index = 0; index < annots.getArrayNItems() && !found; ++index)
            found = annots.getArrayItem(index).isSameObjectAs(field);
        if (found) {
            record.page = int(page);
            break;
        }
    }
    out->append(record);
}

QVector<SignatureField> readWithQpdf(const QString &path, const QString &password, QString *why)
{
    QVector<SignatureField> fields;
    QPDF document;
    try {
        document.processFile(path.toUtf8().constData(),
                             password.isEmpty() ? nullptr : password.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages
            = QPDFPageDocumentHelper(document).getAllPages();
        const QPDFObjectHandle form = document.getRoot().getKey("/AcroForm");
        if (!form.isDictionary())
            return fields; // a file with no interactive form has no signature fields
        const QPDFObjectHandle list = form.getKey("/Fields");
        if (!list.isArray())
            return fields;
        int remaining = 20000;
        for (int index = 0; index < list.getArrayNItems(); ++index)
            collectSignatureFields(list.getArrayItem(index), QString(), pages, 0, remaining,
                                   &fields);
    } catch (const std::exception &e) {
        *why = QString::fromUtf8(e.what());
        return {};
    }
    return fields;
}

// ---- the times ----

// PDF 32000-1 sec.7.9.4: D:YYYYMMDDHHmmSS+HH'MM', the components after SS optional, Z meaning
// UTC and no offset meaning the local zone. Only the digits are looked at.
QDateTime parsePdfDate(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("^D?:(\\d{4})(\\d{2})(\\d{2})(?:(\\d{2})(\\d{2})(\\d{2}))?"
                       "(Z|[+-](\\d{2})'?(\\d{2})'?)?$"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return {};
    const QDate date(match.captured(1).toInt(), match.captured(2).toInt(),
                     match.captured(3).toInt());
    const QTime time(match.captured(4).toInt(), match.captured(5).toInt(),
                     match.captured(6).toInt());
    if (!date.isValid() || !time.isValid())
        return {};
    const QString zone = match.captured(7);
    if (zone == QLatin1String("Z"))
        return QDateTime(date, time, QTimeZone::utc());
    if (zone.size() >= 3) {
        const int minutes = zone.mid(1, 2).toInt() * 60 + zone.mid(3, 2).toInt();
        const int offset = (zone.startsWith(QLatin1Char('-')) ? -minutes : minutes) * 60;
        return QDateTime(date, time, QTimeZone(offset));
    }
    return QDateTime(date, time); // no offset: the specification means the local zone
}

QDateTime fileTimeToUtc(const FILETIME &time)
{
    LARGE_INTEGER raw;
    raw.HighPart = time.dwHighDateTime;
    raw.LowPart = time.dwLowDateTime;
    static const qint64 epochShift = Q_INT64_C(116444736000000000);
    if (raw.QuadPart < epochShift)
        return {};
    return QDateTime::fromMSecsSinceEpoch((raw.QuadPart - epochShift) / Q_INT64_C(10000),
                                         Qt::UTC);
}

// Names, not sentences: whatever the certificate says about itself, shown as it is written.
QString certificateName(const CERT_CONTEXT *context, DWORD type, DWORD flags, LPCSTR keyword)
{
    if (!context)
        return {};
    wchar_t buffer[512] = {0};
    const int length
        = CertGetNameStringW(context, type, flags, const_cast<LPSTR>(keyword),
                                      buffer, 512);
    if (length <= 0)
        return {};
    // A certificate with no CN is an answer, not a failure: plenty of signing certificates put
    // their identity in O or in the serial number, and the panel then shows the issuer rather
    // than inventing a name.
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer), length - 1);
}

const CRYPT_ATTRIBUTE *findAttribute(const CRYPT_ATTRIBUTES &attributes, LPCSTR oid)
{
    for (DWORD index = 0; index < attributes.cAttr; ++index) {
        if (lstrcmpA(attributes.rgAttr[index].pszObjId, oid) == 0)
            return &attributes.rgAttr[index];
    }
    return nullptr;
}

// The one timestamp-token OID that wincrypt.h does not name: CMS id-ct-TSTInfo, the form PAdES
// and most non-Microsoft signers use for an RFC 3161 token. Written as the number because the
// number is what the file carries and there is no system constant to compare it with.
#define XYZPDF_OID_TST_INFO "1.2.840.113549.1.9.16.2.14"

// ---- the certificate chain ----

struct ChainAnswer
{
    bool built = false;
    bool trusted = false;
    bool usedInjectedStore = false;
    bool checkedOnline = false;
    QString signerName;
    QString issuerName;
    QStringList notes;
    QDateTime notBefore;
    QDateTime notAfter;
    quint32 chainErrorStatus = 0;
    quint32 policyError = 0;
};

ChainAnswer checkChain(const CERT_CONTEXT *signer, const QDateTime &at, const Options &options)
{
    ChainAnswer answer;
    if (!signer) {
        answer.notes << QStringLiteral("no certificate inside the signature");
        return answer;
    }
    answer.signerName = certificateName(signer, CERT_NAME_ATTR_TYPE, 0, szOID_COMMON_NAME);
    // CERT_NAME_ISSUER_FLAG is a dwFlags value, not a dwType one: passed as the type it asks for
    // a name of a kind the API does not have and comes back empty. Measured.
    answer.issuerName = certificateName(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                        CERT_NAME_ISSUER_FLAG, nullptr);
    answer.notBefore = fileTimeToUtc(signer->pCertInfo->NotBefore);
    answer.notAfter = fileTimeToUtc(signer->pCertInfo->NotAfter);

    HCERTCHAINENGINE engine = nullptr;
    bool ownsEngine = false;
    if (options.exclusiveRootStore) {
        CERT_CHAIN_ENGINE_CONFIG config{};
        config.cbSize = sizeof(config);
        // hExclusiveRoot replaces the system 受信任的根 store for this engine, and wincrypt.h is
        // explicit that the restricted stores are then not used at all -- which is the whole
        // reason a test can call one of its own roots trusted without writing anywhere.
        config.hExclusiveRoot = static_cast<HCERTSTORE>(options.exclusiveRootStore);
        config.dwFlags = options.checkRevocationOnline ? 0 : CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
        config.dwUrlRetrievalTimeout = options.checkRevocationOnline ? 8000 : 0;
        if (!CertCreateCertificateChainEngine(&config, &engine)) {
            answer.notes << lastErrorText("CertCreateCertificateChainEngine");
            return answer;
        }
        ownsEngine = true;
        answer.usedInjectedStore = true;
    } else {
        engine = HCCE_CURRENT_USER;
    }

    CERT_CHAIN_PARA chainPara{};
    chainPara.cbSize = sizeof(chainPara);
    FILETIME checkTime{};
    const qint64 ms = at.toMSecsSinceEpoch();
    const quint64 ticks = quint64(ms / 1000) * Q_INT64_C(10000000) + Q_INT64_C(116444736000000000);
    checkTime.dwLowDateTime = DWORD(ticks & 0xffffffff);
    checkTime.dwHighDateTime = DWORD(ticks >> 32);

    // The one variable between the two modes is whether the chain may go to the wire. Both ask
    // the revocation question; the default never leaves the machine (handoff-07 I1 3), and the
    // answer says which of the two it was.
    const DWORD flags = options.checkRevocationOnline
        ? CERT_CHAIN_REVOCATION_CHECK_CHAIN
        : (CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT
           | CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY);
    answer.checkedOnline = options.checkRevocationOnline;

    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (!CertGetCertificateChain(engine, signer, &checkTime, nullptr, &chainPara, flags, nullptr,
                                 &chain)) {
        answer.notes << lastErrorText("CertGetCertificateChain");
        if (ownsEngine)
            CertFreeCertificateChainEngine(engine);
        return answer;
    }
    answer.built = true;
    answer.chainErrorStatus = chain->TrustStatus.dwErrorStatus;

    CERT_CHAIN_POLICY_PARA policyPara{};
    policyPara.cbSize = sizeof(policyPara);
    CERT_CHAIN_POLICY_STATUS policyStatus{};
    policyStatus.cbSize = sizeof(policyStatus);
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policyPara,
                                          &policyStatus))
        answer.notes << lastErrorText("CertVerifyCertificateChainPolicy");
    else {
        answer.policyError = policyStatus.dwError;
        answer.trusted = policyStatus.dwError == 0;
    }
    CertFreeCertificateChain(chain);
    if (ownsEngine)
        CertFreeCertificateChainEngine(engine);
    return answer;
}

// ---- what the blob itself says ----

struct BlobAnswer
{
    bool decoded = false;
    QString digestOid;
    QString digestName;
    ALG_ID algId = 0;
    bool hasExpectedDigest = false;
    QByteArray expectedDigest;
    const CERT_CONTEXT *signerContext = nullptr; // owned; freed by the caller
    QString signerName;
    QString issuerName;
    QDateTime signingTime;      // the CMS signing-time attribute: the signer's own word
    bool timestampTokenPresent = false;
    QByteArray timestampToken;  // its bytes, for the check the caller runs against the file
    QStringList notes;
};

void inspectBlob(const QByteArray &contents, BlobAnswer *answer)
{
    if (contents.isEmpty()) {
        answer->notes << QStringLiteral("/Contents is empty");
        return;
    }
    CRYPT_DATA_BLOB blob{};
    blob.cbData = DWORD(contents.size());
    blob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(contents.constData()));
    DWORD encodingType = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    // Asked with every content type allowed, so that a blob that is not a signed message comes
    // back with its own number rather than as a refusal whose reason was thrown away.
    if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob, CERT_QUERY_CONTENT_FLAG_ALL,
                          CERT_QUERY_FORMAT_FLAG_ALL, 0, &encodingType, &contentType, &formatType,
                          nullptr, nullptr, nullptr)) {
        answer->notes << lastErrorText("CryptQueryObject");
        return;
    }
    if (contentType != CERT_QUERY_CONTENT_PKCS7_SIGNED) {
        answer->notes << QStringLiteral("the blob is content type %1, not a PKCS#7 signed "
                                       "message")
                             .arg(contentType);
        return;
    }
    answer->decoded = true;

    // The decoder is opened with a provider that knows SHA-256. Measured: with hCryptProv left
    // null, CMSG_CTRL_VERIFY_SIGNATURE_EX answers CRYPT_E_HASH_VALUE (0x80091007) on a signature
    // this same process produced with the very same key, which is the header's "gets default
    // provider according to the signer's public key OID" path landing on a provider without the
    // digest the signer used. Passing one makes the answer mean what it says.
    EphemeralProvider provider;
    provider.acquired = CryptAcquireContextW(&provider.handle, nullptr, MS_ENH_RSA_AES_PROV_W,
                                             PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    const HCRYPTMSG message
        = CryptMsgOpenToDecode(encodingType, 0, 0, provider.forDecoder(), nullptr, nullptr);
    if (!message) {
        answer->notes << lastErrorText("CryptMsgOpenToDecode");
        return;
    }
    if (!CryptMsgUpdate(message, blob.pbData, blob.cbData, TRUE)) {
        answer->notes << lastErrorText("CryptMsgUpdate");
        CryptMsgClose(message);
        return;
    }

    DWORD needed = 0;
    if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &needed) || needed == 0) {
        answer->notes << lastErrorText("CryptMsgGetParam(CMSG_SIGNER_INFO_PARAM size)");
        CryptMsgClose(message);
        return;
    }
    QByteArray signerBuffer(int(needed), '\0');
    if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0,
                          reinterpret_cast<BYTE *>(signerBuffer.data()), &needed)) {
        answer->notes << lastErrorText("CryptMsgGetParam(CMSG_SIGNER_INFO_PARAM)");
        CryptMsgClose(message);
        return;
    }
    const CMSG_SIGNER_INFO *signerInfo
        = reinterpret_cast<const CMSG_SIGNER_INFO *>(signerBuffer.constData());

    answer->digestOid = QString::fromLatin1(signerInfo->HashAlgorithm.pszObjId);
    if (!digestAlgorithmOf(answer->digestOid, &answer->algId, &answer->digestName)) {
        answer->notes << QStringLiteral("unsupported digest algorithm %1").arg(answer->digestOid);
        CryptMsgClose(message);
        return;
    }

    // The authenticated attributes come from their own parameters. Measured on this SDK,
    // CMSG_SIGNER_INFO_PARAM hands back a struct whose own AuthAttrs/UnauthAttrs fields are both
    // zero on a decoded message (auth=0, unauth=0) while the attributes are plainly inside the
    // DER, so the signer info is used for the digest algorithm and the signer identifier and the
    // two attribute sets are asked for separately.
    CRYPT_ATTRIBUTES authenticatedAttributes{};
    QByteArray authenticatedStorage;
    {
        DWORD attributeSize = 0;
        if (CryptMsgGetParam(message, CMSG_SIGNER_AUTH_ATTR_PARAM, 0, nullptr, &attributeSize)
            && attributeSize > 0) {
            authenticatedStorage.resize(int(attributeSize));
            if (CryptMsgGetParam(message, CMSG_SIGNER_AUTH_ATTR_PARAM, 0,
                                 reinterpret_cast<BYTE *>(authenticatedStorage.data()),
                                 &attributeSize))
                authenticatedAttributes = *reinterpret_cast<const CRYPT_ATTRIBUTES *>(
                    authenticatedStorage.constData());
        }
    }
    CRYPT_ATTRIBUTES unauthenticatedAttributes{};
    QByteArray unauthenticatedStorage;
    {
        DWORD attributeSize = 0;
        if (CryptMsgGetParam(message, CMSG_SIGNER_UNAUTH_ATTR_PARAM, 0, nullptr, &attributeSize)
            && attributeSize > 0) {
            unauthenticatedStorage.resize(int(attributeSize));
            if (CryptMsgGetParam(message, CMSG_SIGNER_UNAUTH_ATTR_PARAM, 0,
                                 reinterpret_cast<BYTE *>(unauthenticatedStorage.data()),
                                 &attributeSize))
                unauthenticatedAttributes = *reinterpret_cast<const CRYPT_ATTRIBUTES *>(
                    unauthenticatedStorage.constData());
        }
    }

    // The message digest the signer attests to. Its value is `OCTET STRING (Digest)`
    // (RFC 5652 5.4), so Windows' own decoder reads it and no DER is walked by hand here.
    const CRYPT_ATTRIBUTE *messageDigest
        = findAttribute(authenticatedAttributes, szOID_PKCS_9_MESSAGE_DIGEST);
    if (messageDigest && messageDigest->cValue > 0) {
        DWORD size = 0;
        if (CryptDecodeObject(X509_ASN_ENCODING, X509_OCTET_STRING,
                              messageDigest->rgValue[0].pbData,
                              messageDigest->rgValue[0].cbData, 0, nullptr, &size)
            && size > 0) {
            QByteArray storage(int(size), '\0');
            if (CryptDecodeObject(X509_ASN_ENCODING, X509_OCTET_STRING,
                                  messageDigest->rgValue[0].pbData,
                                  messageDigest->rgValue[0].cbData, 0,
                                  reinterpret_cast<BYTE *>(storage.data()), &size)) {
                const auto *digest
                    = reinterpret_cast<const CRYPT_DATA_BLOB *>(storage.constData());
                answer->expectedDigest = QByteArray(
                    reinterpret_cast<const char *>(digest->pbData), int(digest->cbData));
                answer->hasExpectedDigest = !answer->expectedDigest.isEmpty();
            }
        }
    }
    if (!answer->hasExpectedDigest)
        answer->notes << QStringLiteral(
            "the signed attributes carry no message digest (auth=%1 unauth=%2)")
                             .arg(authenticatedAttributes.cAttr)
                             .arg(unauthenticatedAttributes.cAttr);

    // The signing time (PKCS #9 signing-time: a UTCTime or a GeneralizedTime, and
    // X509_CHOICE_OF_TIME decodes either into a FILETIME).
    const CRYPT_ATTRIBUTE *signingTime
        = findAttribute(authenticatedAttributes, szOID_RSA_signingTime);
    if (signingTime && signingTime->cValue > 0) {
        FILETIME time{};
        DWORD size = sizeof(time);
        if (CryptDecodeObject(X509_ASN_ENCODING, X509_CHOICE_OF_TIME,
                              signingTime->rgValue[0].pbData, signingTime->rgValue[0].cbData, 0,
                              reinterpret_cast<BYTE *>(&time), &size))
            answer->signingTime = fileTimeToUtc(time);
    }

    // A timestamp token says the time on somebody else's authority, so its presence is a fact of
    // its own whichever way round the token turns out to read. Three OIDs carry it in practice:
    // Microsoft's counter-signature, the older PKCS#9 one and CMS id-ct-TSTInfo.
    for (LPCSTR tokenOid : { szOID_RFC3161_counterSign, XYZPDF_OID_TST_INFO,
                             szOID_RSA_counterSign }) {
        const CRYPT_ATTRIBUTE *token = findAttribute(unauthenticatedAttributes, tokenOid);
        if (!token || token->cValue <= 0)
            token = findAttribute(authenticatedAttributes, tokenOid);
        if (!token || token->cValue <= 0)
            continue;
        answer->timestampTokenPresent = true;
        answer->timestampToken = QByteArray(
            reinterpret_cast<const char *>(token->rgValue[0].pbData),
            int(token->rgValue[0].cbData));
        break;
    }

    // The signer's certificate, taken out of the blob itself: nothing here reads or writes a
    // store, the context is created from the bytes the signature carries.
    DWORD certificates = 0;
    if (CryptMsgGetParam(message, CMSG_CERT_COUNT_PARAM, 0,
                         reinterpret_cast<BYTE *>(&certificates), &needed)) {
        for (DWORD index = 0; index < certificates && !answer->signerContext; ++index) {
            needed = 0;
            if (!CryptMsgGetParam(message, CMSG_CERT_PARAM, index, nullptr, &needed) || needed == 0)
                continue;
            QByteArray storage(int(needed), '\0');
            if (!CryptMsgGetParam(message, CMSG_CERT_PARAM, index,
                                  reinterpret_cast<BYTE *>(storage.data()), &needed))
                continue;
            const CERT_CONTEXT *candidate = CertCreateCertificateContext(
                X509_ASN_ENCODING, reinterpret_cast<const BYTE *>(storage.constData()),
                DWORD(storage.size()));
            if (!candidate)
                continue;
            // Matched to the signer by issuer and serial number, which is what the SignerInfo
            // names (RFC 5652 5.1) -- the first certificate in the bag is not the signer's by
            // right of position.
            const bool matches
                = candidate->pCertInfo->SerialNumber.cbData == signerInfo->SerialNumber.cbData
                && memcmp(candidate->pCertInfo->SerialNumber.pbData,
                          signerInfo->SerialNumber.pbData, signerInfo->SerialNumber.cbData) == 0
                && candidate->pCertInfo->Issuer.cbData == signerInfo->Issuer.cbData
                && memcmp(candidate->pCertInfo->Issuer.pbData, signerInfo->Issuer.pbData,
                          signerInfo->Issuer.cbData) == 0;
            if (matches)
                answer->signerContext = candidate;
            else
                CertFreeCertificateContext(candidate);
        }
    } else {
        answer->notes << lastErrorText("CryptMsgGetParam(CMSG_CERT_COUNT_PARAM)");
    }

    if (answer->signerContext) {
        answer->signerName = certificateName(answer->signerContext, CERT_NAME_ATTR_TYPE, 0,
                                              szOID_COMMON_NAME);
        answer->issuerName = certificateName(answer->signerContext,
                                             CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                             CERT_NAME_ISSUER_FLAG, nullptr);
        // The signature block itself is checked by the caller, which has the covered bytes:
        // see verifySignatureBlock(). A detached message carries no content for this decoder to
        // hash, which is why the two checks are made in two places and reported apart.
    } else {
        answer->notes << QStringLiteral("the blob names no certificate it carries");
    }

    CryptMsgClose(message);
}

// The token branch. Windows reads a TimeStampToken and checks its imprint; what it is asked to
// match is the signed content, so the bytes fed in here are the same spans the digest covers.
// When it says no -- an unreadable token, an imprint that does not match -- the caller keeps the
// declared time and the detail line says which of the two happened.
QDateTime verifyTimestampToken(const QByteArray &token, const QByteArray &covered,
                               QString *note)
{
    if (token.isEmpty() || covered.isEmpty()) {
        *note = QStringLiteral("timestamp token or covered bytes missing");
        return {};
    }
    CRYPT_TIMESTAMP_CONTEXT *context = nullptr;
    const CERT_CONTEXT *signer = nullptr;
    HCERTSTORE store = nullptr;
    const BOOL ok = CryptVerifyTimeStampSignature(
        reinterpret_cast<const BYTE *>(token.constData()), DWORD(token.size()),
        reinterpret_cast<const BYTE *>(covered.constData()), DWORD(covered.size()), nullptr,
        &context, &signer, &store);
    QDateTime answer;
    if (ok && context && context->pTimeStamp)
        answer = fileTimeToUtc(context->pTimeStamp->ftTime);
    else
        *note = lastErrorText("CryptVerifyTimeStampSignature");
    if (context)
        CryptMemFree(context);
    if (signer)
        CertFreeCertificateContext(signer);
    if (store)
        CertCloseStore(store, 0);
    return answer;
}

// ---- the two facts beside the verdict ----

// The number of `startxref` tokens in the bytes past the signed ranges. Every revision of a PDF
// ends with one, and the revision that was signed ends *inside* the ranges -- /ByteRange's second
// span runs to the end of the file as it was when the signing happened -- so a token found past
// that end belongs to a later revision. It is a count of markers, which is what the bytes offer:
// a page whose own content carries the word would be counted, and the line this feeds says
// "签名之后还有 N 次增量更新" rather than claiming to know what those updates did.
int countRevisionsAfter(HANDLE handle, qint64 from, qint64 to)
{
    if (to <= from)
        return 0;
    LARGE_INTEGER position;
    position.QuadPart = from;
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
        return 0;
    QVector<char> chunk(64 * 1024);
    QByteArray carried;
    int total = 0;
    qint64 remaining = to - from;
    while (remaining > 0) {
        const DWORD ask = int(qMin<qint64>(remaining, chunk.size()));
        DWORD got = 0;
        if (!ReadFile(handle, chunk.data(), ask, &got, nullptr) || got == 0)
            break;
        remaining -= got;
        // A marker can straddle a chunk boundary, so the nine bytes before it are looked at again
        // in front of the next chunk.
        QByteArray window = carried;
        window.append(chunk.constData(), int(got));
        total += window.count("startxref");
        carried = window.right(9);
    }
    return total;
}

struct Coverage
{
    bool gap = false;
    qint64 before = 0;
    qint64 hole = 0;
    qint64 after = 0;
};

// /ByteRange's hole is the /Contents string and nothing else is supposed to be missing: the shape
// PDF 32000-1 table 253 states is two pairs, the first starting at 0 and the second running to
// the end of the signed revision, with the hex string in between. Anything else is a signature
// whose own claim skips bytes, so "the covered bytes have not moved" is worth less than it looks
// and the panel says so.
//
// The hole is judged against 2n+2 because /Contents is written as a hexadecimal string: n bytes
// of PKCS#7 are 2n hex digits inside a pair of angle brackets. A writer that used a literal
// string with escapes makes the hole bigger than that and is reported as a gap, which is the
// honest direction to be wrong in -- it warns rather than nods.
Coverage coverageOf(const QVector<int> &byteRange, qint64 contentsSize, qint64 fileSize)
{
    Coverage coverage;
    if (byteRange.size() < 4 || byteRange.size() % 2 != 0) {
        coverage.gap = true;
        return coverage;
    }
    coverage.before = byteRange.at(0);
    for (int pair = 0; pair + 3 < byteRange.size(); ++pair += 2)
        coverage.hole += byteRange.at(pair + 2) - (byteRange.at(pair) + byteRange.at(pair + 1));
    const qint64 expected = (2 * contentsSize + 2) * (byteRange.size() / 2 - 1);
    coverage.after = fileSize - (byteRange.at(byteRange.size() - 2)
                                 + byteRange.at(byteRange.size() - 1));
    coverage.gap = coverage.before > 0 || coverage.hole > expected || coverage.after < 0;
    return coverage;
}

} // namespace

namespace SignatureVerify {

QString verdictLabel(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Valid:
        return QCoreApplication::translate("SignatureVerify", "有效");
    case Verdict::ContentModified:
        return QCoreApplication::translate("SignatureVerify", "内容被改过");
    case Verdict::CertificateProblem:
        return QCoreApplication::translate("SignatureVerify", "证书不受信任或已过期");
    case Verdict::Unverifiable:
        return QCoreApplication::translate("SignatureVerify", "无法验证");
    }
    return QCoreApplication::translate("SignatureVerify", "无法验证");
}

QString Signature::verdictLabel() const
{
    return SignatureVerify::verdictLabel(verdict);
}

QString Signature::timeText() const
{
    if (!time.isValid())
        return {};
    const QString stamp = time.toString(Qt::ISODate);
    switch (timeSource) {
    case TimeSource::TimestampToken:
        return QCoreApplication::translate("SignatureVerify", "%1（时间戳）").arg(stamp);
    case TimeSource::DeclaredBySigner:
    case TimeSource::FileTimeField:
        return QCoreApplication::translate("SignatureVerify", "%1（签名者自己声明的时间）")
            .arg(stamp);
    case TimeSource::None:
        return {};
    }
    return {};
}

QStringList Signature::detailLines() const
{
    QStringList lines;
    if (!signerName.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "签名人：%1").arg(signerName);
    if (!issuerName.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "颁发者：%1").arg(issuerName);
    if (page >= 0)
        lines << QCoreApplication::translate("SignatureVerify", "在第 %1 页").arg(page + 1);
    if (!fieldName.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "字段：%1").arg(fieldName);
    if (!subFilter.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "签名格式：%1").arg(subFilter);
    if (!digestAlgorithm.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "摘要算法：%1").arg(digestAlgorithm);
    if (!reason.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "原因：%1").arg(reason);
    if (!location.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "地点：%1").arg(location);
    if (!timeText().isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "时间：%1").arg(timeText());
    if (docMdpPermission > 0)
        lines << QCoreApplication::translate("SignatureVerify",
                                            "这是文档认证签名，保存时只允许第 %n 级改动。", nullptr,
                                            docMdpPermission);
    if (revisionsAfter > 0)
        lines << QCoreApplication::translate("SignatureVerify", "签名之后还有 %n 次增量更新。",
                                             nullptr, revisionsAfter);
    if (coverageGap)
        lines << QCoreApplication::translate(
            "SignatureVerify", "这份签名没有覆盖它自己那一次修订的全部字节。");
    lines << (digestMatches
                  ? QCoreApplication::translate("SignatureVerify",
                                                "被覆盖的字节与签名里的摘要一致。")
                  : QCoreApplication::translate("SignatureVerify",
                                                "被覆盖的字节与签名里的摘要不一致。"));
    // The second half is only said when the first one passed. The detached verifier recomputes the
    // digest over the content itself, so once the bytes have moved it has nothing left to answer:
    // "the signature block did not verify" would read as a second finding when it is the same one.
    if (digestMatches)
        lines << (signatureBlockVerifies
                      ? QCoreApplication::translate("SignatureVerify",
                                                    "签名块能用这张证书的公钥验过。")
                      : QCoreApplication::translate("SignatureVerify",
                                                    "签名块没能用这张证书的公钥验过。"));
    if (chainBuilt)
        lines << (chainTrusted
                      ? QCoreApplication::translate("SignatureVerify", "证书链受信任。")
                      : QCoreApplication::translate("SignatureVerify", "证书链不受信任。"));
    else if (!signerName.isEmpty() || !technicalNote.isEmpty())
        lines << QCoreApplication::translate("SignatureVerify", "建不起证书链。");
    lines << (revocationCheckedOnline
                  ? QCoreApplication::translate("SignatureVerify", "已联网检查过吊销状态。")
                  : QCoreApplication::translate("SignatureVerify", "未联网检查吊销状态。"));
    if (timestampTokenPresent)
        lines << (timeSource == TimeSource::TimestampToken
                      ? QCoreApplication::translate("SignatureVerify",
                                                    "签名里带着时间戳令牌，时间取的是令牌里的。")
                      : QCoreApplication::translate(
                            "SignatureVerify", "签名里带着时间戳令牌，但这一份没能通过校验。"));
    if (chainUsedInjectedRootStore)
        lines << QCoreApplication::translate(
            "SignatureVerify", "信任来自本次调用注入的根存储，不是这台机器上的。");
    if (!technicalNote.isEmpty())
        lines << technicalNote;
    return lines;
}

int countWithVerdict(const QVector<Signature> &signatures, Verdict verdict)
{
    int total = 0;
    for (const Signature &one : signatures) {
        if (one.verdict == verdict)
            ++total;
    }
    return total;
}

QString summary(const QVector<Signature> &signatures)
{
    if (signatures.isEmpty())
        return {};
    // The order the fragments are appended in is the order a reader has to see them in: the one
    // that means the document itself changed comes first, whatever the file's own order is
    // (handoff-07 I1 4: 有"内容被改过"的，那一句排在最前面).
    struct Fragment {
        Verdict verdict;
        const char *one;
        const char *many;
    };
    static const Fragment fragments[] = {
        { Verdict::ContentModified, "%n 个内容被改过", "%n 个内容被改过" },
        { Verdict::Unverifiable, "%n 个无法验证", "%n 个无法验证" },
        { Verdict::CertificateProblem, "%n 个证书不受信任或已过期",
          "%n 个证书不受信任或已过期" },
        { Verdict::Valid, "%n 个有效", "%n 个有效" },
    };
    QStringList parts;
    for (const Fragment &fragment : fragments) {
        const int count = countWithVerdict(signatures, fragment.verdict);
        if (count <= 0)
            continue;
        parts << QCoreApplication::translate("SignatureVerify", fragment.one, fragment.many,
                                             count);
    }
    // Chinese has one form for every count, so each pair above is the same string; that is what a
    // numerus entry needs, and check_translations.py's 两形相同 rule only bites in the English
    // catalogue, where the two differ.
    return QCoreApplication::translate("SignatureVerify",
                                       "这份文档带着 %n 个数字签名：", nullptr,
                                       signatures.size())
        + parts.join(QStringLiteral("，"))
        + QStringLiteral("。");
}

QVector<Signature> verifyDocument(const QString &path, const QString &password,
                                  const Options &options)
{
    QVector<Signature> results;
    const QFileInfo info(path);
    const qint64 fileSize = info.size();
    if (!info.exists() || fileSize <= 0)
        return results;

    QString pdfiumWhy;
    const QVector<StatedSignature> stated = readWithPdfium(path, password, &pdfiumWhy);
    QString qpdfWhy;
    const QVector<SignatureField> fields = readWithQpdf(path, password, &qpdfWhy);

    if (stated.isEmpty() && (!pdfiumWhy.isEmpty() || !qpdfWhy.isEmpty())) {
        // Nothing was read, and the file is not simply unsigned -- one of the two readers could
        // not get in at all. That is a reportable answer rather than a silent empty list, because
        // an empty list is what an unsigned document produces and the panel would say "no
        // signatures" about a document it never looked at.
        Signature record;
        record.fileSize = fileSize;
        record.verdict = Verdict::Unverifiable;
        record.technicalNote = pdfiumWhy.isEmpty() ? qpdfWhy : pdfiumWhy;
        results.append(record);
        return results;
    }

    DWORD openCode = 0;
    const HANDLE file = openSharedReadOnly(path, &openCode);

    for (const StatedSignature &one : stated) {
        Signature record;
        record.subFilter = one.subFilter;
        record.byteRange = one.byteRange;
        record.reason = one.reason;
        record.docMdpPermission = one.docMdpPermission;
        record.fileSize = fileSize;
        record.verdict = Verdict::Unverifiable;

        // The join: this dictionary's own /ByteRange numbers, matched against the field that
        // states the same numbers. Not the index either library happened to use.
        for (const SignatureField &candidate : fields) {
            if (candidate.byteRange != one.byteRange)
                continue;
            record.fieldName = candidate.name;
            record.page = candidate.page;
            record.location = candidate.location;
            if (record.reason.isEmpty())
                record.reason = candidate.reason;
            break;
        }

        // The two facts the file states, decided before any cryptography is asked about: they
        // hold whatever the answer to the digest turns out to be.
        if (record.byteRange.size() >= 4) {
            const qint64 lastEnd = record.byteRange.at(record.byteRange.size() - 2)
                + record.byteRange.at(record.byteRange.size() - 1);
            record.bytesAfterRanges = qMax(qint64(0), fileSize - lastEnd);
            if (file != INVALID_HANDLE_VALUE)
                record.revisionsAfter
                    = countRevisionsAfter(file, qMin(lastEnd, fileSize), fileSize);
            const Coverage coverage = coverageOf(record.byteRange, one.contents.size(), fileSize);
            record.coverageGap = coverage.gap;
            record.uncoveredBytesBefore = coverage.before;
            record.uncoveredBytesInHole = coverage.hole;
            record.uncoveredBytesAfter = coverage.after;
        }

        if (!subFilterIsSupported(record.subFilter)) {
            record.technicalNote = QCoreApplication::translate(
                "SignatureVerify", "不支持的签名格式：%1")
                .arg(record.subFilter.isEmpty()
                         ? QCoreApplication::translate("SignatureVerify", "（文件没有写出签名格式）")
                         : record.subFilter);
            results.append(record);
            continue;
        }
        if (file == INVALID_HANDLE_VALUE) {
            record.technicalNote = lastErrorText("CreateFileW");
            results.append(record);
            continue;
        }

        BlobAnswer blob;
        inspectBlob(one.contents, &blob);
        record.digestAlgorithm = blob.digestName.isEmpty() ? blob.digestOid : blob.digestName;
        record.timestampTokenPresent = blob.timestampTokenPresent;

        QVector<QByteArray> spans;
        QString readWhy;
        spans = readCoveredSpans(file, record.byteRange, &readWhy);
        if (spans.isEmpty()) {
            record.technicalNote = readWhy.isEmpty() ? blob.notes.join(QStringLiteral(" / "))
                                                     : readWhy;
            results.append(record);
            continue;
        }

        QByteArray computed;
        QString hashWhy;
        if (blob.digestName.isEmpty() || !hashSpans(spans, blob.algId, &computed, &hashWhy)) {
            record.technicalNote = (hashWhy.isEmpty() ? blob.notes.join(QStringLiteral(" / "))
                                                      : hashWhy);
            results.append(record);
            continue;
        }
        if (!blob.hasExpectedDigest) {
            record.technicalNote = blob.notes.join(QStringLiteral(" / "));
            results.append(record);
            continue;
        }
        record.digestMatches = computed == blob.expectedDigest;

        // Check 2, and only asked when the first one says the bytes are what they were: if the
        // document changed, the answer the reader needs is already in hand, and a second failure
        // would only blur which of the two it was.
        if (record.digestMatches && blob.signerContext) {
            QString verifyWhy;
            record.signatureBlockVerifies
                = verifySignatureBlock(one.contents, spans, &verifyWhy);
            if (!record.signatureBlockVerifies && record.technicalNote.isEmpty())
                record.technicalNote = verifyWhy;
        }

        // The signing time, and whose word it is. A verified token beats the signer's own
        // attribute, which beats the dictionary's /M -- and the line says which of the three the
        // reader is looking at, because only the first is a third party's claim.
        if (blob.timestampTokenPresent) {
            QByteArray covered;
            for (const QByteArray &span : spans)
                covered += span;
            QString tokenNote;
            record.time = verifyTimestampToken(blob.timestampToken, covered, &tokenNote);
            if (record.time.isValid()) {
                record.timeSource = TimeSource::TimestampToken;
            } else if (record.technicalNote.isEmpty()) {
                record.technicalNote = tokenNote;
            }
        }
        if (record.timeSource == TimeSource::None && blob.signingTime.isValid()) {
            record.time = blob.signingTime;
            record.timeSource = TimeSource::DeclaredBySigner;
        }
        if (record.timeSource == TimeSource::None) {
            const QDateTime fromFile = parsePdfDate(one.declaredTime);
            if (fromFile.isValid()) {
                record.time = fromFile;
                record.timeSource = TimeSource::FileTimeField;
            }
        }

        // The chain, asked about the moment the signature claims: a certificate that expired
        // afterwards does not retroactively break the document, and a certificate that was
        // already expired when it signed is a real problem. Options::timeToCheck wins when a
        // caller passes one, and the detail line names the moment that was used.
        QDateTime moment = options.timeToCheck;
        if (!moment.isValid())
            moment = record.time;
        if (!moment.isValid())
            moment = QDateTime::currentDateTimeUtc();

        const ChainAnswer chain = checkChain(blob.signerContext, moment, options);
        record.chainBuilt = chain.built;
        record.chainTrusted = chain.trusted;
        record.chainUsedInjectedRootStore = chain.usedInjectedStore;
        record.revocationCheckedOnline = chain.checkedOnline;
        record.chainTimeUsed = moment;
        record.signerName = blob.signerName.isEmpty() ? chain.signerName : blob.signerName;
        record.issuerName = blob.issuerName.isEmpty() ? chain.issuerName : blob.issuerName;

        QStringList notes = blob.notes;
        notes.append(chain.notes);
        if (record.technicalNote.isEmpty())
            record.technicalNote = notes.join(QStringLiteral(" / "));
        record.chainErrorStatus = chain.chainErrorStatus;
        record.policyError = chain.policyError;

        // The verdict, and its order of severity: the document's own bytes coming apart is the
        // finding a reader has to hear first, before any question about who signed it.
        if (!record.digestMatches)
            record.verdict = Verdict::ContentModified;
        else if (!record.signatureBlockVerifies || !blob.decoded)
            record.verdict = Verdict::Unverifiable;
        else if (chain.trusted)
            record.verdict = Verdict::Valid;
        else
            record.verdict = Verdict::CertificateProblem;

        if (blob.signerContext)
            CertFreeCertificateContext(blob.signerContext);
        results.append(record);
    }

    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    return results;
}

} // namespace SignatureVerify
