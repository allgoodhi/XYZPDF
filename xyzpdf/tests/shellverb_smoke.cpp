// The context menu verb: the wording, the decision, and what a click would run.
//
// ---- two halves, and why they are in one file ----
//
// The logic (which extension counts as a PDF, how a selection becomes command
// lines) is compiled straight in from src/windows/shellverb.cpp and called
// directly, the way commandline_smoke does. The COM object that consults it is
// loaded from the DLL, because that is the only way to ask a shell extension what
// it would show without a shell.
//
// The split is the reason the DLL has no Qt in it: everything that decides
// something is in a file that can be called, and what is left is the plumbing the
// shell reaches through.
//
// ---- what this cannot say ----
//
// That the row appears. That is the same routing question the preview handler's
// probe answered for the pane (docs/store-submission.md section 5), and it is
// answered here by right-clicking a PDF. What this can say is that when the shell
// does ask, the handler answers in the order and with the values it wants.
//
// Invoke is deliberately never called with a real selection: it starts a process,
// and a test that opens the reader on somebody's desktop has changed what it
// measures rather than measured it. The empty-selection path is what is asserted.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>
#include <shobjidl.h>

#include "../src/windows/shellverb.h"

#include <string>
#include <vector>

namespace {

// The class the package manifest declares.
// {8AFE6540-7E46-4349-BE06-ECD418310E42}
const CLSID kOpenCommandClsid = {
    0x8afe6540, 0x7e46, 0x4349, {0xbe, 0x06, 0xec, 0xd4, 0x18, 0x31, 0x0e, 0x42}};

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition)
        return;
    printf("FAIL: %s\n", what);
    ++failures;
}

std::wstring fromAnsi(const char *text)
{
    if (!text)
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring wide(needed > 0 ? needed - 1 : 0, L'\0');
    if (needed > 0)
        MultiByteToWideChar(CP_UTF8, 0, text, -1, &wide[0], needed);
    return wide;
}

std::string toAnsi(const std::wstring &text)
{
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0,
                                           nullptr, nullptr);
    std::string narrow(needed > 0 ? needed - 1 : 0, '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, narrow.data(), needed,
                            nullptr, nullptr);
    return narrow;
}

// ---------------------------------------------------------------------------
// The logic, called directly
// ---------------------------------------------------------------------------

void checkExtensionRecognition()
{
    check(ShellVerb::isPdfPath(L"C:\\a\\report.pdf"), "a lower case .pdf is a PDF");
    check(ShellVerb::isPdfPath(L"C:\\a\\REPORT.PDF"), "and so is an upper case one");
    check(ShellVerb::isPdfPath(L"report.PdF"), "however the letters are cased");
    check(!ShellVerb::isPdfPath(L"C:\\a\\notes.txt"), "a text file is not");
    check(!ShellVerb::isPdfPath(L"pdf"), "the extension alone is not");
    check(!ShellVerb::isPdfPath(L"a.pdfx"), "nor is something that merely starts like one");
    check(!ShellVerb::isPdfPath(L""), "and an empty path is not");
    check(!ShellVerb::isPdfPath(L"C:\\a\\pdf"), "a file named pdf with no extension is not");
}

void checkCommandLineBuilding()
{
    const std::wstring exe = L"C:\\app\\xyzpdf.exe";
    const std::vector<std::wstring> paths = {L"D:\\docs\\one.pdf", L"D:\\docs\\two.pdf"};

    size_t consumed = 0;
    const std::wstring line = ShellVerb::commandLineFor(exe, paths, 0, &consumed);
    check(consumed == 2, "one command line takes the whole small selection");
    check(line == L"\"C:\\app\\xyzpdf.exe\" \"D:\\docs\\one.pdf\" \"D:\\docs\\two.pdf\"",
          "and it is the executable then each path, every one of them quoted");

    // A selection too long for one process: every path still has to arrive, and
    // no line may run past the limit the API refuses.
    std::vector<std::wstring> many;
    const std::wstring filler(180, L'x');
    for (int index = 0; index < 300; ++index)
        many.push_back(L"D:\\" + filler + std::to_wstring(index) + L".pdf");
    const std::vector<std::wstring> lines = ShellVerb::commandLinesFor(exe, many);
    check(lines.size() > 1, "a selection that will not fit is split across processes");
    size_t seen = 0;
    bool everyLineFits = true;
    for (const std::wstring &each : lines) {
        if (each.size() > 30500)
            everyLineFits = false;
        size_t from = 0;
        while ((from = each.find(L".pdf", from)) != std::wstring::npos) {
            ++seen;
            from += 4;
        }
    }
    check(everyLineFits, "and none of the resulting lines is anywhere near the limit");
    check(seen == many.size(), "between them they carry every path exactly once");
}

