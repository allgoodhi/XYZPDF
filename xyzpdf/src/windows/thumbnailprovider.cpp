// Explorer's file icons for a PDF: the first page, shrunk.
//
// ---- what this is ----
//
// A second COM server in the same shape as the preview handler: declared in the
// package manifest (com:SurrogateServer, with the CLSID below compiled to match)
// and pointed at by the file type's desktop2:ThumbnailHandler, so the shell
// activates it in DllHost.exe and asks it for an IThumbnailProvider.
//
// ---- the stream, and why it is the one that matters (sec.133) ----
//
// The GitHub installer registers the class itself (packaging/xyzpdf.iss, HKCU
// CLSID\...\InprocServer32) and the program points our ProgID's thumbnail slot at
// it. There is no surrogate declaration outside a package, so where the code runs
// is decided by the shell: a provider that takes IInitializeWithStream is created
// in the isolated thumbnail process (a DllHost of Windows' own) unless its class
// carries DisableProcessIsolation, which ours does not. One that could only take a
// path would have to opt out of that isolation to be called at all -- Qt and PDFium
// loaded into explorer.exe, which is why the installer of sec.131 registered no
// thumbnails. So the stream is read here, whole, into memory, and handed to PDFium
// from there (Document::loadData): no temporary file, nothing written anywhere.
// IInitializeWithFile is still answered, for the package's surrogate and for the
// existing probe; it costs nothing, because which one the shell asks for is
// settled by where it created the object, not by what the object offers.
//
// It is its own DLL rather than another class inside xyzpdfpreview.dll because
// that is the layout docs/PLAN.md §3.3 prescribes (shell / preview / thumbnail),
// and because the two are never in the same process anyway: COM gives each
// surrogate CLSID its own DllHost, which is what the module list showed when the
// preview handler was verified. Sharing a DLL would share the delay-load hook --
// the only thing actually worth sharing, and that file is compiled into both.
//
// ---- why it goes through Document ----
//
// Same reason as the pane, and the same reason twice over now that both exist:
// a page that looks different in the icon than in the preview than in the reader
// is a bug nobody can explain. Three consumers of one seam agree by construction.
//
// ---- the paper ----
//
// The trap the preview handler documents at its own header is here too, and it
// bites harder: a thumbnail is copied into a tile with no blending at all, so
// ink-over-transparency comes out as a black square with pale marks in it. Every
// image is composited onto white before it leaves this file.
//
// ---- what this does not do ----
//
// Page 1 only, and no per-document state: Explorer asks for one size at a time
// and caches what we return in its own thumbnail database, so a document opened
// twice gets the second rendering from the cache rather than from us. Whether
// that cache is keyed on what we would have called the DPI is unmeasured here --
// see the DPI note at the top of previewhandler.cpp, which applies unchanged.

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

#include "../document/document.h"
#include "../document/documentfactory.h"

#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QString>

#include <memory>

// The class the package manifest declares for the thumbnail handler.
// {4032B809-B50E-4F0A-AFC1-D300803A81CE}
const CLSID kThumbnailProviderClsid = {
    0x4032b809, 0xb50e, 0x4f0a, {0xaf, 0xc1, 0xd3, 0x00, 0x80, 0x3a, 0x81, 0xce}};

