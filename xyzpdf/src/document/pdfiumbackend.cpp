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

#include <fpdf_doc.h>
#include <fpdf_formfill.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <QDateTime>
#include <QTimeZone>

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

// ---- the space a box is reported in ----
//
// PDFium's text and link rectangles are in PDF user space: the origin is the
// page's bottom-left corner and y grows upwards. Measured on this machine, on
// tests/fixtures/text.pdf -- a 595x842 page whose only line sits near the top
// answers FPDFText_GetRect with left 60.10, top 779.18, right 200.40, bottom
// 761.74. document.h reports points from the page's top-left corner, which is
// what every layout calculation and every drawn highlight in this program uses,
// so no box leaves this file without passing through here.
QRectF fromUserSpace(double left, double top, double right, double bottom, double pageHeight)
{
    return QRectF(left, pageHeight - top, right - left, top - bottom).normalized();
}

// The page's line boxes: one rectangle per run of characters that share a line
// and a font setting (fpdf_text.h on FPDFText_CountRects). A two-column page
// therefore gives two boxes per row, which is what a drawn highlight wants.
QVector<QRectF> lineBoxes(FPDF_TEXTPAGE text, double pageHeight)
{
    QVector<QRectF> boxes;
    const int count = FPDFText_CountRects(text, 0, FPDFText_CountChars(text));
    boxes.reserve(qMax(0, count));
    for (int index = 0; index < count; ++index) {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
        if (!FPDFText_GetRect(text, index, &left, &top, &right, &bottom))
            continue;
        boxes.append(fromUserSpace(left, top, right, bottom, pageHeight));
    }
    return boxes;
}

// The line a vertical position belongs to. Zero distance inside the line's own
// band, so a point that is on a line always beats one that is merely near it.
int nearestLine(const QVector<QRectF> &lines, double y)
{
    int nearest = -1;
    double best = std::numeric_limits<double>::max();
    for (int line = 0; line < lines.size(); ++line) {
        const QRectF &box = lines.at(line);
        const double distance =
            y < box.top() ? box.top() - y : (y > box.bottom() ? y - box.bottom() : 0.0);
        if (distance < best) {
            best = distance;
            nearest = line;
        }
    }
    return nearest;
}

QRectF characterBox(FPDF_TEXTPAGE text, int index, double pageHeight)
{
    double left = 0;
    double right = 0;
    double bottom = 0;
    double top = 0;
    // The argument order is the header's: left, right, bottom, top.
    if (!FPDFText_GetCharBox(text, index, &left, &right, &bottom, &top))
        return {};
    return fromUserSpace(left, top, right, bottom, pageHeight);
}

// Whether a character comes before a point, in the page's reading order.
//
// The vertical position alone does not say which line a character is on: a space
// has a box of almost no height sitting on the baseline, so it compares as being
// below the point whenever the point is above the baseline -- inside the same
// line of text. So both the character and the point are put on their nearest
// line, and only when they are the same line does the horizontal position
// decide.
//
// A character with no geometry at all is answered by the last one before it that
// has a box. Measured on tests/fixtures/table.pdf: a line break is two of them
// (U+000D then U+000A), each zero-width and each placed at the end of the line
// it breaks, and a space can come back zero-width too. A break is before the
// point exactly when the end of its line is -- and a comparison that answered for
// the break itself would not be monotone, which is what a bisection needs.
bool characterIsBefore(FPDF_TEXTPAGE text, double pageHeight, const QVector<QRectF> &lines,
                       int index, int textLength, QPointF point)
{
    if (index < 0)
        return true;
    if (index >= textLength)
        return false;

    QRectF box = characterBox(text, index, pageHeight);
    while (box.isNull() && index > 0) {
        --index;
        box = characterBox(text, index, pageHeight);
    }
    if (box.isNull())
        return true;

    const int characterLine = nearestLine(lines, box.center().y());
    const int pointLine = nearestLine(lines, point.y());
    if (characterLine >= 0 && pointLine >= 0 && characterLine != pointLine)
        return characterLine < pointLine;
    return box.center().x() < point.x();
}

