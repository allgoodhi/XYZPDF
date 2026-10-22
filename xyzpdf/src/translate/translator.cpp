#include "translator.h"

#include "../core/appsettings.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include <memory>

namespace {

// A sentence for the user, in the module's own translation context -- the same
// shape Ocr and the exporter use, because an error that reaches a window has to
// be phrased in the reader's language rather than this file's.
//
// The name is the load-bearing part. lupdate decides whether a call is
// translatable by the NAME it goes through, so the helper this file used to call
// `said` was invisible to it: every error sentence below was in the code, in the
// right context, and absent from the catalogue -- which showed them to a reader
// in Chinese under an English interface. `tr` is the name it looks for, which is
// why documenteditor.cpp names its helper that way too.
QString tr(const char *text)
{
    return QCoreApplication::translate("Translate", text);
}

// The interface language as a locale name. "system" means whatever the machine
// is set to; anything else is already a code. Resolving it here rather than at
// each call site keeps the two services seeing the same string.
QString currentUiCode()
{
    QString code = AppSettings::languageCode();
    if (code.isEmpty() || code == QLatin1String("system"))
        code = QLocale::system().name(); // e.g. "zh_CN", "en_US"
    return code;
}

// What a request produced at the transport level. `status` is zero when no HTTP
// response ever arrived -- a DNS failure, a refused connection, a timeout -- and
// only then is the network the thing to report. A 403 also sets
// QNetworkReply::error(), which is why the status is checked first: the body of
// an HTTP failure is exactly what says whether a 403 means "key" or "quota".
struct Exchange
{
    int status = 0;
    QByteArray body;
};

// One blocking request, run on the calling thread's own event loop. The manager
// is created here and never leaves this thread, which is what makes it safe to
// call from a QtConcurrent worker: a QNetworkAccessManager is not shareable
// across threads, but a whole request that begins and ends on one thread is.
Exchange exchange(const Translate::ApiRequest &apiRequest)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(apiRequest.url);
    request.setTransferTimeout(15000); // a hung service should not hang a window
    for (const auto &header : apiRequest.headers)
        request.setRawHeader(header.first, header.second);

    QNetworkReply *reply = manager.post(request, apiRequest.body);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    Exchange answer;
    answer.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    answer.body = reply->readAll();
    reply->deleteLater();
    return answer;
}

bool bodySaysQuota(const QByteArray &body)
{
    // Google reports quota exhaustion as 403 too; the reason nested in the error
    // object is the only thing that tells it apart from a key that was refused.
    // DeepL uses its own status (456) and needs no such guess.
    static const QRegularExpression quota(
        QStringLiteral("quota|dailyLimitExceeded|rateLimitExceeded|userRateLimitExceeded"),
        QRegularExpression::CaseInsensitiveOption);
    return quota.match(QString::fromUtf8(body)).hasMatch();
}

bool bodySaysBadKey(const QByteArray &body)
{
    static const QRegularExpression badKey(
        QStringLiteral("keyInvalid|API key not valid|invalid.*key"),
        QRegularExpression::CaseInsensitiveOption);
    return badKey.match(QString::fromUtf8(body)).hasMatch();
}

// Both services take a form-encoded body. It is built by hand rather than
// through QUrlQuery, which leaves a '?' in a value alone because a question mark
// is legal inside a query -- and the text being translated is exactly the value
// that can carry one. A stray separator truncates the sentence at the '?', which
// is the quiet half-wrong answer: the request succeeds and translates what was
// left of it. toPercentEncoding keeps only what cannot be misread.
QByteArray formEncode(const QString &value)
{
    return QUrl::toPercentEncoding(value);
}

QByteArray formBody(const QList<QPair<QString, QString>> &items)
{
    QByteArray body;
    for (const auto &item : items) {
        if (!body.isEmpty())
            body += '&';
        body += formEncode(item.first);
        body += '=';
        body += formEncode(item.second);
    }
    return body;
}

} // namespace

