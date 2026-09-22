// Drives the thumbnail provider the way Explorer does, without Explorer.
//
// ---- what this can and cannot say ----
//
// It can say the DLL loads in a process shaped like DllHost, that it answers the
// interfaces the shell asks for, and that what comes back is a bitmap of the right
// size, the right aspect, and with ink on it. It cannot say the shell routes .pdf
// thumbnails to it: that is the same routing question the preview handler's probe
// answered for the pane, recorded in docs/store-submission.md section 5, and it is
// answered here by looking at a folder in icon view.
//
// The size and aspect assertions are the reason this file exists rather than being
// a copy of the preview test. A provider that returns a square stretched page, or
// a black rectangle, or a bitmap of the requested size but drawn at a quarter of
// it, is correct by every check that costs nothing and wrong in the one way a
// person notices.
//
// ---- the PATH line ----
//
// Same as previewhandler_smoke.cpp and for the same reason: the handler's libraries
// sit beside it rather than beside the executable, and only the delay-load hook
// makes that findable. A runner that puts the build directory on PATH lets the DLL
// load in a way it never can inside DllHost.
//
// The DLL is passed as an argument by tools/build_all_tests.py, which reports a
// missing file as a failure rather than skipping the check.

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
#include <thumbcache.h>

#include <string>
#include <vector>

namespace {

// The class the package manifest declares.
// {4032B809-B50E-4F0A-AFC1-D300803A81CE}
const CLSID kThumbnailProviderClsid = {
    0x4032b809, 0xb50e, 0x4f0a, {0xaf, 0xc1, 0xd3, 0x00, 0x80, 0x3a, 0x81, 0xce}};

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

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned long> values;   // one BGRx per pixel, sampled
    double inkFraction = 0.0;            // of the pixels looked at, how many are not paper
};

