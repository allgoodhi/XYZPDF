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

#include "encryptedsample.h"

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

std::string readFileBytes(const std::wstring &path)
{
    std::string bytes;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return bytes;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        bytes.append(buffer, got);
    CloseHandle(file);
    return bytes;
}

std::wstring siblingOf(const std::wstring &path, const wchar_t *name)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1)) + name;
}

// A stream over bytes, the kind ole32 makes: what the shell's isolated thumbnail
// process hands a provider is a stream over the file, and this is the same interface
// over the same bytes without the shell.
IStream *memoryStream(const std::string &bytes)
{
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size() ? bytes.size() : 1);
    if (!memory)
        return nullptr;
    if (void *at = GlobalLock(memory)) {
        if (!bytes.empty())
            memcpy(at, bytes.data(), bytes.size());
        GlobalUnlock(memory);
    }
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return nullptr;
    }
    ULARGE_INTEGER size;
    size.QuadPart = bytes.size();
    stream->SetSize(size);
    return stream;
}

// A stream that misbehaves in one chosen way, counting the reads it was asked for.
//
//   FailsToRead -- says it holds a small file, and every read fails: a network share
//                  that went away mid-folder.
//   ClaimsHuge  -- says it holds 4 GB: has to be refused before a byte is read.
//   Endless     -- will not say how big it is, and never ends: the limit has to hold
//                  while reading, not only against what Stat said.
class OddStream final : public IStream {
public:
    enum class Kind { FailsToRead, ClaimsHuge, Endless };
    explicit OddStream(Kind kind) : kind_(kind) {}

    ULONG reads = 0;
    unsigned long long handedOut = 0;

    IFACEMETHODIMP QueryInterface(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (id == IID_IUnknown || id == IID_IStream || id == IID_ISequentialStream) {
            *object = static_cast<IStream *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    // Lives on the test's stack: the count is checked, never acted on.
    IFACEMETHODIMP_(ULONG) Release() override { return --references_; }

    IFACEMETHODIMP Read(void *buffer, ULONG wanted, ULONG *got) override
    {
        ++reads;
        if (got)
            *got = 0;
        if (kind_ == Kind::FailsToRead)
            return STG_E_READFAULT;
        memset(buffer, 0, wanted);
        handedOut += wanted;
        if (got)
            *got = wanted;
        return S_OK;
    }
    IFACEMETHODIMP Write(const void *, ULONG, ULONG *) override { return STG_E_ACCESSDENIED; }
    IFACEMETHODIMP Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER *position) override
    {
        if (position)
            position->QuadPart = 0;
        return S_OK;
    }
    IFACEMETHODIMP SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    IFACEMETHODIMP CopyTo(IStream *, ULARGE_INTEGER, ULARGE_INTEGER *, ULARGE_INTEGER *) override
    {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP Commit(DWORD) override { return S_OK; }
    IFACEMETHODIMP Revert() override { return S_OK; }
    IFACEMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }
    IFACEMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }
    IFACEMETHODIMP Stat(STATSTG *stat, DWORD) override
    {
        if (!stat)
            return E_POINTER;
        if (kind_ == Kind::Endless)
            return E_NOTIMPL;
        *stat = {};
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = kind_ == Kind::ClaimsHuge ? 4ull * 1024 * 1024 * 1024 : 4096;
        return S_OK;
    }
    IFACEMETHODIMP Clone(IStream **) override { return E_NOTIMPL; }

private:
    Kind kind_;
    ULONG references_ = 1;
};

// One provider per stream, as the shell does it: a fresh object, initialised once,
// asked for one size. The bitmap, when there is one, is the caller's to delete.
struct StreamOutcome {
    HRESULT initialised = E_FAIL;
    HRESULT result = E_FAIL;
    HBITMAP bitmap = nullptr;
};

StreamOutcome thumbnailFromStream(IClassFactory *factory, IStream *stream, UINT box)
{
    StreamOutcome outcome;
    IThumbnailProvider *provider = nullptr;
    if (FAILED(factory->CreateInstance(nullptr, __uuidof(IThumbnailProvider),
                                       reinterpret_cast<void **>(&provider))))
        return outcome;
    IInitializeWithStream *initialise = nullptr;
    if (SUCCEEDED(provider->QueryInterface(__uuidof(IInitializeWithStream),
                                           reinterpret_cast<void **>(&initialise)))) {
        outcome.initialised = initialise->Initialize(stream, STGM_READ);
        initialise->Release();
    }
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(-1);
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    outcome.result = provider->GetThumbnail(box, &bitmap, &alpha);
    // Untouched counts as nothing handed back; a failure that still handed a bitmap
    // back shows up as a non-null one, which every caller below checks against.
    outcome.bitmap = bitmap == reinterpret_cast<HBITMAP>(-1) ? nullptr : bitmap;
    provider->Release();
    return outcome;
}

