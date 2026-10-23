#include "ocrsession.h"

#include "../core/appsettings.h"
#include "../document/document.h"
#include "../document/pagemetrics.h"
#include "../ocr/ocrengine.h"
#include "../ui/ocrdialog.h"
#include "../view/pageview.h"

#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace {

// How many pixels to put on the recognition crop's longer side.
//
// Recognition quality lives on the height of the letters in pixels: the same
// page at 50% on screen is often too small to read reliably, and at 800% it is
// a waste. Sixteen hundred -- roughly 300 dpi for ordinary text -- is where the
// answers stop improving, so that is the target rather than either extreme.
constexpr double kOcrTargetPixels = 1600.0;

// The whole page is what gets rendered to produce a crop from, so the crop's
// scale is also capped where the whole page stays a *UI-thread-sized* job --
// this render is synchronous, on the thread that draws, and four million
// pixels is a few dozen milliseconds on this machine's documents. A tiny box on
// a huge page would otherwise ask for a fifty-thousand-pixel rendering to get
// its two hundred.
constexpr qint64 kOcrMaximumPagePixels = 4LL * 1024 * 1024;

// Below this, on the crop's longer side, the picture is brought up before it
// goes to the recognizer. The cap above can leave a small box at two hundred
// pixels, which reads badly; doubling costs nothing off-thread and is the
// difference between "nothing found" and the four words that were there.
constexpr int kOcrMinimumCropPixels = 480;

} // namespace

OcrSession::OcrSession(QWidget *host, PageView *view, std::function<Document *()> document)
    : QObject(host)
    , host(host)
    , pageView(view)
    , documentProvider(std::move(document))
{
}

OcrSession::~OcrSession() = default;

void OcrSession::captureRegion(int page, QRectF points)
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document || page < 0 || page >= document->pageCount())
        return;
    const QSizeF pagePoints = document->pageSize(page);
    if (pagePoints.isEmpty() || points.isEmpty())
        return;

    // The crop is rendered at a size of its own rather than the size it
    // happens to be on screen: the user may have zoomed out to see the page
    // they are cropping, and a crop that arrives at 40% reads badly no matter
    // how good the recognizer is.
    double scale = kOcrTargetPixels / qMax(points.width(), points.height());
    // The whole page is what gets rendered to cut the crop from, so the scale
    // is also capped where the whole page stays a UI-thread-sized job -- which
    // is what keeps a tiny box on a huge page from asking for a
    // fifty-thousand-pixel rendering to get its two hundred.
    //
    // The band render below means the *crop* is what is actually drawn, so this
    // cap is more conservative than its name suggests. F7-E measured why it was
    // not raised: a box around text.pdf's one line renders 552 px on its long
    // side under this cap and 1600 px without it, and the machine's recognizer
    // answered ")(YZPDF one" for both -- the size is not what decides that
    // answer, so the cap stays where the shipped behaviour put it.
    const double pageCap =
        std::sqrt(double(kOcrMaximumPagePixels))
        / std::sqrt(double(pagePoints.width()) * double(pagePoints.height()));
    scale = qBound(1.0, scale, pageCap);

    const int rotation = pageView->rotation();
    const QSize full = PageMetrics::pixelsFor(PageMetrics::rotated(pagePoints, rotation), scale,
                                              1.0);
    // The box is in the page's own coordinates; the image it will be cut from
    // is the page the way it is drawn, which is a quarter turn away when the
    // reader has rotated it. One formula, shared with the view -- which is the
    // reason it lives in PageMetrics rather than here as a copy.
    const QRectF drawn = PageMetrics::rotatedRect(points, pagePoints, rotation);
    QRect crop = QRect(int(std::floor(drawn.left() * scale)),
                       int(std::floor(drawn.top() * scale)),
                       int(std::ceil(drawn.width() * scale)),
                       int(std::ceil(drawn.height() * scale)));
    crop = crop.intersected(QRect(QPoint(0, 0), full));
    if (crop.isEmpty())
        return;

    // The crop is rendered here, synchronously, and NOT through PageRenderer.
    // That is a decision with a story: an asynchronous crop render on the
    // renderer's threads, still in flight while the user reopens the document,
    // ended in a fail-fast inside the reload -- reproducible at will in the
    // integration test, and never seen before because a *view* render is never
    // still running when the next document loads. Rendering on this thread
    // makes the overlap impossible by construction: nothing that touches the
    // document runs concurrently with anything else that does. The cost is a
    // synchronous render of a few dozen milliseconds, capped by the pixel
    // budget above, paid at the moment the user releases a box they dragged --
    // the slow part, the recognition, still runs off this thread.
    RenderOptions options;
    options.rotationDegrees = rotation;
    options.annotations = true;
    options.clip = crop;
    options.scaledSize = full;
    const QImage cropImage = document->render(page, crop.size(), options);
    if (cropImage.isNull())
        return;

    // The snapshot tool wanted the pixels and nothing else: on the clipboard,
    // and a line in the status bar so that something visibly happened -- a
    // clipboard that changes silently is a clipboard nobody trusts. Before the
    // recognizer, because running it here would be work for an answer that is
    // thrown away.
    if (pageView->tool() == PageView::Tool::Snapshot) {
        QGuiApplication::clipboard()->setImage(cropImage);
        Q_EMIT regionCopied(cropImage.width(), cropImage.height());
        return;
    }

    // The translate tool boxes a picture to translate it, so it goes straight
    // from the pixels to the translation window -- no intermediate recognition
    // result to click through. The recognize tool still shows what it read.
    if (pageView->tool() == PageView::Tool::Translate) {
        recognize(cropImage, nullptr, /*thenTranslate=*/true);
        return;
    }

    OcrDialog *dialog = new OcrDialog(host);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // The pixels go in before the recognition answer can, so that "复制图片" is
    // available even if the engine finds nothing to read -- which for a chart or
    // a photograph is the usual outcome, and not a reason to withhold the picture.
    // It is handed the screen's own colours, because that is the picture the
    // reader chose. Recognition still gets `cropImage` as the renderer produced
    // it: dark-on-light is what the engine expects, and inverting it to match a
    // night screen would be a way to make the words harder to read for the one
    // consumer that has no eyes.
    dialog->setCropImage(cropImage, pageView->nightInverted());
    // The recognized words can be translated from this window -- the path that
    // reads a scan or a picture into another language.
    connect(dialog, &OcrDialog::translateRequested, this, &OcrSession::translateRequested);
    dialog->show();
    recognize(cropImage, dialog);
}

