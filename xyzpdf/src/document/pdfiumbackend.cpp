#include "pdfiumbackend.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>

// Before fpdfview.h, which includes windows.h itself (line 26 of the release's
// header): once that has run, NOMINMAX is too late and numeric_limits::max() is
// a macro call. The PDFium headers and windows.h have to share one inclusion.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fpdf_formfill.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <limits>

namespace {

// One lock for every FPDF call in this process, and the library's own lifetime
// counted on top of it. Qt PDF has the same shape (pagerenderer.h records what
// it is for); this is the backend's own copy now that the calls are ours.
QRecursiveMutex &pdfiumMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}

int &pdfiumUsers()
{
    static int users = 0;
    return users;
}

void acquirePdfium()
{
    QMutexLocker locker(&pdfiumMutex());
    if (pdfiumUsers()++ == 0)
        FPDF_InitLibrary();
}

void releasePdfium()
{
    QMutexLocker locker(&pdfiumMutex());
    if (--pdfiumUsers() == 0)
        FPDF_DestroyLibrary();
}

// The file PDFium reads through, with the one property that makes R7 worth the
// trouble: the handle does not stop DeleteFileW or ReplaceFileW.
struct PdfFileHandle
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    FPDF_FILEACCESS access{};

    ~PdfFileHandle() { close(); }

    void close()
    {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }
};

// Returns 1 on success, 0 on any failure -- the shape FPDF_FILEACCESS asks for.
// PDFium only ever asks for positions and sizes inside m_FileLen.
int readBlock(void *param, unsigned long position, unsigned char *buffer, unsigned long size)
{
    auto *file = static_cast<PdfFileHandle *>(param);
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(position);
    if (!SetFilePointerEx(file->handle, offset, nullptr, FILE_BEGIN))
        return 0;

    unsigned long done = 0;
    while (done < size) {
        DWORD got = 0;
        if (!ReadFile(file->handle, buffer + done, size - done, &got, nullptr) || got == 0)
            return 0;
        done += got;
    }
    return 1;
}

// A few pages held open, oldest let go. Qt PDF keeps its pages open and that is
// part of why its per-page render figure is lower than a probe that loads and
// closes a page every time (docs/pdfium-plan.md sec.5.2, confound 1). Four is
// what the text caches above this use, and enough for the two pages a drag
// across a page boundary alternates between.
constexpr int kPagesHeld = 4;

} // namespace

struct PdfiumBackend::Impl
{
    struct HeldPage
    {
        FPDF_PAGE page = nullptr;
        FPDF_TEXTPAGE text = nullptr;
        quint64 lastUsed = 0;
    };

    PdfFileHandle file;
    FPDF_DOCUMENT document = nullptr;
    // The form-fill environment and its interface. Both live as long as the
    // document: fpdf_formfill.h says the FPDF_FORMFILLINFO must stay alive until
    // the handle is closed, and the handle must be exited before the document.
    // Zeroed means every callback is null, which is enough to draw -- the F5
    // probe filled none of them and widgets still appeared (pdfium-plan.md
    // sec.7).
    FPDF_FORMFILLINFO formInfo{};
    FPDF_FORMHANDLE form = nullptr;
    QString path;
    QString error;
    bool encrypted = false;

    QHash<int, HeldPage> pages;
    quint64 useCounter = 0;

    // Caller holds the mutex.
    FPDF_PAGE holdPage(int index, bool textNeeded)
    {
        if (!document || index < 0 || index >= FPDF_GetPageCount(document))
            return nullptr;

        auto held = pages.find(index);
        if (held == pages.end()) {
            if (pages.size() >= kPagesHeld) {
                auto oldest = pages.begin();
                for (auto it = pages.begin(); it != pages.end(); ++it) {
                    if (it->lastUsed < oldest->lastUsed)
                        oldest = it;
                }
                if (oldest->page && form)
                    FORM_OnBeforeClosePage(oldest->page, form);
                if (oldest->text)
                    FPDFText_ClosePage(oldest->text);
                if (oldest->page)
                    FPDF_ClosePage(oldest->page);
                pages.erase(oldest);
            }
            HeldPage fresh;
            fresh.page = FPDF_LoadPage(document, index);
            if (!fresh.page)
                return nullptr;
            // fpdf_formfill.h pairs these with FORM_OnBeforeClosePage: the form
            // layer is told which page it is about to draw on.
            if (form)
                FORM_OnAfterLoadPage(fresh.page, form);
            held = pages.insert(index, fresh);
        }

        held->lastUsed = ++useCounter;
        if (textNeeded && !held->text)
            held->text = FPDFText_LoadPage(held->page);
        return held->page;
    }

    // Caller holds the mutex.
    void close()
    {
        for (auto &held : pages) {
            if (held.page && form)
                FORM_OnBeforeClosePage(held.page, form);
            if (held.text)
                FPDFText_ClosePage(held.text);
            if (held.page)
                FPDF_ClosePage(held.page);
        }
        pages.clear();
        useCounter = 0;
        if (form) {
            FPDFDOC_ExitFormFillEnvironment(form);
            form = nullptr;
        }
        if (document) {
            FPDF_CloseDocument(document);
            document = nullptr;
        }
        file.close();
    }
};

