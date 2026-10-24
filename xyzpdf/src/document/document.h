// What the rest of the program is allowed to know about a PDF.
//
// The only place a rendering library is named is behind this interface. There is
// one implementation, PdfiumBackend, and it arrived in F7 by consumer: first the
// view, then the two shell handlers, then everything Qt PDF was still answering
// (docs/pdfium-plan.md sec.10). It is deliberately narrow: it carries what the
// program actually calls, not a guess at what the other engine will want.
// Editing is not here at all and must not be: v2 adds a separate
// DocumentEditor that holds one of these, so the write path can never be
// reached through the read-only type. See docs/PLAN.md §3.2 and §7.3.
//
// ---- every method here blocks, and shares one lock with the renderer ----
//
// The backend serialises every PDF library call behind a single global recursive
// mutex (see the note at the top of render/pagerenderer.h). So calling any of
// these from the UI thread is a call that waits for whatever render is in
// progress --
// hundreds of milliseconds on a scanned page, which the user sees as a frozen
// window.
//
// That is tolerable for what v1 asks on the UI thread: page sizes once per
// document, metadata when the properties dialog opens. It is NOT tolerable for
// M2, which wants text() and textRuns() per page and a search across all of
// them. Those have to run on a worker thread or be made asynchronous, and the
// interface staying synchronous does not mean the call site can be. Do not
// reach for the lock as though it made this cheap.
#pragma once

#include "docmetadata.h"
#include "outline.h"

#include <QDateTime>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QUrl>
#include <QVector>

// How a page should be drawn.
struct RenderOptions
{
    // Clockwise, in degrees. 0, 90, 180 or 270; anything else is treated as 0.
    //
    // Applied by the renderer rather than by turning the image afterwards: at
    // 90 degrees a page that is 595x842 points wants an 842x595 image, and
    // rotating a finished 595x842 one would either crop it or scale it twice.
    int rotationDegrees = 0;

    // Whether annotations (highlights, notes, form fields as flat marks) are
    // drawn into the page image. On by default: a document that shows its
    // highlights in every other reader and not in this one reads as broken.
    bool annotations = true;

    // True when the result is going to be looked at as text on a screen rather
    // than printed, which is what asks the renderer for subpixel antialiasing.
    bool optimizedForLcd = false;

    // True when the pixels are going on paper.
    //
    // This is the backend's own signal rather than a reading of `optimizedForLcd`:
    // the two are independent in the library, and a caller that wants neither is
    // the common case (an image export is not a screen and not a sheet).
    //
    // Measured, and it does nothing today: at the print size -- A4 at 300 dpi,
    // 2480x3508 -- the four fixtures this program is tested against come back
    // pixel-for-pixel identical with and without it (text.pdf, table.pdf,
    // graphic.pdf, scan.pdf; 0 pixels differ on all four, and the ink fraction to
    // three decimal places is the same). It is set because the header calls
    // FPDF_PRINTING "render for printing" and the print path is the one place that
    // means it, and it is pinned as inert in tests/documenteditor_smoke.cpp -- so
    // the day a PDFium build makes this flag change the picture, that row goes red
    // and says our print numbers are describing a different render.
    bool printing = false;

    // Which part of the page to render, in the coordinate system of the whole
    // page rendered at `scaledSize`. Empty means the whole page.
    //
    // This is what keeps one page from costing 288 MB: at 800% a page is far
    // bigger than the screen, and only the band on screen is worth having.
    QRect clip;

    // The size the whole page is scaled to, when that is not what the caller
    // asked the image to be.
    //
    // Both are needed together and neither alone is enough, which is worth
    // spelling out because getting it wrong is silent: `clip` is in *this*
    // coordinate system, and the QSize passed to render() is the size of the
    // image that comes back. Asking for a band means passing the page's size
    // here, the band's size there, and the band in `clip` -- pass the page's
    // size as the image size as well and the image comes back the size of the
    // whole page, which is the memory the banding was for. Measured, and pinned
    // by render_smoke; there is no Qt source on this machine to read it out of.
    QSize scaledSize;
};

// One line-shaped box of text on a page, in points, with the origin at the
// page's top-left corner.
//
// The text itself is not in here. The page's text and its boxes are handed out as
// two separate things with no pairing between them, and pairing them by guessing
// would be wrong on exactly the documents where it matters -- columns, tables,
// rotated captions. src/document/textlayer (M2) is where a character range
// becomes a box, by asking for the selection it already knows the range of.
struct TextRun
{
    QRectF box;
};

// A run of characters on one page, with the boxes it occupies: what a selection
// is made of before it is drawn or copied.
//
// One page at a time, because that is what the renderer can answer. A selection
// that runs across pages is several of these -- see src/document/textlayer.h.
struct PageSelection
{
    QString text;
    // In points, top-left origin: the same space textRuns() and pageSize()
    // report, so a caller can put them on screen by scaling.
    QVector<QRectF> boxes;
};

// A clickable area on a page: either a jump inside the document or a URL.
struct Link
{
    QRectF box;
    // Set for an external link and empty for an internal one.
    QUrl url;
    // The page an internal link goes to, or -1. Zero-based, like everything
    // else that names a page in this program.
    int targetPage = -1;
    // Where on that page, in points. Invalid when the link only names a page.
    QPointF target;
};

