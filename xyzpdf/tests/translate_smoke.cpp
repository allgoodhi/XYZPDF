// The parts of translation that are arithmetic rather than a network call.
//
// A request that puts the key where a proxy can read it, and a response read
// with the wrong assumption about its shape, are the two ways this feature breaks
// quietly -- so both are pinned here with bytes on hand, no socket, no real key,
// and no service that could be down on the day the test runs.
//
// The store is the in-memory one (Translate::makeMemoryKeyStore) plus one that
// refuses, written down the road in this file. Nothing in here reaches the
// Windows credential manager: an automated test must not write to the machine it
// runs on, and the real store is verified by hand once instead. See keystore.h.

#include "../src/core/appsettings.h"
#include "../src/translate/keystore.h"
#include "../src/translate/translator.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

void checkEquals(const QString &actual, const QString &expected, const QString &what)
{
    if (actual == expected)
        return;
    std::cerr << "FAIL: " << what.toStdString() << "\n      expected "
              << expected.toStdString() << "\n      got      " << actual.toStdString() << '\n';
    ++failures;
}

void checkKind(Translate::ErrorKind actual, Translate::ErrorKind expected, const QString &what)
{
    if (actual == expected)
        return;
    std::cerr << "FAIL: " << what.toStdString() << " (kind " << int(actual) << ", expected "
              << int(expected) << ")\n";
    ++failures;
}

// The header's value, or an empty QByteArray when the request does not carry it.
QByteArray headerOf(const Translate::ApiRequest &request, const QByteArray &name)
{
    for (const auto &header : request.headers) {
        if (header.first == name)
            return header.second;
    }
    return QByteArray();
}

// A store that refuses every write, for the migration's failure path: the old
// plaintext value has to stay where it was rather than be deleted on the way to
// a store that rejected it.
class RefusingKeyStore final : public Translate::KeyStore
{
public:
    bool read(const QString &, QString *key, QString *) override
    {
        key->clear();
        return false;
    }
    bool write(const QString &, const QString &, QString *error) override
    {
        if (error)
            *error = QStringLiteral("refused");
        return false;
    }
    bool remove(const QString &, QString *) override { return false; }
};

void setLegacyKey(const QString &value)
{
    QSettings store = AppSettings::settings();
    if (value.isEmpty())
        store.remove(QStringLiteral("translate/apiKey"));
    else
        store.setValue(QStringLiteral("translate/apiKey"), value);
    store.sync();
}

QString legacyKey()
{
    return AppSettings::settings().value(QStringLiteral("translate/apiKey")).toString();
}

} // namespace