PdfiumBackend::PdfiumBackend()
    : impl(std::make_unique<Impl>())
{
    acquirePdfium();
}

PdfiumBackend::~PdfiumBackend()
{
    {
        QMutexLocker locker(&pdfiumMutex());
        impl->close();
    }
    releasePdfium();
}

Document::Status PdfiumBackend::load(const QString &path, const QString &password)
{
    QMutexLocker locker(&pdfiumMutex());
    impl->close();
    impl->path.clear();
    impl->error.clear();
    impl->encrypted = false;

    // The shares are the point of this function: READ because we read, WRITE
    // because a writer may be regenerating the file under us (the bar above the
    // page exists for that), and DELETE because a save by replace has to be able
    // to move a new file over this one while it is on screen.
    const HANDLE handle =
        CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto tr = [](const char *text) {
            return QCoreApplication::translate("PdfiumBackend", text);
        };
        const DWORD code = GetLastError();
        impl->error =
            (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                ? tr("找不到文件：%1").arg(path)
                : tr("打开 %1 时出错：%2").arg(path, tr("文件无法解析"));
        return Status::FileError;
    }

    LARGE_INTEGER length;
    // FPDF_FILEACCESS carries the length in an unsigned long, which is 32 bits
    // on Windows even in a 64-bit build. A PDF larger than that cannot be handed
    // to this API at all, and refusing here is better than a truncated file.
    if (!GetFileSizeEx(handle, &length)
        || static_cast<unsigned long long>(length.QuadPart)
               > std::numeric_limits<unsigned long>::max()) {
        CloseHandle(handle);
        const auto tr = [](const char *text) {
            return QCoreApplication::translate("PdfiumBackend", text);
        };
        impl->error = tr("打开 %1 时出错：%2").arg(path, tr("文件无法解析"));
        return Status::FileError;
    }

    impl->file.handle = handle;
    impl->file.access.m_FileLen = static_cast<unsigned long>(length.QuadPart);
    impl->file.access.m_GetBlock = &readBlock;
    impl->file.access.m_Param = &impl->file;

    impl->document = FPDF_LoadCustomDocument(&impl->file.access, password.toUtf8().constData());
    if (!impl->document) {
        const unsigned long code = FPDF_GetLastError();
        // The handle goes with the failed load: FPDF_LoadCustomDocument did not
        // take ownership of it (the application keeps the file resources valid
        // while the document is open, and there is no document).
        impl->file.close();

        const auto tr = [](const char *text) {
            return QCoreApplication::translate("PdfiumBackend", text);
        };
        switch (code) {
        case FPDF_ERR_PASSWORD:
            impl->error = tr("此文档已加密，需要密码。");
            return Status::NeedsPassword;
        case FPDF_ERR_FORMAT:
        case FPDF_ERR_PAGE:
            impl->error = tr("这个文件不是有效的 PDF，或者已经损坏。");
            return Status::InvalidFormat;
        case FPDF_ERR_SECURITY:
            impl->error = tr("这个 PDF 使用了不支持的加密方式，无法打开。");
            return Status::InvalidFormat;
        case FPDF_ERR_FILE:
            impl->error = tr("打开 %1 时出错：%2").arg(path, tr("文件无法解析"));
            return Status::FileError;
        default:
            impl->error = tr("打开 %1 时出错：%2")
                              .arg(path, QFileInfo::exists(path) ? tr("文件无法解析")
                                                                : tr("文件不存在"));
            return Status::FileError;
        }
    }

    impl->path = path;
    // The form layer is initialized for every document, not only ones with
    // fields: asking whether there are any costs a parse, and on a document
    // without fields this handle draws nothing. Version 1 is the stable
    // interface, and this build of PDFium has no XFA module (pdfium-plan.md
    // sec.4.3). Every callback stays null -- F7-C only draws, it accepts no
    // input, and the F5 probe drew widgets with the same empty interface.
    impl->formInfo.version = 1;
    impl->form = FPDFDOC_InitFormFillEnvironment(impl->document, &impl->formInfo);
    // The same rule QtPdfBackend uses: a non-empty password that opened the
    // document means it was encrypted. There is no cheaper way to ask a PDF that
    // did not need one.
    impl->encrypted = !password.isEmpty();
    return Status::Ready;
}

QString PdfiumBackend::lastError() const
{
    QMutexLocker locker(&pdfiumMutex());
    return impl->error;
}

QString PdfiumBackend::path() const
{
    QMutexLocker locker(&pdfiumMutex());
    return impl->path;
}

int PdfiumBackend::pageCount() const
{
    QMutexLocker locker(&pdfiumMutex());
    return impl->document ? FPDF_GetPageCount(impl->document) : 0;
}

