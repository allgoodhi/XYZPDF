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
// ---- who renders, and on which thread ----
//
// One worker thread owned by this DLL. The reason is the reader's reason: a
// scanned page is hundreds of milliseconds inside PDFium, and the thread that
// runs this window procedure belongs to DllHost -- a render inside the paint that
// wants it is a pane that does not repaint and a host that stops dispatching. The
// worker calls the same synchronous Document::render() the seam has always
// offered, then PostMessages the picture back; a paint only ever copies what has
// already arrived, and a page that is still coming shows paper with its number on
// it.
//
// Deliberately not `PageRenderer` from src/render, which is the reader's machinery
// for exactly this: Qt delivers its results through a queued connection, which
// needs a Qt event loop, and the real host runs a Win32 one. Gaining that class by
// putting a QCoreApplication inside a COM surrogate is the worse trade -- and
// previewhandler_smoke exists partly to prove this DLL renders without an
// application object at all, which is a property worth keeping rather than
// trading.
//
// What comes from that arrangement and cannot be engineered away: a render
// already inside PDFium cannot be cancelled, so work made pointless by a resize is
// dropped on arrival instead (see beginGeneration), and the thread has to be joined
// before the document it reads is destroyed (see stopWorker).
//
// ---- dragging the pane's width ----
//
// A resize is the one moment the cache holds pictures drawn for some other width,
// and it is also the moment the pane is asked to redraw the most: dragging the
// splitter is a continuous stream of WM_SIZE. Neither of those is a reason to throw
// the pictures away, which is what the first asynchronous version did and what read
// as a white flash on every drag. `blit` has always stretched, so what a drag shows
// is the page that was already there at the new width, and the request for pages at
// the width being dragged *towards* waits until the width stops moving
// (kSettleMs). Two details carry that: a cached page drawn *wider* than the pane now
// asks for is still the sharper picture, so shrinking re-renders nothing at all,
// and the timer belongs to the window rather than to this object, so a pane that
// closes mid-drag has nothing left to fire.
//
// The wait covers only what is on screen to cover. A page the resize has just pulled
// into view has no picture to stretch, so the blank is being looked at from that
// moment either way, and asking for it early is the only way the wait can be made
// shorter -- which is why the debounce holds back *soft* pages and never *missing*
// ones (see PageFit). Deferring both is what this file did for one round, and the
// report that came back was the white flash again: "只有页码，没有内容".
//
// ---- what this still does not do ----
//
// There is no zoom (the pane's width decides the size, as in every other preview
// handler), night mode is not honoured -- pages are always white paper -- and the
// DPI question stays open: this DLL declares no manifest, so what it is told about
// scale factors is whatever the host's context happens to be. The render width is
// still the fixed multiple below rather than one derived from the DPI; now that
// the size travels with the job, changing that formula is one function.
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
#include "../document/documentfactory.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

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

// Above this the document is opened where it is rather than copied. A copy is what lets Explorer
// delete a file the pane is showing (see stageSource), and past a few hundred megabytes the copy
// costs more than the inconvenience it removes -- a reader who wants to delete a 400 MB file can
// close the pane first, which is not an invitation to lose data.
constexpr qint64 kMaxStagedBytes = 256LL * 1024 * 1024;

// How many rendered pages are held. A scanned A4 at the widths above is a few
// megabytes each, and a pane that remembered every page it ever showed is how a
// preview of a 900-page document eats the machine. It is also the queue's length
// limit: wanting more pages than the cache holds is wanting pictures that are
// evicted as fast as they arrive.
constexpr int kCachedPages = 6;

// How far past the last page on screen the worker is told to look, in the
// direction the pane is being moved. Two is what a person scrolls past while the
// first of them renders.
constexpr int kPrefetchPages = 2;

// The worker's "a picture is ready", to the window it was rendered for.
constexpr UINT kMsgRendered = WM_APP + 1;