namespace Translate {

Provider providerFromId(const QString &id)
{
    if (id == QLatin1String("deepl"))
        return Provider::DeepL;
    if (id == QLatin1String("google"))
        return Provider::GoogleCloud;
    return Provider::None;
}

QString providerId(Provider provider)
{
    switch (provider) {
    case Provider::DeepL:
        return QStringLiteral("deepl");
    case Provider::GoogleCloud:
        return QStringLiteral("google");
    case Provider::None:
        break;
    }
    return QString();
}

QString providerName(Provider provider)
{
    switch (provider) {
    case Provider::DeepL:
        return QStringLiteral("DeepL");
    case Provider::GoogleCloud:
        return QStringLiteral("Google Cloud Translation");
    case Provider::None:
        break;
    }
    return QString();
}

QString endpointHost(Provider provider, const QString &key)
{
    switch (provider) {
    case Provider::DeepL:
        // The free tier lives on its own host, and the suffix is the only thing
        // that says which one a key belongs to -- DeepL documents it as such.
        return key.endsWith(QLatin1String(":fx")) ? QStringLiteral("api-free.deepl.com")
                                                  : QStringLiteral("api.deepl.com");
    case Provider::GoogleCloud:
        return QStringLiteral("translation.googleapis.com");
    case Provider::None:
        break;
    }
    return QString();
}

QString googleTargetFor(const QString &uiOrLocaleCode)
{
    // Google's codes are BCP-47-ish: Simplified Chinese is "zh-CN". A Chinese
    // interface -- the language this program is mostly used in -- translates
    // into Chinese; everything else into English.
    if (uiOrLocaleCode.startsWith(QLatin1String("zh")))
        return QStringLiteral("zh-CN");
    return QStringLiteral("en");
}

QString deeplTargetFor(const QString &uiOrLocaleCode)
{
    // DeepL wants an uppercase language, and distinguishes the Chinese it
    // produces from the Japanese; "ZH" is its Simplified-Chinese target.
    if (uiOrLocaleCode.startsWith(QLatin1String("zh")))
        return QStringLiteral("ZH");
    return QStringLiteral("EN");
}

ApiRequest buildRequest(Provider provider, const QString &text, const QString &targetLang,
                        const QString &key)
{
    ApiRequest request;
    if (provider == Provider::None)
        return request;

    if (provider == Provider::DeepL) {
        request.url = QUrl(key.endsWith(QLatin1String(":fx"))
                               ? QStringLiteral("https://api-free.deepl.com/v2/translate")
                               : QStringLiteral("https://api.deepl.com/v2/translate"));
        request.body = formBody({{QStringLiteral("text"), text},
                                 {QStringLiteral("target_lang"), targetLang}});
        // The documented header. The key used to sit in the form body, which is
        // readable by anything that logs request bodies.
        request.headers.append(
            {QByteArrayLiteral("Authorization"),
             QByteArrayLiteral("DeepL-Auth-Key ") + key.toUtf8()});
    } else {
        // Google Cloud Translation v2. The key goes in the header, never in the
        // URL: a query string is written to every proxy log between here and
        // there. Format text keeps markup out of an answer that is shown as
        // plain text.
        request.url =
            QUrl(QStringLiteral("https://translation.googleapis.com/language/translate/v2"));
        request.body = formBody({{QStringLiteral("q"), text},
                                 {QStringLiteral("target"), targetLang},
                                 {QStringLiteral("format"), QStringLiteral("text")}});
        request.headers.append(
            {QByteArrayLiteral("X-goog-api-key"), key.toUtf8()});
    }

    request.headers.append({QByteArrayLiteral("Content-Type"),
                            QByteArrayLiteral("application/x-www-form-urlencoded")});
    return request;
}

QString unescapeHtml(const QString &text)
{
    // Google's translatedText is HTML-escaped even for format=text, so an
    // apostrophe arrives as &#39; and would be shown to the reader as those
    // five characters. Named entities plus both numeric forms covers what the
    // API produces; anything unrecognised is left alone rather than guessed at.
    static const QRegularExpression entity(QStringLiteral("&(?:amp|lt|gt|quot|apos|#39|#[0-9]+|#x[0-9A-Fa-f]+);"));
    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    auto matches = entity.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        out += text.mid(last, match.capturedStart() - last);
        const QString token = match.captured();
        if (token == QLatin1String("&amp;"))
            out += QLatin1Char('&');
        else if (token == QLatin1String("&lt;"))
            out += QLatin1Char('<');
        else if (token == QLatin1String("&gt;"))
            out += QLatin1Char('>');
        else if (token == QLatin1String("&quot;"))
            out += QLatin1Char('"');
        else if (token == QLatin1String("&apos;") || token == QLatin1String("&#39;"))
            out += QLatin1Char('\'');
        else {
            bool understood = false;
            const qsizetype number = token.startsWith(QLatin1String("&#x"))
                                         ? token.mid(3, token.size() - 4).toLongLong(&understood, 16)
                                         : token.mid(2, token.size() - 3).toLongLong(&understood, 10);
            if (understood && number > 0 && number <= 0x10FFFF) {
                const char32_t code = char32_t(number);
                out += QString::fromUcs4(&code, 1);
            } else {
                out += token;
            }
        }
        last = match.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

Result parseGoogleResponse(const QByteArray &body)
{
    Result result;
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        result.kind = ErrorKind::MalformedResponse;
        result.error = tr("翻译服务返回了看不懂的内容。");
        return result;
    }

    // {"data":{"translations":[{"translatedText":"…","detectedSourceLanguage":"en"}]}}
    const QJsonObject root = document.object();
    const QJsonArray translations =
        root.value(QStringLiteral("data")).toObject().value(QStringLiteral("translations")).toArray();
    if (translations.isEmpty() || !translations.at(0).isObject()) {
        result.kind = ErrorKind::MalformedResponse;
        result.error = tr("翻译服务没有返回译文。");
        return result;
    }

    const QJsonObject first = translations.at(0).toObject();
    result.text = unescapeHtml(first.value(QStringLiteral("translatedText")).toString());
    result.detectedLang = first.value(QStringLiteral("detectedSourceLanguage")).toString();
    if (result.text.trimmed().isEmpty()) {
        result.kind = ErrorKind::MalformedResponse;
        result.error = tr("翻译服务没有返回译文。");
        return result;
    }
    result.ok = true;
    return result;
}

Result parseDeeplResponse(const QByteArray &body)
{
    Result result;
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        result.kind = ErrorKind::MalformedResponse;
        result.error = tr("翻译服务返回了看不懂的内容。");
        return result;
    }
    const QJsonArray translations = document.object().value(QStringLiteral("translations")).toArray();
    if (translations.isEmpty() || !translations.at(0).isObject()) {
        // A body that parses but has no translation is what an authentication
        // failure or an empty input looks like. The HTTP layer has already
        // classified the status; reaching here means the shape was unexpected.
        result.kind = ErrorKind::MalformedResponse;
        result.error = tr("翻译服务没有返回译文。");
        return result;
    }
    const QJsonObject first = translations.at(0).toObject();
    result.text = first.value(QStringLiteral("text")).toString();
    result.detectedLang = first.value(QStringLiteral("detected_source_language")).toString();
    if (result.text.trimmed().isEmpty()) {
        result.kind = ErrorKind::MalformedResponse;
        result.error = tr("翻译服务没有返回译文。");
        return result;
    }
    result.ok = true;
    return result;
}

