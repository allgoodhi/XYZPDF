// Drives the preview handler the way Explorer does, without Explorer.
//
// ---- what this can and cannot say ----
//
// It can say the DLL loads in a process that looks like the one it will really
// be loaded into, that it answers the interfaces the shell asks for, that the
// sequence the shell performs succeeds in order, and that something was actually
// drawn. It cannot say the shell routes .pdf previews to it -- that was measured
// by hand and is recorded in docs/store-submission.md section 5.
//
// ---- the two things that make it worth having ----
//
// First: PATH is restricted to System32 before the DLL is loaded. The handler's
// own libraries sit beside it, not beside the executable, and the only reason
// that works is the delay-load hook. A test runner that puts the build directory
// on PATH -- which this one's does, for Qt's own sake -- would let the DLL load
// in a way it never can inside DllHost. XYZip learned that from a smoke test that
// passed over a handler which could not work at all, and the comment at the top
// of previewdependencies.cpp is the record of it.
//
// Second: there is no QCoreApplication here, and none in the handler either. The
// real host is DllHost.exe, which is not going to make one for us, so a test that
// constructed one would be testing a condition that does not occur. Whether Qt PDF
// renders without an application object is therefore answered here rather than in
// Explorer at 11pm.
//
// The DLL is passed in as an argument rather than found relative to this test,
// because where it was built is a fact about the build directory. tools/
// build_all_tests.py knows to pass it, and says so loudly when the file is not
// there -- a check that silently skips looks exactly like a check that passed.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>
#include <propsys.h>
#include <shobjidl_core.h>

#include <string>

namespace {

// The class the package manifest declares.
// {12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7}
const CLSID kPreviewHandlerClsid = {
    0x12df01c2, 0x01d6, 0x4af8, {0xad, 0x5f, 0x96, 0x9c, 0x3c, 0x3f, 0x88, 0xd7}};

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

HWND makeHostWindow()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"xyzpdfPreviewSmokeHost";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    // On screen but behind everything else is not enough: a window that is not
    // visible still receives WM_PRINTCLIENT, which is how it is read below, and
    // keeping it off-screen means a test run does not put a grey box on somebody's
    // desktop.
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
                           -32000, -32000, 420, 560, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

// A fingerprint of what the pane is showing: one number for the whole client area,
// so that "it changed" is a comparison rather than a screenshot to eyeball.
unsigned long colourAt(HWND child, int width, int height)
{
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    unsigned long total = 0;
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(dc, bitmap);
        SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), 0);
        auto *pixels = static_cast<unsigned char *>(bits);
        const std::size_t count = std::size_t(width) * std::size_t(height);
        for (std::size_t index = 0; index < count; index += 61)
            total += *reinterpret_cast<unsigned long *>(pixels + index * 4);
        SelectObject(dc, previous);
    }
    if (bitmap)
        DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return total;
}

// What the handler drew, as pixels. WM_PRINTCLIENT is the message a window is
// sent when something wants a copy of it without it being on screen.
bool drewSomething(HWND child, int width, int height)
{
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap)
            DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return false;
    }
    HGDIOBJ previous = SelectObject(dc, bitmap);
    // Start from a colour nothing here would ever paint, so that "the message was
    // not handled" cannot be mistaken for "it drew magenta".
    RECT all{0, 0, width, height};
    HBRUSH sentinel = CreateSolidBrush(RGB(1, 2, 3));
    FillRect(dc, &all, sentinel);
    DeleteObject(sentinel);

    SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), 0);

    auto *pixels = static_cast<unsigned char *>(bits);
    const std::size_t total = std::size_t(width) * std::size_t(height);
    bool varied = false;
    unsigned long first = 0;
    bool haveFirst = false;
    for (std::size_t index = 0; index < total; index += 37) {
        const unsigned long colour = *reinterpret_cast<unsigned long *>(pixels + index * 4);
        if (!haveFirst) {
            first = colour;
            haveFirst = true;
        } else if (colour != first) {
            varied = true;
            break;
        }
    }

    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return varied;
}

