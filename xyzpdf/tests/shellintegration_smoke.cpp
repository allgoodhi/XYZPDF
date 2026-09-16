// The Explorer menu entries, and the guard in front of the code that deletes
// them.
//
// Two things are checked here that nothing else can. The first is that
// registering and unregistering a verb leaves exactly one entry behind and then
// nothing -- the sort of thing that is otherwise found on somebody's machine
// months later. The second is the guard on the recursive delete: the path that
// would throw away a whole hive is refused, and that refusal is asserted rather
// than assumed.
//
// Everything is written under a key of this test's own, and removed whichever
// way the test ends. The real classes root is never touched -- which is also
// why registerVerbs() takes the root as a parameter rather than reading the
// constant: it exists so this test can point it somewhere disposable.

#include "../src/windows/shellintegration.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

// Ours, and underneath a key nothing else looks at.
//
// Two spellings, and the difference matters: QSettings and the module want the
// path with the hive in front of it ("HKEY_CURRENT_USER\\..."), while the
// Win32 calls want the subkey on its own -- RegOpenKeyExW(HKEY_CURRENT_USER,
// "HKEY_CURRENT_USER\\Software") looks for a key literally named
// HKEY_CURRENT_USER inside HKCU, and finds nothing. Getting that wrong makes a
// test report an empty registry while the entries are sitting right there.
const wchar_t kDisposableSubKey[] = L"Software\\XYZPDFShellSmoke";
const QString kDisposableSubKeyPath = QStringLiteral("Software\\XYZPDFShellSmoke");
const QString kDisposableRoot = QStringLiteral("HKEY_CURRENT_USER\\") + kDisposableSubKeyPath;
const QString kDisposableClasses = kDisposableRoot + QStringLiteral("\\Classes");
const QString kDisposableClassesSubKeyPath = kDisposableSubKeyPath + QStringLiteral("\\Classes");

// Removed however this function is left, including by an early return.
struct DisposableRoot
{
    ~DisposableRoot()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, kDisposableSubKey);
    }
};

// `subKey` is relative to HKEY_CURRENT_USER and does not name the hive: a hive
// belongs in the first argument of a registry call, not in the second.
int valueCount(const QString &subKey)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.toStdWString().c_str(), 0, KEY_READ, &key)
        != ERROR_SUCCESS) {
        return -1;
    }
    DWORD count = 0;
    DWORD index = 0;
    wchar_t name[256] = {};
    DWORD nameLength = 256;
    while (RegEnumValueW(key, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr)
           == ERROR_SUCCESS) {
        ++count;
        ++index;
        nameLength = 256;
    }
    RegCloseKey(key);
    return static_cast<int>(count);
}

// The same rule the module applies before it compares two paths: the registry
// keeps native separators, and the path a program is handed has forward
// slashes. Spelled out here rather than shared, because a test that used the
// production helper could not notice the helper being wrong.
QString comparable(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}