QSizeF PdfiumBackend::pageSize(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return {};
    FS_SIZEF size{};
    if (!FPDF_GetPageSizeByIndexF(impl->document, page, &size))
        return {};
    // Measured, not assumed (the plan's C6 only compared the first page of each
    // fixture): tests/fixtures/rotated.pdf is stored 300x400 and marked /Rotate
    // 90, and this call answers 400x300 -- the same displayed size Qt PDF
    // reports. Applying the page's rotation here as well would turn a page
    // marked 90 into one marked 180.
    return QSizeF(size.width, size.height);
}

QImage PdfiumBackend::render(int page, QSize pixels, const RenderOptions &options)
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document || page < 0 || pixels.isEmpty())
        return {};

    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return {};

    QImage image(pixels, QImage::Format_RGB32);
    if (image.isNull())
        return {};

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(image.width(), image.height(), FPDFBitmap_BGRx,
                                             image.bits(), image.bytesPerLine());
    if (!bitmap)
        return {};
    // PDFium does not fill the buffer it is given; a page that paints nothing
    // would come back as uninitialised memory, which is how the F5 probe first
    // counted the paper itself as ink (docs/pdfium-plan.md sec.3, C8).
    FPDFBitmap_FillRect(bitmap, 0, 0, image.width(), image.height(), 0xFFFFFFFF);

    int rotation = options.rotationDegrees % 360;
    if (rotation < 0)
        rotation += 360;
    const int quarterTurns = rotation == 90 ? 1 : rotation == 180 ? 2 : rotation == 270 ? 3 : 0;

    int startX = 0;
    int startY = 0;
    int sizeX = pixels.width();
    int sizeY = pixels.height();
    if (!options.clip.isNull()) {
        // The page is displayed at scaledSize and the band is a rectangle in
        // that grid; start_x/y name where the page's own origin lands in the
        // bitmap, so the band's top-left is subtracted rather than copied.
        const QSize whole = options.scaledSize.isEmpty() ? pixels : options.scaledSize;
        startX = -options.clip.x();
        startY = -options.clip.y();
        sizeX = whole.width();
        sizeY = whole.height();
    }

    int flags = 0;
    if (options.annotations)
        flags |= FPDF_ANNOT;
    if (options.optimizedForLcd)
        flags |= FPDF_LCD_TEXT;

    FPDF_RenderPageBitmap(bitmap, handle, startX, startY, sizeX, sizeY, quarterTurns, flags);
    // Field appearances are a second pass, with the same geometry: the form
    // layer owns where a widget's box lands after scaling, clipping and
    // rotation, so it has to be handed exactly what the page render was. This
    // is the whole of F7-C -- fields are drawn, not editable; FPDF_FFLDraw's
    // own comment says widgets are drawn whatever the annotation flag says.
    if (impl->form) {
        FPDF_FFLDraw(impl->form, bitmap, handle, startX, startY, sizeX, sizeY, quarterTurns,
                     flags);
    }
    FPDFBitmap_Destroy(bitmap);
    return image;
}

QString PdfiumBackend::text(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return {};
    if (!impl->holdPage(page, true))
        return {};

    const auto held = impl->pages.constFind(page);
    if (held == impl->pages.constEnd() || !held->text)
        return {};

    const int count = FPDFText_CountChars(held->text);
    if (count <= 0)
        return {};
    QVector<unsigned short> buffer(count + 1);
    const int written = FPDFText_GetText(held->text, 0, count, buffer.data());
    if (written <= 0)
        return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()), written - 1);
}

QVector<TextRun> PdfiumBackend::textRuns(int page) const
{
    Q_UNUSED(page)
    // F7-E: FPDFText_CountRects / FPDFText_GetRect, one box per line segment.
    return {};
}

int PdfiumBackend::characterIndexAt(int page, QPointF point) const
{
    Q_UNUSED(page)
    Q_UNUSED(point)
    // F7-E: FPDFText_GetCharBox and the same bisection QtPdfBackend uses.
    return -1;
}

PageSelection PdfiumBackend::pageSelection(int page, int from, int to) const
{
    Q_UNUSED(page)
    Q_UNUSED(from)
    Q_UNUSED(to)
    // F7-E.
    return {};
}

bool PdfiumBackend::hasTextAt(int page, QPointF point) const
{
    Q_UNUSED(page)
    Q_UNUSED(point)
    // F7-E.
    return false;
}

Outline PdfiumBackend::outline() const
{
    // F7-E: FPDFBookmark_GetFirstChild / FPDFBookmark_GetNextSibling.
    return {};
}

QVector<Link> PdfiumBackend::links(int page) const
{
    Q_UNUSED(page)
    // F7-E: FPDFLink_Enumerate / FPDFLink_GetDest / FPDFLink_GetURIPath.
    return {};
}

DocMetadata PdfiumBackend::metadata() const
{
    // F7-E: FPDF_GetMetaText for the eight keys.
    return {};
}
