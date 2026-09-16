#include "ocrengine.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFuture>
#include <QImage>
#include <QSize>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <exception>
#include <type_traits>
#include <utility>

namespace {

using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
using winrt::Windows::Graphics::Imaging::BitmapDecoder;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::Ocr::OcrEngine;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::InMemoryRandomAccessStream;

QString sentence(const char *text)
{
    return QCoreApplication::translate("Ocr", text);
}

// Runs a block of WinRT work on a global-pool thread and waits for it.
//
// The GUI thread is an STA thread -- Qt initialises OLE on it -- and blocking
// an STA thread on an async operation (`RecognizeAsync().get()`) is a deadlock
// that happens "sometimes", which is worse than one that always does. The
// pool's threads are plain Win32 threads; they become MTA further down, and
// blocking on an MTA thread is safe.
template <typename Function>
auto onMtaThread(Function &&function) -> decltype(function())
{
    using ReturnType = decltype(function());
    QFuture<ReturnType> future = QtConcurrent::run(std::forward<Function>(function));
    if constexpr (std::is_void_v<ReturnType>)
        future.waitForFinished();
    else
        return future.result();
}

// The engine, or a null one when this machine has no language packs.
OcrEngine pickEngine()
{
    // The languages the user's Windows is set up with, in the order they set
    // them up in -- which is the only opinion about which script a screenshot
    // is written in that is worth trusting. Falling back to the first installed
    // pack covers the case of a pack that can recognize but is not in the
    // profile's list.
    if (OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages())
        return engine;
    const auto available = OcrEngine::AvailableRecognizerLanguages();
    if (available.Size() > 0)
        return OcrEngine::TryCreateFromLanguage(available.GetAt(0));
    return nullptr;
}

// The image as a SoftwareBitmap, by way of PNG.
//
// The direct route -- LockBuffer, CreateReference, GetBuffer -- needs the
// IMemoryBufferByteAccess interop and has a premultiplication question at the
// end of it. A PNG round trip of an OCR-sized crop costs a few dozen
// milliseconds once per drag, and the decoder hands back exactly the format
// RecognizeAsync is documented to take.
SoftwareBitmap toSoftwareBitmap(const QImage &image)
{
    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }

    InMemoryRandomAccessStream stream;
    DataWriter writer(stream);
    const auto *first = reinterpret_cast<const uint8_t *>(png.constData());
    writer.WriteBytes(
        winrt::array_view<const uint8_t>(first, first + png.size()));
    writer.StoreAsync().get();
    writer.FlushAsync().get();
    writer.DetachStream();
    stream.Seek(0);

    return BitmapDecoder::CreateAsync(stream)
        .get()
        .GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied)
        .get();
}

Ocr::Result recognizeWorker(QImage image)
{
    try {
        // Pool threads arrive with no apartment at all, and this is what makes
        // blocking .get() legal on one. A thread that already has an apartment
        // throws here; that is fine, because it was already the way the work
        // needs it.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error &) {
        // Already initialised on this thread.
    }

    try {
        const OcrEngine engine = pickEngine();
        if (!engine)
            return {false, {},
                    sentence("这台 Windows 没有安装文字识别的语言包，无法识别文字。")};

        // The service refuses images larger than this outright. Rather than
        // fail on a huge crop, bring it down to a size it accepts: the text
        // that survives a halving is still text, and a refusal nobody can
        // recover from is the worst answer available.
        const unsigned maximum = OcrEngine::MaxImageDimension();
        if (unsigned(image.width()) > maximum || unsigned(image.height()) > maximum) {
            image = image.scaled(QSize(image.width(), image.height()).boundedTo(
                                     QSize(int(maximum), int(maximum))),
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        const auto bitmap = toSoftwareBitmap(image);
        const auto recognized = engine.RecognizeAsync(bitmap).get();

        // Line by line, not Text(): the whole-picture string joins with \r\n,
        // and the lines are also what keeps the caller free to reflow them.
        QStringList lines;
        for (const auto &line : recognized.Lines())
            lines.append(QString::fromWCharArray(line.Text().c_str()));
        return {true, lines.join(QLatin1Char('\n')), {}};
    } catch (const winrt::hresult_error &error) {
        return {false, {},
                sentence("识别时出错：%1").arg(QString::fromWCharArray(error.message().c_str()))};
    } catch (const std::exception &error) {
        return {false, {}, sentence("识别时出错：%1").arg(QString::fromLocal8Bit(error.what()))};
    }
}

} // namespace

bool Ocr::isAvailable()
{
    return onMtaThread([] {
        try {
            return static_cast<bool>(pickEngine());
        } catch (...) {
            return false;
        }
    });
}

Ocr::Result Ocr::recognize(const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return {false, {}, sentence("框选的区域里没有可以识别的内容。")};

    // Moved in, so the caller's copy is untouched and no second full image is
    // made. QImage is explicitly shared across threads; the detach happens
    // here, in this thread, before the worker takes over.
    QImage copy = image;
    return onMtaThread([copy = std::move(copy)]() mutable {
        return recognizeWorker(std::move(copy));
    });
}
