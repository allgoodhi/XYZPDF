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
// ---- the three things that make it worth having ----
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
// Third: it can say what the pane shows while a page is still being rendered, which
// is the property the worker thread exists to buy. The handler will park that
// thread on request -- XYZPDF_PREVIEW_TEST_LATCH, exported for this one purpose --
// so "the paint does not wait for PDFium" is a checked fact here rather than an
// impression got by scrolling a real Explorer pane and hoping to catch it.
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

// 1 parks the render thread before it touches PDFium, 2 lets it go, 3 asks whether
// one is parked. Defined in previewhandler.cpp, and exported for no other reason.
using LatchProc = BOOL(STDAPICALLTYPE *)(int);

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

// How much ink is below the caption strip, counting pixels rather than asking
// whether the pane "looks loaded".
//
// Counting variation is not enough any more: rendering happens on the handler's own
// thread now, so a pane that has drawn nothing but paper still varies -- the
// caption's text and the grey page numbers are not one flat colour. Ink is
// separable from that. The caption is 24 pixels tall and drawn in whatever colour
// the host asked for, which this test does set to near-black, so the count starts
// below it; a page that is still coming is white with grey numerals, and grey is
// lighter than the threshold.
int inkBelowCaption(HWND child, int width, int height)
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
    int ink = 0;
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(dc, bitmap);
        // Filled with paper, not with a sentinel darker than everything: an
        // unhandled message has to read as "nothing was drawn", not as a page
        // covered in ink.
        RECT all{0, 0, width, height};
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &all, white);
        DeleteObject(white);

        SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), 0);

        auto *pixels = static_cast<unsigned char *>(bits);
        for (int y = 30; y < height; ++y) {
            const unsigned long *row = reinterpret_cast<unsigned long *>(
                static_cast<unsigned char *>(bits) + std::size_t(y) * width * 4);
            for (int x = 0; x < width; ++x) {
                const unsigned long colour = row[x];
                // BGRA in memory, so the bytes are the channels in reverse.
                if ((colour & 0xFFu) < 0x50 && ((colour >> 8) & 0xFFu) < 0x50
                    && ((colour >> 16) & 0xFFu) < 0x50)
                    ++ink;
            }
        }
        SelectObject(dc, previous);
    }
    if (bitmap)
        DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ink;
}

void pumpMessages()
{
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// Waits for a render that is on another thread now, so the answer is a bounded wait
// on a condition rather than a pause and a hope: this pumps until the pane shows ink
// and reports whether it got there. The handler parks on request (see the latch
// below), which is what makes the other half -- "it does not show ink yet" -- a
// fact rather than a race.
bool waitForInk(HWND child, int width, int height, DWORD milliseconds)
{
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    for (;;) {
        pumpMessages();
        if (inkBelowCaption(child, width, height) > 0)
            return true;
        if (GetTickCount64() >= deadline)
            return false;
        Sleep(10);
    }
}

// Catching the thread at the latch is a wait on a condition, not a guess about
// timing; the polling is only for however long starting a thread takes.
bool waitUntilParked(LatchProc latch)
{
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (latch && latch(3))
            return true;
        Sleep(10);
    }
    return false;
}

// The same catch for when something has to be dispatched first -- a timer, which is
// how a resize finds out it is over. The wait is still on the park, not on a clock.
bool pumpUntilParked(LatchProc latch)
{
    for (int attempt = 0; attempt < 600; ++attempt) {
        pumpMessages();
        if (latch && latch(3))
            return true;
        Sleep(10);
    }
    return false;
}

