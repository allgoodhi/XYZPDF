// Where the translation key lives.
//
// ---- why this is an interface ----
//
// Three things read and write the key -- the settings dialog, the translate
// path, and the one-time migration of the plaintext key older versions kept in
// QSettings -- and two of them need to be testable. The Windows credential
// manager is a real, machine-wide store: an automated test that wrote into it
// would leave an entry on whoever ran the suite, and one that read from it could
// pick up a real key and send a request nobody asked for. So the storage is an
// interface, production fills it with Cred*W (keystore_windows.cpp), and tests
// fill it with makeMemoryKeyStore(). No test binary ever calls the real one.
//
// The target name is "XYZPDF/translate/<provider>", one entry per service: the
// two keys are different credentials from different companies, and deleting one
// must not delete the other.
#pragma once

#include <QString>

#include <memory>

namespace Translate {

class KeyStore
{
public:
    virtual ~KeyStore() = default;

    // True when a key was found. False with an empty error means "there is no
    // key for this service", which is an ordinary state; false with an error
    // means the store itself failed and the caller must not treat it as "not
    // configured" silently.
    virtual bool read(const QString &providerId, QString *key, QString *error) = 0;

    // An empty key removes the entry. Returns false and fills error when the
    // store refused.
    virtual bool write(const QString &providerId, const QString &key, QString *error) = 0;
    virtual bool remove(const QString &providerId, QString *error) = 0;
};

// "XYZPDF/translate/deepl". Exposed so a manual check of the real store knows
// what to look for.
QString credentialTarget(const QString &providerId);

// Process-local storage for tests and for nothing else.
std::unique_ptr<KeyStore> makeMemoryKeyStore();

// The Windows credential manager. Automatic tests must not call this: they
// would touch the machine they run on. Manually verified once, see the report.
std::unique_ptr<KeyStore> makeSystemKeyStore();

// Moves the plaintext "translate/apiKey" that versions before the credential
// manager kept in QSettings into the DeepL slot, and removes the old setting.
//
// DeepL specifically, because it was the only service that ever had a key: the
// old free Google endpoint was keyless, and the field's placeholder said "仅
// DeepL 需要". A key stored while the provider said "google" was never used by
// any code path, so the only faithful home for it is the one key slot the old
// program actually had.
//
// Returns true when there was nothing to migrate or the migration succeeded.
// On failure the old value is left in place, so the next launch can try again
// rather than losing the key.
bool migrateLegacyApiKey(KeyStore &store, QString *error = nullptr);

} // namespace Translate
