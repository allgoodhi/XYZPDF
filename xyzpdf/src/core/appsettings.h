#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "../theme/theme.h"

#include <QSettings>
#include <QString>
#include <QStringList>

// Every stored preference this program has, in one place.
//
// Kept as free functions over QSettings rather than as a settings object with
// setters: each of these is read where it is used and written where it is
// changed, and a cached copy in memory would have to be kept in step with the
// registry for no gain -- none of these are read in a loop.
namespace AppSettings {

inline QString themeKey()
{
    return QStringLiteral("appearance/theme");
}

inline QSettings settings()
{
    // Redirected to a file when asked, so a test never writes into the
    // registry of whoever is running it. tools/build_all_tests.py sets this for
    // every test it starts, which is why a test that saves a preference is not
    // a test that changes the machine it runs on.
    const QByteArray override = qgetenv("XYZPDF_SETTINGS_PATH");
    if (!override.isEmpty())
        return QSettings(QString::fromLocal8Bit(override), QSettings::IniFormat);
    return QSettings(QStringLiteral("xyzpdf"), QStringLiteral("xyzpdf"));
}

inline AppTheme theme()
{
    QSettings valueStore = settings();
    return appThemeFromId(valueStore.value(themeKey(), QStringLiteral("default")).toString());
}

inline void setTheme(AppTheme theme)
{
    QSettings valueStore = settings();
    valueStore.setValue(themeKey(), appThemeId(theme));
    valueStore.sync();
}

// Whether the page is drawn inverted, for reading in the dark.
//
// Separate from the interface theme on purpose: the two are not the same wish.
// Someone reading a scanned document on a laptop at night wants the page dark
// and the menus exactly as they are; tying the two together made choosing one
// change the other.
inline QString nightReadingKey()
{
    return QStringLiteral("appearance/nightReading");
}

inline bool nightReading()
{
    QSettings valueStore = settings();
    return valueStore.value(nightReadingKey(), false).toBool();
}

inline void setNightReading(bool enabled)
{
    QSettings valueStore = settings();
    valueStore.setValue(nightReadingKey(), enabled);
    valueStore.sync();
}

// Which language the interface speaks.
//
// "system" rather than a fixed default: a Chinese Windows gets Chinese and an
// English one gets English without anybody choosing, which is what almost
// everyone wants. An explicit code is for the people who want otherwise -- a
// Chinese speaker on an English system, most often.
//
// Stored as a locale code rather than an enumerator: Languages::known() is the
// one list of what exists, and an enumerator would have to be added to here,
// the loader and the picker every time a language was.
inline QString languageKey()
{
    return QStringLiteral("appearance/language");
}

inline QString languageCode()
{
    return settings().value(languageKey(), QStringLiteral("system")).toString();
}

inline void setLanguageCode(const QString &code)
{
    settings().setValue(languageKey(), code);
}

// Whether XYZPDF's entries are in the Explorer context menu.
//
// Kept here as well as in the registry so that the registry can be repaired
// after an uninstall-and-reinstall, or after another program overwrote the
// keys: the registry says what the shell currently does, this says what the
// user asked for.
inline QString shellIntegrationKey()
{
    return QStringLiteral("shell/contextMenu");
}

inline bool shellIntegrationEnabled()
{
    QSettings valueStore = settings();
    return valueStore.value(shellIntegrationKey(), false).toBool();
}

inline void setShellIntegrationEnabled(bool enabled)
{
    QSettings valueStore = settings();
    valueStore.setValue(shellIntegrationKey(), enabled);
    valueStore.sync();
}

// Where the file picker starts. Remembered because the alternative is every
// Open starting in Documents, however far away the work actually is.
inline QString lastOpenDirectoryKey()
{
    return QStringLiteral("files/lastOpenDirectory");
}

inline QString lastOpenDirectory()
{
    return settings().value(lastOpenDirectoryKey()).toString();
}

inline void setLastOpenDirectory(const QString &directory)
{
    QSettings valueStore = settings();
    valueStore.setValue(lastOpenDirectoryKey(), directory);
    valueStore.sync();
}

// How much of the render cache may sit in memory, in bytes.
//
// Zero means the default. Exposed because the right answer depends on the
// machine and on what else is running on it -- a 500-page scanned document at
// 200% zoom will happily fill a gigabyte, and somebody on a small laptop needs
// to be able to say so.
inline QString renderCacheKey()
{
    return QStringLiteral("render/cacheBytes");
}

inline QByteArray cacheOverride()
{
    return qgetenv("XYZPDF_RENDER_CACHE_BYTES");
}

inline qint64 renderCacheBytes(qint64 fallback)
{
    // The environment variable is read first: it is how a test or a developer
    // measures the eviction path without changing a real preference.
    const QByteArray override = cacheOverride();
    if (!override.isEmpty()) {
        bool understood = false;
        const qlonglong asked = override.toLongLong(&understood);
        if (understood && asked > 0)
            return asked;
    }
    QSettings valueStore = settings();
    bool understood = false;
    const qlonglong stored = valueStore.value(renderCacheKey(), qlonglong(0)).toLongLong(&understood);
    return understood && stored > 0 ? stored : fallback;
}

inline void setRenderCacheBytes(qint64 bytes)
{
    QSettings valueStore = settings();
    valueStore.setValue(renderCacheKey(), qlonglong(bytes));
    valueStore.sync();
}

} // namespace AppSettings

#endif // APPSETTINGS_H
