// The parts of translation that are arithmetic rather than a network call.
//
// A request URL that escapes the wrong character, and a response read with the
// wrong assumption about its shape, are the two ways this feature breaks quietly
// -- so both are pinned down here with bytes on hand, no socket, no key, and no
// service that could be down on the day the test runs.

#include "../src/translate/translator.h"

#include <QCoreApplication>
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

} // namespace

int main(int argc, char **argv)
{
    // An application object so the error sentences can be translated the way
    // they will be in the real program; the checks below never open a socket.
    QCoreApplication application(argc, argv);
    using namespace Translate;

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
    checkEquals(deeplTargetFor(QStringLiteral("de")), QStringLiteral("EN"),
                "DeepL falls back to EN");

    // ---- the request URL, and the escaping that has to be right ----
    {
        const QString url = googleRequestUrl(QStringLiteral("hello world?"), QStringLiteral("en"));
        check(url.startsWith(QStringLiteral("https://translate.googleapis.com/translate_a/single?")),
              "the free endpoint is asked");
        check(url.contains(QStringLiteral("client=gtx")) && url.contains(QStringLiteral("sl=auto"))
                  && url.contains(QStringLiteral("tl=en")) && url.contains(QStringLiteral("dt=t")),
              "the parameters that make it answer without a key are present");
        // The question mark in the text must not read as a second query
        // separator, and the space must be encoded -- both are what a naive
        // concatenation gets wrong.
        check(url.contains(QStringLiteral("q=hello%20world%3F")),
              "the selected text is percent-encoded into the query");
    }

    // ---- the Google answer ----
    {
        // The real shape: a list of [translated, original, ...] segments, then
        // the detected language further along.
        const QByteArray body = R"JSON([[["你好","hello",null,null,10],["世界","world",null,null,10]],null,"en",[],"en"])JSON";
        const Result result = parseGoogleResponse(body);
        check(result.ok, "a well-formed answer is a success");
        checkEquals(result.text, QStringLiteral("你好世界"),
                    "the segments are joined into one translation");
        checkEquals(result.source, QStringLiteral("helloworld"),
                    "the original segments are kept for the window to show");
        checkEquals(result.detectedLang, QStringLiteral("en"), "the language it decided is reported");
    }
    {
        const Result empty = parseGoogleResponse("[]");
        check(!empty.ok, "an empty array is not a success");
        const Result junk = parseGoogleResponse("this is not json");
        check(!junk.ok && !junk.error.isEmpty(), "unparseable bytes fail with a sentence");
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
        // What a bad key looks like: valid JSON, no translations array.
        const Result failed = parseDeeplResponse(R"JSON({"message":"Forbidden"})JSON");
        check(!failed.ok && !failed.error.isEmpty(), "an authentication failure is not a silent empty success");
    }

    if (failures == 0) {
        std::cout << "translate smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