Result httpErrorResult(Provider provider, int status, const QByteArray &body)
{
    Result result;
    if (status == 456 || status == 429) {
        // 456 is DeepL's own "quota exceeded"; 429 is the rate-limit shape both
        // services use. For a reader they mean the same thing: not now, and not
        // because of anything they typed wrong.
        result.kind = ErrorKind::Quota;
        result.error = tr("翻译服务的额度已用完，或请求过于频繁。");
        return result;
    }
    if (status == 401 || status == 403) {
        if (provider != Provider::DeepL && bodySaysQuota(body)) {
            result.kind = ErrorKind::Quota;
            result.error = tr("翻译服务的额度已用完，或请求过于频繁。");
            return result;
        }
        result.kind = ErrorKind::InvalidKey;
        result.error = tr("翻译服务的密钥无效，请在设置里检查 API 密钥。");
        return result;
    }
    if (status == 400 && bodySaysBadKey(body)) {
        result.kind = ErrorKind::InvalidKey;
        result.error = tr("翻译服务的密钥无效，请在设置里检查 API 密钥。");
        return result;
    }
    result.kind = ErrorKind::Service; // the service answered, but not usefully
    result.error = tr("翻译请求失败了（HTTP %1）。").arg(status);
    return result;
}

Result networkErrorResult()
{
    Result result;
    result.kind = ErrorKind::Network;
    result.error = tr("翻译请求失败了，请检查网络连接后重试。");
    return result;
}

