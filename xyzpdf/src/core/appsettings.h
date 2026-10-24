#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "../theme/theme.h"

#include <QCryptographicHash>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>

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

// Which translation service "划线翻译" asks.
//
// Empty is the default and a real state: nothing is asked anywhere until the
// reader picks a service and gives it a key, because both services this program
// talks to are documented APIs that need one. The key itself is not here -- it
// is a credential and lives in the Windows credential manager (see
// src/translate/keystore.h). Settings hold only which service was chosen and
// whether the reader agreed to send text to it.
inline QString translateProviderKey()
{
    return QStringLiteral("translate/provider");
}

inline QString translateProvider()
{
    return settings().value(translateProviderKey()).toString();
}

inline void setTranslateProvider(const QString &provider)
{
    QSettings valueStore = settings();
    valueStore.setValue(translateProviderKey(), provider);
    valueStore.sync();
}

// Whether the reader has agreed to send selected text to one particular
// service. Per provider on purpose: agreeing to DeepL is not agreeing to
// Google, and switching services asks again. Revocable in the settings dialog.
inline QString translateConsentKey(const QString &provider)
{
    return QStringLiteral("translate/consent/") + provider;
}

inline bool translateConsentGiven(const QString &provider)
{
    if (provider.isEmpty())
        return false;
    return settings().value(translateConsentKey(provider), false).toBool();
}

inline void setTranslateConsent(const QString &provider, bool given)
{
    if (provider.isEmpty())
        return;
    QSettings valueStore = settings();
    valueStore.setValue(translateConsentKey(provider), given);
    valueStore.sync();
}

// Whether the reader wants a certificate's revocation status asked of the network.
//
// Off by default and the default is the whole design: the chain question is asked with
// CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY, which never leaves the machine, and the answer says
// "未联网检查吊销状态" rather than letting a clean verdict imply more than it was asked. Turning
// this on means XYZPDF opens the CRL and OCSP addresses that are written inside the certificate
// of whatever document the reader happens to have open -- which says to somebody else that this
// machine is reading that document. So the switch is not enough on its own: the first time a
// verification would actually go out, the reader is asked (see signatureRevocationConsentGiven).
inline QString signatureRevocationCheckKey()
{
    return QStringLiteral("signatures/checkRevocationOnline");
}

inline bool signatureRevocationCheckEnabled()
{
    QSettings valueStore = settings();
    return valueStore.value(signatureRevocationCheckKey(), false).toBool();
}

inline void setSignatureRevocationCheckEnabled(bool enabled)
{
    QSettings valueStore = settings();
    valueStore.setValue(signatureRevocationCheckKey(), enabled);
    valueStore.sync();
}

// Whether the reader has agreed, once, to that first connection. Stored apart from the switch
// because they are two different wishes: turning a setting on in a dialog is not the same act as
// agreeing, in front of a document, to have its certificates queried. Revocable, and saying no
// stores nothing -- the question comes again next time rather than being answered by a refusal.
inline QString signatureRevocationConsentKey()
{
    return QStringLiteral("signatures/revocationConsent");
}

inline bool signatureRevocationConsentGiven()
{
    return settings().value(signatureRevocationConsentKey(), false).toBool();
}

inline void setSignatureRevocationConsent(bool given)
{
    QSettings valueStore = settings();
    valueStore.setValue(signatureRevocationConsentKey(), given);
    valueStore.sync();
}

// How a translation is asked for.
//
// "menu" (the default) only translates when the reader chooses it -- the
// toolbar/menu action or the right-click menu over a selection. "auto" also
// fires the moment a selection is finished, which is what "划线即翻" means: a
// small window pops up with the answer without a second click. It is opt-in
// because a translation that pops up on every selection -- including the ones
// made just to copy -- is a window in the way more often than it is a help.
inline QString translateTriggerKey()
{
    return QStringLiteral("translate/trigger");
}

inline QString translateTrigger()
{
    return settings().value(translateTriggerKey(), QStringLiteral("menu")).toString();
}

inline void setTranslateTrigger(const QString &mode)
{
    QSettings valueStore = settings();
    valueStore.setValue(translateTriggerKey(), mode);
    valueStore.sync();
}

// Where a reader left off in a document, so reopening it puts them back.
//
// The enums are stored as their integer values: ZoomController::Mode and
// PageLayout::Mode, which the caller casts. Kept as ints here so this header
// does not have to know those types -- a stored 3 that no longer names a mode is
// clamped by the caller, not a crash.
struct DocumentSession
{
    int page = 0;
    int zoomMode = 0;
    double zoomPercent = 1.0;
    int layoutMode = 0;
};

