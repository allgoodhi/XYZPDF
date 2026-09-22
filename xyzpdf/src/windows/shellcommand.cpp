// The context menu entry, as the shell asks for it.
//
// ---- what this is ----
//
// A COM server declared in the package manifest (com:SurrogateServer) and
// referenced from the file type's desktop5:Verb, so Explorer activates it and
// asks for an IExplorerCommand: what is the entry called, should it be showing
// for this selection, and what happens when it is clicked.
//
// It exists because a packaged app cannot write the registry keys the loose
// build writes. Declaring the .pdf association in the manifest buys the file
// type, the icon and the default-app candidacy, and takes away the "用 XYZPDF
// 打开" row that HKCU\Software\Classes used to carry -- measured, and recorded
// in docs/store-submission.md section 3. This is the only route back.
//
// ---- no Qt, and no delay-load hook, on purpose ----
//
// The other two handlers in this directory read a PDF, so they carry Qt and need
// previewdependencies.cpp to make it findable from DllHost. This one never opens
// a document: it decides a label, looks at a file extension, and starts a
// process. Everything it needs is in Windows, which is why it is its own DLL
// rather than a third class inside xyzpdfpreview.dll -- a host that asks for a
// menu should not have to load a PDF renderer to answer one.
//
// ---- one verb, not a submenu ----
//
// GetFlags says ECF_DEFAULT and EnumSubCommands says E_NOTIMPL, so the shell
// shows a single row. A submenu would need IObjectWithSelection to carry the
// selection down to children, which is machinery worth having when there is more
// than one thing to offer.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>
#include <shobjidl.h>

#include "shellverb.h"

#include <string>
#include <vector>

// The class the package manifest declares for the verb.
// {8AFE6540-7E46-4349-BE06-ECD418310E42}
const CLSID kOpenCommandClsid = {
    0x8afe6540, 0x7e46, 0x4349, {0xbe, 0x06, 0xec, 0xd4, 0x18, 0x31, 0x0e, 0x42}};

namespace {

HMODULE g_module = nullptr;

long liveCommands_ = 0;
long serverLocks_ = 0;

// xyzpdf.exe sits beside this DLL, in the package's bin\.
//
// Asked of the module rather than of the registry or of the package APIs: this is
// the one place that is guaranteed to be true for the build being run, and it is
// how XYZip finds its own executable from inside Explorer. The working directory
// belongs to the host, which is not a thing to reason about.
std::wstring executablePath()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(g_module, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();
    std::wstring path(buffer, length);
    const std::size_t cut = path.find_last_of(L'\\');
    if (cut == std::wstring::npos)
        return std::wstring();
    return path.substr(0, cut + 1) + L"xyzpdf.exe";
}

// COM hands strings back through the task allocator, and the shell frees them.
HRESULT copyToShell(const std::wstring &text, LPWSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    auto *buffer = static_cast<LPWSTR>(
        CoTaskMemAlloc((text.size() + 1) * sizeof(wchar_t)));
    if (!buffer)
        return E_OUTOFMEMORY;
    memcpy(buffer, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    *out = buffer;
    return S_OK;
}

// The filesystem paths of a selection, in the order the shell listed them.
//
// Items with no path are dropped rather than guessed at: a library, a search
// result, or an entry inside somebody's archive has no filename that xyzpdf.exe
// could open, and passing an empty string would start the reader on nothing.
std::vector<std::wstring> pathsOf(IShellItemArray *selection)
{
    std::vector<std::wstring> paths;
    if (!selection)
        return paths;
    DWORD count = 0;
    selection->GetCount(&count);
    for (DWORD index = 0; index < count; ++index) {
        IShellItem *item = nullptr;
        if (FAILED(selection->GetItemAt(index, &item)) || !item)
            continue;
        PWSTR raw = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
            paths.emplace_back(raw);
            CoTaskMemFree(raw);
        }
        item->Release();
    }
    return paths;
}

} // namespace

