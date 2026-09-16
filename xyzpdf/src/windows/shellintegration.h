#ifndef SHELLINTEGRATION_H
#define SHELLINTEGRATION_H

#include "../cli/commandline.h"

#include <QString>

// XYZPDF's entries in the Explorer context menu, written into HKCU.
//
// HKCU rather than HKLM so that nothing here needs administrator rights: these
// are one user's menu entries, and a program that asks for elevation to add a
// right-click item is asking for too much.
//
// On Windows 11 these land under 显示更多选项 rather than the first menu. The
// first menu takes an IExplorerCommand handler inside a package, which is
// separate work; this one stands on its own and keeps working when that
// arrives.
//
// ---- what M0 leaves out ----
//
// One verb: open with XYZPDF. Flatten, watermark, extract and the rest are M3,
// and they belong in a verb table that both this and the packaged handler read
// -- the arrangement XYZip uses, which is what stops the menu and the parser
// from disagreeing about what exists. Until there is a second verb there is
// nothing for a table to hold together.
namespace ShellIntegration {

// Where the entries live. A parameter rather than a constant so the tests can
// write somewhere disposable instead of into the real shell.
QString defaultClassesRoot();

// Whether a registry path names one specific key: a hive, and something after
// it.
//
// This is the guard in front of every recursive delete below, and it is exposed
// because a guard that cannot be tested is a guard nobody checks. The path that
// matters is "HKEY_CURRENT_USER\\" on its own -- a hive with nothing after it,
// which RegDeleteTree reads as "delete everything under this hive". Nothing
// reaches that today; the whole point is to keep it that way when something
// starts building these paths out of a variable.
bool isSpecificKeyPath(const QString &path);

// Writes the verb, the ProgID and the 打开方式 registration.
//
// Registering does not make XYZPDF the default handler for .pdf: Windows
// guards that with a per-user hash that only its own settings UI can produce.
// The ProgID is registered so that XYZPDF appears in the 打开方式 list, and the
// user chooses from there.
bool registerVerbs(const QString &executablePath, const QString &classesRoot,
                   QString *errorMessage);

// Removes everything registerVerbs wrote, and nothing else.
bool unregisterVerbs(const QString &classesRoot, QString *errorMessage);

// The executable the registered entries currently point at, empty when nothing
// is registered.
QString registeredExecutable(const QString &classesRoot);

inline bool isRegistered(const QString &classesRoot)
{
    return !registeredExecutable(classesRoot).isEmpty();
}

// Whether anything of XYZPDF's is in the shell at all, working or not.
//
// Not the same question as isRegistered(). A key with no values in it is not a
// working registration -- but Explorer may still draw it, and nothing else is
// ever going to clear it away. XYZip left exactly that behind once, so "off"
// has to mean "and none of the wreckage either".
bool hasAnyTrace(const QString &classesRoot);

// Rewrites the entries when they point at an executable that has moved or is no
// longer there.
//
// Called at startup. Without it, moving xyzpdf.exe leaves a menu full of
// entries that do nothing -- and the person who moved it has no reason to
// connect the two.
bool refreshIfStale(const QString &executablePath, const QString &classesRoot);

} // namespace ShellIntegration

#endif // SHELLINTEGRATION_H
