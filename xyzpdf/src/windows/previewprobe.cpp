// The smallest thing that can answer one question: does a COM server declared in
// a package's manifest get loaded at all when that package is registered loosely
// from a directory, rather than installed from a signed .msix?
//
// That question decides whether the preview pane is buildable now or has to wait
// behind a code-signing certificate, so this DLL deliberately contains nothing
// else. No Qt, no PDFium, no common controls: a window of its own class, a
// background colour, and a list of facts about the process it ended up inside. If
// it paints, the plumbing works and the real handler has somewhere to land. If it
// does not, 1800 lines of rendering code would have been written into a void.
//
// The facts are the point as much as the picture. A preview handler runs inside
// somebody else's process -- Explorer itself, or dllhost -- at an integrity level
// and with a file-system view that this program's own process does not have, and
// every one of those differences is invisible from the outside. So the pane says
// where it is, and the same line is appended to a log file: whether the pane
// painted is something only a person can judge, but whether the handler was ever
// instantiated can be read off a file afterwards.
//
// The measurement helpers are ported from XYZip's src/shell/previewprobe.cpp,
// comments included: each of those comments is a measurement that cost an hour.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <appmodel.h>
#include <objbase.h>
#include <propsys.h>
#include <shobjidl_core.h>
#include <shlobj.h>

#include <string>
#include <vector>

// The class compiled into the package manifest. The two have to agree, and the
// manifest is the only place anyone would think to look.
// {12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7}
const CLSID kPreviewProbeClsid = {
    0x12df01c2, 0x01d6, 0x4af8, {0xad, 0x5f, 0x96, 0x9c, 0x3c, 0x3f, 0x88, 0xd7}};

namespace {

// ---------------------------------------------------------------------------
// The facts
// ---------------------------------------------------------------------------

std::wstring hexadecimal(unsigned long value)
{
    wchar_t buffer[16] = {};
    wsprintfW(buffer, L"0x%08X", value);
    return buffer;
}

// The tail of a path, because the pane is narrow and the interesting part of
// C:\Windows\System32\prevhost.exe is the last word.
std::wstring fileNameOf(const std::wstring &path)
{
    const std::size_t cut = path.find_last_of(L'\\');
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

std::wstring describeLastError(unsigned long error)
{
    if (error == ERROR_SUCCESS)
        return L"0x00000000";

    LPWSTR text = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
                                            | FORMAT_MESSAGE_FROM_SYSTEM
                                            | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, error, 0,
                                        reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = hexadecimal(error);
    if (length && text) {
        std::wstring message(text, length);
        // FormatMessage ends its sentences with a newline, which would put a
        // blank line in the middle of the pane.
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'
                                    || message.back() == L' '))
            message.pop_back();
        if (!message.empty())
            result += L" (" + message + L")";
    }
    if (text)
        LocalFree(text);
    return result;
}

std::wstring hostProcess()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0)
        return L"unknown: " + describeLastError(GetLastError());
    return fileNameOf(std::wstring(buffer, length));
}

std::wstring architecture()
{
    return sizeof(void *) == 8 ? L"64-bit" : L"32-bit";
}

// Whether this process came from a package at all. It is the whole question the
// probe exists to ask, so it is read from the operating system rather than
// inferred from the directory the DLL happened to be loaded from.
std::wstring packageIdentity()
{
    UINT32 length = 0;
    const LONG probe = GetCurrentPackageFullName(&length, nullptr);
    if (probe == APPMODEL_ERROR_NO_PACKAGE)
        return L"none: this process has no package identity";
    if (probe != ERROR_INSUFFICIENT_BUFFER)
        return L"unreadable: " + describeLastError(static_cast<unsigned long>(probe));

    std::vector<wchar_t> name(length + 1, L'\0');
    const LONG result = GetCurrentPackageFullName(&length, name.data());
    if (result != ERROR_SUCCESS)
        return L"unreadable: " + describeLastError(static_cast<unsigned long>(result));
    return name.data();
}

