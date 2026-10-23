// Finding our own libraries from inside somebody else's process.
//
// Ported from XYZip's src/shell/previewdependencies.cpp, comments included: they
// are the record of what this cost to find, and the trap is identical here.
//
// ---- the failure this exists to prevent ----
//
// The preview handler is a DLL, and Windows loads it into DllHost.exe. When it
// then needs Qt6Core.dll, the loader looks where it always looks: beside the
// *executable* -- System32 -- then System32 again, then PATH. It does not look
// beside the DLL that asked. So every library this program ships with is
// invisible, the load fails with ERROR_MOD_NOT_FOUND before a single line of our
// code runs, and Explorer's pane says "没有预览" with nothing anywhere saying why.
//
// This is not hypothetical. It is what happened in XYZip the first time the
// handler stopped being a stub and started linking the archive readers, and the
// smoke test did not catch it: the test runner puts the build directory on PATH,
// so in the test the loader found everything. An environment more generous than
// the real one is how a test passes over a thing that cannot work at all -- which
// is why tests/previewhandler_smoke.cpp restricts PATH before loading, rather
// than trusting that a successful load means anything.
//
// ---- why delay loading ----
//
// DllMain runs too late. Static imports are resolved before it, so there is no
// moment at which our code could call AddDllDirectory in time. Delay loading
// moves each of these libraries to first use, which is after we exist, and the
// hook below answers with the copy that sits beside this DLL.
//
// The list of what is delay-loaded is in CMakeLists.txt next to this target,
// because it is a link option. It has to stay in step with what the handler
// actually imports -- dumpbin /dependents on the built DLL is how to check, and
// the smoke test is what notices when it does not.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "previewdependencies.h"

#include <windows.h>

#include <delayimp.h>

#include <string>

namespace PreviewDependencies {

std::wstring ownDirectory()
{
    // From an address inside this module rather than from a handle recorded in
    // DllMain: this has to work during a delay-load hook, which can fire before
    // anything else has had a chance to remember one.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ownDirectory),
                            &self)
        || !self) {
        return std::wstring();
    }

    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(self, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();

    std::wstring path(buffer, length);
    const std::size_t cut = path.find_last_of(L'\\');
    if (cut == std::wstring::npos)
        return std::wstring();
    return path.substr(0, cut + 1);
}

} // namespace PreviewDependencies

namespace {

FARPROC WINAPI loadFromOurOwnDirectory(unsigned reason, DelayLoadInfo *info)
{
    if (reason != dliNotePreLoadLibrary || !info || !info->szDll)
        return nullptr;

    const std::wstring directory = PreviewDependencies::ownDirectory();
    if (directory.empty())
        return nullptr;

    // szDll is the plain name from the import table, which is ASCII.
    std::wstring name;
    for (const char *at = info->szDll; *at; ++at)
        name += static_cast<wchar_t>(static_cast<unsigned char>(*at));

    // LOAD_WITH_ALTERED_SEARCH_PATH so that what this library imports in turn
    // -- Qt6Gui.dll wants Qt6Core.dll beside it -- is looked for beside it rather
    // than beside DllHost.exe, which is the same trap one level down. The engine
    // is pdfium.dll, which brings only itself (docs/pdfium-plan.md sec.6.1), so
    // the same line is harmless there.
    const HMODULE loaded = LoadLibraryExW((directory + name).c_str(),
                                          nullptr,
                                          LOAD_WITH_ALTERED_SEARCH_PATH);

    // Answering null is not a failure: it hands the decision back to the
    // ordinary loader, which is the right outcome for a library that genuinely
    // lives somewhere else -- ole32 and the rest of Windows, for instance.
    return reinterpret_cast<FARPROC>(loaded);
}

} // namespace

// The name the delay-load runtime looks for. Not ours to choose.
extern "C" const PfnDliHook __pfnDliNotifyHook2 = loadFromOurOwnDirectory;
