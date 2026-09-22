// Explorer's preview pane, showing a PDF: page 1 first, and every page below it.
//
// ---- what this is ----
//
// A COM server declared in the package manifest (com:SurrogateServer, with the
// CLSID below compiled to match) that the shell loads into DllHost.exe and asks
// for an IPreviewHandler. The plumbing was proven by a probe that carried no
// dependencies at all; docs/store-submission.md section 5 has what that measured,
// including the two facts that shape this file: the host is DllHost rather than
// Explorer, and this DLL's own libraries are invisible to the loader unless they
// are delay-loaded. See previewdependencies.cpp.
//
// ---- why it goes through Document rather than straight to Qt PDF ----
//
// PLAN.md §3.2 makes `Document` the one place a rendering library is named, and
// the pane is its second consumer. That is the point of using it here: a page that
// looked different in the pane than in the reader would be a bug report nobody
// could explain, and going through the same seam is what makes the two agree by
// construction rather than by remembering. The one thing left out of the pane's
// build of that file is the link model, and the reason is recorded in
// qtpdfbackend.cpp.
//
// ---- what this still does not do ----
//
// Pages are rendered synchronously, on the host's thread, inside the paint that
// needs them: the first time a scanned page scrolls into view, the pane waits for
// PDFium. There is no zoom (the pane's width decides the size, as in every other
// preview handler), night mode is not honoured -- pages are always white paper --
// and the DPI question stays open: this DLL declares no manifest, so what it is
// told about scale factors is whatever the host's context happens to be.
//
// ---- the paper, and the resolution ----
//
// Two things a first attempt at this got wrong, both of which read as "糊":
//
//   * A Qt PDF page carries no background: the renderer puts ink over
//     transparency, which is why the reader fills its target with a paper colour
//     first. A pane that copies such an image to the DC with SRCCOPY shows a black
//     page with pale text on it -- the opposite way round. So every page is
//     composited onto white before it reaches any DC.
//   * Rendering a page at the width it is displayed at is about half a pixel per
//     point in a pane this narrow, and text at that sampling is genuinely soft --
//     not because anything stretched it afterwards. This machine is at 100% scale,
//     which is what ruled the usual suspect out. Pages are therefore rendered at
//     several times the display width and scaled down on the way out.

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

#include "../document/document.h"
#include "../document/qtpdfbackend.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QVector>

#include <memory>
#include <string>

// The class the package manifest declares. The two have to agree, and the
// manifest is the only place anyone would think to look.
// {12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7}
const CLSID kPreviewHandlerClsid = {
    0x12df01c2, 0x01d6, 0x4af8, {0xad, 0x5f, 0x96, 0x9c, 0x3c, 0x3f, 0x88, 0xd7}};

namespace {

const wchar_t kWindowClass[] = L"xyzpdfPreviewWindow";

// The page is rendered at several times the width it is shown at, and scaled down
// on the way to the DC. See the note at the top of the file: this is what the
// first version of this pane was missing.
constexpr int kRenderScale = 3;
constexpr int kMaxRenderWidth = 2400;
constexpr int kMinRenderWidth = 600;

// How many rendered pages are held. A scanned A4 at the widths above is a few
// megabytes each, and a pane that remembered every page it ever showed is how a
// preview of a 900-page document eats the machine.
constexpr int kCachedPages = 6;

// The strip above the pages that says which document this is, and the gaps.
constexpr int kCaptionHeight = 24;
constexpr int kMargin = 8;
constexpr int kPageGap = 10;

// How far one line of the scroll bar, and one notch of the wheel, moves.
constexpr int kLineScroll = 30;
constexpr int kWheelNotch = 90;

HMODULE moduleHandle()
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleHandle), &self);
    return self;
}

HFONT captionFont()
{
    static HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                    L"Segoe UI");
    return font;
}

// The caption is the file's name and nothing else: the pane's own header already
// shows where it came from, and repeating the path costs the width a page needs.
std::wstring fileNameOf(const QString &path)
{
    const int cut = qMax(path.lastIndexOf(QLatin1Char('/')),
                         path.lastIndexOf(QLatin1Char('\\')));
    return path.mid(cut + 1).toStdWString();
}

// One page, copied to the DC inside `rect`, scaled down from whatever resolution
// it was rendered at.
void blit(HDC dc, const QImage &page, const RECT &rect)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = page.width();
    // A negative height is how a top-down buffer is described to GDI, which is
    // what QImage holds and what a DIB is not by default.
    info.bmiHeader.biHeight = -page.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                  0, 0, page.width(), page.height(),
                  page.constBits(), &info, DIB_RGB_COLORS, SRCCOPY);
}

