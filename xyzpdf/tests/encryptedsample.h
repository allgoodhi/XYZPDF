// A one-page PDF encrypted with a user password, built in memory from the
// specification -- for the tests that need "an encrypted file" and cannot link qpdf
// to make one (thumbnailprovider_smoke is plain Win32: a Qt module loaded into its
// process before the provider DLL would hide a missing delay-load entry).
//
// ---- the shape, from PDF 32000-1 sec.7.6.3, not from the code under test ----
//
// Standard security handler, /V 1 /R 2 (40-bit RC4), the oldest form every reader
// accepts:
//
//   * Algorithm 3 -- /O: MD5 of the padded owner password, first 5 bytes, as an RC4
//     key over the padded user password.
//   * Algorithm 2 -- the file key: MD5 of the padded user password, /O, /P as four
//     little-endian bytes and the first /ID string; first 5 bytes.
//   * Algorithm 4 -- /U: the 32 padding bytes, RC4-encrypted under the file key.
//
// The document holds no strings and no streams (a page with a /MediaBox and no
// /Contents), so nothing beyond /O, /U and /ID -- which are never encrypted -- needs
// the object keys of Algorithm 1. It is still an encrypted document in every sense a
// reader checks: without a password the empty-password test of Algorithm 6 fails and
// the file does not open, and with kUserPassword (or kOwnerPassword) it does.
// pdfiumbackend_smoke asserts both, so a sample that stopped being encrypted (or
// stopped opening with its password) is caught there before thumbnailprovider_smoke
// leans on it.
//
// MD5 comes from Windows' CNG (bcrypt.lib), so the header carries no dependency
// either test does not already have on this platform.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace encryptedsample {

constexpr const char kUserPassword[] = "hunter2";
constexpr const char kOwnerPassword[] = "sample-owner";

inline std::vector<unsigned char> md5(const std::vector<unsigned char> &data)
{
    std::vector<unsigned char> digest(16, 0);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0)
        return {};
    BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data.data()), ULONG(data.size()),
               digest.data(), ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return digest;
}

inline std::vector<unsigned char> rc4(const std::vector<unsigned char> &key,
                                      const std::vector<unsigned char> &data)
{
    unsigned char state[256];
    for (int i = 0; i < 256; ++i)
        state[i] = static_cast<unsigned char>(i);
    for (int i = 0, j = 0; i < 256; ++i) {
        j = (j + state[i] + key[i % key.size()]) & 0xff;
        const unsigned char swap = state[i];
        state[i] = state[j];
        state[j] = swap;
    }
    std::vector<unsigned char> out(data.size());
    for (size_t n = 0, i = 0, j = 0; n < data.size(); ++n) {
        i = (i + 1) & 0xff;
        j = (j + state[i]) & 0xff;
        const unsigned char swap = state[i];
        state[i] = state[j];
        state[j] = swap;
        out[n] = data[n] ^ state[(state[i] + state[j]) & 0xff];
    }
    return out;
}

// The 32 bytes of Algorithm 2 step (a), and a password padded or cut to them.
inline std::vector<unsigned char> padded(const char *password)
{
    static const unsigned char kPadding[32] = {
        0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E,
        0x56, 0xFF, 0xFA, 0x01, 0x08, 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68,
        0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};
    std::vector<unsigned char> out(password, password + std::strlen(password));
    if (out.size() > 32)
        out.resize(32);
    out.insert(out.end(), kPadding, kPadding + (32 - out.size()));
    return out;
}

inline std::string hex(const std::vector<unsigned char> &bytes)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char byte : bytes) {
        out += kDigits[byte >> 4];
        out += kDigits[byte & 15];
    }
    return out;
}

// The file's bytes. Empty only if CNG would not compute an MD5.
inline std::string bytes()
{
    const long permissions = -4; // every operation allowed; the bits are not the point
    const std::vector<unsigned char> id = {0x58, 0x59, 0x5A, 0x50, 0x44, 0x46, 0x2D, 0x31,
                                           0x33, 0x33, 0x2D, 0x65, 0x6E, 0x63, 0x72, 0x79};

    // Algorithm 3: /O.
    std::vector<unsigned char> ownerKey = md5(padded(kOwnerPassword));
    if (ownerKey.size() != 16)
        return {};
    ownerKey.resize(5);
    const std::vector<unsigned char> owner = rc4(ownerKey, padded(kUserPassword));

    // Algorithm 2: the file key.
    std::vector<unsigned char> input = padded(kUserPassword);
    input.insert(input.end(), owner.begin(), owner.end());
    const unsigned long p = static_cast<unsigned long>(permissions);
    for (int shift = 0; shift < 32; shift += 8)
        input.push_back(static_cast<unsigned char>((p >> shift) & 0xff));
    input.insert(input.end(), id.begin(), id.end());
    std::vector<unsigned char> key = md5(input);
    if (key.size() != 16)
        return {};
    key.resize(5);

    // Algorithm 4: /U.
    const std::vector<unsigned char> user = rc4(key, padded(""));

    std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << >> >>",
        "<< /Filter /Standard /V 1 /R 2 /O <" + hex(owner) + "> /U <" + hex(user)
            + "> /P " + std::to_string(permissions) + " >>",
    };

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    for (size_t index = 0; index < objects.size(); ++index) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t offset : offsets) {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offset);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1)
           + " /Root 1 0 R /Encrypt 4 0 R /ID [<" + hex(id) + "> <" + hex(id) + ">] >>\n";
    pdf += "startxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

} // namespace encryptedsample