std::wstring integrityLevel()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return L"unreadable: " + describeLastError(GetLastError());

    std::wstring result;
    DWORD needed = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &needed);
    if (needed == 0) {
        result = L"unreadable: " + describeLastError(GetLastError());
    } else {
        std::vector<unsigned char> buffer(needed, 0);
        if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), needed, &needed)) {
            result = L"unreadable: " + describeLastError(GetLastError());
        } else {
            auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer.data());
            const UCHAR *count = GetSidSubAuthorityCount(label->Label.Sid);
            const DWORD rid = count && *count ? *GetSidSubAuthority(label->Label.Sid, *count - 1)
                                              : 0;
            if (rid >= SECURITY_MANDATORY_SYSTEM_RID)
                result = L"System";
            else if (rid >= SECURITY_MANDATORY_HIGH_RID)
                result = L"High";
            else if (rid >= SECURITY_MANDATORY_MEDIUM_RID)
                result = L"Medium";
            else if (rid >= SECURITY_MANDATORY_LOW_RID)
                result = L"Low";
            else
                result = L"Untrusted";
            result += L" (" + hexadecimal(rid) + L")";
        }
    }

    DWORD container = 0;
    DWORD size = sizeof(container);
    if (GetTokenInformation(token, TokenIsAppContainer, &container, size, &size) && container)
        result += L", AppContainer";

    CloseHandle(token);
    return result;
}

struct Fact {
    std::wstring label;
    std::wstring value;
};

std::vector<Fact> facts(const std::wstring &previewedFile)
{
    std::vector<Fact> gathered;
    gathered.push_back({L"file", previewedFile.empty() ? L"(none yet)" : previewedFile});
    gathered.push_back({L"host", hostProcess() + L", " + architecture()});
    gathered.push_back({L"package", packageIdentity()});
    gathered.push_back({L"integrity", integrityLevel()});
    return gathered;
}

// Where the activation is recorded. LocalAppData rather than a package data
// folder, because a loosely registered package has no such folder of its own.
std::wstring logPath()
{
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return {};
    return std::wstring(base) + L"\\xyzpdf-preview-probe.log";
}

