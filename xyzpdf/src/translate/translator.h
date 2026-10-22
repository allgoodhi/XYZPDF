// Turning a piece of selected text into the same words in another language.
//
// ---- what this is, and what it is not ----
//
// The feature is "select some words in a document and see what they mean". The
// words come from the two places this program can already get text: a selection
// of a page's text layer, and the text read out of a box the user drew on a
// picture (see Ocr). What happens to those words after that is somebody else's
// business -- a translation service -- and this module is the seam between them.
//
// There is no translation in here. There is a request to build, a response to
// read, and a choice of which service to ask. That is deliberate: the service is
// the part that costs money, needs a key, sends the user's text to a third
// machine, or can be offline, and none of those are decisions this program should
// make once and never revisit. So the shape of an answer is fixed here
// (Translate::Result), the pure parts of asking are testable here without a
// network, and the one thing that actually opens a socket is behind an interface
// with more than one way to fill it.
//
// ---- why there is no "free" endpoint ----
//
// Through 2026-10 the default was Google's web endpoint
// (translate.googleapis.com/translate_a/single?client=gtx): no key, no contract,
// and no promise it would answer tomorrow. A store application cannot stand on
// that -- it may disappear, it is not licensed for this use, and there is nobody
// to ask when it changes shape. Both services this program talks to now are the
// documented, key-bearing APIs, and the key is the user's own. That is also why
// an unconfigured program says so instead of quietly translating through a
// back door.
//
// ---- where the key lives ----
//
// Not in QSettings. The key is a credential, so it goes to the Windows credential
// manager through the KeyStore interface in keystore.h; what settings hold is
// which service the reader chose and whether they agreed to send text to it.
//
// ---- why the call blocks, and where it runs ----
//
// translate() blocks until the answer comes back or the request fails. That is
// the same contract Ocr::recognize() has, and for the same reason: the caller is
// expected to run it off the thread that draws (DocumentTab does, through
// QtConcurrent), so a half-second round trip is a half-second the reading window
// keeps repainting. A blocking call on a worker thread is simpler to reason
// about than a second callback chain layered on the async the caller already has.
#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

#include <memory>

namespace Translate {

// The services this program can ask. None is a real state -- a fresh install --
// and not an error: the translate commands stay clickable and say what to do.
enum class Provider {
    None,
    DeepL,
    GoogleCloud,
};

// The id stored in QSettings ("", "deepl", "google"), and the names a reader
// sees. The names are product names and are not translated.
Provider providerFromId(const QString &id);
QString providerId(Provider provider);
QString providerName(Provider provider);
// Where the bytes actually go, for the consent sentence. DeepL's free keys
// (ending in ":fx") use a different host from its paid ones.
QString endpointHost(Provider provider, const QString &key);

// Why a translation did not happen, as a kind rather than only as a sentence.
// The sentence is what the window shows; the kind is what a test can assert
// without depending on the catalogue's wording.
enum class ErrorKind {
    None,
    Network,
    InvalidKey,
    Quota,
    Service, // an HTTP answer that was neither an auth nor a quota failure
    MalformedResponse,
    EmptyText,
    NotConfigured,
};

// What came back from a translation, whatever service produced it.
struct Result
{
    // False means nothing usable came out, and error says why -- as a sentence
    // to show the user.
    bool ok = false;
    ErrorKind kind = ErrorKind::None;
    // The words that were asked about, echoed back so a window can show the
    // original beside the translation without the caller keeping it.
    QString source;
    // The translation. Empty when ok is false.
    QString text;
    // Which language the service thought the source was written in, as its own
    // code (e.g. "en", "ja"). May be empty when the service did not say.
    QString detectedLang;
    QString error;
};

// ---- the pure parts, testable with no socket anywhere near them ----
//
// A UI language code (what AppSettings stores: "system", "zh_CN", "en", ...) or
// a resolved locale name, turned into the target-language code one particular
// service wants. Google wants "zh-CN"; DeepL wants "ZH". Both fall back to
// English, which is the language a program with no other signal should translate
// into.
QString googleTargetFor(const QString &uiOrLocaleCode);
QString deeplTargetFor(const QString &uiOrLocaleCode);

// One request to one service: where it goes, what is sent, and with which
// headers. Kept a value with no socket in it so the parts that break silently --
// escaping, and where the key is allowed to appear -- are things a test can pin.
//
// The key is never in the URL and never in the body. DeepL takes it in
// "Authorization: DeepL-Auth-Key <key>", Google Cloud in "X-goog-api-key"; a key
// in a URL ends up in logs, proxies and crash reports.
struct ApiRequest
{
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body; // form-encoded, sent as POST by both services
};
ApiRequest buildRequest(Provider provider, const QString &text, const QString &targetLang,
                        const QString &key);

// The answers read back. A malformed or unexpected body comes back as ok ==
// false with an error, not as a crash and not as an empty success.
Result parseDeeplResponse(const QByteArray &body);
Result parseGoogleResponse(const QByteArray &body);

// Google returns the translation HTML-escaped even when format=text is asked
// for (`&#39;` for an apostrophe), so the entities are put back before the
// reader sees them. Exposed because it is arithmetic, and because the failure it
// prevents -- a translation full of `&#39;` -- is one a reader would see and a
// test should not have to guess at.
QString unescapeHtml(const QString &text);

// What an HTTP failure means, split into the three cases the reader can act on:
// a key that does not work, an allowance that is used up, and everything else.
// The body is consulted because Google reports a quota failure as a plain 403,
// indistinguishable from a bad key by the status alone.
Result httpErrorResult(Provider provider, int status, const QByteArray &body);
Result networkErrorResult();

// The two failures that are nobody's fault but the reader's: nothing was
// selected, and nothing is configured. They are free functions rather than
// sentences written inside the translator classes because of how the catalogue
// is built: lupdate takes a `tr()` row's context from the scope the call sits in,
// so a sentence typed inside a class lands in "Translate::HttpTranslator" -- a
// context no call at runtime ever asks for, and the English interface answers in
// Chinese. Kept at namespace scope, they land in "Translate" where they are read.
Result emptyTextResult();
Result notConfiguredResult();

// Whether a request may be sent at all. The pure half of the consent rule:
// unconfigured means "no provider or no key", and a configured service is still
// asked once per provider before the first byte leaves the machine.
enum class Gate {
    NotConfigured,
    NeedsConsent,
    Ready,
};
Gate requestGate(Provider provider, const QString &key, bool consented);

// ---- the pluggable interface ----
//
// One method, blocking, safe to call from a worker thread. `text` is what to
// translate; the target language is decided by the implementation (from the
// interface language) so the caller does not have to know which service speaks
// which codes.
class Translator
{
public:
    virtual ~Translator() = default;
    virtual Result translate(const QString &text) = 0;
};

// A translator for a service and a key the caller has already read and been
// allowed to use. The settings are read by the caller -- the consent check has
// to happen on the thread with the window on it, and the key must not be fetched
// a second time from the worker. Never null: an unconfigured pair produces a
// translator whose answer is an honest failure, so a caller that got the gate
// wrong gets a sentence rather than a crash.
std::unique_ptr<Translator> makeTranslator(Provider provider, const QString &key);

} // namespace Translate