Result emptyTextResult()
{
    Result result;
    result.kind = ErrorKind::EmptyText;
    result.error = tr("没有选中要翻译的文字。");
    return result;
}

Result notConfiguredResult()
{
    Result result;
    result.kind = ErrorKind::NotConfigured;
    result.error = tr("翻译还没有配置服务和密钥。");
    return result;
}

Gate requestGate(Provider provider, const QString &key, bool consented)
{
    if (provider == Provider::None || key.trimmed().isEmpty())
        return Gate::NotConfigured;
    if (!consented)
        return Gate::NeedsConsent;
    return Gate::Ready;
}

namespace {

// The one implementation both services share: build, send, classify, parse.
// Which service it is decides the endpoint, the headers and the shape of the
// answer, and nothing else.
class HttpTranslator final : public Translator
{
public:
    HttpTranslator(Provider provider, QString key)
        : service(provider)
        , authKey(std::move(key))
    {
    }

    Result translate(const QString &text) override
    {
        // Every failure carries the words back with it, so the window can still
        // show what was asked about beside the reason.
        auto about = [&text](Result failed) {
            failed.source = text;
            return failed;
        };

        if (text.trimmed().isEmpty())
            return about(emptyTextResult());

        const QString target = service == Provider::DeepL ? deeplTargetFor(currentUiCode())
                                                          : googleTargetFor(currentUiCode());
        const ApiRequest apiRequest = buildRequest(service, text, target, authKey);
        const Exchange answer = exchange(apiRequest);

        if (answer.status == 0)
            return about(networkErrorResult());
        if (answer.status < 200 || answer.status >= 300)
            return about(httpErrorResult(service, answer.status, answer.body));

        Result parsed = service == Provider::DeepL ? parseDeeplResponse(answer.body)
                                                   : parseGoogleResponse(answer.body);
        if (parsed.source.isEmpty())
            parsed.source = text;
        return parsed;
    }

private:
    Provider service = Provider::None;
    QString authKey;
};

// Only reachable if a caller skipped the gate. Saying so is better than a null
// dereference on a worker thread.
class UnavailableTranslator final : public Translator
{
public:
    Result translate(const QString &text) override
    {
        Result result = notConfiguredResult();
        result.source = text;
        return result;
    }
};

} // namespace

std::unique_ptr<Translator> makeTranslator(Provider provider, const QString &key)
{
    if (provider == Provider::None || key.trimmed().isEmpty())
        return std::make_unique<UnavailableTranslator>();
    return std::make_unique<HttpTranslator>(provider, key);
}

} // namespace Translate