// Sessions are keyed by a hash of the path, not the path itself: a Windows path
// is full of ':' and '\' and QSettings reads '/' as a group separator, so the
// literal path would not survive being a key. The hash is fixed-length and
// separator-free; a collision between two of one person's files is not worth
// worrying about.
inline QString documentSessionGroup(const QString &path)
{
    const QByteArray digest =
        QCryptographicHash::hash(path.toLower().toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("session/") + QString::fromLatin1(digest);
}

inline void saveDocumentSession(const QString &path, const DocumentSession &session)
{
    if (path.isEmpty())
        return;
    const QString group = documentSessionGroup(path);
    QSettings valueStore = settings();
    valueStore.setValue(group + QStringLiteral("/page"), session.page);
    valueStore.setValue(group + QStringLiteral("/zoomMode"), session.zoomMode);
    valueStore.setValue(group + QStringLiteral("/zoomPercent"), session.zoomPercent);
    valueStore.setValue(group + QStringLiteral("/layoutMode"), session.layoutMode);
    valueStore.sync();
}

// True when a session was stored for this path. Absent is the common case -- a
// first open -- and the caller then leaves the view at its defaults.
inline bool loadDocumentSession(const QString &path, DocumentSession &out)
{
    if (path.isEmpty())
        return false;
    const QString group = documentSessionGroup(path);
    QSettings valueStore = settings();
    if (!valueStore.contains(group + QStringLiteral("/page")))
        return false;
    out.page = valueStore.value(group + QStringLiteral("/page")).toInt();
    out.zoomMode = valueStore.value(group + QStringLiteral("/zoomMode")).toInt();
    out.zoomPercent = valueStore.value(group + QStringLiteral("/zoomPercent")).toDouble();
    out.layoutMode = valueStore.value(group + QStringLiteral("/layoutMode")).toInt();
    return true;
}

// A staged file this program wrote beside a reader's document, and the only kind of
// record that lets a later launch delete anything.
//
// `path` is the whole point of storing this: the sweep on the next launch removes files at
// paths it can find here and at no others -- a name that merely looks like one of ours is not
// evidence. `pid` and `program` together say who wrote it, which is what separates "this save
// is still running somewhere" from "the process that made this file is gone"; a PID alone is a
// recycled number, so the running process's own image name is compared with the one recorded
// here. `createdMs` is that process's reading of the file's creation time, the second proof
// that the bytes at the path are still the ones it staged. Zero means the record was written
// before the file existed -- which is how a write cut short by a kill is still covered.
struct StagedRecord
{
    QString path;
    qint64 pid = 0;
    qint64 createdMs = 0;
    QString program;
};

inline QString stagedRecordsKey()
{
    return QStringLiteral("staging/records");
}

// The stored shape: one line per record, the lines joined with a character no Windows path
// can hold, and the fields in the order `path|pid|createdMs|program`.
//
// The path goes first and the reading is done from the right, so that a '|' inside a path --
// illegal in a file name, but a directory component can carry one through a volume that is
// not NTFS -- lands in the path rather than shifting the numbers. It is stored as one string
// rather than a QStringList because IniFormat writes a list as a brace-separated set and a
// path is full of commas: escaping them works and reading the file by eye does not.
inline QString stagedRecordLine(const StagedRecord &record)
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(record.path)
        .arg(record.pid)
        .arg(record.createdMs)
        .arg(record.program);
}

inline StagedRecord stagedRecordFromLine(const QString &line)
{
    StagedRecord record;
    const int programStart = line.lastIndexOf(QLatin1Char('|'));
    const int createdStart = line.lastIndexOf(QLatin1Char('|'), programStart - 1);
    const int pidStart = line.lastIndexOf(QLatin1Char('|'), createdStart - 1);
    if (programStart <= 0 || createdStart <= 0 || pidStart <= 0)
        return record; // an empty path, and the caller skips it
    record.path = line.left(pidStart);
    record.pid = line.mid(pidStart + 1, createdStart - pidStart - 1).toLongLong();
    record.createdMs = line.mid(createdStart + 1, programStart - createdStart - 1).toLongLong();
    record.program = line.mid(programStart + 1);
    return record;
}

// Every record, oldest line first. A line that cannot be read is dropped here rather than
// passed on as an empty path: the sweep's one job is to never delete anything it did not
// record, and a half-parsed path is the closest this gets to deleting something it did.
inline QVector<StagedRecord> stagedRecords()
{
    QVector<StagedRecord> records;
    const QStringList lines
        = settings().value(stagedRecordsKey()).toString().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const StagedRecord record = stagedRecordFromLine(line);
        if (!record.path.isEmpty())
            records.append(record);
    }
    return records;
}

// Put one record in, replacing the one already stored for the same path.
//
// Replacing is what lets a staged file be recorded twice -- before its bytes exist, then
// again once the creation time can be read -- without the list growing by one every save.
inline void putStagedRecord(const StagedRecord &record)
{
    if (record.path.isEmpty())
        return;
    QStringList lines;
    for (const QString &line
         : settings().value(stagedRecordsKey()).toString().split(QLatin1Char('\n'))) {
        const StagedRecord existing = stagedRecordFromLine(line);
        // Case-insensitive because Windows paths are: the same file written down twice with
        // a different letter's case must not become two records, or the second one outlives
        // the delete of the first.
        if (!existing.path.isEmpty()
            && existing.path.compare(record.path, Qt::CaseInsensitive) != 0)
            lines << line;
    }
    lines << stagedRecordLine(record);
    QSettings valueStore = settings();
    valueStore.setValue(stagedRecordsKey(), lines.join(QLatin1Char('\n')));
    valueStore.sync();
}

// Drop the record for this path, and say whether one was there. A false answer is the common
// case: the save paths call this unconditionally, and most of the time they are deleting a
// staged file that was never recorded because the write came from a build of this program
// that did not record -- a file's record going missing costs a leftover, never a delete.
inline bool forgetStagedRecord(const QString &path)
{
    if (path.isEmpty())
        return false;
    QStringList kept;
    bool dropped = false;
    const QStringList lines
        = settings().value(stagedRecordsKey()).toString().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const StagedRecord existing = stagedRecordFromLine(line);
        if (!existing.path.isEmpty()
            && existing.path.compare(path, Qt::CaseInsensitive) == 0) {
            dropped = true;
            continue;
        }
        if (!line.isEmpty())
            kept << line;
    }
    if (!dropped)
        return false;
    QSettings valueStore = settings();
    if (kept.isEmpty())
        valueStore.remove(stagedRecordsKey());
    else
        valueStore.setValue(stagedRecordsKey(), kept.join(QLatin1Char('\n')));
    valueStore.sync();
    return true;
}

} // namespace AppSettings

#endif // APPSETTINGS_H