// The strip above the pages that says which document this is, and the gaps.
constexpr int kCaptionHeight = 24;
constexpr int kMargin = 8;
constexpr int kPageGap = 10;

// How far one line of the scroll bar, and one notch of the wheel, moves.
constexpr int kLineScroll = 30;
constexpr int kWheelNotch = 90;

// How long the pane's width has to hold still before it is worth drawing pages for
// it. See the note at the top of the file: the stretch covers the drag, so this
// delays only the sharpening. Long enough that a whole drag of the splitter fits
// inside it, short enough that it is not a second wait after one.
constexpr UINT kSettleMs = 150;
constexpr UINT_PTR kSettleTimer = 0x5a1;

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

// How many handlers are alive, how many server locks are held, and how many
// render threads are running. The DLL may not go while any of them is non-zero: a
// handler has a window inside somebody else's message loop, and "unload was
// allowed" is the crash that follows.
long liveHandlers_ = 0;
long serverLocks_ = 0;
long liveWorkers_ = 0;

// One page the worker has been asked to draw, and the answer.
//
// The job carries its size rather than working it out on arrival because the
// layout it came from may have changed by the time it is rendered -- and because
// that is what lets the width formula below change without touching anything else.
struct PageJob
{
    quint64 generation = 0;
    int page = -1;
    QSize pixels;
};

struct PageResult
{
    PageJob job;
    QImage image; // null when the page would not render at all
};

// ---- the test latch ----
//
// "What does the pane show while a page is still being rendered" is the question
// this whole arrangement is about, and no test can catch a render mid-flight by
// luck. So the worker can be parked just before it touches PDFium by an export the
// smoke test looks up by name, the way it looks up DllGetClassObject. Nothing else
// arms it, and it is inert in every run that does not.
//
// The wait is a short sleep loop rather than one long condition-variable wait: a
// missed wake-up here would be a hang inside DllHost, and a hang in a code path
// only tests walk is not worth that risk.
std::atomic<bool> latchArmed_{false};
std::atomic<bool> latchOpen_{true};
std::atomic<bool> workerParked_{false};
// The page size the worker is held on, so that a test can ask "what width did that
// resize end up asking for" instead of inferring it from pixels.
std::atomic<int> workerJobWidth_{0};

