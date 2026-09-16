#include "languages.h"

#include <QFile>
#include <QLocale>

namespace Languages {

namespace {

// Native names, not English ones. A picker that says "Japanese" is no use to
// someone who cannot read the language it is currently showing.
const QList<Entry> &table()
{
    static const QList<Entry> entries = {
        {QStringLiteral("zh_CN"), QStringLiteral("简体中文")},
        {QStringLiteral("zh_TW"), QStringLiteral("繁體中文")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("nl"), QStringLiteral("Nederlands")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
        {QStringLiteral("tr"), QStringLiteral("Türkçe")},
        {QStringLiteral("ar"), QStringLiteral("العربية")},
    };
    return entries;
}

// The source language needs no catalogue, so it is always available and must
// never be looked for on disk.
bool isSourceLanguage(const QString &code)
{
    return code == QStringLiteral("zh_CN");
}

} // namespace

QList<Entry> known()
{
    return table();
}

QString catalogueResource(const QString &code)
{
    return QStringLiteral(":/translations/xyzpdf_%1.qm").arg(code);
}

bool hasCatalogue(const QString &code)
{
    return isSourceLanguage(code) || QFile::exists(catalogueResource(code));
}

QList<Entry> available()
{
    QList<Entry> present;
    for (const Entry &entry : table()) {
        if (hasCatalogue(entry.code))
            present.append(entry);
    }
    return present;
}

QString nativeName(const QString &code)
{
    for (const Entry &entry : table()) {
        if (entry.code == code)
            return entry.nativeName;
    }
    return code;
}

QString resolve(const QString &storedCode)
{
    if (storedCode != systemCode()) {
        if (hasCatalogue(storedCode))
            return storedCode;
        // A language that was picked and then removed from the build, or a
        // value written by a newer version. Falling back is better than an
        // interface in a language nobody chose.
        return QStringLiteral("en");
    }

    const QLocale system = QLocale::system();

    // The full name first, so a Taiwanese or Hong Kong system gets traditional
    // Chinese rather than simplified, and a Brazilian one gets pt_BR.
    const QString full = system.name();
    if (hasCatalogue(full))
        return full;

    // Chinese is the case worth spelling out: QLocale::system().name() can be
    // zh_HK or zh_MO, neither of which is in the table, and both of which want
    // traditional rather than simplified.
    if (system.language() == QLocale::Chinese) {
        const QLocale::Script script = system.script();
        const bool traditional = script == QLocale::TraditionalHanScript
                                 || system.territory() == QLocale::Taiwan
                                 || system.territory() == QLocale::HongKong
                                 || system.territory() == QLocale::Macao;
        const QString wanted = traditional ? QStringLiteral("zh_TW")
                                           : QStringLiteral("zh_CN");
        if (hasCatalogue(wanted))
            return wanted;
        return QStringLiteral("zh_CN");
    }

    // Then the bare language, so en_GB and en_AU both land on English and
    // de_AT on German.
    const QString bare = QLocale::languageToCode(system.language());
    if (hasCatalogue(bare))
        return bare;

    // English rather than Chinese: someone whose system is set to a language
    // this build does not have is far more likely to read English.
    return QStringLiteral("en");
}

} // namespace Languages