void OcrSession::recognize(const QImage &crop, QPointer<OcrDialog> dialog, bool thenTranslate)
{
    // The crop is copied into the worker by value, so the view can go on
    // scrolling, zooming, even closing this tab while it runs -- nothing in
    // the task touches this object. The dialog is watched with a QPointer:
    // the user is free to close it, and a result nobody is waiting for is
    // dropped rather than kept alive to be shown into nothing.
    auto *watcher = new QFutureWatcher<Ocr::Result>(this);
    connect(watcher, &QFutureWatcher<Ocr::Result>::finished, this,
            [this, watcher, dialog, thenTranslate] {
                const Ocr::Result result = watcher->result();
                watcher->deleteLater();
                if (dialog)
                    dialog->setResult(result);
                // Translate the recognized words when the reader either chose the
                // translate tool (thenTranslate) or turned on "划线即翻" for the
                // recognize tool. Only when there was something to read.
                const bool wantTranslate =
                    thenTranslate || AppSettings::translateTrigger() == QLatin1String("auto");
                if (wantTranslate && result.ok && !result.text.trimmed().isEmpty())
                    Q_EMIT translateRequested(result.text);
            });
    watcher->setFuture(QtConcurrent::run([crop] {
        // Onto white paper first, and this is the whole trick. A PDF page has no
        // background: what the renderer hands back is the ink at full alpha over
        // transparent *black*, so the crop is a black rectangle with black text
        // on it, and the recognizer reads exactly nothing from it -- which is
        // what the first version of this feature did, on every picture, while
        // looking correct in every debug print. The view flattens a page the
        // same way before drawing it; this is the same step, for the same
        // reason.
        QImage input(crop.size(), QImage::Format_RGB32);
        input.fill(Qt::white);
        {
            QPainter painter(&input);
            painter.drawImage(0, 0, crop);
        }

        // A crop that came out small -- a few words boxed on a page whose
        // scale was capped -- is brought up before recognition. Doubling costs
        // nothing here, off the thread that draws, and is the difference
        // between "nothing found" and the words that were there.
        const int longerSide = qMax(input.width(), input.height());
        if (longerSide > 0 && longerSide < kOcrMinimumCropPixels) {
            input = input.scaled(longerSide * 2, longerSide * 2, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }
        return Ocr::recognize(input);
    }));
}