class OpenCommand final : public IExplorerCommand {
public:
    OpenCommand() { InterlockedIncrement(&liveCommands_); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(IExplorerCommand))
            *object = static_cast<IExplorerCommand *>(this);
        else {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&references_); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG left = InterlockedDecrement(&references_);
        if (left == 0)
            delete this;
        return left;
    }

    // IExplorerCommand
    IFACEMETHODIMP GetTitle(IShellItemArray *, LPWSTR *name) override
    {
        return copyToShell(ShellVerb::title(), name);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray *, LPWSTR *icon) override
    {
        // The program's own icon, out of the executable's resource table. Empty
        // would be the shell default, which is the PDF page icon the file already
        // has -- the same picture as the row above it, saying nothing.
        const std::wstring executable = executablePath();
        if (executable.empty())
            return E_FAIL;
        return copyToShell(executable + L",0", icon);
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray *, LPWSTR *tip) override
    {
        // No tooltip. The label is the whole instruction, and the shell gives a
        // handler no place to put anything that would explain a refusal.
        if (!tip)
            return E_POINTER;
        *tip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID *name) override
    {
        if (!name)
            return E_POINTER;
        *name = kOpenCommandClsid;
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray *selection, BOOL okToBeSlow,
                            EXPCMDSTATE *state) override
    {
        // Nothing here is slow: an extension is read off the string the shell
        // already had, so the answer is the same whether or not waiting is fine.
        (void)okToBeSlow;
        if (!state)
            return E_POINTER;
        const std::vector<std::wstring> paths = pathsOf(selection);
        if (paths.empty()) {
            *state = ECS_HIDDEN;
            return S_OK;
        }
        for (const std::wstring &path : paths) {
            if (!ShellVerb::isPdfPath(path)) {
                // Hidden rather than greyed out. The row would be dead for every
                // non-PDF on the machine, and a dead row is a question -- this
                // handler is asked about selections it was not promised to, and
                // the answer to "why can't I click this" is not available here.
                *state = ECS_HIDDEN;
                return S_OK;
            }
        }
        *state = ECS_ENABLED;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray *selection, IBindCtx *) override
    {
        const std::wstring executable = executablePath();
        if (executable.empty())
            return E_FAIL;

        std::vector<std::wstring> paths;
        for (const std::wstring &path : pathsOf(selection)) {
            if (ShellVerb::isPdfPath(path))
                paths.push_back(path);
        }
        if (paths.empty())
            return E_FAIL;

        // One process per command line, and as many paths on a line as fit.
        //
        // The whole selection goes together so that one right-click produces one
        // reader with one tab per document, rather than N launches that the
        // single-instance queue has to fold back into that -- which it can, and
        // does, but only because the coalescing exists.
        for (const std::wstring &line : ShellVerb::commandLinesFor(executable, paths)) {
            // CreateProcessW writes into the buffer it is given, so this is a
            // mutable copy rather than the std::wstring's own storage.
            std::vector<wchar_t> buffer(line.begin(), line.end());
            buffer.push_back(L'\0');

            STARTUPINFOW startup = {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process = {};
            if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                               CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup,
                               &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
            // A failed launch is not reported from here. This runs on a click in
            // somebody else's window, and a message box from a context menu is
            // worse than a menu entry that did nothing; the reason it failed is in
            // the event log if anyone is chasing it.
        }
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *flags) override
    {
        if (!flags)
            return E_POINTER;
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **commands) override
    {
        if (!commands)
            return E_POINTER;
        *commands = nullptr;
        return E_NOTIMPL;
    }

private:
    ~OpenCommand() { InterlockedDecrement(&liveCommands_); }

    long references_ = 1;
};

namespace {

class CommandFactory final : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (id == IID_IUnknown || id == IID_IClassFactory)
            *object = static_cast<IClassFactory *>(this);
        else {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&references_); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG left = InterlockedDecrement(&references_);
        if (left == 0)
            delete this;
        return left;
    }

    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID id, void **object) override
    {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        if (!object)
            return E_POINTER;
        auto *command = new OpenCommand();
        const HRESULT result = command->QueryInterface(id, object);
        command->Release();
        return result;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        InterlockedExchangeAdd(&serverLocks_, lock ? 1 : -1);
        return S_OK;
    }

private:
    long references_ = 1;
};

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID id, REFIID interfaceId,
                                                    void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (id != kOpenCommandClsid)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new CommandFactory();
    const HRESULT result = factory->QueryInterface(interfaceId, object);
    factory->Release();
    return result;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    const bool idle = InterlockedCompareExchange(&liveCommands_, 0, 0) == 0
                   && InterlockedCompareExchange(&serverLocks_, 0, 0) == 0;
    return idle ? S_OK : S_FALSE;
}