// How far outside a glyph the pointer still counts as being over the line, in
// points. About the width of a space at ordinary text sizes, so that a pointer
// in the gap between two words is over the line while one in the margin is not
// -- and a two-point tolerance would make the cursor flicker back to an arrow in
// every word space, which reads as the program being unsure.
constexpr double kTextHitTolerance = 4.0;

// The text of a range of the page, in the same index space as the boxes above.
//
// FPDFText_GetText writes UCS-2 values and a terminator, so the string it hands
// back is one unit shorter than the number it reports. It skips characters with
// no UCS-2 representation, which is why the length of a page's text can come out
// below FPDFText_CountChars -- and why the index a caller gets back is a place in
// this string, which is what document.h promises.
QString textRange(FPDF_TEXTPAGE text, int start, int count)
{
    if (count <= 0)
        return {};
    QVector<char16_t> buffer(count + 1);
    const int written = FPDFText_GetText(text, start, count,
                                         reinterpret_cast<unsigned short *>(buffer.data()));
    if (written <= 0)
        return {};
    return QString::fromUtf16(buffer.constData(), written - 1);
}

// ---- strings PDFium hands back as UTF-16LE ----
//
// FPDFBookmark_GetTitle, FPDF_GetMetaText and FPDF_GetPageLabel all have the same
// shape: ask once with a null buffer for the length in bytes including the
// terminator, then ask again into a buffer that size. The headers report an
// absent value as 0 bytes and an empty one as 2, so <= 2 means "nothing".
template <typename Query>
QString utf16String(Query query)
{
    const unsigned long bytes = query(nullptr, 0);
    if (bytes <= 2)
        return {};

    QVector<char16_t> buffer(int(bytes / 2) + 1);
    const unsigned long written = query(buffer.data(), static_cast<unsigned long>(
                                                             buffer.size() * sizeof(char16_t)));
    if (written <= 2)
        return {};

    const int units = qMin<int>(int(written / 2), int(buffer.size())) - 1;
    QString text = QString::fromUtf16(buffer.constData(), units);
    // A PDF string may carry a byte order mark; the value it encodes does not.
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    return text;
}

QString bookmarkTitle(FPDF_BOOKMARK bookmark)
{
    return utf16String([bookmark](void *buffer, unsigned long length) {
        return FPDFBookmark_GetTitle(bookmark, buffer, length);
    });
}

QString metaText(FPDF_DOCUMENT document, const char *key)
{
    return utf16String([document, key](void *buffer, unsigned long length) {
        return FPDF_GetMetaText(document, key, buffer, length);
    });
}

// The address a URI action points at -- and this one is a byte string, not
// UTF-16. fpdf_doc.h says so while saying it cannot promise what the bytes are:
// "the |buffer| may contain badly encoded data. The caller should validate the
// output. e.g. check to see if it is UTF-8." A URI is ASCII in the specification
// and UTF-8 in the wild, so fromUtf8 is the validation that keeps a stray byte
// from becoming a stray path segment; it answers U+FFFD for anything else.
QString uriOf(FPDF_DOCUMENT document, FPDF_ACTION action)
{
    const unsigned long bytes = FPDFAction_GetURIPath(document, action, nullptr, 0);
    if (bytes <= 1)
        return {};

    QVector<char> buffer(int(bytes) + 1);
    const unsigned long written = FPDFAction_GetURIPath(
        document, action, buffer.data(), static_cast<unsigned long>(buffer.size()));
    if (written <= 1)
        return {};

    return QString::fromUtf8(buffer.constData(),
                             qMin<int>(int(written) - 1, int(buffer.size()) - 1));
}