void checkPathologicalCommandLine()
{
    const std::wstring exe = L"C:\\app\\xyzpdf.exe";
    const std::wstring enormous(40000, L'y');
    const std::vector<std::wstring> one = {enormous + L".pdf"};
    size_t consumed = 0;
    const std::wstring line = ShellVerb::commandLineFor(exe, one, 0, &consumed);
    check(consumed == 1 && !line.empty(), "a path longer than the cap is still taken");

    check(ShellVerb::commandLinesFor(exe, {}).empty(), "no paths means no command lines");
    check(ShellVerb::commandLinesFor(L"", {L"a.pdf"}).empty(),
          "and no executable means no command lines either");
}

// ---------------------------------------------------------------------------
// The COM object, through the DLL
// ---------------------------------------------------------------------------

IShellItemArray *arrayFor(const std::wstring &path)
{
    // Resolved to absolute first. The fixture arrives as a path relative to the
    // build directory, which is how the other handler tests are handed theirs, and
    // the shell item parser will not take it -- while the real selection always
    // does arrive absolute, so resolving here asks the same question the shell
    // would.
    wchar_t absolute[MAX_PATH * 2] = {};
    const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH * 2, absolute, nullptr);
    if (length == 0 || length >= MAX_PATH * 2)
        return nullptr;

    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(absolute, nullptr, IID_IShellItem,
                                           reinterpret_cast<void **>(&item)))
        || !item)
        return nullptr;
    IShellItemArray *array = nullptr;
    const HRESULT result = SHCreateShellItemArrayFromShellItem(
        item, IID_IShellItemArray, reinterpret_cast<void **>(&array));
    item->Release();
    return SUCCEEDED(result) ? array : nullptr;
}

