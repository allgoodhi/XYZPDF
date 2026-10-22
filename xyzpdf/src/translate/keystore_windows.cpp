// The real key store: the Windows credential manager.
//
// Separate from keystore.cpp so that the interface, the in-memory implementation
// and the migration can be linked into tests that must not touch a machine's
// credentials. This file is linked only into the program and into the manual
// probe; no automated test calls into it.
//
// CredWriteW stores an opaque blob, so the key is written as UTF-16 and read
// back the same way. CRED_PERSIST_LOCAL_MACHINE keeps it for this user on this
// machine, which is the lifetime a desktop program's API key wants: it survives
// restarts and is not carried to another account.
#include "keystore.h"

#include <QCoreApplication>

#include <windows.h>
#include <wincred.h>

#include <string>

namespace {

QString failureMessage(DWORD code)
{
    return QCoreApplication::translate("Translate", "无法访问 Windows 凭据管理器（错误 %1）。")
        .arg(qulonglong(code));
}

// One helper so read, write and delete cannot disagree about which provider
// names are addressable. An empty id would produce the target "XYZPDF/translate/"
// -- a shared slot for a state that has no service -- so it is refused rather
// than written.
bool targetFor(const QString &providerId, std::wstring *target, QString *error)
{
    if (providerId.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("Translate", "没有选择翻译服务。");
        return false;
    }
    *target = Translate::credentialTarget(providerId).toStdWString();
    return true;
}

class WindowsKeyStore final : public Translate::KeyStore
{
public:
    bool read(const QString &providerId, QString *key, QString *error) override
    {
        std::wstring target;
        if (!targetFor(providerId, &target, error))
            return false;

        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            const DWORD code = GetLastError();
            if (code == ERROR_NOT_FOUND) {
                key->clear();
                return false;
            }
            if (error)
                *error = failureMessage(code);
            return false;
        }

        const auto *text = reinterpret_cast<const char16_t *>(credential->CredentialBlob);
        *key = QString::fromUtf16(text, credential->CredentialBlobSize / sizeof(wchar_t));
        CredFree(credential);
        return true;
    }

    bool write(const QString &providerId, const QString &key, QString *error) override
    {
        if (key.isEmpty())
            return remove(providerId, error);

        std::wstring target;
        if (!targetFor(providerId, &target, error))
            return false;

        const std::wstring blob = key.toStdWString();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.CredentialBlobSize = DWORD(blob.size() * sizeof(wchar_t));
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t *>(blob.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<LPWSTR>(L"XYZPDF");

        if (!CredWriteW(&credential, 0)) {
            if (error)
                *error = failureMessage(GetLastError());
            return false;
        }
        return true;
    }

    bool remove(const QString &providerId, QString *error) override
    {
        std::wstring target;
        if (!targetFor(providerId, &target, error))
            return false;

        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
            return true;
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) // already gone is the outcome that was asked for
            return true;
        if (error)
            *error = failureMessage(code);
        return false;
    }
};

} // namespace

namespace Translate {

std::unique_ptr<KeyStore> makeSystemKeyStore()
{
    return std::make_unique<WindowsKeyStore>();
}

} // namespace Translate