// The bitmap's own bits, read back.
//
// A DIB section is created with a negative height here, which is how a top-down
// buffer is described to GDI, so bmHeight comes back negative and is what the
// row count is taken from.
bool readBack(HBITMAP bitmap, Pixels &out)
{
    BITMAP info = {};
    if (!bitmap || GetObjectW(bitmap, sizeof(info), &info) == 0 || !info.bmBits)
        return false;
    out.width = info.bmWidth;
    out.height = info.bmHeight < 0 ? -info.bmHeight : info.bmHeight;
    if (out.width <= 0 || out.height <= 0 || info.bmBitsPixel != 32)
        return false;

    const auto *pixels = static_cast<const unsigned char *>(info.bmBits);
    // Sampled rather than counted in full: the question is "is there ink", and a
    // 256x256 thumbnail is 65536 pixels of which a few hundred are text.
    out.inkFraction = 0.0;
    std::size_t seen = 0;
    std::size_t ink = 0;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            if (((x ^ y) & 3) != 0)
                continue;   // one pixel in four, spread over both axes
            const unsigned char *at = pixels + (std::size_t(y) * out.width + x) * 4;
            ++seen;
            // Anything more than a hint off white counts: a page that came back
            // all paper would pass a "not black" test and still be useless.
            if (at[0] < 240 || at[1] < 240 || at[2] < 240)
                ++ink;
        }
    }
    out.inkFraction = seen ? double(ink) / double(seen) : 0.0;
    return seen > 0;
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
        printf("usage: xyzpdf_thumbnailprovider_smoke <path to xyzpdfthumbnail.dll> [fixture.pdf]\n");
        return 2;
    }
    const std::wstring dllPath = fromAnsi(argv[1]);
    const std::wstring fixture = argc > 2 ? fromAnsi(argv[2])
                                          : std::wstring(L"tests\\fixtures\\text.pdf");

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        printf("FAIL: CoInitializeEx failed, so nothing below this can mean anything\n");
        return 1;
    }

    SetEnvironmentVariableW(L"PATH", L"C:\\Windows\\System32");

    HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(module != nullptr, "the provider DLL loads with PATH restricted to System32");
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
    check(SUCCEEDED(getClassObject(kThumbnailProviderClsid, IID_IClassFactory,
                                  reinterpret_cast<void **>(&factory))),
          "the factory is handed out for the declared CLSID");
    if (!factory) {
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }

    IThumbnailProvider *provider = nullptr;
    check(SUCCEEDED(factory->CreateInstance(nullptr, __uuidof(IThumbnailProvider),
                                            reinterpret_cast<void **>(&provider))),
          "an IThumbnailProvider can be created out of the factory");
    if (!provider) {
        factory->Release();
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }

    IInitializeWithFile *initialise = nullptr;
    check(SUCCEEDED(provider->QueryInterface(__uuidof(IInitializeWithFile),
                                             reinterpret_cast<void **>(&initialise))),
          "it answers IInitializeWithFile, which is how the file is handed over");
    IObjectWithSite *site = nullptr;
    check(SUCCEEDED(provider->QueryInterface(IID_IObjectWithSite,
                                             reinterpret_cast<void **>(&site))),
          "and IObjectWithSite, which is how the host introduces itself");

    // Before any path is given: a provider that answered this call with a bitmap
    // would be showing one document's page as another document's icon.
    {
        HBITMAP early = reinterpret_cast<HBITMAP>(-1);
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        const HRESULT before = provider->GetThumbnail(64, &early, &alpha);
        check(FAILED(before) && early == nullptr,
              "with no file given it fails and hands back no bitmap");
    }

    if (site)
        check(SUCCEEDED(site->SetSite(nullptr)), "SetSite tolerates nothing being given");
    if (initialise)
        check(SUCCEEDED(initialise->Initialize(fixture.c_str(), STGM_READ)),
              "it takes the path of the document to draw");

    // The sizes Explorer actually asks for, and the two properties every one of
    // them has to keep: it fits the box, and it is a page rather than a square.
    const UINT boxes[] = {16, 32, 64, 96, 256};
    int lastWidth = 0;
    int lastHeight = 0;
    for (const UINT box : boxes) {
        HBITMAP bitmap = reinterpret_cast<HBITMAP>(-1);
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        const HRESULT result = provider->GetThumbnail(box, &bitmap, &alpha);
        char note[128];
        snprintf(note, sizeof(note), "GetThumbnail(%u) returns a bitmap", box);
        check(SUCCEEDED(result) && bitmap != nullptr, note);
        if (FAILED(result) || !bitmap)
            continue;

        snprintf(note, sizeof(note), "GetThumbnail(%u) says the bitmap is opaque", box);
        check(alpha == WTSAT_RGB, note);

        Pixels pixels;
        snprintf(note, sizeof(note), "GetThumbnail(%u) hands back readable bits", box);
        check(readBack(bitmap, pixels), note);
        if (pixels.width > 0) {
            snprintf(note, sizeof(note),
                     "GetThumbnail(%u) fits inside the box it was given", box);
            check(pixels.width <= int(box) && pixels.height <= int(box)
                      && (pixels.width == int(box) || pixels.height == int(box)),
                  note);

            // The fixture is a portrait page, so the long edge is the height at
            // every size, and the width grows with the box. A provider that
            // returned a square, or ignored the aspect, fails here.
            snprintf(note, sizeof(note),
                     "GetThumbnail(%u) keeps a portrait page taller than it is wide", box);
            check(pixels.height > pixels.width, note);
            snprintf(note, sizeof(note), "GetThumbnail(%u) is at least as big as the last", box);
            check(pixels.width >= lastWidth && pixels.height >= lastHeight, note);
            lastWidth = pixels.width;
            lastHeight = pixels.height;

            snprintf(note, sizeof(note),
                     "GetThumbnail(%u) has ink on it, and the page is still mostly paper", box);
            check(pixels.inkFraction > 0.0 && pixels.inkFraction < 0.5, note);
        }
        DeleteObject(bitmap);
    }

    // Refusing a request is better than drawing a wrong one: at zero there is no
    // page to show, and a bitmap of an undefined size is how a tile ends up blank.
    {
        HBITMAP bitmap = reinterpret_cast<HBITMAP>(-1);
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        check(provider->GetThumbnail(0, &bitmap, &alpha) == E_INVALIDARG && bitmap == nullptr,
              "a box of zero is refused rather than drawn");
        check(provider->GetThumbnail(64, nullptr, &alpha) == E_POINTER,
              "and a missing out parameter is reported as one");
    }

    // A file that is not a PDF: no thumbnail, so that Explorer falls back to the
    // icon it already has rather than to a white square that reads as an empty page.
    {
        const std::wstring junk = fixture + L".not-a-pdf.tmp";
        writeJunkDocument(junk);
        if (initialise)
            initialise->Initialize(junk.c_str(), STGM_READ);
        HBITMAP bitmap = reinterpret_cast<HBITMAP>(-1);
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        const HRESULT result = provider->GetThumbnail(64, &bitmap, &alpha);
        check(FAILED(result) && bitmap == nullptr,
              "a document that is not a PDF returns nothing rather than a blank page");
        DeleteFileW(junk.c_str());
    }

    using CanUnloadProc = HRESULT(*)();
    auto canUnload = reinterpret_cast<CanUnloadProc>(GetProcAddress(module, "DllCanUnloadNow"));
    check(canUnload != nullptr, "and DllCanUnloadNow is reachable");
    check(canUnload && canUnload() == S_FALSE,
          "it refuses to unload while a provider is still alive");

    provider->Release();
    if (initialise)
        initialise->Release();
    if (site)
        site->Release();
    factory->Release();
    check(canUnload && canUnload() == S_OK,
          "and once nothing is alive any more, it says the DLL may unload");
    FreeLibrary(module);
    CoUninitialize();

    if (failures == 0) {
        printf("the thumbnail provider loads without a Qt application object, and returns a "
               "page-shaped, inked bitmap at every size Explorer asks for\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