void parkBeforeRender(const std::atomic<bool> &stopping, int jobWidth)
{
    if (!latchArmed_.load())
        return;
    workerJobWidth_ = jobWidth;
    workerParked_ = true;
    while (!stopping.load() && !latchOpen_.load())
        Sleep(5);
    workerParked_ = false;
}

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
        // Joined before the document is reloaded into it: a second DoPreview is a
        // different file, and the first thread is still inside the old one.
        stopWorker();
        const bool usable = open();
        relayout();
        ShowWindow(window_, SW_SHOW);
        InvalidateRect(window_, nullptr, TRUE);
        if (usable) {
            startWorker();
            enqueueVisible();
        }
        return S_OK;
    }

    IFACEMETHODIMP Unload() override
    {
        // The order is the whole of the safety here: nothing may outlive the
        // document it is reading, and nothing may post to a window that is gone.
        stopWorker();
        if (window_) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        pages_.clear();
        wanted_.clear();
        outstanding_.clear();
        failed_.clear();
        layout_.clear();
        pageSizes_.clear();
        document_.reset();
        // After the document, because until it is gone the copy is a file with an open handle and
        // cannot be deleted at all.
        removeStaged();
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
        scrollDirection_ = pixels > 0 ? 1 : -1;
        scroll_ = qBound(0, scroll_ + pixels, maximumScroll());
        showScrollPosition();
        enqueueVisible();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void scrollTo(int position)
    {
        if (!window_)
            return;
        scrollDirection_ = position >= scroll_ ? 1 : -1;
        scroll_ = qBound(0, position, maximumScroll());
        showScrollPosition();
        enqueueVisible();
        InvalidateRect(window_, nullptr, FALSE);
    }

    // Where each page goes, at the current width. Heights come from the page's own
    // box, so a document that mixes portrait and landscape is laid out the way the
    // reader lays it out.
    //
    // The sizes come from the cache `open` filled, and never from the document:
    // `pageSize` takes the same global PDFium lock as a render, so asking the
    // document here would put this loop -- one call per page, on the host's thread,
    // on every WM_SIZE -- behind whatever the worker is drawing.
    //
    // Nothing drawn is dropped here. The pages were rendered for another width and
    // are shown stretched to this one until the width settles; see the note at the
    // top of the file.
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
        for (int page = 0; page < pageSizes_.size(); ++page) {
            const QSizeF points = pageSizes_.at(page);
            if (points.isEmpty() || points.width() <= 0.0 || points.height() <= 0.0) {
                layout_.append(QRect());
                continue;
            }
            const int height = int(qreal(displayWidth) * points.height() / points.width());
            layout_.append(QRect(view.left + kMargin, y, displayWidth, height));
            y += height + kPageGap;
        }
        contentHeight_ = y;

        scroll_ = qBound(0, scroll_, maximumScroll());
        showScrollPosition();
        InvalidateRect(window_, nullptr, FALSE);

        // Two halves of one decision, and the split is the whole of it: what the
        // pane already has a picture of can wait out the drag, because that picture
        // is being shown stretched and the wait changes nothing a person can see.
        // What it has no picture of cannot wait -- the blank is on screen from this
        // message onwards either way, so asking for it is the only thing that makes
        // the wait shorter.
        computeWanted();
        if (anyWantedPageSoft()) {
            sharpenPending_ = true;
            ::SetTimer(window_, kSettleTimer, kSettleMs, nullptr);
        }
        enqueueVisible(); // what is missing goes out now; the soft pages are held by
                          // sharpenPending_ until the width settles
    }

    // Is any page the pane is showing on file, but at a lower resolution than this
    // width would draw it.
    bool anyWantedPageSoft() const
    {
        for (const int page : std::as_const(wanted_)) {
            if (pageFit(page) == PageFit::Soft)
                return true;
        }
        return false;
    }

    // The width stopped moving: ask for the pages at the size they are now shown at.
    void sizeSettled()
    {
        if (!document_ || !window_)
            return;
        ::KillTimer(window_, kSettleTimer);
        sharpenPending_ = false;
        computeWanted();
        if (!anyWantedPageSoft())
            return;
        // Anything still in flight was asked for by one of the widths the drag
        // passed through. It cannot be cancelled, so it is dropped on arrival --
        // and `outstanding_` has to be emptied with it, or those pages would be
        // written off as already coming.
        beginGeneration();
        enqueueVisible();
    }

    // A picture that arrived. Called on the host's thread from the message the
    // worker posts, which is the only way anything gets from one to the other:
    // there is no Qt event loop here to deliver a signal.
    void collectRendered()
    {
        std::deque<PageResult> arrived;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            arrived.swap(results_);
        }
        bool drew = false;
        for (const PageResult &result : arrived) {
            outstanding_.remove(result.job.page);
            if (result.job.generation != generation_)
                continue; // asked for by a layout that has since gone away
            if (result.image.isNull()) {
                // Remembered, because a page that will not render is otherwise a
                // page asked for again on every paint, by a worker with nothing
                // better to do.
                failed_.insert(result.job.page);
                continue;
            }
            cachePage(result.job.page, result.image);
            drew = true;
        }
        if (!window_)
            return;
        if (drew)
            InvalidateRect(window_, nullptr, FALSE);
        enqueueVisible();
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

    // Anything staged more than a day ago is left over from a process that never got its Unload --
    // Explorer closing, DllHost being recycled, a crash. Swept before each new copy so a crash cannot
    // leave a reader's document sitting in the temp folder indefinitely. A copy some other pane is
    // still reading refuses to be deleted, which is the right outcome and needs no special case.
    void sweepStaleStagedCopies()
    {
        const QDir temp(QDir::tempPath());
        const QDateTime oldest = QDateTime::currentDateTimeUtc().addDays(-1);
        const QFileInfoList copies = temp.entryInfoList(
            {QStringLiteral("xyzpdf-preview-*.pdf")}, QDir::Files, QDir::NoSort);
        for (const QFileInfo &copy : copies) {
            if (copy.birthTime().toUTC() > oldest)
                continue;
            QFile::remove(copy.absoluteFilePath());
        }
    }

    // The copy the document is opened from, and why there is one.
    //
    // PDFium keeps the handle it opens for the life of the document, and it opens without
    // FILE_SHARE_DELETE -- measured, and the same is true of a plain QFile -- so a pane that is only
    // *showing* a file is the reason Explorer says "in use" when the reader tries to delete it. The
    // bytes are copied to a temporary file and that copy is what gets opened: the user's file is
    // touched for the length of one sequential read, and the copy leaves with the pane.
    //
    // True when `stagedPath` names something to open. False for a file too big to copy, where the
    // original is opened in place instead -- see kMaxStagedBytes.
    bool stageSource()
    {
        sweepStaleStagedCopies();
        if (stagedFrom_ == path_ && QFile::exists(stagedPath_))
            return true; // the same document, already copied for this pane
        removeStaged();

        const qint64 bytes = QFileInfo(path_).size();
        if (bytes > kMaxStagedBytes)
            return false;

        const QString name =
            QStringLiteral("xyzpdf-preview-%1.pdf")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(13));
        const QString target = QDir::tempPath() + QLatin1Char('/') + name;
        if (!QFile::copy(path_, target))
            return false;

        stagedPath_ = target;
        stagedFrom_ = path_;
        return true;
    }

    // Best effort: while PDFium still has the copy open the delete is refused, and a leftover in
    // %TEMP% is a smaller problem than refusing to show the document. Unload() calls this again
    // after the document is gone, which is when it always succeeds.
    void removeStaged()
    {
        if (stagedPath_.isEmpty())
            return;
        if (QFile::remove(stagedPath_)) {
            stagedPath_.clear();
            stagedFrom_.clear();
        }
    }

    // The one place the document is opened, and the one place anything about this
    // file can go wrong in a way worth saying. False when there is nothing to show,
    // which is also the answer to "may the worker start".
    bool open()
    {
        caption_ = fileNameOf(path_);
        message_.clear();
        pages_.clear();
        wanted_.clear();
        outstanding_.clear();
        failed_.clear();
        layout_.clear();
        pageSizes_.clear();
        contentHeight_ = 0;
        scroll_ = 0;
        sharpenPending_ = false;

        if (path_.isEmpty()) {
            message_ = L"没有给出文件路径";
            return false;
        }

        // The previous document goes before the copy it was opened from is removed -- while PDFium
        // still holds that file nothing can delete it, and a leftover per document the pane glanced
        // at is a leak with a different name. Safe to drop here because DoPreview() joined the worker
        // before calling this.
        document_ = createDocument();

        Document::Status status = Document::Status::FileError;
        if (stageSource()) {
            status = document_->load(stagedPath_, QString());
            if (status != Document::Status::Ready) {
                // The copy is not the thing worth telling the reader about: retry the file they
                // clicked, whose name belongs in the sentence. The copy stays where it is until
                // Unload(), which is also the point at which it can always be deleted.
                status = document_->load(path_, QString());
            }
        } else {
            status = document_->load(path_, QString());
        }
        if (status != Document::Status::Ready) {
            // The backend's own sentence, already worded for a person: it knows the
            // difference between "this is not a PDF" and "this PDF is not readable",
            // and flattening that here throws the useful half away.
            message_ = document_->lastError().toStdWString();
            return false;
        }

        const int pages = document_->pageCount();
        if (pages > 1)
            caption_ += L"  ·  共 " + std::to_wstring(pages) + L" 页";

        // Every page size, asked once here rather than per layout later. This is
        // the last moment at which nothing is being rendered: the worker is started
        // below and takes the same global PDFium lock that each of these calls
        // does, so asking after it starts is a stall with a different name.
        pageSizes_.reserve(pages);
        for (int page = 0; page < pages; ++page)
            pageSizes_.append(document_->pageSize(page));
        return pages > 0;
    }

    // ---- the worker ----

    void startWorker()
    {
        if (workerRunning_)
            return;
        stopping_ = false;
        worker_ = std::thread([this] { workerLoop(); });
        workerRunning_ = true;
    }

    void stopWorker()
    {
        if (!workerRunning_)
            return;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            stopping_ = true;
            queue_.clear();
        }
        workCv_.notify_all();
        worker_.join();
        workerRunning_ = false;
        std::lock_guard<std::mutex> lock(stateMutex_);
        results_.clear();
    }

    void workerLoop()
    {
        InterlockedIncrement(&liveWorkers_);
        for (;;) {
            PageJob job;
            {
                std::unique_lock<std::mutex> lock(stateMutex_);
                workCv_.wait(lock, [this] { return stopping_.load() || !queue_.empty(); });
                if (stopping_.load())
                    break;
                job = queue_.front();
                queue_.pop_front();
            }
            // Rendering happens with no lock of ours held, which is the point: the
            // host's thread has to be able to paint while this is going on.
            parkBeforeRender(stopping_, job.pixels.width());
            if (stopping_.load())
                break;
            PageResult result;
            result.job = job;
            result.image = renderToPaper(job);
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (stopping_.load())
                break; // nobody is left who would read it
            results_.push_back(result);
            PostMessageW(window_, kMsgRendered, 0, 0);
        }
        InterlockedDecrement(&liveWorkers_);
    }

    QImage renderToPaper(const PageJob &job)
    {
        QImage ink = document_->render(job.page, job.pixels, RenderOptions{});
        if (ink.isNull())
            return {};

        // The compositing step: what comes back from Qt PDF has no paper in it, and
        // GDI is about to copy it as if it did.
        QImage paper(ink.size(), QImage::Format_ARGB32_Premultiplied);
        paper.fill(Qt::white);
        QPainter flattener(&paper);
        flattener.drawImage(0, 0, ink);
        flattener.end();
        return paper;
    }

    // Everything in flight was asked for by a layout that is now gone. A render
    // already inside PDFium cannot be cancelled, so it is dropped when it arrives
    // instead -- which costs the render and nothing else.
    void beginGeneration()
    {
        ++generation_;
        outstanding_.clear();
        failed_.clear();
        std::lock_guard<std::mutex> lock(stateMutex_);
        queue_.clear();
    }

    // The size a page is drawn at: the width it is shown at, several times over,
    // kept inside the limits, with the page's own aspect from the cached points.
    QSize renderSizeFor(int page) const
    {
        if (page < 0 || page >= layout_.size() || page >= pageSizes_.size())
            return {};
        const int displayWidth = layout_.at(page).width();
        const QSizeF points = pageSizes_.at(page);
        if (displayWidth <= 0 || points.isEmpty() || points.width() <= 0.0)
            return {};
        const int width = qBound(kMinRenderWidth, displayWidth * kRenderScale,
                                 kMaxRenderWidth);
        return QSize(width, int(qreal(width) * points.height() / points.width()));
    }

    // Whether a page has to be drawn again, as opposed to being shown as it is.
    // Three answers because the two ways a page is not right are not worth the same
    // wait: absent, so there is nothing on screen to improve and every millisecond
    // of a defer is a blank page someone sees; or present but drawn for a narrower
    // pane than this one, which is a picture that is merely softer than it could be
    // and can wait for the width to stop moving. So a shrink costs nothing, and a
    // widening is the only resize that owes work.
    enum class PageFit { Missing, Soft, Current };

    PageFit pageFit(int page) const
    {
        const auto rendered = pages_.find(page);
        if (rendered == pages_.end() || rendered.value().isNull())
            return PageFit::Missing;
        const QSize wanted = renderSizeFor(page);
        if (!wanted.isEmpty() && rendered.value().width() < wanted.width())
            return PageFit::Soft;
        return PageFit::Current;
    }

    // The pages worth having a picture of: the ones on screen, then
    // kPrefetchPages more at the end the pane is being moved towards, and never
    // more than the cache holds.
    void computeWanted()
    {
        wanted_.clear();
        if (layout_.isEmpty())
            return;

        const RECT view = viewport();
        const int viewTop = view.top + scroll_;
        const int viewBottom = view.bottom + scroll_;
        int first = -1;
        int last = -1;
        for (int page = 0; page < layout_.size(); ++page) {
            const QRect &slot = layout_.at(page);
            if (slot.height() <= 0 || slot.bottom() <= viewTop || slot.top() >= viewBottom)
                continue;
            if (first < 0)
                first = page;
            last = page;
        }
        if (first < 0)
            return;

        const bool upwards = scrollDirection_ < 0;
        const int from = qMax(0, first - (upwards ? kPrefetchPages : 0));
        const int to = qMin(int(layout_.size()) - 1,
                            last + (upwards ? 0 : kPrefetchPages));
        for (int page = from; page <= to && wanted_.size() < kCachedPages; ++page) {
            if (layout_.at(page).height() > 0)
                wanted_.insert(page);
        }
    }

    // Asks the worker for the wanted pages it does not already have on the way.
    void enqueueVisible()
    {
        if (!workerRunning_)
            return;
        computeWanted();

        QVector<int> order;
        for (const int page : std::as_const(wanted_)) {
            if (outstanding_.contains(page) || failed_.contains(page))
                continue;
            const PageFit fit = pageFit(page);
            if (fit == PageFit::Current)
                continue;
            // The one thing the resize debounce actually holds back: a page that is
            // on screen and merely soft. A page with nothing drawn at all is never
            // worth waiting for, because waiting is what the blank page is.
            if (fit == PageFit::Soft && sharpenPending_)
                continue;
            order.append(page);
        }
        // In page order, so that the first picture a person scrolling down sees is
        // the page at the top of the view rather than one of the prefetches.
        std::sort(order.begin(), order.end());

        std::lock_guard<std::mutex> lock(stateMutex_);
        if (stopping_.load())
            return;
        for (const int page : std::as_const(order)) {
            if (queue_.size() >= kCachedPages)
                break;
            PageJob job;
            job.generation = generation_;
            job.page = page;
            job.pixels = renderSizeFor(page);
            if (job.pixels.isEmpty())
                continue;
            outstanding_.insert(page);
            queue_.push_back(job);
        }
        workCv_.notify_one();
    }

    void cachePage(int page, const QImage &image)
    {
        // Oldest-page-out among the pages nobody is asking for, and only then the
        // lowest-numbered one there is. Dropping the lowest unconditionally is what
        // the synchronous pane could get away with, because it only ever rendered
        // the page it was drawing; with a queue it evicts the page it is about to
        // ask for again, and a scroll back up thrashes.
        pages_.insert(page, image);
        while (pages_.size() > kCachedPages) {
            int victim = -1;
            for (auto it = pages_.constBegin(); it != pages_.constEnd(); ++it) {
                if (wanted_.contains(it.key()))
                    continue;
                victim = victim < 0 ? it.key() : qMin(victim, it.key());
            }
            if (victim < 0)
                break;
            pages_.remove(victim);
        }
    }

    long references_ = 1;
    IUnknown *site_ = nullptr;
    HWND window_ = nullptr;
    QString path_;
    // The copy the document is opened from, and which file that copy was made from. Both empty when
    // the document is being read where it is -- see stageSource for why that is the default.
    QString stagedPath_;
    QString stagedFrom_;
    std::unique_ptr<Document> document_;
    QVector<QRect> layout_;
    QVector<QSizeF> pageSizes_;
    QHash<int, QImage> pages_;
    int contentHeight_ = 0;
    int scroll_ = 0;
    int scrollDirection_ = 1;
    std::wstring caption_;
    std::wstring message_;
    COLORREF background_ = RGB(255, 255, 255);
    COLORREF text_ = RGB(0, 0, 0);

    QSet<int> wanted_;
    QSet<int> outstanding_;
    QSet<int> failed_;
    quint64 generation_ = 1;
    // The pane's width is still moving and the pages on file are the ones the drag
    // is showing stretched. Set by a resize, cleared when the width settles; while
    // it is set, `enqueueVisible` holds back only the pages that are merely soft.
    bool sharpenPending_ = false;

    std::thread worker_;
    bool workerRunning_ = false;
    std::atomic<bool> stopping_{false};
    std::mutex stateMutex_;
    std::condition_variable workCv_;
    std::deque<PageJob> queue_;
    std::deque<PageResult> results_;
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
        const auto rendered = pages_.find(page);
        if (rendered != pages_.end() && !rendered.value().isNull()) {
            blit(dc, rendered.value(), onScreen);
            continue;
        }
        // Paper and the page's number. A page that has simply vanished reads as a
        // bug in the pane, and now that rendering is not part of the paint there is
        // a third case to cover: the picture is on its way. Since the two look the
        // same from here, the number is what makes the wait legible -- it says
        // which page to keep looking at either way.
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &onScreen, white);
        DeleteObject(white);
        RECT shown = {};
        IntersectRect(&shown, &onScreen, &view);
        ::SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        const std::wstring number = std::to_wstring(page + 1);
        DrawTextW(dc, number.c_str(), -1, &shown,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
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
    case kMsgRendered:
        // The worker's way of handing something over without a Qt event loop to
        // deliver a signal: this queue is the host's, and it is already pumping it.
        if (handler)
            handler->collectRendered();
        return 0;
    case WM_SIZE:
        if (handler)
            handler->relayout();
        return 0;
    case WM_TIMER:
        // The width stopped moving. A timer with no callback function is owned by
        // the window, so one that was armed just before the pane closed has nowhere
        // to fire and DestroyWindow drops it with the rest of the queue.
        if (handler && wParam == kSettleTimer) {
            handler->sizeSettled();
            return 0;
        }
        break;
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
    // A handler joins its own worker before it lets go of anything else, so the
    // thread count is already covered by the handler count in the code as written.
    // It is counted here because the one thing this must never get wrong is saying
    // "you may unload" while something is still running: a leaked render thread
    // would keep the file mapped for the session and look exactly like a pane that
    // simply stopped drawing.
    const bool idle = InterlockedCompareExchange(&liveHandlers_, 0, 0) == 0
                   && InterlockedCompareExchange(&serverLocks_, 0, 0) == 0
                   && InterlockedCompareExchange(&liveWorkers_, 0, 0) == 0;
    return idle ? S_OK : S_FALSE;
}

// The test latch, for previewhandler_smoke and nothing else: 1 parks the worker
// before every render, 2 lets it go, 3 reports whether one is parked right now --
// which is the positive control, since a paint that shows paper proves nothing if
// the render had already finished by other means. Looked up by name from a PRIVATE
// export the way DllGetClassObject is, so the test still links nothing.
extern "C" BOOL STDAPICALLTYPE XYZPDF_PREVIEW_TEST_LATCH(int action)
{
    switch (action) {
    case 1:
        latchArmed_ = true;
        latchOpen_ = false;
        return TRUE;
    case 2:
        latchOpen_ = true;
        return TRUE;
    case 3:
        return workerParked_.load() ? TRUE : FALSE;
    case 4:
        // Not a yes/no: the width the held job was asked to draw at. A BOOL is an
        // int, and a page is never wider than the cap, so this travels over the
        // same one-argument door as the rest of the latch.
        return workerJobWidth_.load();
    default:
        return FALSE;
    }
}
