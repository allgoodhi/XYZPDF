#include "shellintegration.h"

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
#include <QSettings>

#include <string>

namespace ShellIntegration {
namespace {

// The menu strings, under a context of this namespace's own.
//
// QObject::tr() would put them in the "QObject" context, which is where every
// class without a Q_OBJECT macro lands -- including Qt's own internals -- and
// two strings that read the same in different contexts would be translated
// once and used for both.
QString tr(const char *text)
{
    return QCoreApplication::translate("ShellIntegration", text);
}

// The ProgID. Registered so XYZPDF shows up in 打开方式; it does not make XYZPDF
// the default for anything, which is not something a program gets to decide.
const QString kProgId = QStringLiteral("xyzpdf.Document");

// The one key everything hangs off. Also what unregisterVerbs removes, which is
// why it is a name of ours rather than something generic.
const QString kCascadeKey = QStringLiteral("xyzpdf");

// What the extension is, and what the type is called in the 打开方式 list.
const QString kExtension = QStringLiteral("pdf");

// The preview handler: its DLL's name beside the executable, the class compiled into
// src/windows/previewhandler.cpp (and declared in packaging/AppxManifest.xml, and registered by
// packaging/xyzpdf.iss -- tests/shellintegration_smoke.cpp reads it back), and the shell's own
// slot for "the preview handler of this type", IID_IPreviewHandler.
const QString kPreviewDll = QStringLiteral("xyzpdfpreview.dll");
const QString kPreviewHandlerClsid = QStringLiteral("{12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7}");
const QString kPreviewHandlerSlot = QStringLiteral("{8895b1c6-b41f-4c1c-a562-0d564250836f}");

// Windows enumerates subkeys alphabetically, so the order of the menu only
// survives if it is written into the key names. One verb for now, numbered
// anyway: the numbering is the part that would be forgotten when the second one
// arrives.
QString orderedKey(int index, const char *key)
{
    return QStringLiteral("%1_%2").arg(index, 2, 10, QLatin1Char('0')).arg(QString::fromLatin1(key));
}

QString quotedCommand(const QString &executablePath, CommandLine::Action action)
{
    const QString program =
        QLatin1Char('"') + QDir::toNativeSeparators(executablePath) + QLatin1Char('"');
    const QString option = CommandLine::switchFor(action);
    // %1 is the clicked item. Quoted, because paths have spaces in them and
    // Explorer does not add the quotes.
    return option.isEmpty() ? program + QStringLiteral(" \"%1\"")
                            : program + QLatin1Char(' ') + option + QStringLiteral(" \"%1\"");
}

QSettings openKey(const QString &path)
{
    return QSettings(path, QSettings::NativeFormat);
}

// Split a "HKEY_CURRENT_USER\\..." path into the hive and the rest.
bool splitRoot(const QString &path, HKEY *hive, std::wstring *subKey)
{
    struct Root
    {
        const char *prefix;
        HKEY key;
    };
    static const Root roots[] = {
        {"HKEY_CURRENT_USER\\", HKEY_CURRENT_USER},
        {"HKEY_LOCAL_MACHINE\\", HKEY_LOCAL_MACHINE},
        {"HKEY_CLASSES_ROOT\\", HKEY_CLASSES_ROOT},
    };

    for (const Root &root : roots) {
        const QString prefix = QString::fromLatin1(root.prefix);
        if (!path.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        *hive = root.key;
        *subKey = path.mid(prefix.size()).toStdWString();
        return true;
    }
    return false;
}

// Read a value without creating anything.
//
// This is not what QSettings does. Constructing a QSettings on a registry path
// creates that path, so asking "is XYZPDF registered?" was quietly building the
// very keys it was looking for -- an empty cascade, written afresh on every
// launch. Those keys are then old enough to look like leftovers from a previous
// version, and Explorer is entitled to draw an empty verb key as a menu entry.
QString readStringValue(const QString &path, const QString &valueName)
{
    HKEY hive = nullptr;
    std::wstring subKey;
    if (!splitRoot(path, &hive, &subKey))
        return QString();

    const std::wstring wideName = valueName.toStdWString();
    DWORD bytes = 0;
    if (RegGetValueW(hive, subKey.c_str(), wideName.c_str(), RRF_RT_REG_SZ, nullptr, nullptr,
                     &bytes)
            != ERROR_SUCCESS
        || bytes == 0) {
        return QString();
    }

    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(hive, subKey.c_str(), wideName.c_str(), RRF_RT_REG_SZ, nullptr,
                     buffer.data(), &bytes)
        != ERROR_SUCCESS) {
        return QString();
    }
    return QString::fromWCharArray(buffer.c_str());
}

bool keyExists(const QString &path)
{
    HKEY hive = nullptr;
    std::wstring subKey;
    if (!splitRoot(path, &hive, &subKey))
        return false;
    HKEY handle = nullptr;
    if (RegOpenKeyExW(hive, subKey.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS)
        return false;
    RegCloseKey(handle);
    return true;
}

// Delete a key and everything under it, for real.
//
// QSettings::remove() clears the values but is not a promise about the keys.
// RegDeleteTree is the API that actually means "and everything below".
//
// It also means "everything below" when the subkey is empty, which is why the
// path is checked first: "HKEY_CURRENT_USER\\" with no subkey would throw away
// the user's whole hive. Nothing builds such a path today -- the two call sites
// append compile-time constants -- but that is exactly the shape of the
// accident XYZip had, and the guard costs one line and one test.
bool removeKeyTree(const QString &path)
{
    if (!isSpecificKeyPath(path))
        return false;

    HKEY hive = nullptr;
    std::wstring subKey;
    if (!splitRoot(path, &hive, &subKey))
        return false;
    const LSTATUS status = RegDeleteTreeW(hive, subKey.c_str());
    // Not being there is the outcome asked for.
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

// The one place the menu is written.
//
// It has to be exactly one. Explorer only collapses two identically named
// entries when a single item is selected; select several and it merges every
// applicable source, so a second key means a second "XYZPDF" sitting next to
// the first with no way to tell them apart.
//
// AllFilesystemObjects rather than "*": "*" is files only, and folders would
// need Directory as well -- which is how the duplicate appeared in XYZip in the
// first place. This covers files and folders together.
QString cascadeLocation(const QString &classesRoot)
{
    return classesRoot + QStringLiteral("\\AllFilesystemObjects\\shell");
}

// The condition that keeps the entry off a .txt.
//
// AppliesTo is the shell's own answer to "which verbs belong to what was
// clicked": Explorer evaluates the query against the selection and leaves the
// verb out when it does not match.
QString documentAppliesTo()
{
    return QStringLiteral("System.FileName:\"*.") + kExtension + QStringLiteral("\"");
}

} // namespace

QString defaultClassesRoot()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes");
}

bool isSpecificKeyPath(const QString &path)
{
    HKEY hive = nullptr;
    std::wstring subKey;
    if (!splitRoot(path, &hive, &subKey))
        return false;
    // Whitespace is not a subkey either: a path assembled from a variable that
    // came out empty is how this would go wrong in practice, and " " would
    // otherwise be treated as one.
    return !subKey.empty() && subKey.find_first_not_of(L"\\ \t") != std::wstring::npos;
}

bool registerVerbs(const QString &executablePath, const QString &classesRoot,
                   QString *errorMessage)
{
    const QFileInfo executable(executablePath);
    if (!executable.isFile()) {
        if (errorMessage) {
            *errorMessage = tr("找不到 XYZPDF 程序文件：%1")
                                .arg(QDir::toNativeSeparators(executablePath));
        }
        return false;
    }
    const QString program = executable.absoluteFilePath();
    const QString icon =
        QLatin1Char('"') + QDir::toNativeSeparators(program) + QStringLiteral("\",0");

    // Start from a clean slate, so that anything an earlier version wrote does
    // not linger in somebody's menu forever.
    unregisterVerbs(classesRoot, nullptr);

    const QString cascade = cascadeLocation(classesRoot) + QLatin1Char('\\') + kCascadeKey;

    // One entry, with the verbs underneath it.
    //
    // An empty SubCommands value is what tells Explorer to build the flyout
    // from the "shell" subkey below. It has to be present and it has to be
    // empty: a non-empty value means "look these verb names up in the machine
    // wide CommandStore", which is not where these live.
    {
        QSettings entry = openKey(cascade);
        entry.setValue(QStringLiteral("MUIVerb"), QStringLiteral("XYZPDF"));
        entry.setValue(QStringLiteral("Icon"), icon);
        entry.setValue(QStringLiteral("SubCommands"), QString());
        // Say where it goes, or it lands somewhere different depending on what
        // was selected: a verb with no Position takes whatever place falls out
        // of the order Explorer merged its sources in, and that order changes
        // with the selection.
        entry.setValue(QStringLiteral("Position"), QStringLiteral("Top"));
        entry.sync();
    }

    {
        const QString base = cascade + QStringLiteral("\\shell\\")
                             + orderedKey(1, "open");
        QSettings item = openKey(base);
        item.setValue(QStringLiteral("MUIVerb"), tr("用 XYZPDF 打开"));
        item.setValue(QStringLiteral("Icon"), icon);
        item.setValue(QStringLiteral("AppliesTo"), documentAppliesTo());
        item.sync();

        QSettings command = openKey(base + QStringLiteral("\\command"));
        command.setValue(QStringLiteral("Default"),
                         quotedCommand(program, CommandLine::Action::Open));
        command.sync();
        if (command.status() != QSettings::NoError) {
            if (errorMessage)
                *errorMessage = tr("无法写入注册表：%1").arg(base);
            return false;
        }
    }

    // The ProgID, so XYZPDF can be picked from 打开方式.
    {
        QSettings progId = openKey(classesRoot + QLatin1Char('\\') + kProgId);
        progId.setValue(QStringLiteral("Default"), tr("PDF 文档"));
        progId.sync();

        QSettings progIdIcon =
            openKey(classesRoot + QLatin1Char('\\') + kProgId + QStringLiteral("\\DefaultIcon"));
        progIdIcon.setValue(QStringLiteral("Default"), icon);
        progIdIcon.sync();

        QSettings progIdCommand = openKey(classesRoot + QLatin1Char('\\') + kProgId
                                          + QStringLiteral("\\shell\\open\\command"));
        progIdCommand.setValue(QStringLiteral("Default"),
                               quotedCommand(program, CommandLine::Action::Open));
        progIdCommand.sync();
    }

    // The preview pane, when the preview handler sits beside this executable -- which is how the
    // installer lays it out (packaging/xyzpdf.iss), and how the build tree has it too.
    //
    // Under our ProgID, not under .pdf: the extension's ShellEx is where the reader's other PDF
    // program keeps its own previewer (Edge has one there, in HKCU as well as HKLM, on this
    // machine), and overwriting it would take that preview away whether or not XYZPDF is what
    // they open PDFs with. Under the ProgID it applies exactly when they chose XYZPDF, which is
    // also what the packaged build's DesktopPreviewHandler declaration amounts to. The class
    // itself (CLSID\...\InprocServer32) is the installer's to register, beside the DLL it
    // names; a pointer to a class nobody registered is the pane saying "no preview", the same
    // as no pointer at all. Removed with the ProgID by unregisterVerbs().
    if (QFileInfo(executable.dir().filePath(kPreviewDll)).isFile()) {
        QSettings preview = openKey(classesRoot + QLatin1Char('\\') + kProgId
                                    + QStringLiteral("\\ShellEx\\") + kPreviewHandlerSlot);
        preview.setValue(QStringLiteral("Default"), kPreviewHandlerClsid);
        preview.sync();
    }

    // Offered for the type, never forced. Windows protects the actual default
    // with a hash only its own settings UI can produce; a program that writes it
    // anyway is doing what malware does, so this does not.
    {
        QSettings openWith(classesRoot + QStringLiteral("\\.") + kExtension
                               + QStringLiteral("\\OpenWithProgids"),
                           QSettings::NativeFormat);
        openWith.setValue(kProgId, QString());
        openWith.sync();
    }

    return true;
}

bool unregisterVerbs(const QString &classesRoot, QString *errorMessage)
{
    Q_UNUSED(errorMessage)

    removeKeyTree(cascadeLocation(classesRoot) + QLatin1Char('\\') + kCascadeKey);
    removeKeyTree(classesRoot + QLatin1Char('\\') + kProgId);

    // The OpenWithProgids key belongs to every program that can open the type,
    // so only our own entry goes -- and only if the key is already there:
    // opening it through QSettings would create it, and uninstalling should not
    // leave behind a key for a type that had none.
    const QString openWithPath = classesRoot + QStringLiteral("\\.") + kExtension
                                 + QStringLiteral("\\OpenWithProgids");
    if (keyExists(openWithPath)) {
        QSettings openWith(openWithPath, QSettings::NativeFormat);
        openWith.remove(kProgId);
        openWith.sync();
    }
    return true;
}

namespace {

// Two spellings of the same file, made comparable.
//
// The registry holds what was written, with native separators; the path a
// program is handed by QCoreApplication::applicationFilePath() has forward
// slashes, because that is how Qt keeps paths internally. Compared as strings
// those never match, so the startup repair decided the program had moved on
// every single launch and rewrote the registry each time -- harmless-looking,
// and it means a registry write per launch as well as an entry that is
// rewritten under the user rather than only when it needs to be.
QString comparablePath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}

} // namespace

QString registeredExecutable(const QString &classesRoot)
{
    const QString value =
        readStringValue(cascadeLocation(classesRoot) + QLatin1Char('\\') + kCascadeKey
                            + QStringLiteral("\\shell\\") + orderedKey(1, "open")
                            + QStringLiteral("\\command"),
                        QString());
    if (value.isEmpty() || !value.startsWith(QLatin1Char('"')))
        return QString();
    const qsizetype closing = value.indexOf(QLatin1Char('"'), 1);
    if (closing < 0)
        return QString();
    // Exactly what is in the registry, separators and all: this is the answer to
    // "what does the shell run", and tidying it up here would hide a
    // registration that points somewhere nobody meant.
    return value.mid(1, closing - 1);
}

bool hasAnyTrace(const QString &classesRoot)
{
    if (keyExists(cascadeLocation(classesRoot) + QLatin1Char('\\') + kCascadeKey))
        return true;
    return keyExists(classesRoot + QLatin1Char('\\') + kProgId);
}

bool refreshIfStale(const QString &executablePath, const QString &classesRoot)
{
    const QString registered = registeredExecutable(classesRoot);
    if (registered.isEmpty())
        return false; // nothing registered; not this function's business

    const QString current = QFileInfo(executablePath).absoluteFilePath();
    if (comparablePath(registered) == comparablePath(current) && QFileInfo::exists(registered))
        return false;
    return registerVerbs(current, classesRoot, nullptr);
}

} // namespace ShellIntegration