// A PDF date is a string of PDF's own making -- "D:YYYYMMDDHHmmSSOHH'mm'", with
// everything after the year optional (PDF 32000-1, §14.3.3, Table 231) -- and
// PDFium hands it over exactly as the file wrote it, where Qt PDF parsed it first.
//
// A whole date or nothing: "D:2026" is not shown as 1 January, because the file
// did not say January. That is the rule the Qt backend recorded in its own
// dateOf(): a missing date beats an invented one, but a date the file did state
// and this program threw away is a Properties dialog with a blank line for no
// reason.
QDateTime pdfDate(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.startsWith(QLatin1String("D:")))
        text.remove(0, 2);

    QString digits;
    int position = 0;
    while (position < text.size() && text.at(position).isDigit())
        digits.append(text.at(position++));
    if (digits.size() < 8)
        return {};

    const QDate date(digits.mid(0, 4).toInt(), digits.mid(4, 2).toInt(),
                     digits.mid(6, 2).toInt());
    if (!date.isValid())
        return {};
    const int hour = digits.size() >= 10 ? digits.mid(8, 2).toInt() : 0;
    const int minute = digits.size() >= 12 ? digits.mid(10, 2).toInt() : 0;
    const int second = digits.size() >= 14 ? digits.mid(12, 2).toInt() : 0;
    const QTime time(hour, minute, second);
    if (!time.isValid())
        return {};

    if (position >= text.size())
        return QDateTime(date, time); // no zone written: the clock of wherever it was made

    const QChar mark = text.at(position);
    if (mark == QLatin1Char('Z'))
        return QDateTime(date, time, QTimeZone::UTC);
    if (mark != QLatin1Char('+') && mark != QLatin1Char('-'))
        return QDateTime(date, time);

    ++position;
    QString zone;
    while (position < text.size() && text.at(position).isDigit())
        zone.append(text.at(position++));
    // The offset may be written HHmm, HH'mm' or HH alone; in every case the hours
    // are the first two digits and the minutes whatever follows them.
    const int hours = zone.left(2).toInt();
    const int minutes = zone.size() > 2 ? zone.mid(2, 2).toInt() : 0;
    const int offset = (hours * 3600 + minutes * 60) * (mark == QLatin1Char('-') ? -1 : 1);
    return QDateTime(date, time, QTimeZone(offset));
}

// A bookmark tree is data from a file, and fpdf_doc.h puts cycles on the caller:
// "the caller is responsible for handling circular bookmark references, as may
// arise from malformed documents". Both numbers are far above any document
// anybody has read, and a walk that reaches one stops instead of spinning.
constexpr int kMaxBookmarkDepth = 128;
constexpr int kMaxBookmarks = 20000;

// Depth-first, in the order a sidebar draws: each sibling in turn, then its
// children. The level is counted from 1, which is what outlineFromRows assembles
// against -- the same rule the Qt read followed, and for the same reason: a
// level of 0 for every row loses the nesting.
void appendBookmarks(FPDF_DOCUMENT document, FPDF_BOOKMARK parent, int depth,
                     QVector<OutlineRow> &rows, int &remaining)
{
    if (depth > kMaxBookmarkDepth || remaining <= 0)
        return;
    for (FPDF_BOOKMARK item = FPDFBookmark_GetFirstChild(document, parent); item && remaining > 0;
         item = FPDFBookmark_GetNextSibling(document, item)) {
        OutlineRow entry;
        entry.title = bookmarkTitle(item);
        const FPDF_DEST destination = FPDFBookmark_GetDest(document, item);
        entry.page = destination ? FPDFDest_GetDestPageIndex(document, destination) : -1;
        entry.level = depth;
        rows.append(entry);
        --remaining;
        appendBookmarks(document, item, depth + 1, rows, remaining);
    }
}

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

    // The text page holdPage() loaded for a page. Caller holds the mutex and has
    // already called holdPage(index, true); a page it dropped, and a page it never
    // had, both answer null.
    FPDF_TEXTPAGE heldText(int index) const
    {
        const auto held = pages.constFind(index);
        return held == pages.constEnd() ? nullptr : held->text;
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
    // A non-empty password that opened the document means it was encrypted. There
    // is no cheaper way to ask a PDF that did not need one.
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

    const FPDF_TEXTPAGE text = impl->heldText(page);
    if (!text)
        return {};
    return textRange(text, 0, FPDFText_CountChars(text));
}