// Returns what it managed to do, because the pane shows that too: being allowed
// to write anywhere at all is one of the things a handler in somebody else's
// process has to find out.
std::wstring noteActivation(const std::wstring &previewedFile)
{
    const std::wstring path = logPath();
    if (path.empty())
        return L"log path unavailable";

    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return L"log refused: " + describeLastError(GetLastError());

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    const std::wstring line = std::wstring(L"host=") + hostProcess()
                            + L", " + architecture()
                            + L"  package=" + packageIdentity()
                            + L"  file=" + (previewedFile.empty() ? L"(none)" : previewedFile)
                            + L"  at " + std::to_wstring(now.wYear) + L"-"
                            + std::to_wstring(now.wMonth) + L"-" + std::to_wstring(now.wDay)
                            + L" " + std::to_wstring(now.wHour) + L":"
                            + std::to_wstring(now.wMinute) + L":"
                            + std::to_wstring(now.wSecond) + L"\r\n";

    DWORD wrote = 0;
    const BOOL written = WriteFile(file, line.c_str(),
                                   static_cast<DWORD>(line.size() * sizeof(wchar_t)),
                                   &wrote, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(file);

    if (!written || wrote == 0)
        return L"log write failed: " + describeLastError(error);
    return L"logged";
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

const wchar_t kWindowClass[] = L"xyzpdfPreviewProbeWindow";

std::wstring g_file;        // what the shell asked to be previewed
std::wstring g_activation;  // what writing the log said
COLORREF g_background = RGB(38, 28, 64);
COLORREF g_text = RGB(240, 240, 240);

LRESULT CALLBACK probeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT area = {};
        GetClientRect(window, &area);
        HBRUSH brush = CreateSolidBrush(g_background);
        FillRect(dc, &area, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        RECT area = {};
        GetClientRect(window, &area);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_text);

        int top = area.top + 12;
        RECT title{area.left + 12, top, area.right - 12, top + 22};
        DrawTextW(dc, L"XYZPDF preview probe", -1, &title, DT_LEFT | DT_TOP | DT_SINGLELINE);
        top += 30;

        for (const Fact &fact : facts(g_file)) {
            const std::wstring line = fact.label + L": " + fact.value;
            RECT row{area.left + 12, top, area.right - 12, top + 54};
            DrawTextW(dc, line.c_str(), -1, &row, DT_LEFT | DT_TOP | DT_WORDBREAK);
            top += 58;
        }
        if (!g_activation.empty()) {
            RECT row{area.left + 12, top, area.right - 12, top + 36};
            DrawTextW(dc, g_activation.c_str(), -1, &row, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }

        EndPaint(window, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerWindowClass()
{
    static bool registered = false;
    if (registered)
        return true;

    // Registered on first use rather than in DllMain: the loader lock is held in
    // DllMain and registering a class takes it too, which is a deadlock waiting
    // for whichever order the host happens to load in. XYZip's note about this is
    // the reason it is written this way here.
    WNDCLASSW wc = {};
    wc.lpfnWndProc = probeWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = nullptr; // WM_ERASEBKGND paints it
    wc.lpszClassName = kWindowClass;
    registered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

// ---------------------------------------------------------------------------
// The handler
//
// Bases in this order on purpose: IPreviewHandler is the first, so the implicit
// conversion to IUnknown that every one of these interfaces shares resolves to
// one vtable rather than being ambiguous between five.
class ProbeHandler : public IPreviewHandler,
                     public IPreviewHandlerVisuals,
                     public IInitializeWithFile,
                     public IObjectWithSite {
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(IPreviewHandler))
            *object = static_cast<IPreviewHandler *>(this);
        else if (id == __uuidof(IPreviewHandlerVisuals))
            *object = static_cast<IPreviewHandlerVisuals *>(this);
        else if (id == __uuidof(IInitializeWithFile))
            *object = static_cast<IInitializeWithFile *>(this);
        else if (id == IID_IObjectWithSite)
            *object = static_cast<IObjectWithSite *>(this);
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

    // IInitializeWithFile: the shell hands over the path before anything else.
    IFACEMETHODIMP Initialize(LPCWSTR path, DWORD) override
    {
        g_file = path ? path : L"(no path given)";
        return S_OK;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown *site) override
    {
        if (site)
            site->AddRef();
        if (site_)
            site_->Release();
        site_ = site;
        return S_OK;
    }

    IFACEMETHODIMP GetSite(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (!site_)
            return E_NOINTERFACE;
        return site_->QueryInterface(id, object);
    }

    // IPreviewHandler
    IFACEMETHODIMP SetWindow(HWND parent, const RECT *area) override
    {
        if (!registerWindowClass())
            return E_FAIL;
        if (!window_) {
            // The host may call this twice, and one window is enough: a second
            // one would be painted over the first and the pane would flicker.
            window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_CLIPCHILDREN,
                                      0, 0, 0, 0, parent, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
            if (!window_)
                return E_FAIL;
        }
        return SetRect(area);
    }

    IFACEMETHODIMP SetRect(const RECT *area) override
    {
        if (window_ && area)
            MoveWindow(window_, area->left, area->top,
                       area->right - area->left, area->bottom - area->top, TRUE);
        return S_OK;
    }

    IFACEMETHODIMP DoPreview() override
    {
        if (!window_)
            return E_UNEXPECTED;
        // Recorded here, synchronously, before anything is painted. An earlier
        // version deferred it to a timer "because then the host's loop is
        // running", and the timer never fired in the offscreen smoke run -- which
        // is precisely the kind of thing that reads as "the shell never called
        // us" when it is this file's own bug.
        g_activation = noteActivation(g_file);
        ShowWindow(window_, SW_SHOW);
        InvalidateRect(window_, nullptr, TRUE);
        return S_OK;
    }

    IFACEMETHODIMP Unload() override
    {
        if (window_) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        return S_OK;
    }

    IFACEMETHODIMP SetFocus() override { return S_OK; }

    IFACEMETHODIMP QueryFocus(HWND *window) override
    {
        if (!window)
            return E_POINTER;
        *window = nullptr;
        return S_OK;
    }

    IFACEMETHODIMP TranslateAccelerator(MSG *) override { return S_FALSE; }

    // IPreviewHandlerVisuals: honoured for the colours, because Explorer sets
    // them from the current theme and a pane that ignores them is the wrong
    // colour in light mode. The font is not: the probe draws with the default.
    IFACEMETHODIMP SetBackgroundColor(COLORREF colour) override
    {
        g_background = colour;
        return S_OK;
    }

    IFACEMETHODIMP SetFont(const LOGFONTW *) override { return S_OK; }

    IFACEMETHODIMP SetTextColor(COLORREF colour) override
    {
        g_text = colour;
        return S_OK;
    }

private:
    ~ProbeHandler()
    {
        Unload();
        if (site_)
            site_->Release();
    }

    long references_ = 1;
    IUnknown *site_ = nullptr;
    HWND window_ = nullptr;
};

class ProbeFactory : public IClassFactory {
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
        auto *handler = new ProbeHandler();
        const HRESULT result = handler->QueryInterface(id, object);
        handler->Release();
        return result;
    }

    IFACEMETHODIMP LockServer(BOOL) override { return S_OK; }

private:
    long references_ = 1;
};

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID id, REFIID interfaceId,
                                                   void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (id != kPreviewProbeClsid)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new ProbeFactory();
    const HRESULT result = factory->QueryInterface(interfaceId, object);
    factory->Release();
    return result;
}

// Always refuse to unload. The host decides when the DLL may go, and a preview
// handler that has handed a window to somebody else's message loop can be
// unloaded with that window still on screen, which is a crash attributed to
// nothing. XYZip's note about this race is why the answer here is a flat no.
extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    return S_FALSE;
}