// How many handlers are alive, and how many server locks are held. The DLL may
// not go while either is non-zero: a handler has a window inside somebody else's
// message loop, and "unload was allowed" is the crash that follows.
long liveHandlers_ = 0;
long serverLocks_ = 0;

// Declared here and defined below the class, which is what it reaches into. Both
// in this same unnamed namespace, or the two declarations are two functions and
// the link says so.
LRESULT CALLBACK previewWindowProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam);

} // namespace

class PreviewHandler final : public IPreviewHandler,
                             public IPreviewHandlerVisuals,
                             public IInitializeWithFile,
                             public IObjectWithSite {
public:
    PreviewHandler() { InterlockedIncrement(&liveHandlers_); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID id, void **object) override
    {
        if (!object)
            return E_POINTER;
        // IPreviewHandler first, so the IUnknown every one of these bases shares
        // resolves to one vtable rather than being ambiguous between four.
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

    // IInitializeWithFile: the path arrives before anything is drawn.
    IFACEMETHODIMP Initialize(LPCWSTR path, DWORD) override
    {
        if (!path)
            return E_INVALIDARG;
        path_ = QString::fromWCharArray(path);
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
            window_ = CreateWindowExW(0, kWindowClass, L"",
                                      WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL,
                                      0, 0, 0, 0, parent, nullptr, moduleHandle(), this);
            if (!window_)
                return E_FAIL;
        }
        return SetRect(area);
    }

    IFACEMETHODIMP SetRect(const RECT *area) override
    {
        if (!window_)
            return E_UNEXPECTED;
        if (area) {
            MoveWindow(window_, area->left, area->top,
                       area->right - area->left, area->bottom - area->top, TRUE);
        }
        InvalidateRect(window_, nullptr, FALSE);
        return S_OK;
    }

    IFACEMETHODIMP DoPreview() override
    {
        if (!window_)
            return E_UNEXPECTED;
        open();
        relayout();
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
        pages_.clear();
        layout_.clear();
        document_.reset();
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

    // IPreviewHandlerVisuals. The background is honoured for the space around the
    // pages; the pages themselves stay white paper, because that is what a page is.
    IFACEMETHODIMP SetBackgroundColor(COLORREF colour) override
    {
        background_ = colour;
        if (window_)
            InvalidateRect(window_, nullptr, TRUE);
        return S_OK;
    }

    IFACEMETHODIMP SetFont(const LOGFONTW *) override { return S_OK; }

    IFACEMETHODIMP SetTextColor(COLORREF colour) override
    {
        text_ = colour;
        return S_OK;
    }

    // ---- reached from the window procedure, which is a free function ----
    //
    // It finds this object through GWLP_USERDATA, so these cannot be private
    // however much the arrangement would prefer to keep them that way.

    void draw(HDC dc, const RECT &area);

    void scrollBy(int pixels)
    {
        if (pixels == 0 || !window_)
            return;
        scroll_ = qBound(0, scroll_ + pixels, maximumScroll());
        showScrollPosition();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void scrollTo(int position)
    {
        if (!window_)
            return;
        scroll_ = qBound(0, position, maximumScroll());
        showScrollPosition();
        InvalidateRect(window_, nullptr, FALSE);
    }

    // Where each page goes, at the current width. Heights come from the page's own
    // box, so a document that mixes portrait and landscape is laid out the way the
    // reader lays it out.
    void relayout()
    {
        layout_.clear();
        contentHeight_ = 0;
        if (!document_ || !window_)
            return;

        const RECT view = viewport();
        const int displayWidth = view.right - view.left - 2 * kMargin;
        if (displayWidth <= 0)
            return;

        int y = 0;
        const int pages = document_->pageCount();
        for (int page = 0; page < pages; ++page) {
            const QSizeF points = document_->pageSize(page);
            if (points.isEmpty() || points.width() <= 0.0 || points.height() <= 0.0) {
                layout_.append(QRect());
                continue;
            }
            const int height = int(qreal(displayWidth) * points.height() / points.width());
            layout_.append(QRect(view.left + kMargin, y, displayWidth, height));
            y += height + kPageGap;
        }
        contentHeight_ = y;

        // The pages were rendered for a width and this is a different one, so
        // whatever is in the cache no longer fits anything.
        pages_.clear();
        scroll_ = qBound(0, scroll_, maximumScroll());
        showScrollPosition();
    }

private:
    ~PreviewHandler()
    {
        Unload();
        if (site_)
            site_->Release();
        InterlockedDecrement(&liveHandlers_);
    }

    static bool registerWindowClass()
    {
        static bool registered = false;
        if (registered)
            return true;
        // On first use, never in DllMain: the loader lock is held there and
        // registering a class takes it too.
        WNDCLASSW wc = {};
        wc.lpfnWndProc = previewWindowProc;
        wc.hInstance = moduleHandle();
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wc.hbrBackground = nullptr; // WM_ERASEBKGND declines, WM_PAINT paints all
        wc.lpszClassName = kWindowClass;
        registered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        return registered;
    }

    // The area the pages live in: everything below the caption.
    RECT viewport() const
    {
        RECT area = {};
        if (window_)
            GetClientRect(window_, &area);
        return {area.left, area.top + kCaptionHeight, area.right, area.bottom};
    }

    int maximumScroll() const
    {
        const RECT view = viewport();
        return qMax(0, contentHeight_ - (view.bottom - view.top));
    }

    void showScrollPosition()
    {
        if (!window_)
            return;
        const RECT view = viewport();
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = contentHeight_ > 0 ? contentHeight_ - 1 : 0;
        info.nPage = UINT(view.bottom - view.top);
        info.nPos = scroll_;
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
    }

    // The one place the document is opened, and the one place anything about this
    // file can go wrong in a way worth saying.
    void open()
    {
        caption_ = fileNameOf(path_);
        message_.clear();
        pages_.clear();
        layout_.clear();
        contentHeight_ = 0;
        scroll_ = 0;

        if (path_.isEmpty()) {
            message_ = L"没有给出文件路径";
            return;
        }

        if (!document_)
            document_ = std::make_unique<QtPdfBackend>();

        const Document::Status status = document_->load(path_, QString());
        if (status != Document::Status::Ready) {
            // The backend's own sentence, already worded for a person: it knows the
            // difference between "this is not a PDF" and "this PDF is not readable",
            // and flattening that here throws the useful half away.
            message_ = document_->lastError().toStdWString();
            return;
        }

        const int pages = document_->pageCount();
        if (pages > 1)
            caption_ += L"  ·  共 " + std::to_wstring(pages) + L" 页";
    }

    // The rendered page, asked for when it is first needed and kept until the
    // cache says otherwise.
    const QImage *pageImage(int page)
    {
        if (!document_ || page < 0 || page >= layout_.size())
            return nullptr;

        auto found = pages_.find(page);
        if (found != pages_.end())
            return &found.value();

        const QRect slot = layout_.at(page);
        if (slot.width() <= 0)
            return nullptr;

        const QSizeF points = document_->pageSize(page);
        const int renderWidth = qBound(kMinRenderWidth, slot.width() * kRenderScale,
                                       kMaxRenderWidth);
        const int renderHeight = int(qreal(renderWidth) * points.height() / points.width());

        QImage ink = document_->render(page, QSize(renderWidth, renderHeight),
                                       RenderOptions{});
        if (ink.isNull())
            return nullptr;

        // The compositing step: what comes back from Qt PDF has no paper in it, and
        // GDI is about to copy it as if it did.
        QImage paper(ink.size(), QImage::Format_ARGB32_Premultiplied);
        paper.fill(Qt::white);
        QPainter flattener(&paper);
        flattener.drawImage(0, 0, ink);
        flattener.end();

        // Oldest-page-out rather than least-recently-used: the pane moves in one
        // direction nearly all the time, and the bookkeeping of the alternative is
        // not worth what it would buy here.
        if (pages_.size() >= kCachedPages) {
            int oldest = page;
            for (auto it = pages_.constBegin(); it != pages_.constEnd(); ++it)
                oldest = qMin(oldest, it.key());
            pages_.remove(oldest);
        }
        return &pages_.insert(page, paper).value();
    }

    long references_ = 1;
    IUnknown *site_ = nullptr;
    HWND window_ = nullptr;
    QString path_;
    std::unique_ptr<Document> document_;
    QVector<QRect> layout_;
    QHash<int, QImage> pages_;
    int contentHeight_ = 0;
    int scroll_ = 0;
    std::wstring caption_;
    std::wstring message_;
    COLORREF background_ = RGB(255, 255, 255);
    COLORREF text_ = RGB(0, 0, 0);
};

// Outside any namespace: it is a member of a class that is not in one, and the
// compiler says so.
void PreviewHandler::draw(HDC dc, const RECT &area)
{
    HBRUSH backdrop = CreateSolidBrush(background_);
    FillRect(dc, &area, backdrop);
    DeleteObject(backdrop);

    RECT strip{area.left + kMargin, area.top, area.right - kMargin,
               area.top + kCaptionHeight};
    HFONT previous = static_cast<HFONT>(SelectObject(dc, captionFont()));
    SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, text_);
    DrawTextW(dc, caption_.c_str(), -1, &strip,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const RECT view = viewport();

    if (layout_.isEmpty()) {
        // White where a page would be, and the reason in words. An empty pane reads
        // as "still loading" forever; a pane that says "这个文件打不开" reads as an
        // answer.
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &view, white);
        DeleteObject(white);
        if (!message_.empty()) {
            RECT note{view.left + kMargin, view.top + kMargin, view.right - kMargin,
                      view.bottom - kMargin};
            ::SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            DrawTextW(dc, message_.c_str(), -1, &note, DT_LEFT | DT_WORDBREAK);
        }
        SelectObject(dc, previous);
        return;
    }

    // One clip for the whole run. Without it, the page that is partway above the
    // top is drawn across the caption, which is the first thing a scrolling pane
    // gets wrong.
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, view.left, view.top, view.right, view.bottom);

    const int viewTop = view.top + scroll_;
    const int viewBottom = view.bottom + scroll_;
    for (int page = 0; page < layout_.size(); ++page) {
        const QRect &slot = layout_.at(page);
        if (slot.height() <= 0 || slot.bottom() <= viewTop || slot.top() >= viewBottom)
            continue;
        RECT onScreen{slot.left(), slot.top() - scroll_, slot.right(),
                      slot.bottom() - scroll_};
        const QImage *image = pageImage(page);
        if (image && !image->isNull()) {
            blit(dc, *image, onScreen);
            continue;
        }
        // A page that could not be rendered is left as paper rather than skipped: a
        // pane in which page 4 has simply vanished reads as a bug in the pane.
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &onScreen, white);
        DeleteObject(white);
    }

    RestoreDC(dc, saved);
    SelectObject(dc, previous);
}