EXPCMDSTATE stateOf(IExplorerCommand *command, const std::wstring &path)
{
    EXPCMDSTATE state = ECS_DISABLED;
    IShellItemArray *array = arrayFor(path);
    if (!array)
        return ECS_HIDDEN;   // the fixture is missing, which is reported elsewhere
    command->GetState(array, FALSE, &state);
    array->Release();
    return state;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: xyzpdf_shellverb_smoke <path to xyzpdfshell.dll> [fixture.pdf]\n");
        return 2;
    }
    const std::wstring dllPath = fromAnsi(argv[1]);
    const std::wstring fixture = argc > 2 ? fromAnsi(argv[2])
                                          : std::wstring(L"tests\\fixtures\\text.pdf");

    checkExtensionRecognition();
    checkCommandLineBuilding();
    checkPathologicalCommandLine();

    wchar_t notAPdf[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, notAPdf);
    lstrcpynW(notAPdf + wcslen(notAPdf), L"xyzpdf-shellverb-not-a-pdf.txt",
              MAX_PATH - int(wcslen(notAPdf)));
    {
        const HANDLE file = CreateFileW(notAPdf, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            const char text[] = "not a pdf";
            DWORD wrote = 0;
            WriteFile(file, text, sizeof(text) - 1, &wrote, nullptr);
            CloseHandle(file);
        }
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        printf("FAIL: CoInitializeEx failed, so nothing below this can mean anything\n");
        return 1;
    }

    // The same line the other handler tests carry. This DLL imports nothing that
    // is not in System32, so it would load either way -- which is exactly why the
    // restriction stays: it is the condition the others have to survive, and a
    // DLL that starts needing one of them should fail here rather than in Explorer.
    SetEnvironmentVariableW(L"PATH", L"C:\\Windows\\System32");

    HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(module != nullptr, "the verb DLL loads with PATH restricted to System32");
    if (!module) {
        printf("  LoadLibrary of %S failed with 0x%08lX\n", dllPath.c_str(), GetLastError());
        CoUninitialize();
        return 1;
    }

    using GetClassObjectProc = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **);
    auto getClassObject = reinterpret_cast<GetClassObjectProc>(
        GetProcAddress(module, "DllGetClassObject"));
    check(getClassObject != nullptr, "the DLL exports DllGetClassObject, as the manifest claims");
    if (!getClassObject) {
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }

    const CLSID kOtherClsid = {0xdeadbeef, 0x1234, 0x5678,
                               {0x9a, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1}};
    IClassFactory *notOurs = reinterpret_cast<IClassFactory *>(-1);
    check(getClassObject(kOtherClsid, IID_IClassFactory,
                         reinterpret_cast<void **>(&notOurs))
                  == CLASS_E_CLASSNOTAVAILABLE
              && notOurs == nullptr,
          "it refuses a CLSID that is not ours, and hands back nothing");

    IClassFactory *factory = nullptr;
    check(SUCCEEDED(getClassObject(kOpenCommandClsid, IID_IClassFactory,
                                  reinterpret_cast<void **>(&factory))),
          "the factory is handed out for the declared CLSID");
    if (!factory) {
        FreeLibrary(module);
        CoUninitialize();
        DeleteFileW(notAPdf);
        return 1;
    }

    IExplorerCommand *command = nullptr;
    check(SUCCEEDED(factory->CreateInstance(nullptr, __uuidof(IExplorerCommand),
                                            reinterpret_cast<void **>(&command))),
          "an IExplorerCommand can be created out of the factory");
    if (!command) {
        factory->Release();
        FreeLibrary(module);
        CoUninitialize();
        DeleteFileW(notAPdf);
        return 1;
    }

    GUID canonical = {};
    check(SUCCEEDED(command->GetCanonicalName(&canonical)) && canonical == kOpenCommandClsid,
          "its canonical name is the CLSID the manifest declares, which is how the shell "
          "remembers a command between sessions");

    EXPCMDFLAGS flags = ECF_HASSUBCOMMANDS;
    check(SUCCEEDED(command->GetFlags(&flags)) && flags == ECF_DEFAULT,
          "it says it is one row rather than a submenu");

    IEnumExplorerCommand *subcommands = reinterpret_cast<IEnumExplorerCommand *>(-1);
    check(command->EnumSubCommands(&subcommands) == E_NOTIMPL && subcommands == nullptr,
          "and asks for no children");

    LPWSTR title = nullptr;
    check(SUCCEEDED(command->GetTitle(nullptr, &title)) && title && *title,
          "it has a label to show");
    if (title) {
        printf("  label: %S\n", title);
        CoTaskMemFree(title);
    }

    LPWSTR icon = nullptr;
    check(SUCCEEDED(command->GetIcon(nullptr, &icon)) && icon,
          "and an icon reference, so the row is not the generic page again");
    if (icon) {
        const std::wstring text = icon;
        check(text.size() > 2 && text[text.size() - 2] == L',' && text.back() == L'0',
              "which points at an executable's first icon group");
        CoTaskMemFree(icon);
    }

    LPWSTR tip = nullptr;
    check(FAILED(command->GetToolTip(nullptr, &tip)) && tip == nullptr,
          "it declines to supply a tooltip rather than hand back an empty string");

    // The decision the shell acts on, for the three kinds of selection that
    // matter. A missing fixture counts as a failure rather than passing quietly.
    {
        const DWORD attributes = GetFileAttributesW(fixture.c_str());
        check(attributes != INVALID_FILE_ATTRIBUTES, "the PDF fixture is there to be asked about");
    }
    check(stateOf(command, fixture) == ECS_ENABLED,
          "for a PDF the row is offered");
    check(stateOf(command, notAPdf) == ECS_HIDDEN,
          "for anything else it is hidden rather than greyed out");
    {
        EXPCMDSTATE state = ECS_ENABLED;
        check(command->GetState(nullptr, FALSE, &state) == S_OK && state == ECS_HIDDEN,
              "and with no selection at all it stays out of the way");
    }

    // A click with nothing selected must not start the reader, and must not crash
    // the host that asked.
    check(command->Invoke(nullptr, nullptr) == E_FAIL,
          "invoking with no selection fails instead of launching an empty window");

    using CanUnloadProc = HRESULT(*)();
    auto canUnload = reinterpret_cast<CanUnloadProc>(GetProcAddress(module, "DllCanUnloadNow"));
    check(canUnload && canUnload() == S_FALSE,
          "it refuses to unload while the command is alive");

    command->Release();
    factory->Release();
    check(canUnload && canUnload() == S_OK,
          "and once nothing is alive any more, it says the DLL may unload");
    FreeLibrary(module);
    CoUninitialize();
    DeleteFileW(notAPdf);

    if (failures == 0) {
        printf("the verb recognises a PDF, builds a command line that carries the whole "
               "selection, and answers the shell's eight calls in the shape it wants\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