int main(int argc, char **argv)
{
    // An application object so the error sentences can be translated the way
    // they will be in the real program; the checks below never open a socket.
    QCoreApplication application(argc, argv);
    using namespace Translate;

    // ---- the services, by the id the settings store ----
    checkEquals(providerId(providerFromId(QStringLiteral("deepl"))), QStringLiteral("deepl"),
                "deepl round-trips through its id");
    checkEquals(providerId(providerFromId(QStringLiteral("google"))), QStringLiteral("google"),
                "google round-trips through its id");
    check(providerFromId(QStringLiteral("bogus")) == Provider::None,
          "an id this build does not know is no service, not a guess");
    check(providerFromId(QString()) == Provider::None,
          "no id at all -- the state of a fresh install -- is no service");
    checkEquals(providerName(Provider::DeepL), QStringLiteral("DeepL"),
                "the consent sentence can name DeepL");
    checkEquals(providerName(Provider::GoogleCloud), QStringLiteral("Google Cloud Translation"),
                "and Google Cloud by its product name");
    checkEquals(endpointHost(Provider::DeepL, QStringLiteral("abc123:fx")),
                QStringLiteral("api-free.deepl.com"), "a free key names the free host");
    checkEquals(endpointHost(Provider::DeepL, QStringLiteral("abc123")),
                QStringLiteral("api.deepl.com"), "a paid key names the paid host");
    checkEquals(endpointHost(Provider::GoogleCloud, QStringLiteral("anything")),
                QStringLiteral("translation.googleapis.com"),
                "Google Cloud has one host whatever the key looks like");

    // ---- the target language, from the interface language ----
    checkEquals(googleTargetFor(QStringLiteral("zh_CN")), QStringLiteral("zh-CN"),
                "a Chinese interface translates into Simplified Chinese");
    checkEquals(googleTargetFor(QStringLiteral("zh")), QStringLiteral("zh-CN"),
                "a bare zh still means Chinese");
    checkEquals(googleTargetFor(QStringLiteral("en_US")), QStringLiteral("en"),
                "an English interface translates into English");
    checkEquals(googleTargetFor(QStringLiteral("system")), QStringLiteral("en"),
                "an unresolved code falls back to English, not to nothing");
    checkEquals(deeplTargetFor(QStringLiteral("zh_TW")), QStringLiteral("ZH"),
                "DeepL takes an uppercase ZH for Chinese");
    checkEquals(deeplTargetFor(QStringLiteral("de")), QStringLiteral("EN"), "DeepL falls back to EN");

    // ---- DeepL's request: official endpoint, key in the header ----
    {
        const QString key = QStringLiteral("abcd-1234-secret:fx");
        const ApiRequest request =
            buildRequest(Provider::DeepL, QStringLiteral("hello world?"), QStringLiteral("ZH"), key);
        checkEquals(request.url.toString(), QStringLiteral("https://api-free.deepl.com/v2/translate"),
                    "a key ending in :fx asks the free DeepL host");
        checkEquals(headerOf(request, QByteArrayLiteral("Authorization")),
                    QByteArrayLiteral("DeepL-Auth-Key abcd-1234-secret:fx"),
                    "the key travels in the documented header");
        check(!request.url.toString().contains(key), "the key is not in the URL");
        check(!request.body.contains(key.toUtf8()), "the key is not in the body");
        check(request.body.startsWith(QByteArrayLiteral("text=hello%20world%3F&target_lang=ZH")),
                QStringLiteral("the text is escaped and the target travels with it (%1)")
                    .arg(QString::fromUtf8(request.body)));
        checkEquals(headerOf(request, QByteArrayLiteral("Content-Type")),
                    QByteArrayLiteral("application/x-www-form-urlencoded"),
                    "a form POST says so");

        const ApiRequest paid =
            buildRequest(Provider::DeepL, QStringLiteral("x"), QStringLiteral("EN"),
                         QStringLiteral("plain-key"));
        checkEquals(paid.url.toString(), QStringLiteral("https://api.deepl.com/v2/translate"),
                    "a key without the suffix asks the paid host");
    }

    // ---- Google Cloud's request: official endpoint, key in the header ----
    {
        const QString key = QStringLiteral("AIzaSECRETkey1234567890");
        const ApiRequest request =
            buildRequest(Provider::GoogleCloud, QStringLiteral("cats & dogs?"),
                         QStringLiteral("zh-CN"), key);
        checkEquals(request.url.toString(),
                    QStringLiteral("https://translation.googleapis.com/language/translate/v2"),
                    "the v2 endpoint is asked, with no query of its own");
        checkEquals(headerOf(request, QByteArrayLiteral("X-goog-api-key")), key.toUtf8(),
                    "the key travels in X-goog-api-key");
        check(!request.url.hasQuery(), "nothing at all is in the URL's query");
        check(!request.url.toString().contains(key), "the key is not in the URL");
        check(!request.body.contains(key.toUtf8()), "the key is not in the body");
        // '&' and '?' in the selected text must not read as separators. This is
        // the escaping the removed free endpoint needed by hand; here QUrlQuery
        // does it, and the check is that it actually happened.
        check(request.body.contains(QByteArrayLiteral("q=cats%20%26%20dogs%3F")),
              QStringLiteral("the selected text is percent-encoded (%1)")
                  .arg(QString::fromUtf8(request.body)));
        check(request.body.contains(QByteArrayLiteral("target=zh-CN"))
                  && request.body.contains(QByteArrayLiteral("format=text")),
              "the parameters the answer depends on are present");
    }

    {
        const ApiRequest none = buildRequest(Provider::None, QStringLiteral("x"),
                                             QStringLiteral("en"), QStringLiteral("k"));
        check(none.url.isEmpty(), "no service builds no request");
    }

    // ---- the Google Cloud answer ----
    {
        const QByteArray body =
            R"JSON({"data":{"translations":[{"translatedText":"你好","detectedSourceLanguage":"en"}]}})JSON";
        const Result result = parseGoogleResponse(body);
        check(result.ok, "a well-formed answer is a success");
        checkEquals(result.text, QStringLiteral("你好"), "its text is read out");
        checkEquals(result.detectedLang, QStringLiteral("en"), "and the language it detected");
    }
    {
        const Result junk = parseGoogleResponse("this is not json");
        check(!junk.ok && !junk.error.isEmpty(), "unparseable bytes fail with a sentence");
        checkKind(junk.kind, ErrorKind::MalformedResponse, "and say what kind of failure they were");
        const Result noData = parseGoogleResponse(R"JSON({"data":{"translations":[]}})JSON");
        check(!noData.ok, "an answer with no translation in it is not a success");
        checkKind(noData.kind, ErrorKind::MalformedResponse, "and is not reported as a network fault");
    }

    // ---- what Google returns escaped, put back ----
    {
        checkEquals(unescapeHtml(QStringLiteral("it&#39;s a &quot;test&quot; &amp; more")),
                    QStringLiteral("it's a \"test\" & more"),
                    "the named entities Google sends are put back");
        checkEquals(unescapeHtml(QStringLiteral("中文&#20013;")), QStringLiteral("中文中"),
                    "and so are numeric ones");
        checkEquals(unescapeHtml(QStringLiteral("&#x4e2d;")), QStringLiteral("中"),
                    "in either base");
        checkEquals(unescapeHtml(QStringLiteral("&nbsp;here")), QStringLiteral("&nbsp;here"),
                    "an entity this list does not know is left exactly as it came");
        checkEquals(unescapeHtml(QStringLiteral("&#;")), QStringLiteral("&#;"),
                    "and so is one that has no number in it");
        const Result escaped = parseGoogleResponse(
            R"JSON({"data":{"translations":[{"translatedText":"it&#39;s here"}]}})JSON");
        checkEquals(escaped.text, QStringLiteral("it's here"),
                    "and the reader never sees the entities in a translation");
    }

    // ---- the DeepL answer ----
    {
        const QByteArray body =
            R"JSON({"translations":[{"detected_source_language":"EN","text":"你好"}]})JSON";
        const Result result = parseDeeplResponse(body);
        check(result.ok, "a DeepL object with a translation is a success");
        checkEquals(result.text, QStringLiteral("你好"), "its text is read out");
        checkEquals(result.detectedLang, QStringLiteral("EN"), "and its detected language");
    }
    {
        // Valid JSON with nothing in it is the shape an unexpected answer takes;
        // the status codes below are what a bad key looks like.
        const Result failed = parseDeeplResponse(R"JSON({"message":"Forbidden"})JSON");
        check(!failed.ok && !failed.error.isEmpty(),
              "an answer with no translations array is not a silent empty success");
        checkKind(failed.kind, ErrorKind::MalformedResponse, "and is not reported as a network fault");
    }

    // ---- the three failures a reader can act on, kept apart ----
    {
        const Result deeplKey = httpErrorResult(Provider::DeepL, 403, R"({"message":"Forbidden"})");
        checkKind(deeplKey.kind, ErrorKind::InvalidKey, "DeepL 403 is the key, not the network");
        check(!deeplKey.error.isEmpty(), "and it comes with a sentence to show");
        check(deeplKey.error != networkErrorResult().error,
              "a refused key is not said in the words a network fault uses");

        checkKind(httpErrorResult(Provider::DeepL, 456, QByteArray()).kind, ErrorKind::Quota,
                  "DeepL's 456 is the allowance being used up");
        checkKind(httpErrorResult(Provider::DeepL, 429, QByteArray()).kind, ErrorKind::Quota,
                  "and a 429 too, whichever service sent it");

        // Google answers a quota exhaustion with a plain 403: the body is the
        // only thing that tells it apart from a key that was refused.
        const Result googleQuota = httpErrorResult(
            Provider::GoogleCloud, 403,
            R"({"error":{"errors":[{"reason":"dailyLimitExceeded"}]}})");
        checkKind(googleQuota.kind, ErrorKind::Quota, "Google 403 with a quota reason is quota");
        const Result googleKey =
            httpErrorResult(Provider::GoogleCloud, 403, R"({"error":{"message":"bad key"}})");
        checkKind(googleKey.kind, ErrorKind::InvalidKey, "Google 403 without one is the key");
        checkKind(httpErrorResult(Provider::GoogleCloud, 400,
                                  R"({"error":{"errors":[{"reason":"keyInvalid"}]}})")
                      .kind,
                  ErrorKind::InvalidKey, "and a 400 that names the key is the key as well");
        checkKind(httpErrorResult(Provider::GoogleCloud, 500, QByteArray()).kind, ErrorKind::Service,
                  "anything else the service answers is its own kind of failure");
        check(httpErrorResult(Provider::GoogleCloud, 503, QByteArray()).error.contains("503"),
              "and the status is in the sentence, so the two are distinguishable afterwards");

        checkKind(networkErrorResult().kind, ErrorKind::Network, "no answer at all is the network");
        check(!networkErrorResult().error.isEmpty(), "with a sentence of its own");
    }

    // ---- what may be sent, and what may not ----
    {
        check(requestGate(Provider::None, QStringLiteral("k"), true) == Gate::NotConfigured,
              "no service selected asks for nothing, even with a key lying about");
        check(requestGate(Provider::DeepL, QString(), true) == Gate::NotConfigured,
              "a service with no key is not configured");
        check(requestGate(Provider::DeepL, QStringLiteral("   "), true) == Gate::NotConfigured,
              "and whitespace is not a key");
        check(requestGate(Provider::DeepL, QStringLiteral("k"), false) == Gate::NeedsConsent,
              "a configured service is still asked about once before the first byte leaves");
        check(requestGate(Provider::DeepL, QStringLiteral("k"), true) == Gate::Ready,
              "consent is what makes it ready");
        // Consent belongs to one service. Agreeing to DeepL must not be readable
        // as agreement to send the same words to Google.
        check(requestGate(Provider::GoogleCloud, QStringLiteral("k"), false) == Gate::NeedsConsent,
              "a second service asks again");
    }

    // ---- the key that is not there ----
    {
        const std::unique_ptr<Translator> translator = makeTranslator(Provider::None, QString());
        check(translator != nullptr, "a caller that skipped the gate gets a sentence, not a crash");
        const Result result = translator->translate(QStringLiteral("hello"));
        check(!result.ok, "and it is a failure");
        checkKind(result.kind, ErrorKind::NotConfigured, "said as its own kind");

        const std::unique_ptr<Translator> idle = makeTranslator(Provider::DeepL,
                                                                QStringLiteral("some-key"));
        const Result nothing = idle->translate(QStringLiteral("   "));
        check(!nothing.ok, "selecting nothing is not a request");
        checkKind(nothing.kind, ErrorKind::EmptyText, "and is said as its own kind");
    }

    // ---- the plaintext key older versions left behind ----
    {
        setLegacyKey(QString());
        const std::unique_ptr<KeyStore> store = makeMemoryKeyStore();
        check(migrateLegacyApiKey(*store), "nothing to migrate succeeds rather than failing");

        setLegacyKey(QStringLiteral("  legacy-key:fx  "));
        check(migrateLegacyApiKey(*store), "a legacy key moves");
        QString migrated;
        check(store->read(QStringLiteral("deepl"), &migrated, nullptr),
              "into the DeepL slot, which is the only key the old program used");
        checkEquals(migrated, QStringLiteral("legacy-key:fx"), "trimmed, as typed");
        checkEquals(legacyKey(), QString(), "and the plaintext copy is gone from settings");

        // A store that refuses must leave the value where it is: deleting the
        // only copy the user has, on the way to a failed write, loses the key.
        setLegacyKey(QStringLiteral("second-key"));
        RefusingKeyStore refusing;
        QString error;
        check(!migrateLegacyApiKey(refusing, &error), "a store that refuses fails the migration");
        check(!error.isEmpty(), "with a reason");
        checkEquals(legacyKey(), QStringLiteral("second-key"),
                    "and the plaintext key is still there for the next launch");
        setLegacyKey(QString());
    }

    // ---- the consent is remembered per service, in settings ----
    {
        AppSettings::setTranslateConsent(QStringLiteral("deepl"), true);
        check(AppSettings::translateConsentGiven(QStringLiteral("deepl")),
              "agreement to DeepL is stored");
        check(!AppSettings::translateConsentGiven(QStringLiteral("google")),
              "and does not spread to Google");
        AppSettings::setTranslateConsent(QStringLiteral("deepl"), false);
        check(!AppSettings::translateConsentGiven(QStringLiteral("deepl")),
              "revoking it in the settings dialog is what the store then says");
        check(!AppSettings::translateConsentGiven(QString()),
              "and no service means no agreement, without reading a broken key");
    }

    // ---- the credential name the real store would use ----
    checkEquals(credentialTarget(QStringLiteral("deepl")), QStringLiteral("XYZPDF/translate/deepl"),
                "the target name is the one written into the report and checked by hand");

    if (failures == 0) {
        std::cout << "translate smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