// The whole bitmap, every byte, for comparing two of them.
std::vector<unsigned char> bitsOf(HBITMAP bitmap, int *width, int *height)
{
    BITMAP info = {};
    if (!bitmap || GetObjectW(bitmap, sizeof(info), &info) == 0 || !info.bmBits)
        return {};
    *width = info.bmWidth;
    *height = info.bmHeight < 0 ? -info.bmHeight : info.bmHeight;
    const auto *bits = static_cast<const unsigned char *>(info.bmBits);
    return std::vector<unsigned char>(bits, bits + size_t(info.bmWidthBytes) * size_t(*height));
}

bool directoryIsEmpty(const std::wstring &directory)
{
    WIN32_FIND_DATAW found = {};
    const HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE)
        return true;
    bool empty = true;
    do {
        if (wcscmp(found.cFileName, L".") != 0 && wcscmp(found.cFileName, L"..") != 0)
            empty = false;
    } while (empty && FindNextFileW(search, &found));
    FindClose(search);
    return empty;
}

// IInitializeWithStream (sec.133): the interface the shell's isolated thumbnail process
// uses, and the reason the installer can register this provider at all -- one that only
// took a path would have to run inside explorer.exe.
void checkStreams(IClassFactory *factory, IThumbnailProvider *fileProvider,
                  const std::wstring &fixture)
{
    // Somewhere to notice a temporary file in. The provider must not write one: the
    // bytes go from the stream to memory to PDFium. GetTempPathW reads TMP and TEMP at
    // the time of the call, so pointing both at an empty folder of our own is enough to
    // see anything that asks Windows where temporary files go.
    wchar_t base[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, base);
    const std::wstring watched = std::wstring(base) + L"xyzpdf-thumbnail-stream-"
                                 + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(watched.c_str(), nullptr);
    wchar_t oldTmp[MAX_PATH] = {};
    wchar_t oldTemp[MAX_PATH] = {};
    const DWORD hadTmp = GetEnvironmentVariableW(L"TMP", oldTmp, MAX_PATH);
    const DWORD hadTemp = GetEnvironmentVariableW(L"TEMP", oldTemp, MAX_PATH);
    SetEnvironmentVariableW(L"TMP", watched.c_str());
    SetEnvironmentVariableW(L"TEMP", watched.c_str());

    const std::string pdf = readFileBytes(fixture);
    check(!pdf.empty(), "stream: the fixture's bytes can be read");

    // The good document: a bitmap, and the same bitmap the path gives -- byte for byte,
    // because the two are one renderer over one file and anything else is a second
    // thumbnail for the same page.
    {
        IStream *stream = memoryStream(pdf);
        check(stream != nullptr, "stream: a stream over the fixture");
        if (stream) {
            const StreamOutcome outcome = thumbnailFromStream(factory, stream, 96);
            check(SUCCEEDED(outcome.initialised), "stream: it takes the stream");
            check(SUCCEEDED(outcome.result) && outcome.bitmap != nullptr,
                  "stream: GetThumbnail(96) returns a bitmap from the stream");

            HBITMAP fromPath = nullptr;
            WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
            fileProvider->GetThumbnail(96, &fromPath, &alpha);
            int streamWidth = 0, streamHeight = 0, pathWidth = 0, pathHeight = 0;
            const std::vector<unsigned char> streamBits =
                bitsOf(outcome.bitmap, &streamWidth, &streamHeight);
            const std::vector<unsigned char> pathBits = bitsOf(fromPath, &pathWidth, &pathHeight);
            Pixels pixels;
            check(outcome.bitmap && readBack(outcome.bitmap, pixels) && pixels.inkFraction > 0.0
                      && pixels.inkFraction < 0.5 && pixels.height > pixels.width
                      && pixels.height == 96,
                  "stream: a portrait page with ink on it, as tall as the box");
            check(!streamBits.empty() && streamBits == pathBits && streamWidth == pathWidth
                      && streamHeight == pathHeight,
                  "stream: byte for byte the thumbnail the path gives");
            if (fromPath)
                DeleteObject(fromPath);
            if (outcome.bitmap)
                DeleteObject(outcome.bitmap);

            // Read from its start, whatever the position: a stream someone already read
            // to the end would otherwise hand over nothing.
            LARGE_INTEGER zero = {};
            stream->Seek(zero, STREAM_SEEK_END, nullptr);
            const StreamOutcome atEnd = thumbnailFromStream(factory, stream, 64);
            check(SUCCEEDED(atEnd.result) && atEnd.bitmap != nullptr,
                  "stream: a stream left at its end is read from the start");
            if (atEnd.bitmap)
                DeleteObject(atEnd.bitmap);

            // Once per object, as IInitializeWithStream's contract says.
            IThumbnailProvider *provider = nullptr;
            IInitializeWithStream *initialise = nullptr;
            factory->CreateInstance(nullptr, __uuidof(IThumbnailProvider),
                                    reinterpret_cast<void **>(&provider));
            if (provider)
                provider->QueryInterface(__uuidof(IInitializeWithStream),
                                         reinterpret_cast<void **>(&initialise));
            check(initialise != nullptr, "stream: the provider answers IInitializeWithStream");
            if (initialise) {
                check(initialise->Initialize(nullptr, STGM_READ) == E_INVALIDARG,
                      "stream: no stream is refused as an argument");
                check(SUCCEEDED(initialise->Initialize(stream, STGM_READ)),
                      "stream: the first stream is taken");
                check(initialise->Initialize(stream, STGM_READ)
                          == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED),
                      "stream: a second one is refused as already initialised");
                initialise->Release();
            }
            if (provider)
                provider->Release();
            stream->Release();
        }
    }

    // What has to fail, and fail without a bitmap: the shell then keeps its own icon.
    struct Refused {
        const char *what;
        std::string bytes;
    };
    const std::wstring damagedPath = siblingOf(fixture, L"damaged.pdf");
    const Refused refused[] = {
        {"stream: junk that is not a PDF gives no thumbnail",
         "this is not a PDF, whatever the extension says\n"},
        {"stream: an empty stream gives no thumbnail", std::string()},
        {"stream: a truncated PDF (damaged.pdf) gives no thumbnail", readFileBytes(damagedPath)},
        {"stream: the encrypted sample gives no thumbnail, and asks nobody for a password",
         encryptedsample::bytes()},
    };
    check(refused[2].bytes.size() > 0, "stream: damaged.pdf can be read");
    check(refused[3].bytes.size() > 0, "stream: the encrypted sample was built");
    for (const Refused &item : refused) {
        IStream *stream = memoryStream(item.bytes);
        if (!stream) {
            check(false, item.what);
            continue;
        }
        const StreamOutcome outcome = thumbnailFromStream(factory, stream, 64);
        check(SUCCEEDED(outcome.initialised) && FAILED(outcome.result)
                  && outcome.bitmap == nullptr,
              item.what);
        if (outcome.bitmap)
            DeleteObject(outcome.bitmap);
        stream->Release();
    }

    {
        OddStream failing(OddStream::Kind::FailsToRead);
        const StreamOutcome outcome = thumbnailFromStream(factory, &failing, 64);
        check(FAILED(outcome.result) && outcome.bitmap == nullptr && failing.reads > 0,
              "stream: a stream whose reads fail gives no thumbnail");
    }
    {
        OddStream huge(OddStream::Kind::ClaimsHuge);
        const StreamOutcome outcome = thumbnailFromStream(factory, &huge, 64);
        check(FAILED(outcome.result) && outcome.bitmap == nullptr,
              "stream: a stream that says it holds 4 GB gives no thumbnail");
        char note[160];
        snprintf(note, sizeof(note),
                 "stream: and is refused before a byte is read (reads: %lu)", huge.reads);
        check(huge.reads == 0, note);
    }
    {
        OddStream endless(OddStream::Kind::Endless);
        const StreamOutcome outcome = thumbnailFromStream(factory, &endless, 64);
        check(FAILED(outcome.result) && outcome.bitmap == nullptr,
              "stream: a stream that never ends gives no thumbnail");
        // The provider's ceiling is 256 MB; whatever it reads past that is one chunk.
        char note[160];
        snprintf(note, sizeof(note),
                 "stream: and reading stops at the ceiling (%llu bytes handed out)",
                 endless.handedOut);
        check(endless.handedOut > 0 && endless.handedOut <= 257ull * 1024 * 1024, note);
    }

    check(directoryIsEmpty(watched), "stream: and not one temporary file was written");
    RemoveDirectoryW(watched.c_str());
    SetEnvironmentVariableW(L"TMP", hadTmp ? oldTmp : nullptr);
    SetEnvironmentVariableW(L"TEMP", hadTemp ? oldTemp : nullptr);
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

    // Plain load, no LOAD_WITH_ALTERED_SEARCH_PATH: DllHost looks for the
    // handler's imports beside itself, not beside the DLL, and that is the only
    // reason the delay-load hook exists. Handing the loader the DLL's directory
    // would let a missed delay-load entry pass.
    HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, 0);
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

    if (provider)
        checkStreams(factory, provider, fixture);

    // The engine actually in this process. Same assertion as the preview
    // handler's, and the same reason: a provider wired back to the other engine
    // would still return a picture, so only the module identity can tell the two
    // apart. Checked once, after a thumbnail has existed -- that is the call that
    // makes a delay-loaded engine load.
    check(GetModuleHandleW(L"pdfium.dll") != nullptr,
          "the thumbnail's engine is pdfium.dll");
    check(GetModuleHandleW(L"Qt6Pdf.dll") == nullptr,
          "and Qt's PDF engine is not loaded in this process");

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