namespace {

// How much bigger than the tile the page is rendered at before being shrunk back.
//
// Two, not the preview pane's three: a thumbnail is at most a few hundred pixels
// and the cost is per file in a folder that may hold fifty of them, all of them
// asked for the moment the folder opens. Two times is enough for the text to read
// as text rather than as texture, which is all a thumbnail is being asked to do.
constexpr int kRenderScale = 2;

// Explorer's own requests run from 16 to 256; the ceiling is generous so an odd
// request still works, and the floor is where a rendering stops being meaningful.
constexpr int kMinThumbnail = 16;
constexpr int kMaxThumbnail = 512;

// The most a stream may hold before it is refused without a thumbnail.
//
// The whole file is read into the thumbnail process's memory before PDFium sees a
// byte of it (a stream from the shell is not something to hand PDFium a block
// reader over: it may be a copy across a process boundary, and every seek is a
// call). 256 MB is far above any document people browse a folder of, and low
// enough that a folder of scanned books does not turn into gigabytes in DllHost.
// A bigger file keeps the icon the shell already has; the reader opens it fine.
constexpr quint64 kMaxStreamBytes = 256ull * 1024 * 1024;

// The stream, read whole, from its start. False -- and nothing in `out` -- for a
// stream that says it is too big, turns out to be too big while being read, or
// fails a read: a thumbnail is a best effort, and a partial file is not a smaller
// file, it is a damaged one.
bool readWholeStream(IStream *stream, QByteArray *out)
{
    out->clear();
    STATSTG stat = {};
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))) {
        if (stat.cbSize.QuadPart > kMaxStreamBytes)
            return false; // refused before a byte is read
    } else {
        stat.cbSize.QuadPart = 0; // unknown: the limit is enforced while reading
    }
    // From the start, whatever the position: the shell hands a fresh stream, but
    // a stream that was read before would otherwise give the tail of a file.
    // A stream that cannot seek is read from where it stands.
    const LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);

    QByteArray bytes;
    bytes.reserve(qsizetype(stat.cbSize.QuadPart));
    constexpr ULONG kChunk = 64 * 1024;
    for (;;) {
        const qsizetype at = bytes.size();
        if (quint64(at) > kMaxStreamBytes)
            return false; // past the limit, whatever Stat said (at most one chunk over)
        bytes.resize(at + kChunk);
        ULONG got = 0;
        const HRESULT result = stream->Read(bytes.data() + at, kChunk, &got);
        if (FAILED(result))
            return false;
        bytes.resize(at + qsizetype(got));
        // S_FALSE, or S_OK with nothing read, is the end of the stream.
        if (result == S_FALSE || got == 0)
            break;
    }
    if (quint64(bytes.size()) > kMaxStreamBytes)
        return false;
    *out = bytes;
    return true;
}

// How many handlers are alive, and how many server locks are held. The DLL may
// not go while either is non-zero.
long liveThumbnails_ = 0;
long serverLocks_ = 0;

// The page, flattened onto white and fitted inside a `box` by `box` square, which
// is the shape IThumbnailProvider asks for. Null when there is no page to show --
// and returning nothing is right, because Explorer then draws the file icon, which
// is an honest answer, where a blank white square would read as an empty document.
QImage thumbnailOf(Document &document, int box)
{
    const QSizeF points = document.pageSize(0);
    if (points.isEmpty() || points.width() <= 0.0 || points.height() <= 0.0)
        return QImage();

    // The long edge decides, so a landscape page fills the square by its width and
    // a portrait one by its height, and neither is cropped.
    const bool landscape = points.width() >= points.height();
    const int displayLong = box;
    const int displayShort =
        int(qint64(box) * (landscape ? points.height() : points.width())
            / qint64(landscape ? points.width() : points.height()));
    if (displayShort <= 0)
        return QImage();

    // Rendered at kRenderScale times the display size, then scaled down: the same
    // half-pixel-per-point problem the pane has, in a smaller dose.
    const QSize rendered(landscape ? displayLong * kRenderScale
                                   : displayShort * kRenderScale,
                        landscape ? displayShort * kRenderScale : displayLong * kRenderScale);

    QImage ink = document.render(0, rendered, RenderOptions{});
    if (ink.isNull())
        return QImage();

    QImage paper(ink.size(), QImage::Format_ARGB32_Premultiplied);
    paper.fill(Qt::white);
    QPainter flattener(&paper);
    flattener.drawImage(0, 0, ink);
    flattener.end();

    return paper.scaled(landscape ? QSize(displayLong, displayShort)
                                  : QSize(displayShort, displayLong),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// The image, handed over as the GDI object the interface returns.
//
// A DIB section rather than CreateBitmap because the bits are then addressable
// while we fill them, and top-down because that is the order QImage holds rows in
// and the order a BI_RGB buffer is not by default.
HRESULT toBitmap(const QImage &image, HBITMAP *out)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width();
    info.bmiHeader.biHeight = -image.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits)
        return E_OUTOFMEMORY;

    // Format_ARGB32 is already blue-first in memory, which is what a 32-bit DIB
    // is, so this is a copy rather than a conversion. The stride is 4 bytes per
    // pixel at any width, so the two buffers line up row for row.
    const uchar *source = image.constBits();
    const int rowBytes = image.width() * 4;
    auto *target = static_cast<uchar *>(bits);
    for (int row = 0; row < image.height(); ++row)
        memcpy(static_cast<uchar *>(target) + size_t(row) * rowBytes,
               source + size_t(row) * image.bytesPerLine(), rowBytes);

    *out = bitmap;
    return S_OK;
}

} // namespace