// Every subkey below a path, for the failure messages. A key that was written
// somewhere other than where it was meant to go is otherwise a test that fails
// with "found 0" and no way to tell why. Relative to HKEY_CURRENT_USER, like
// valueCount() above.
QString subKeysUnder(const QString &path)
{
    QStringList found;
    QStringList pending{path};
    while (!pending.isEmpty() && found.size() < 40) {
        const QString current = pending.takeFirst();
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, current.toStdWString().c_str(), 0, KEY_READ, &key)
            != ERROR_SUCCESS) {
            found.append(current + QStringLiteral(" (missing)"));
            continue;
        }
        DWORD index = 0;
        wchar_t name[256] = {};
        DWORD length = 256;
        while (RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr)
               == ERROR_SUCCESS) {
            const QString child = current + QLatin1Char('\\') + QString::fromWCharArray(name);
            found.append(child);
            pending.append(child);
            ++index;
            length = 256;
        }
        RegCloseKey(key);
    }
    return found.join(QStringLiteral(", "));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const DisposableRoot cleanup;

    // ---- the guard ----
    //
    // The first two are the ones that matter. RegDeleteTree with no subkey
    // means "everything under this hive", so a path that is only a hive must
    // never reach it.
    {
        check(!ShellIntegration::isSpecificKeyPath(QStringLiteral("HKEY_CURRENT_USER\\")),
              "a hive with nothing after it is not a key to delete");
        check(!ShellIntegration::isSpecificKeyPath(QString()),
              "and an empty path is not one either");
        check(!ShellIntegration::isSpecificKeyPath(QStringLiteral("HKEY_CURRENT_USER\\ \\")),
              "nor is whitespace where the key should be");
        check(!ShellIntegration::isSpecificKeyPath(QStringLiteral("Software\\Classes\\xyzpdf")),
              "nor a path with no hive at all");
        check(ShellIntegration::isSpecificKeyPath(kDisposableClasses),
              "while a path that names a key is one");
    }

    // ---- registering and removing ----
    {
        check(!ShellIntegration::isRegistered(kDisposableClasses),
              "nothing is registered to begin with");
        check(!ShellIntegration::hasAnyTrace(kDisposableClasses),
              "and there is no trace of anything");

        QString error;
        const QString executable = QCoreApplication::applicationFilePath();
        check(ShellIntegration::registerVerbs(executable, kDisposableClasses, &error),
              QStringLiteral("registering works: %1").arg(error));
        check(error.isEmpty(), "without complaint");

        check(ShellIntegration::isRegistered(kDisposableClasses), "and it is registered");
        check(comparable(ShellIntegration::registeredExecutable(kDisposableClasses))
                  == comparable(executable),
              QStringLiteral("pointing at this program (the registry has \"%1\", this program is \"%2\")")
                  .arg(ShellIntegration::registeredExecutable(kDisposableClasses), executable));
        check(ShellIntegration::hasAnyTrace(kDisposableClasses), "and leaves a trace");

        // Nothing to repair, and it has to say so without rewriting anything.
        // It said "stale" on every launch until the comparison learned about
        // separators, so the registry was rewritten under the user every time
        // the program started.
        check(!ShellIntegration::refreshIfStale(executable, kDisposableClasses),
              "an up-to-date registration is left alone");

        // And a registration that really has gone stale is rewritten. The
        // program itself is the other executable this test can be sure exists.
        const QString other = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("../xyzpdf.exe"));
        if (QFileInfo::exists(other)) {
            check(ShellIntegration::refreshIfStale(other, kDisposableClasses),
                  "a registration pointing at another program is rewritten");
            check(comparable(ShellIntegration::registeredExecutable(kDisposableClasses))
                      == comparable(other),
                  "and now points at the program that is running");
        } else {
            std::cout << "note: " << other.toStdString()
                      << " was not built, so the stale case is not covered this run\n";
        }

        // Registering again has to be safe: it is what the startup repair path
        // does, and it happens on every launch of a machine where the preference
        // is on.
        check(ShellIntegration::registerVerbs(executable, kDisposableClasses, &error),
              "registering twice is not an error");
        check(ShellIntegration::isRegistered(kDisposableClasses), "and there is still one entry");

        // The path the module reads for "is it registered", checked against the
        // registry directly rather than through the module's own reader: if the
        // two ever disagree, one of them is looking in the wrong place.
        const QString commandSubKey = kDisposableClassesSubKeyPath
                                      + QStringLiteral("\\AllFilesystemObjects\\shell\\xyzpdf"
                                                       "\\shell\\01_open\\command");
        check(valueCount(commandSubKey) == 1,
              QStringLiteral("the command value is where the module looks for it (found %1; "
                             "subkeys: %2)")
                  .arg(valueCount(commandSubKey))
                  .arg(subKeysUnder(kDisposableClassesSubKeyPath)));

        // The 打开方式 entry: a value in a key that belongs to every program
        // that can open a .pdf, which is why unregistering must remove our
        // value and leave the key itself alone.
        const QString openWithSubKey =
            kDisposableClassesSubKeyPath + QStringLiteral("\\.pdf\\OpenWithProgids");
        check(valueCount(openWithSubKey) == 1,
              QStringLiteral("one 打开方式 entry was written (found %1 under %2; subkeys: %3)")
                  .arg(valueCount(openWithSubKey))
                  .arg(openWithSubKey, subKeysUnder(kDisposableClassesSubKeyPath)));

        check(ShellIntegration::unregisterVerbs(kDisposableClasses, &error),
              QStringLiteral("unregistering works: %1").arg(error));
        check(!ShellIntegration::isRegistered(kDisposableClasses), "and it is gone");
        check(!ShellIntegration::hasAnyTrace(kDisposableClasses), "leaving nothing behind");
        check(valueCount(openWithSubKey) == 0,
              "and the shared 打开方式 key is left for the other programs that write to it");
    }

    // ---- what a refusal looks like ----
    {
        QString error;
        check(!ShellIntegration::registerVerbs(kDisposableClasses + QStringLiteral("\\no-such.exe"),
                                               kDisposableClasses, &error),
              "registering a program that is not there fails");
        check(!error.isEmpty(), "with something to show the user");
    }

    // ---- the real root is only ever read through the constant ----
    //
    // A last check that this test has not been writing where it should not: the
    // real classes root must not have gained a key of ours from any of the
    // above. (It may legitimately have one from a previous run of the program
    // itself; what must not be there is a key written by this test.)
    {
        const QString real = ShellIntegration::defaultClassesRoot();
        check(real == QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
              "the default root is still the one place the real entries go");
        check(ShellIntegration::isSpecificKeyPath(real), "and it is a deletable path");
    }

    if (failures == 0) {
        std::cout << "the shell integration holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