class Document
{
public:
    enum class Status {
        Ready,
        // The file is encrypted and the password given was empty or wrong.
        // The caller asks for one and calls load() again.
        NeedsPassword,
        // Kept apart from FileError because the two want different sentences:
        // this one means "this is not a PDF", the other means "this PDF is not
        // there, or not readable".
        InvalidFormat,
        FileError
    };

    virtual ~Document() = default;

    // Opens `path`, replacing anything already open.
    //
    // Not const and not a constructor: a password-protected document cannot be
    // opened in one step, and the second attempt is the same call with the
    // password filled in.
    virtual Status load(const QString &path, const QString &password) = 0;

    // Lets go of the file while this object -- and everything holding a pointer to
    // it -- stays alive: afterwards there are no pages, path() still names the file,
    // and the next load() opens it again. The in-place save (handoff-03 F8) closes
    // before it replaces the file and loads after.
    //
    // With PDFium the file is opened with FILE_SHARE_DELETE (handoff-03 F7), so a
    // replace would succeed even with the document open; the save closes anyway,
    // because "nothing of ours is reading the old bytes when they are swapped" is
    // the rule, not a property of whichever engine is underneath. Qt PDF, the engine
    // before, did hold a handle that only destroying its document let go of --
    // F8 measured a sharing violation two seconds after QPdfDocument::close().
    virtual void close() = 0;

    // Why the last load() did not return Ready, as a sentence to show the user:
    // already translated, and already worded. Always set after a failure. A
    // backend that cannot say more than "it failed" is expected to say that
    // rather than return an empty string, because an empty one turns into an
    // empty dialog.
    virtual QString lastError() const = 0;

    // The path the open document came from; empty before a successful load.
    virtual QString path() const = 0;

    virtual int pageCount() const = 0;

    // In points (1/72 inch), with the page's own /Rotate applied -- a page
    // stored landscape and marked 90 has a portrait size, because that is what
    // the reader shows and what every layout calculation needs.
    virtual QSizeF pageSize(int page) const = 0;

    // Renders one page at exactly `pixels`. Synchronous, and meant for callers
    // that are not the view: tests, and later the export and print paths.
    //
    // The view does not use this. It goes through render/PageRenderer, which
    // renders off the UI thread and can be told to forget a result that has
    // been overtaken. Doing it here would mean the window stops painting for
    // as long as the page takes.
    virtual QImage render(int page, QSize pixels, const RenderOptions &options) = 0;

    virtual QString text(int page) const = 0;

    virtual QVector<TextRun> textRuns(int page) const = 0;

    // The character index at a point, in the page's own coordinates (points,
    // top-left origin -- the same space textRuns() reports).
    //
    // This is what a click is: a point turned into a place in the page's text.
    // Everything else about a selection is a range of those indices, which is
    // why a selection does not care how the text is laid out.
    //
    // The answer is always an index into the page's text, including
    // text().length() for a point past the end of it -- which is where a drag
    // off the end of a line lands, and where a click below the last line puts
    // the cursor. -1 means the page has no text at all, and only that.
    virtual int characterIndexAt(int page, QPointF point) const = 0;

    // The text and boxes of a character range on one page, [from, to), clamped
    // to what the page has. An empty or backwards range gives nothing back
    // rather than everything -- the difference between a click and a
    // select-all.
    virtual PageSelection pageSelection(int page, int from, int to) const = 0;

    // Whether a point is over a line of text rather than merely on the page.
    //
    // Not the same question as characterIndexAt(), which answers for every
    // point on a page that has any text at all -- and has to, because that is
    // where a drag past the end of a line stops. This one is about the pointer:
    // a reader is over text when the pointer is on a line, and pointing at the
    // margin or at a page of scans is not.
    virtual bool hasTextAt(int page, QPointF point) const = 0;

    // The bookmark tree, empty when the document has none.
    //
    // By value rather than the pointer in the plan's sketch: the tree is built
    // once and cached in the backend, and returning it copies a QVector, which
    // is a refcount bump. A pointer would raise a lifetime question that has no
    // good answer -- the caller does not own the backend's cache.
    virtual Outline outline() const = 0;

    virtual QVector<Link> links(int page) const = 0;

    virtual DocMetadata metadata() const = 0;

    // How many digital signatures the file carries, and what the certification
    // signature allows (PDF 32000-1 sec.12.8): 0 when the document has no DocMDP
    // signature, otherwise the /P of its /TransformParams -- 1 no changes, 2 form
    // filling and signing, 3 that plus annotations. Read once at load from
    // fpdf_signature.h, because both answers are about the bytes that were opened and
    // an incremental save leaves them standing.
    //
    // Read, not checked: nothing in this program validates a signature's
    // cryptography, and the sentence the reader is shown says so rather than letting
    // the count imply more than it is.
    //
    // Signature dictionaries are counted, not signature fields: an empty /Sig field --
    // what a blank template hands a reader -- answers no /ByteRange and no /Contents, and
    // signing nothing it is not a signature (sec.12.8.1). DocumentEditor's qpdf pass
    // applies the same rule, and the two numbers are the one the status line says, the
    // one the eight boxes count against, and the one that picks the save flag.
    virtual int signatureCount() const = 0;
    virtual int docMdpPermission() const = 0;
};