void writeJunkDocument(const std::wstring &path)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const char junk[] = "this is not a PDF, whatever the extension says\n";
    DWORD wrote = 0;
    WriteFile(file, junk, sizeof(junk) - 1, &wrote, nullptr);
    CloseHandle(file);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: xyzpdf_previewhandler_smoke <path to xyzpdfpreview.dll> [fixture.pdf]\n");
        return 2;
    }
    const std::wstring dllPath = fromAnsi(argv[1]);
    const std::wstring fixture = argc > 2 ? fromAnsi(argv[2])
                                          : std::wstring(L"tests\\fixtures\\text.pdf");

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        printf("FAIL: CoInitializeEx failed, so nothing below this can mean anything\n");
        return 1;
    }

    // The whole point of this line: after it, the only directory the loader will
    // consult for a bare module name is System32, which is where DllHost.exe is.
    // Anything the handler finds otherwise, it finds the way it will in the real
    // host -- or not at all, which is the failure this test exists to catch.
    SetEnvironmentVariableW(L"PATH", L"C:\\Windows\\System32");

    HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(module != nullptr, "the handler DLL loads with PATH restricted to System32");
    if (!module) {
        printf("  LoadLibrary of %S failed with 0x%08lX\n", dllPath.c_str(), GetLastError());
        CoUninitialize();
        return 1;
    }

    using GetClassObjectProc = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **);
    auto getClassObject = reinterpret_cast<GetClassObjectProc>(
        GetProcAddress(module, "DllGetClassObject"));
    check(getClassObject != nullptr, "the DLL exports DllGetClassObject, as the manifest claims");
    check(GetProcAddress(module, "DllCanUnloadNow") != nullptr,
          "and exports DllCanUnloadNow too");
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
    check(SUCCEEDED(getClassObject(kPreviewHandlerClsid, IID_IClassFactory,
                                  reinterpret_cast<void **>(&factory))),
          "the factory is handed out for the declared CLSID");
    if (!factory) {
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }

    IPreviewHandler *handler = nullptr;
    check(SUCCEEDED(factory->CreateInstance(nullptr, __uuidof(IPreviewHandler),
                                            reinterpret_cast<void **>(&handler))),
          "an IPreviewHandler can be created out of the factory");
    if (!handler) {
        factory->Release();
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }

    IInitializeWithFile *initialise = nullptr;
    check(SUCCEEDED(handler->QueryInterface(__uuidof(IInitializeWithFile),
                                            reinterpret_cast<void **>(&initialise))),
          "it answers IInitializeWithFile, which is how the file is handed over");
    IPreviewHandlerVisuals *visuals = nullptr;
    check(SUCCEEDED(handler->QueryInterface(__uuidof(IPreviewHandlerVisuals),
                                            reinterpret_cast<void **>(&visuals))),
          "it answers IPreviewHandlerVisuals, which is how the theme is handed over");
    IObjectWithSite *site = nullptr;
    check(SUCCEEDED(handler->QueryInterface(IID_IObjectWithSite,
                                            reinterpret_cast<void **>(&site))),
          "and IObjectWithSite, which is how the host introduces itself");

    // A real document, opened the way the shell opens one: initialise, hand over
    // a window, size it, ask for the preview.
    if (initialise)
        check(SUCCEEDED(initialise->Initialize(fixture.c_str(), STGM_READ)),
              "it takes the path of the document to preview");
    if (visuals) {
        visuals->SetBackgroundColor(RGB(240, 240, 240));
        visuals->SetTextColor(RGB(20, 20, 20));
        LOGFONTW font = {};
        lstrcpynW(font.lfFaceName, L"Segoe UI", LF_FACESIZE);
        check(SUCCEEDED(visuals->SetFont(&font)), "and it accepts a font");
    }
    if (site)
        check(SUCCEEDED(site->SetSite(nullptr)), "SetSite tolerates nothing being given");

    HWND host = makeHostWindow();
    check(host != nullptr, "there is a host window to give it");
    const RECT area{0, 0, 420, 560};
    HWND child = nullptr;
    if (host) {
        check(SUCCEEDED(handler->SetWindow(host, &area)), "SetWindow succeeds");
        child = GetWindow(host, GW_CHILD);
        check(child != nullptr, "and it created a window of its own inside ours");
        check(SUCCEEDED(handler->SetRect(&area)), "SetRect succeeds");
        check(SUCCEEDED(handler->DoPreview()), "DoPreview succeeds");
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (child) {
            check(drewSomething(child, 420, 560),
                  "a real document produced a page with ink on it, not a flat rectangle");

            // The scroll bar is the state a person can actually use, so it is the
            // state worth asserting: a nine-page document whose range is one
            // viewport tall is a pane that cannot be turned, however well it draws.
            SCROLLINFO info = {};
            info.cbSize = sizeof(info);
            info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            check(GetScrollInfo(child, SB_VERT, &info) != 0 && info.fMask != 0,
                  "the pane publishes a scroll range");
            check(int(info.nMax) > int(info.nPage),
                  "and it is longer than one viewport, which is what makes it scrollable");

            const unsigned long before = colourAt(child, 420, 560);
            SendMessageW(child, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
            MSG wheel = {};
            while (PeekMessageW(&wheel, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&wheel);
                DispatchMessageW(&wheel);
            }
            SCROLLINFO moved = {};
            moved.cbSize = sizeof(moved);
            moved.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            GetScrollInfo(child, SB_VERT, &moved);
            check(moved.nPos > info.nPos, "a page down moves the position");
            check(colourAt(child, 420, 560) != before,
                  "and what the pane shows afterwards is different from before");
        }
    }

    // And the other half: a file that is not a PDF has to say so in the pane
    // rather than leave it blank, which is what a reader reads as "still loading".
    if (child) {
        const std::wstring junk = fixture + L".not-a-pdf.tmp";
        writeJunkDocument(junk);
        if (initialise)
            initialise->Initialize(junk.c_str(), STGM_READ);
        check(SUCCEEDED(handler->DoPreview()),
              "a document that is not a PDF still previews without failing the call");
        check(drewSomething(child, 420, 560),
              "and it draws the sentence that says what went wrong");
        DeleteFileW(junk.c_str());
    }

    check(SUCCEEDED(handler->Unload()), "Unload succeeds");

    // Looked up rather than called by name: the test links nothing, which is the
    // point -- the DLL is loaded the way a host loads it.
    using CanUnloadProc = HRESULT(*)();
    auto canUnload = reinterpret_cast<CanUnloadProc>(GetProcAddress(module, "DllCanUnloadNow"));
    check(canUnload != nullptr, "and DllCanUnloadNow is reachable");
    // Asked while this test still holds the handler. A window was handed into our
    // message loop, so "you may unload" here would be a licence to yank it away.
    check(canUnload && canUnload() == S_FALSE,
          "it refuses to unload while a handler is still alive");

    handler->Release();
    if (initialise)
        initialise->Release();
    if (visuals)
        visuals->Release();
    if (site)
        site->Release();
    factory->Release();
    if (host)
        DestroyWindow(host);
    // Everything the DLL had is back: the handler, its window, and the factory.
    // This is the answer a host acts on, and getting it wrong the other way keeps
    // the file mapped for the length of the session.
    check(canUnload && canUnload() == S_OK,
          "and once nothing is alive any more, it says the DLL may unload");
    FreeLibrary(module);
    CoUninitialize();

    if (failures == 0) {
        printf("the preview handler loads without a Qt application object, answers the shell's "
               "sequence, and paints a page\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