// Until the picture stops moving. Used where a change has to be attributable to one
// thing: the condition is a whole second of nothing arriving.
bool waitForStable(HWND child, int width, int height, DWORD milliseconds)
{
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    unsigned long last = colourAt(child, width, height);
    ULONGLONG steady = GetTickCount64();
    for (;;) {
        pumpMessages();
        const unsigned long now = colourAt(child, width, height);
        const ULONGLONG tick = GetTickCount64();
        if (now != last) {
            last = now;
            steady = tick;
        } else if (tick - steady >= 1000) {
            return true;
        }
        if (tick >= deadline)
            return false;
        Sleep(10);
    }
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

// Another fixture in the same folder as the one this run was given. Where the
// fixtures are is a fact about the invocation (the default is a relative path, and
// the gate runs from the project directory), so a second document is found by
// walking beside the first rather than by naming the folder again.
std::wstring siblingFixture(const std::wstring &fixture, const wchar_t *name)
{
    const size_t cut = fixture.find_last_of(L"\\/");
    if (cut == std::wstring::npos)
        return name;
    return fixture.substr(0, cut + 1) + name;
}

// How many copies the preview pane has staged for itself in the temp folder. The names carry a uuid,
// so a leak cannot be looked for by name -- which is why this counts the pattern instead, before and
// after one preview, and the assertion is about the difference.
int countStagedCopies()
{
    wchar_t dir[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, dir);
    if (length == 0 || length > MAX_PATH)
        return -1;
    const std::wstring pattern = std::wstring(dir) + L"xyzpdf-preview-*.pdf";
    WIN32_FIND_DATAW found = {};
    const HANDLE scan = FindFirstFileW(pattern.c_str(), &found);
    if (scan == INVALID_HANDLE_VALUE)
        return 0;
    int seen = 1;
    while (FindNextFileW(scan, &found))
        ++seen;
    FindClose(scan);
    return seen;
}

// Called once before anything is previewed, so the "left nothing behind" assertion below measures
// this run rather than whatever an earlier crashed one stranded. The pattern belongs to the handler,
// which is why a test may clear it.
void removeStagedCopies()
{
    wchar_t dir[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, dir);
    if (length == 0 || length > MAX_PATH)
        return;
    const std::wstring directory = std::wstring(dir) + L"xyzpdf-preview-*.pdf";
    WIN32_FIND_DATAW found = {};
    const HANDLE scan = FindFirstFileW(directory.c_str(), &found);
    if (scan == INVALID_HANDLE_VALUE)
        return;
    // The directory prefix, not `found.cFileName`: what that returns is a bare name, and a bare
    // name resolves against the working directory -- so the first version of this "clear the slate"
    // call deleted nothing, and the leftover from a previous run made the next run's
    // "the pane staged a copy" check true with the staging switched off. A control caught it.
    const std::wstring parent = std::wstring(dir);
    DeleteFileW((parent + found.cFileName).c_str());
    while (FindNextFileW(scan, &found))
        DeleteFileW((parent + found.cFileName).c_str());
    FindClose(scan);
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

    // Before anything is previewed, so the "leaves nothing behind" assertion at the end measures this
    // run rather than whatever an earlier crashed one stranded.
    removeStagedCopies();

    // The whole point of this line: after it, the only directory the loader will
    // consult for a bare module name is System32, which is where DllHost.exe is.
    // Anything the handler finds otherwise, it finds the way it will in the real
    // host -- or not at all, which is the failure this test exists to catch.
    // No LOAD_WITH_ALTERED_SEARCH_PATH here: that flag would quietly add the
    // handler's own directory to the search path for its imports, so a delay-load
    // entry that went missing would be found anyway, by the wrong mechanism. The
    // DLL has no static non-system imports (dumpbin -dependents lists them all
    // under delay load), so a plain load is both the honest model and the strict one.
    SetEnvironmentVariableW(L"PATH", L"C:\\Windows\\System32");

    HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, 0);
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
    auto latch = reinterpret_cast<LatchProc>(
        GetProcAddress(module, "XYZPDF_PREVIEW_TEST_LATCH"));
    check(latch != nullptr,
          "and exports the render latch, without which the assertions below about a "
          "page that has not arrived yet cannot be made at all");
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

        // Park the render thread before it draws anything, so that the next few
        // assertions are about a pane whose pages have definitely not arrived
        // rather than about whichever order this machine happened to run in.
        if (latch)
            latch(1);
        check(SUCCEEDED(handler->DoPreview()), "DoPreview succeeds");
        pumpMessages();

        if (child) {
            const bool parked = waitUntilParked(latch);
            check(parked,
                  "the render thread can be caught before it touches PDFium");
            // What the narrow pane asked the worker to draw at, kept for the resize
            // below so that its assertion compares two widths instead of guessing
            // what formula produced one.
            const int narrowJob = latch ? latch(4) : 0;
            check(narrowJob > 0, "and the latch can be asked what size was asked for");

            // The point of the whole arrangement, and the positive control for it:
            // with a render held back, the paint still answers, and what it answers
            // with is paper. A pane that drew ink here would be a pane that had
            // rendered inside the paint after all.
            check(inkBelowCaption(child, 420, 560) == 0,
                  "while the render is held the pane shows paper and no ink");
            const unsigned long paper = colourAt(child, 420, 560);
            check(colourAt(child, 420, 560) == paper,
                  "and painting it again shows the same paper, because nothing "
                  "arrives on its own");

            // The scroll bar is the state a person can actually use, so it is the
            // state worth asserting: a nine-page document whose range is one
            // viewport tall is a pane that cannot be turned, however well it draws.
            // Asked here, while nothing is rendered, it is also the assertion that
            // the layout does not depend on the renderer -- the page sizes were
            // taken once at open() rather than per paint.
            SCROLLINFO info = {};
            info.cbSize = sizeof(info);
            info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            check(GetScrollInfo(child, SB_VERT, &info) != 0 && info.fMask != 0,
                  "the pane publishes a scroll range");
            check(int(info.nMax) > int(info.nPage),
                  "and it is longer than one viewport, which is what makes it scrollable");

            if (latch)
                latch(2);
            check(waitForInk(child, 420, 560, 20000),
                  "let go, the page arrives by itself and the pane shows ink without "
                  "being asked to draw again");
            check(latch && !latch(3), "and the thread is not parked any more");
            check(colourAt(child, 420, 560) != paper,
                  "what it shows now is not the paper it showed while held");
            check(drewSomething(child, 420, 560),
                  "a real document produced a page with ink on it, not a flat rectangle");

            // Which engine this pane actually loaded. F7-D moved the handler onto
            // PDFium and F7-E left only one engine in the build, so the second
            // line is the one that would catch a wiring that came back: a pane
            // that pulled in Qt6Pdf.dll as well is two engines in the package,
            // which is the state the whole of F7 exists to end.
            check(GetModuleHandleW(L"pdfium.dll") != nullptr,
                  "the pane's engine is pdfium.dll");
            check(GetModuleHandleW(L"Qt6Pdf.dll") == nullptr,
                  "and Qt's PDF engine is not loaded in this process");

            const unsigned long before = colourAt(child, 420, 560);
            SendMessageW(child, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
            pumpMessages();
            SCROLLINFO moved = {};
            moved.cbSize = sizeof(moved);
            moved.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            GetScrollInfo(child, SB_VERT, &moved);
            check(moved.nPos > info.nPos, "a page down moves the position");
            check(colourAt(child, 420, 560) != before,
                  "and what the pane shows afterwards is different from before");

            // Dragging the pane's width. What a resize shows is the pages drawn for
            // the width it came from, stretched -- never fresh paper, and never a
            // queue of renders at every transient width the drag passes through.
            // Held at the latch nothing can arrive after the resize, so the ink below
            // is the fix or its absence with no timing in it; and a pane that only
            // ever stretched would fail the last two, which is what keeps this from
            // being a licence to stop drawing.
            if (latch) {
                // Drained first, because the page down above has the worker busy: an
                // armed latch would then catch a job that has nothing to do with the
                // resize, and the width read below would be somebody else's.
                latch(2);
                check(waitForStable(child, 420, 560, 20000),
                      "the queue empties, so what follows is the resize's doing");
                latch(1);

                constexpr int wide = 700;
                const RECT wider{0, 0, wide, 560};
                check(SUCCEEDED(handler->SetRect(&wider)), "the pane can be widened");
                pumpMessages();
                check(inkBelowCaption(child, wide, 560) > 0,
                      "and a wider pane still shows the page it had, not paper");

                SCROLLINFO grown = {};
                grown.cbSize = sizeof(grown);
                grown.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                GetScrollInfo(child, SB_VERT, &grown);
                check(int(grown.nMax) > int(moved.nMax),
                      "the pages were laid out again for the new width");

                const bool sharpened = pumpUntilParked(latch);
                check(sharpened,
                      "once the width holds still the pages are asked for again");
                check(sharpened && latch(4) > narrowJob,
                      "and at the resolution the new width wants, not the old one");
                latch(2);
                // Back to the size every assertion below this one reads the pane at.
                handler->SetRect(&area);
                pumpMessages();

                // The other half of a resize, and the half the first version of this
                // fix got wrong: a page the new geometry has just pulled into view has
                // no picture to stretch, so waiting for the width to settle buys it
                // nothing and only lengthens the blank that is already on screen.
                // Nothing is dispatched while waiting for the park, so a job that
                // appears there is one the resize message itself handed over -- which
                // is the difference between asking during the drag and asking after it.
                //
                // This needs a document with more pages than the cache holds. The two
                // page fixture cannot show it at all: whatever its geometry, every page
                // of it is on the way after the first render, so "nothing drawn yet"
                // never happens there -- which is exactly the case that was deferred by
                // mistake, and why it took a person dragging the pane to find it.
                const std::wstring longDoc = siblingFixture(fixture, L"many-pages.pdf");
                const bool haveLong =
                    GetFileAttributesW(longDoc.c_str()) != INVALID_FILE_ATTRIBUTES;
                check(haveLong, "the forty-page fixture is beside the two-page one, "
                                "because this part needs more pages than the cache holds");
                if (initialise && haveLong) {
                    initialise->Initialize(longDoc.c_str(), STGM_READ);
                    check(SUCCEEDED(handler->DoPreview()),
                          "a longer document previews at the narrow width");
                    check(waitForStable(child, 420, 560, 20000),
                          "and what it wants at that width is drawn before the resize");

                    latch(1);
                    const RECT tall{0, 0, 200, 1600};
                    check(SUCCEEDED(handler->SetRect(&tall)),
                          "the pane can be made narrower and much taller");
                    const bool askedAtOnce = waitUntilParked(latch);
                    check(askedAtOnce,
                          "a page with nothing drawn is asked for during the drag, "
                          "without waiting for the width to settle");
                    check(askedAtOnce && latch(4) < narrowJob,
                          "at the size the new width wants, not the one it came from");
                    latch(2);
                    handler->SetRect(&area);
                    pumpMessages();
                }
            }
        }
    }

    // ---- the pane must not be why the file cannot be deleted ----
    //
    // What a reader does after single-clicking a PDF in Explorer is look at the pane, then delete the
    // file -- and until §66 that second step answered "in use", because PDFium keeps the handle it
    // opened for the life of the document and does not share it for delete. So: hand the pane a copy
    // of the fixture, delete that copy while the pane is showing it, and ask whether the page is still
    // on screen afterwards. A pane that went blank when its file vanished would pass a delete-only
    // check, which is why the drawing is read on both sides of it.
    //
    // Nothing is unloaded here: a COM handler is not reusable after Unload, and the sections below
    // this one still have to run against it. That is also why the "left nothing behind" assertion sits
    // at the final Unload rather than here.
    if (child && initialise) {
        wchar_t tempDir[MAX_PATH] = {};
        const DWORD tempLength = GetTempPathW(MAX_PATH, tempDir);
        // Deliberately NOT named like the pane's own copies: the assertion below counts
        // `xyzpdf-preview-*` to prove the pane staged one, and a test file matching the same
        // pattern makes that count true whatever the handler does. The control found that -- with
        // staging switched off the count was still >=1, because this file was sitting in it.
        const std::wstring copy =
            tempLength > 0 ? std::wstring(tempDir) + L"xyzpdf-pane-lock-target.pdf" : std::wstring();
        const bool copied = !copy.empty() && CopyFileW(fixture.c_str(), copy.c_str(), FALSE);
        check(copied, "a copy of the fixture is in the temp folder for this part");
        if (copied) {
            check(SUCCEEDED(initialise->Initialize(copy.c_str(), STGM_READ)),
                  "the pane is given that copy");
            check(SUCCEEDED(handler->DoPreview()), "and it previews it");
            check(waitForStable(child, 420, 560, 20000),
                  "the page is drawn before anything is deleted");
            check(DeleteFileW(copy.c_str()),
                  "the file can be deleted while the pane is still showing it");
            check(drewSomething(child, 420, 560),
                  "and the pane still shows its page afterwards");
            // Counted with the pane's own naming pattern only, so what is in this number is the copy
            // it made for itself. This is the half that carries the fix: "the delete worked" can also
            // mean "the pane never opened the file at all", and a pane that reads the original would
            // still pass the end-of-suite "nothing left behind" check.
            check(countStagedCopies() >= 1,
                  "and the pane is reading a copy, not the file it was named");
            // Best effort, and only for the run where the check above just failed: that file *is* the
            // lock being asserted, and leaving it in the temp folder would make the next run's
            // failure its own rather than the code's.
            DeleteFileW(copy.c_str());
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

    // The same door from the other side: Unload has to join a thread that is parked
    // in the middle of a render. A join that waited for the page instead would hang
    // DllHost rather than fail a test, which is the failure this is here to catch --
    // and the reason the park is a sleep loop the handler can wake on its own.
    if (child && latch) {
        latch(1);
        if (initialise)
            initialise->Initialize(fixture.c_str(), STGM_READ);
        check(SUCCEEDED(handler->DoPreview()),
              "previewing a real file after a broken one works again");
        check(waitUntilParked(latch), "and the worker can be caught held a second time");
        check(SUCCEEDED(handler->Unload()),
              "while it is held, Unload still returns: it stops the thread rather "
              "than waiting for the page");
        latch(2);
    }

    check(SUCCEEDED(handler->Unload()), "Unload succeeds");
    // The copy the pane made for itself leaves with it. Counted rather than looked for by name,
    // because the name carries a uuid -- and a leftover per file a reader glanced at is a leak that
    // nobody would notice until the temp folder was full of PDFs.
    check(countStagedCopies() == 0, "and it leaves no copy of any document behind");

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