namespace {

LRESULT CALLBACK previewWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCCREATE: {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    default:
        break;
    }

    auto *handler = reinterpret_cast<PreviewHandler *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint_ = {};
        HDC dc = BeginPaint(window, &paint_);
        RECT area = {};
        GetClientRect(window, &area);
        if (handler)
            handler->draw(dc, area);
        EndPaint(window, &paint_);
        return 0;
    }
    case WM_PRINTCLIENT:
        // How a window that is not on screen is asked what it looks like -- and how
        // the smoke test sees what was drawn.
        if (handler) {
            RECT area = {};
            GetClientRect(window, &area);
            handler->draw(reinterpret_cast<HDC>(wParam), area);
        }
        return 0;
    case WM_ERASEBKGND:
        // Painted in full by WM_PAINT; erasing separately is what makes a pane
        // flash while it is being resized.
        return 1;
    case WM_SIZE:
        if (handler)
            handler->relayout();
        return 0;
    case WM_VSCROLL: {
        if (!handler)
            break;
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(window, SB_VERT, &info);
        switch (LOWORD(wParam)) {
        case SB_LINEUP:        handler->scrollBy(-kLineScroll); break;
        case SB_LINEDOWN:      handler->scrollBy(kLineScroll); break;
        case SB_PAGEUP:        handler->scrollBy(-int(info.nPage)); break;
        case SB_PAGEDOWN:      handler->scrollBy(int(info.nPage)); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: handler->scrollTo(int(info.nTrackPos)); break;
        case SB_TOP:           handler->scrollTo(0); break;
        case SB_BOTTOM:        handler->scrollTo(int(info.nMax)); break;
        default:               break;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        // The wheel reaches the child only when the pointer is over it, which is
        // all a pane can ask for.
        if (handler)
            handler->scrollBy(-(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * kWheelNotch);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

class PreviewFactory final : public IClassFactory {
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
        auto *handler = new PreviewHandler();
        const HRESULT result = handler->QueryInterface(id, object);
        handler->Release();
        return result;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        // A host that holds the factory locked must not be able to unload the DLL
        // out from under it, so the count is what DllCanUnloadNow adds up.
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
    if (id != kPreviewHandlerClsid)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new PreviewFactory();
    const HRESULT result = factory->QueryInterface(interfaceId, object);
    factory->Release();
    return result;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    const bool idle = InterlockedCompareExchange(&liveHandlers_, 0, 0) == 0
                   && InterlockedCompareExchange(&serverLocks_, 0, 0) == 0;
    return idle ? S_OK : S_FALSE;
}
