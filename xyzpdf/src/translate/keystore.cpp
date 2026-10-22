#include "keystore.h"

#include "../core/appsettings.h"

#include <QHash>
#include <QSettings>

namespace {

class MemoryKeyStore final : public Translate::KeyStore
{
public:
    bool read(const QString &providerId, QString *key, QString *error) override
    {
        Q_UNUSED(error);
        const auto found = keys.constFind(providerId);
        if (found == keys.constEnd()) {
            key->clear();
            return false;
        }
        *key = *found;
        return true;
    }

    bool write(const QString &providerId, const QString &key, QString *error) override
    {
        Q_UNUSED(error);
        if (providerId.isEmpty())
            return false;
        if (key.isEmpty())
            keys.remove(providerId);
        else
            keys.insert(providerId, key);
        return true;
    }

    bool remove(const QString &providerId, QString *error) override
    {
        Q_UNUSED(error);
        keys.remove(providerId);
        return true;
    }

private:
    QHash<QString, QString> keys;
};

} // namespace

namespace Translate {

QString credentialTarget(const QString &providerId)
{
    return QStringLiteral("XYZPDF/translate/") + providerId;
}

std::unique_ptr<KeyStore> makeMemoryKeyStore()
{
    return std::make_unique<MemoryKeyStore>();
}

bool migrateLegacyApiKey(KeyStore &store, QString *error)
{
    // The key literal is repeated here on purpose: AppSettings no longer knows
    // about a key at all, which is what keeps a future edit from adding the call
    // that writes plaintext back.
    const QString legacyKey = QStringLiteral("translate/apiKey");
    QSettings valueStore = AppSettings::settings();
    const QString plain = valueStore.value(legacyKey).toString().trimmed();
    if (plain.isEmpty())
        return true;

    QString storeError;
    if (!store.write(QStringLiteral("deepl"), plain, &storeError)) {
        if (error)
            *error = storeError;
        return false;
    }
    valueStore.remove(legacyKey);
    valueStore.sync();
    return true;
}

} // namespace Translate