class ThumbnailProvider final : public IThumbnailProvider,
                                public IInitializeWithStream,
                                public IInitializeWithFile,
                                public IObjectWithSite {
public:
    ThumbnailProvider() { InterlockedIncrement(&liveThumbnails_); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(IThumbnailProvider))
            *object = static_cast<IThumbnailProvider *>(this);
        else if (id == __uuidof(IInitializeWithStream))
            *object = static_cast<IInitializeWithStream *>(this);
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

    // IInitializeWithStream: what the isolated thumbnail process hands over.
    //
    // Once per object, as the interface's contract says; the stream is kept and read
    // when the thumbnail is asked for, not here -- the shell may create a provider
    // and never ask it for anything.
    IFACEMETHODIMP Initialize(IStream *stream, DWORD) override
    {
        if (!stream)
            return E_INVALIDARG;
        if (stream_)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream->AddRef();
        stream_ = stream;
        path_.clear();
        bytes_.clear();
        return S_OK;
    }

    // IInitializeWithFile
    IFACEMETHODIMP Initialize(LPCWSTR path, DWORD) override
    {
        if (!path)
            return E_INVALIDARG;
        if (stream_)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        path_ = QString::fromWCharArray(path);
        return S_OK;
    }

    // IObjectWithSite. Nothing is done with the site; the shell will not ask for
    // one it cannot get, and refusing the interface outright is how handlers end
    // up not being called.
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

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT box, HBITMAP *bitmap,
                                WTS_ALPHATYPE *alphaType) override
    {
        if (!bitmap || !alphaType)
            return E_POINTER;
        *bitmap = nullptr;
        // Opaque: the page is white paper, and claiming otherwise would have
        // Explorer blend a rectangle that has nothing to blend with.
        *alphaType = WTSAT_RGB;

        if (box < kMinThumbnail || box > kMaxThumbnail)
            return E_INVALIDARG;
        if (path_.isEmpty() && !stream_)
            return E_FAIL;

        // The stream is read once: a second size from the same object is the same
        // bytes, and a stream that has been read to the end has nothing more to say.
        if (stream_ && bytes_.isEmpty() && !readWholeStream(stream_, &bytes_))
            return E_FAIL; // too big, or a read failed: the shell keeps its icon

        if (!document_)
            document_ = createDocument();
        // No password, ever: an encrypted document answers NeedsPassword, and a
        // folder view is not where anybody types one.
        const Document::Status status = stream_ ? document_->loadData(bytes_, QString())
                                                : document_->load(path_, QString());
        if (status != Document::Status::Ready) {
            // Not a PDF, or not a readable one. No thumbnail, no dialog: the
            // icon the shell already has is the right thing to show, and this is
            // a folder view rather than a place to report an error.
            return E_FAIL;
        }

        const QImage image = thumbnailOf(*document_, int(box));
        if (image.isNull())
            return E_FAIL;
        return toBitmap(image, bitmap);
    }

private:
    ~ThumbnailProvider()
    {
        // The document first: it reads from bytes_, which go with this object.
        document_.reset();
        if (stream_)
            stream_->Release();
        if (site_)
            site_->Release();
        InterlockedDecrement(&liveThumbnails_);
    }

    long references_ = 1;
    IUnknown *site_ = nullptr;
    IStream *stream_ = nullptr;
    QByteArray bytes_;
    QString path_;
    std::unique_ptr<Document> document_;
};

namespace {

class ThumbnailFactory final : public IClassFactory {
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
        auto *provider = new ThumbnailProvider();
        const HRESULT result = provider->QueryInterface(id, object);
        provider->Release();
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
    if (id != kThumbnailProviderClsid)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new ThumbnailFactory();
    const HRESULT result = factory->QueryInterface(interfaceId, object);
    factory->Release();
    return result;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    const bool idle = InterlockedCompareExchange(&liveThumbnails_, 0, 0) == 0
                   && InterlockedCompareExchange(&serverLocks_, 0, 0) == 0;
    return idle ? S_OK : S_FALSE;
}
