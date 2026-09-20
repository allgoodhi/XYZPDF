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
#include <QString>

#include <memory>

class QJsonDocument;

namespace Translate {

// What came back from a translation, whatever service produced it.
struct Result
{
    // False means nothing usable came out, and error says why -- as a sentence
    // to show the user.
    bool ok = false;
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

// The request URL for the free, no-key Google endpoint. Kept a pure function of
// its inputs so that the escaping -- the part that breaks silently on a question
// mark or a Chinese character -- is a thing a test can check.
QString googleRequestUrl(const QString &text, const QString &targetLang);

// The body and the endpoint for a DeepL request, which is a form POST rather than
// a URL, and needs the caller's key.
QByteArray deeplRequestBody(const QString &text, const QString &targetLang, const QString &key);

// The answers read back. parseGoogleResponse understands the nested-array shape
// the translate_a endpoint returns; parseDeeplResponse the object DeepL returns.
// A malformed or unexpected body comes back as ok == false with an error, not as
// a crash and not as an empty success.
Result parseGoogleResponse(const QByteArray &body);
Result parseDeeplResponse(const QByteArray &body);

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

// The translator the user's settings ask for: the free endpoint by default, or
// DeepL when a key has been filled in. Reading the settings is the only reason
// this is a function rather than a class the caller picks.
std::unique_ptr<Translator> makeTranslator();

} // namespace Translate