QVector<TextRun> PdfiumBackend::textRuns(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    QVector<TextRun> runs;
    if (!impl->document)
        return runs;
    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return runs;

    // One box per line segment, in points from the page's top-left corner -- the
    // same space pageSize() reports, so a caller can put one on a rendered image
    // by scaling.
    const double height = FPDF_GetPageHeightF(handle);
    const int count = FPDFText_CountChars(text);
    for (int index = 0; index < FPDFText_CountRects(text, 0, count); ++index) {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
        if (!FPDFText_GetRect(text, index, &left, &top, &right, &bottom))
            continue;
        runs.append(TextRun{fromUserSpace(left, top, right, bottom, height)});
    }
    return runs;
}

int PdfiumBackend::characterIndexAt(int page, QPointF point) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return -1;
    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return -1;

    const QString pageText = textRange(text, 0, FPDFText_CountChars(text));
    if (pageText.isEmpty())
        return -1;

    // ---- why this is a search rather than a query ----
    //
    // PDFium has a point query -- FPDFText_GetCharIndexAtPos -- and measured on
    // tests/fixtures/text.pdf it answers -1 for five of the nine gaps between the
    // characters of the page's only line, and -1 for every point above the line,
    // below it, past its end or out in the margin (with zero tolerance it answers
    // -1 for all nine). That is no use for a click: a reader clicking a line
    // expects the cursor somewhere on that line, and half a line is not ink.
    //
    // So the index is found by bisection over the character boxes, which are
    // exact and always available -- twelve FPDFText_GetCharBox calls on a page of
    // three thousand characters, and a character box is cheap here in a way it was
    // not through Qt PDF's selection API.
    const double height = FPDF_GetPageHeightF(handle);
    const QVector<QRectF> lines = lineBoxes(text, height);

    // Above the text is its beginning and below it is its end, whatever the
    // horizontal position: the contract in document.h, and where a drag that
    // starts in the margin lands.
    if (!lines.isEmpty()) {
        double top = lines.first().top();
        double bottom = lines.first().bottom();
        for (const QRectF &box : lines) {
            top = qMin(top, box.top());
            bottom = qMax(bottom, box.bottom());
        }
        if (point.y() < top)
            return 0;
        if (point.y() > bottom)
            return pageText.length();
    }

    int low = 0;
    int high = pageText.length();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (characterIsBefore(text, height, lines, middle, pageText.length(), point))
            low = middle + 1;
        else
            high = middle;
    }
    // One past the last character is a legitimate answer, and the useful one: it
    // is where a selection dragged off the end of the text stops, and where a
    // click below the last line puts the cursor. Only a page with no text at all
    // has no answer.
    return low;
}

PageSelection PdfiumBackend::pageSelection(int page, int from, int to) const
{
    QMutexLocker locker(&pdfiumMutex());
    PageSelection result;
    if (!impl->document)
        return result;

    // A backwards or empty range is not a range. Normalising it here would turn
    // a caller's mistake into one character selected at the end it did not mean,
    // which is worse than nothing: nothing is a selection that does not appear,
    // and a wrong character is a selection that appears to be right. The one
    // caller sorts its ends before it gets here, and sorts them because dragging
    // backwards is a real gesture.
    const int start = qMax(0, from);
    const int length = to - start;
    if (length <= 0)
        return result;

    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return result;

    result.text = textRange(text, start, length);
    if (result.text.isEmpty())
        return result;

    const double height = FPDF_GetPageHeightF(handle);
    // FPDFText_CountRects for this range, not the page's: a selection that stops
    // halfway down a line must not be drawn with the rest of it. The rectangle
    // list is rebuilt by every CountRects call, so the GetRect loop below runs
    // against the one it just asked for.
    const int rects = FPDFText_CountRects(text, start, length);
    result.boxes.reserve(qMax(0, rects));
    for (int index = 0; index < rects; ++index) {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
        if (!FPDFText_GetRect(text, index, &left, &top, &right, &bottom))
            continue;
        result.boxes.append(fromUserSpace(left, top, right, bottom, height));
    }
    return result;
}

