#include "packageidentity.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <appmodel.h>
#include <bcrypt.h>

namespace PackageIdentity {
namespace {

// SHA-256 of the publisher string as UTF-16LE, which is how Windows sees it.
bool sha256(const std::wstring &text, unsigned char digest[32])
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return false;

    bool ok = false;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        const auto bytes = reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(text.data()));
        const ULONG length = static_cast<ULONG>(text.size() * sizeof(wchar_t));
        ok = BCryptHashData(hash, bytes, length, 0) == 0
             && BCryptFinishHash(hash, digest, 32, 0) == 0;
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

} // namespace

std::wstring familyName(const std::wstring &identityName, const std::wstring &publisher)
{
    unsigned char digest[32] = {};
    if (!sha256(publisher, digest))
        return std::wstring();

    // The first eight bytes, big-endian.
    unsigned long long value = 0;
    for (int index = 0; index < 8; ++index)
        value = (value << 8) | digest[index];

    // Thirteen groups of five bits is sixty-five, and there are only
    // sixty-four, so a zero bit is appended -- which is why the last character
    // comes from four bits shifted up rather than five.
    // Crockford's base32, not the usual RFC 4648 alphabet: digits first, and
    // i, l, o and u left out so nothing reads as a typo of something else.
    // The ordinary alphabet produces thirteen plausible characters that are
    // simply the wrong thirteen, and nothing looks wrong until the package is
    // never found.
    static const wchar_t alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    std::wstring publisherId(13, L'\0');
    publisherId[12] = alphabet[(value & 0x0F) << 1];
    value >>= 4;
    for (int index = 11; index >= 0; --index) {
        publisherId[index] = alphabet[value & 0x1F];
        value >>= 5;
    }

    return identityName + L'_' + publisherId;
}

bool isInstalled(const std::wstring &packageFamilyName)
{
    if (packageFamilyName.empty())
        return false;

    // Asked of the app model rather than the registry: a packaged COM
    // registration is in the state repository, where nothing under
    // HKCU\\Software\\Classes reflects it.
    UINT32 count = 0;
    UINT32 bufferLength = 0;
    const LONG status = GetPackagesByPackageFamily(packageFamilyName.c_str(), &count, nullptr,
                                                  &bufferLength, nullptr);
    if (status == ERROR_SUCCESS)
        return count > 0;
    // The buffer is too small precisely when there is something to put in it.
    return status == ERROR_INSUFFICIENT_BUFFER && count > 0;
}

} // namespace PackageIdentity
