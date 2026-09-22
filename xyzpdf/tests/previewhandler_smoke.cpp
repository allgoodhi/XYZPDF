// Drives the preview probe the way Explorer would, without Explorer.
//
// The question this answers is the narrower and more useful one: is the DLL
// itself correct -- does it export a class factory, hand out an IPreviewHandler,
// accept the sequence the shell performs, and paint? The wider question, whether
// the shell routes .pdf previews to a COM server declared by a loosely
// registered package, cannot be answered here and is not pretended to be: that
// one is answered by the log file the handler writes, which is checked by hand
// after somebody opens a preview pane.
//
// Separating the two is the whole point. Without this, "the pane stayed empty"
// has three explanations -- the DLL is broken, the manifest is wrong, the shell
// does not route -- and the registry offers no way to tell them apart: a package
// that is known to work (XYZip, installed properly, in WindowsApps) has no COM
// key here either, so the absence of one proves nothing.
//
// The DLL is passed in as an argument rather than found relative to this test,
// because where it was built is a fact about the build directory, not about the
// test. tools/build_all_tests.py knows to pass it, and says so loudly when the
// file is not there -- which is the failure mode this project has been bitten by
// before: a check that silently skips looks exactly like a check that passed.

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

#include <QCoreApplication>
#include <QFileInfo>

#include <iostream>
#include <string>

namespace {

// The class the package manifest declares, and the DLL exports a factory for.
// {12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7}
const CLSID kPreviewProbeClsid = {
    0x12df01c2, 0x01d6, 0x4af8, {0xad, 0x5f, 0x96, 0x9c, 0x3c, 0x3f, 0x88, 0xd7}};

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

// A window of our own to be handed to SetWindow, off the screen and never shown:
// the probe creates a child inside whatever parent it is given, and what is being
// checked is that it manages to, not that anybody sees it.
HWND makeHostWindow()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"xyzpdfProbeSmokeHost";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
                           -32000, -32000, 400, 300, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: xyzpdf_previewhandler_smoke <path to xyzpdfpreviewprobe.dll>\n";
        return 2;
    }
    const std::string dllPath = QFileInfo(QString::fromLocal8Bit(argv[1]))
                                    .absoluteFilePath().toStdString();

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::cerr << "CoInitializeEx failed, so nothing below this can mean anything\n";
        return 1;
    }

    HMODULE module = LoadLibraryExA(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(module != nullptr, "the probe DLL loads");
    if (!module) {
        std::cerr << "  LoadLibrary of " << dllPath << " failed with " << GetLastError() << '\n';
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

    // The other two exports a shell extension is expected to have. DllMain does
    // not disable thread library calls, so the reason this one is here is the
    // same reason it is in every handler: the host asks before it unloads.
    check(GetProcAddress(module, "DllCanUnloadNow") != nullptr,
          "and exports DllCanUnloadNow too");

    IClassFactory *factory = nullptr;
    check(SUCCEEDED(getClassObject(kPreviewProbeClsid, IID_IClassFactory,
                                  reinterpret_cast<void **>(&factory))),
          "the factory is handed out for the declared CLSID");
    // Some other class, so the comparison is not with itself: a factory that
    // answers for anything is a factory that ignores what it was asked for.
    const CLSID kOtherClsid = {0xdeadbeef, 0x1234, 0x5678,
                               {0x9a, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1}};
    IClassFactory *notOurs = reinterpret_cast<IClassFactory *>(-1);
    check(getClassObject(kOtherClsid, IID_IClassFactory,
                         reinterpret_cast<void **>(&notOurs))
                  == CLASS_E_CLASSNOTAVAILABLE
              && notOurs == nullptr,
          "and refuses a CLSID that is not ours, handing back nothing");

    IPreviewHandler *handler = nullptr;
    if (factory) {
        check(SUCCEEDED(factory->CreateInstance(nullptr, __uuidof(IPreviewHandler),
                                                reinterpret_cast<void **>(&handler))),
              "an IPreviewHandler can be created out of the factory");
    }
    if (!handler) {
        if (factory)
            factory->Release();
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }

    // The sequence, in the order the shell performs it.
    IInitializeWithFile *initialise = nullptr;
    check(SUCCEEDED(handler->QueryInterface(__uuidof(IInitializeWithFile),
                                            reinterpret_cast<void **>(&initialise))),
          "it answers IInitializeWithFile, which is how the file is handed over");
    if (initialise) {
        check(SUCCEEDED(initialise->Initialize(L"C:\\xyzpdf-probe-smoke.pdf", STGM_READ)),
              "and takes the path");
    }

    IPreviewHandlerVisuals *visuals = nullptr;
    check(SUCCEEDED(handler->QueryInterface(__uuidof(IPreviewHandlerVisuals),
                                            reinterpret_cast<void **>(&visuals))),
          "it answers IPreviewHandlerVisuals, which is how the theme is handed over");
    if (visuals) {
        visuals->SetBackgroundColor(RGB(20, 20, 20));
        visuals->SetTextColor(RGB(250, 250, 250));
        LOGFONTW font = {};
        lstrcpynW(font.lfFaceName, L"Segoe UI", LF_FACESIZE);
        check(SUCCEEDED(visuals->SetFont(&font)), "and it accepts a font, honoured or not");
    }

    HWND host = makeHostWindow();
    check(host != nullptr, "there is a host window to be given to it");
    RECT area{0, 0, 380, 260};
    if (host) {
        check(SUCCEEDED(handler->SetWindow(host, &area)), "SetWindow succeeds");
        HWND child = nullptr;
        check(SUCCEEDED(handler->QueryFocus(&child)), "QueryFocus answers without complaining");
        check(SUCCEEDED(handler->SetRect(&area)), "SetRect succeeds");
        check(SUCCEEDED(handler->DoPreview()), "DoPreview succeeds");
        // The probe records its activation on a timer, because that is the first
        // moment the host's message loop is really running. Pumps here are what
        // let it happen, which is also the only way to see a hang in that path.
        for (int pass = 0; pass < 40; ++pass) {
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(20);
        }
        check(GetWindow(host, GW_CHILD) != nullptr,
              "and it actually put a window inside the host, which is the whole job");

        // The log line is the half nobody has to look at. If the shell does route
        // a preview here later, that file is the evidence -- so check the handler
        // can and does write it, rather than finding out at the same moment as
        // the routing question, when the two can no longer be told apart.
        wchar_t local[4096] = {};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local,
                                                    ARRAYSIZE(local));
        const std::wstring log = length
            ? std::wstring(local) + L"\\xyzpdf-preview-probe.log" : std::wstring();
        check(!log.empty(), "LOCALAPPDATA is where the handler says it writes");
        check(!log.empty() && GetFileAttributesW(log.c_str()) != INVALID_FILE_ATTRIBUTES,
              "and the activation was recorded there, where it can be read back");
    }

    check(SUCCEEDED(handler->Unload()), "Unload succeeds");
    handler->Release();
    if (initialise)
        initialise->Release();
    if (visuals)
        visuals->Release();
    if (factory)
        factory->Release();
    if (host)
        DestroyWindow(host);
    FreeLibrary(module);
    CoUninitialize();

    if (failures == 0) {
        std::cout << "the probe DLL answers the shell's whole sequence and paints a window\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
