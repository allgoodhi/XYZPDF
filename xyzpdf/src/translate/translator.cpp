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
#include <QUrl>
#include <QUrlQuery>

#include <memory>

namespace {

// A sentence for the user, in the module's own translation context -- the same
// shape Ocr and the exporter use, because an error that reaches a window has to
// be phrased in the reader's language rather than this file's.
QString said(const char *text)
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

// One blocking request, run on the calling thread's own event loop. The manager
// is created here and never leaves this thread, which is what makes it safe to
// call from a QtConcurrent worker: a QNetworkAccessManager is not shareable
// across threads, but a whole request that begins and ends on one thread is.
QByteArray exchange(const QUrl &url, const QByteArray *body,
                    const QList<QPair<QByteArray, QByteArray>> &rawHeaders, QString *error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(15000); // a hung service should not hang a window
    for (const auto &header : rawHeaders)
        request.setRawHeader(header.first, header.second);

    QNetworkReply *reply = body ? manager.post(request, *body) : manager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray result;
    const QNetworkReply::NetworkError status = reply->error();
    if (status != QNetworkReply::NoError) {
        if (error)
            *error = said("翻译请求失败了，可能是没有网络或对方暂时不可用。");
        result.clear();
    } else {
        result = reply->readAll();
    }
    reply->deleteLater();
    return result;
}

} // namespace

namespace Translate {

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

QString googleRequestUrl(const QString &text, const QString &targetLang)
{
    // client=gtx is the parameter the free web endpoint answers without a key.
    // sl=auto lets it guess the source, which is what a reader selecting an
    // unknown language wants. dt=t asks for just the translation segments.
    //
    // The selected text is escaped by hand rather than through QUrlQuery, which
    // leaves a '?' in a value alone (it is legal in a query) -- and a stray
    // separator is exactly the kind of thing that truncates the text being
    // translated. toPercentEncoding keeps only the characters that cannot
    // confuse the query, so a question mark, a space and an '&' all survive the
    // trip intact.
    const QByteArray escaped = QUrl::toPercentEncoding(text, "/");
    return QStringLiteral("https://translate.googleapis.com/translate_a/single?")
        + QStringLiteral("client=gtx&sl=auto&dt=t")
        + QStringLiteral("&tl=") + targetLang
        + QStringLiteral("&q=") + QString::fromUtf8(escaped);
}

QByteArray deeplRequestBody(const QString &text, const QString &targetLang, const QString &key)
{
    // No source_lang: DeepL detects it, the same way the free endpoint does.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("auth_key"), key);
    query.addQueryItem(QStringLiteral("text"), text);
    query.addQueryItem(QStringLiteral("target_lang"), targetLang);
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

Result parseGoogleResponse(const QByteArray &body)
{
    Result result;
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isArray()) {
        result.error = said("翻译服务返回了看不懂的内容。");
        return result;
    }

    // The shape is [[ [translated, original, ...], ... ], <alignments>, "lang", ...].
    // The first element is the list of translated segments; the third, when
    // present, is the language it decided the source was in.
    const QJsonArray root = document.array();
    if (root.isEmpty() || !root.at(0).isArray()) {
        result.error = said("翻译服务没有返回译文。");
        return result;
    }

    QString translated;
    QString original;
    for (const QJsonValue &segment : root.at(0).toArray()) {
        const QJsonArray parts = segment.toArray();
        if (parts.isEmpty())
            continue;
        translated += parts.at(0).toString();
        if (parts.size() > 1)
            original += parts.at(1).toString();
    }

    if (translated.trimmed().isEmpty()) {
        result.error = said("翻译服务没有返回译文。");
        return result;
    }

    if (root.size() > 2)
        result.detectedLang = root.at(2).toString();
    result.source = original;
    result.text = translated;
    result.ok = true;
    return result;
}

Result parseDeeplResponse(const QByteArray &body)
{
    Result result;
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = said("翻译服务返回了看不懂的内容。");
        return result;
    }
    const QJsonArray translations = document.object().value(QStringLiteral("translations")).toArray();
    if (translations.isEmpty() || !translations.at(0).isObject()) {
        // A body that parses but has no translation is what an authentication
        // failure or an empty input looks like -- say so rather than "nothing".
        result.error = said("翻译失败，请检查 DeepL 密钥是否有效。");
        return result;
    }
    const QJsonObject first = translations.at(0).toObject();
    result.text = first.value(QStringLiteral("text")).toString();
    result.detectedLang = first.value(QStringLiteral("detected_source_language")).toString();
    if (result.text.trimmed().isEmpty()) {
        result.error = said("翻译服务没有返回译文。");
        return result;
    }
    result.ok = true;
    return result;
}

namespace {

// The service that answers without a key. Free, and the default.
class GoogleTranslator final : public Translator
{
public:
    Result translate(const QString &text) override
    {
        Result result;
        result.source = text;
        if (text.trimmed().isEmpty()) {
            result.error = said("没有选中要翻译的文字。");
            return result;
        }
        const QString target = googleTargetFor(currentUiCode());
        QString error;
        const QByteArray body =
            exchange(QUrl(googleRequestUrl(text, target)), nullptr, {}, &error);
        if (body.isEmpty()) {
            result.error = error.isEmpty() ? said("翻译请求失败了。") : error;
            return result;
        }
        Result parsed = parseGoogleResponse(body);
        if (parsed.source.isEmpty())
            parsed.source = text;
        return parsed;
    }
};

// The keyed service, used when the user has filled in a key. Better quality, and
// the answer to the free endpoint's limits going away some day.
class DeeplTranslator final : public Translator
{
public:
    explicit DeeplTranslator(QString key)
        : authKey(std::move(key))
    {
    }

    Result translate(const QString &text) override
    {
        Result result;
        result.source = text;
        if (text.trimmed().isEmpty()) {
            result.error = said("没有选中要翻译的文字。");
            return result;
        }
        const QString target = deeplTargetFor(currentUiCode());
        const QByteArray body = deeplRequestBody(text, target, authKey);
        QString error;
        const QByteArray answer = exchange(
            QUrl(QStringLiteral("https://api-free.deepl.com/v2/translate")), &body,
            {{QByteArrayLiteral("Content-Type"),
              QByteArrayLiteral("application/x-www-form-urlencoded")}},
            &error);
        if (answer.isEmpty()) {
            result.error = error.isEmpty() ? said("翻译请求失败了。") : error;
            return result;
        }
        Result parsed = parseDeeplResponse(answer);
        if (parsed.source.isEmpty())
            parsed.source = text;
        return parsed;
    }

private:
    QString authKey;
};

} // namespace

std::unique_ptr<Translator> makeTranslator()
{
    const QString key = AppSettings::translateApiKey();
    if (AppSettings::translateProvider() == QLatin1String("deepl") && !key.isEmpty())
        return std::make_unique<DeeplTranslator>(key);
    return std::make_unique<GoogleTranslator>();
}

} // namespace Translate