bool PdfiumBackend::hasTextAt(int page, QPointF point) const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return false;
    const FPDF_PAGE handle = impl->holdPage(page, true);
    const FPDF_TEXTPAGE text = handle ? impl->heldText(page) : nullptr;
    if (!text)
        return false;

    // ---- the line box, not the characters ----
    //
    // Every character's box would say this exactly. The line box says it well
    // enough and is already in hand: a pointer is over a line when it is beside
    // that line's ink, which is what "the pointer shows an I-beam over text"
    // means -- in the gap between two words, yes; in the margin, no.
    //
    // Only the nearest line, so that a point beside the last line of a page is
    // not over the first line of whatever comes next.
    const double height = FPDF_GetPageHeightF(handle);
    const QVector<QRectF> lines = lineBoxes(text, height);
    const int line = nearestLine(lines, point.y());
    if (line < 0)
        return false;

    const QRectF box = lines.at(line);
    return box.adjusted(-kTextHitTolerance, -kTextHitTolerance, kTextHitTolerance,
                        kTextHitTolerance)
        .contains(point);
}

Outline PdfiumBackend::outline() const
{
    QMutexLocker locker(&pdfiumMutex());
    if (!impl->document)
        return {};

    QVector<OutlineRow> rows;
    int remaining = kMaxBookmarks;
    appendBookmarks(impl->document, nullptr, 1, rows, remaining);
    return outlineFromRows(rows);
}

QVector<Link> PdfiumBackend::links(int page) const
{
    QMutexLocker locker(&pdfiumMutex());
    QVector<Link> found;
    if (!impl->document)
        return found;
    const FPDF_PAGE handle = impl->holdPage(page, false);
    if (!handle)
        return found;

    const double height = FPDF_GetPageHeightF(handle);
    int position = 0;
    FPDF_LINK link = nullptr;
    while (FPDFLink_Enumerate(handle, &position, &link)) {
        Link entry;
        FS_RECTF rect{};
        if (FPDFLink_GetAnnotRect(link, &rect))
            entry.box = fromUserSpace(rect.left, rect.top, rect.right, rect.bottom, height);

        // A URI first, because those are what a link is: an address outside the
        // document, or a place inside it. FPDFAction_GetURIPath answers nothing
        // when the action is not a URI action, and an annotation may carry a
        // destination and no action at all -- which is the internal case, and the
        // common one.
        const FPDF_ACTION action = FPDFLink_GetAction(link);
        const QString uri = action ? uriOf(impl->document, action) : QString();
        if (!uri.isEmpty()) {
            entry.url = QUrl(uri);
        } else {
            const FPDF_DEST destination = FPDFLink_GetDest(impl->document, link);
            if (destination) {
                entry.targetPage = FPDFDest_GetDestPageIndex(impl->document, destination);
                FPDF_BOOL hasX = 0;
                FPDF_BOOL hasY = 0;
                FPDF_BOOL hasZoom = 0;
                FS_FLOAT x = 0;
                FS_FLOAT y = 0;
                FS_FLOAT zoom = 0;
                // Only a /XYZ destination names a place; /Fit and friends name the
                // page and let the reader choose. document.h asks for an invalid
                // point in that case, which is what the default gives.
                if (FPDFDest_GetLocationInPage(destination, &hasX, &hasY, &hasZoom, &x, &y, &zoom)
                    && hasX && hasY)
                    entry.target = QPointF(x, height - y);
            }
        }
        found.append(entry);
    }
    return found;
}

DocMetadata PdfiumBackend::metadata() const
{
    QMutexLocker locker(&pdfiumMutex());
    DocMetadata result;
    if (!impl->document)
        return result;

    // The keys are the dictionary's own names, with no slash: measured against
    // tests/fixtures/info.pdf, which carries all eight, and "/Title" answers 2
    // bytes -- the empty string -- on the same file that "Title" reads back as
    // "XYZPDF Fixture Title". docs/pdfium-plan.md sec.6.2 wrote the call with the
    // slash, and fpdf_doc.h lists the tags without it.
    result.title = metaText(impl->document, "Title");
    result.author = metaText(impl->document, "Author");
    result.subject = metaText(impl->document, "Subject");
    result.keywords = metaText(impl->document, "Keywords");
    result.producer = metaText(impl->document, "Producer");
    result.creator = metaText(impl->document, "Creator");
    result.creationDate = pdfDate(metaText(impl->document, "CreationDate"));
    result.modificationDate = pdfDate(metaText(impl->document, "ModDate"));
    result.encrypted = impl->encrypted;
    return result;
}
