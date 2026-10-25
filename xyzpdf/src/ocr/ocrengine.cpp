#include "ocrengine.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFuture>
#include <QImage>
#include <QSize>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
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

// Every call site wraps its literal in QT_TRANSLATE_NOOP("Ocr", ...), and that is
// the load-bearing part: lupdate finds translatable text by the NAME a call goes
// through, and `sentence` is not a name it knows -- so until the wrapping, every
// sentence in this file was in the code, in the right context at run time, and
// absent from the catalogue, which showed them in Chinese under an English
// interface. translator.cpp met the same thing with a helper called `said`.
QString sentence(const char *text)
{
    return QCoreApplication::translate("Ocr", text);
}

// The pool the WinRT work runs on, and why it is not the global one.
//
// The callers of this class are themselves usually on a pool thread: the tab
// runs the recognition through QtConcurrent so that the window keeps drawing.
// Sending the work to the same global pool and then waiting for it means the
// caller's thread is blocked while its own replacement waits for a free thread
// -- which is fine while there are threads to spare and a deadlock the moment
// there are not (a two-core machine, or a handful of boxes dragged in a row).
// A pool of its own cannot be starved by its callers, and the wait becomes an
// ordinary wait.
QThreadPool &winrtPool()
{
    static QThreadPool pool;
    static const bool sized = [] {
        // More than one, always: with a single thread a second request would
        // queue behind the first while a caller waits for it, which is the same
        // starvation one level down.
        pool.setMaxThreadCount(qMax(2, QThread::idealThreadCount() / 2));
        // These threads must not expire, and the reason was measured the hard
        // way (handoff-03 F10). QThreadPool's default expiry is thirty seconds
        // of idleness; these are the process's only MTA threads (the GUI thread
        // is an STA), so when the last idle one exits the apartment is torn
        // down and Windows unloads the WinRT server DLLs -- Windows.Media.Ocr.dll
        // among them. C++/WinRT's process-wide activation-factory cache still
        // holds the factory it handed back, but that reference does not keep
        // the implementation DLL loaded: the next recognition picked the cached
        // factory and called through a vtable in unmapped memory -- 0xC0000005,
        // `call [rax+0x50]` into a vtable whose page had been released. So a
        // reader who recognizes one region and then, more than thirty seconds
        // later, another, killed the process. -1 is forever, and the pool is a
        // function-local static: its threads live exactly as long as the cache
        // they are keeping alive, and its destructor still waits for any work
        // in flight before they go.
        pool.setExpiryTimeout(-1);
        return true;
    }();
    Q_UNUSED(sized)
    return pool;
}

// Runs a block of WinRT work on an MTA thread and waits for it.
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
    QFuture<ReturnType> future = QtConcurrent::run(&winrtPool(), std::forward<Function>(function));
    if constexpr (std::is_void_v<ReturnType>)
        future.waitForFinished();
    else
        return future.result();
}

// The two kinds of writing a recognizer here can be built for.
//
// Windows.Media.Ocr's packs split cleanly: the ones for Chinese, Japanese and
// Korean carry the thousand-glyph tables, and every other pack it ships reads a
// Latin alphabet. Which one a picture wants is not a guess -- measured on this
// machine (tests/ocrprobe.cpp, one crop of text.pdf's first line and one of
// Chinese characters, against every installed engine and fourteen preprocessings):
// en-US read the Latin line exactly in 14 of 14 pictures, zh-Hans in 4 of 14
// (the X comes back as ")(" in the other ten), and the picture of Chinese
// characters came back correctly spaced out from zh-Hans and *empty* from en-US.
// So the script of the answer says which engine should have been asked, and
// that is what the second pass below acts on.
enum class Family { Latin, Cjk };

Family familyOf(const QString &languageTag)
{
    // The tag's own prefix is all this needs: BCP-47 puts the script's language
    // first, so "zh-Hans-CN", "zh-Hant" and "zh" are the same family, and no
    // other pack Windows.Media.Ocr ships starts with these three letters.
    const QString prefix = languageTag.left(2).toLower();
    return prefix == QStringLiteral("zh") || prefix == QStringLiteral("ja")
            || prefix == QStringLiteral("ko")
        ? Family::Cjk
        : Family::Latin;
}

// Whether any character in the answer is one that only a CJK pack writes.
//
// The ranges are the blocks those scripts occupy: CJK and the kana and Hangul
// that share them, the compatibility ideographs, Hangul syllables, and the
// fullwidth punctuation a Chinese answer is sprinkled with. A surrogate pair --
// the rare extension-B ideographs -- is not looked through, so an answer made
// only of those reads as "no CJK here"; that costs one extra engine pass on a
// page almost nobody boxes, and it does not change what comes back.
bool hasCjkCharacter(const QString &text)
{
    for (const QChar character : text) {
        const ushort code = character.unicode();
        if ((code >= 0x2E80 && code <= 0x9FFF) || (code >= 0xAC00 && code <= 0xD7AF)
            || (code >= 0xF900 && code <= 0xFAFF) || (code >= 0xFF00 && code <= 0xFFEF))
            return true;
    }
    return false;
}

// An engine, with the two things about it the second pass needs to know: which
// pack it is, and which family that pack reads.
struct Engine
{
    // `{nullptr}` rather than nothing: a C++/WinRT interface has no default
    // constructor of its own, so a bare member here would make the struct have
    // no default constructor either -- and `return {};` in pickEngine() is the
    // honest way of saying "this machine has no language packs".
    OcrEngine handle{nullptr};
    Family family = Family::Latin;
};

Engine describe(const OcrEngine &engine)
{
    Engine described;
    described.handle = engine;
    const auto language = engine.RecognizerLanguage();
    described.family = familyOf(QString::fromWCharArray(language.LanguageTag().c_str()));
    return described;
}

// The engine, or a null one when this machine has no language packs.
Engine pickEngine()
{
    // The languages the user's Windows is set up with, in the order they set
    // them up in -- which is the only opinion about which script a screenshot
    // is written in that is worth trusting. Falling back to the first installed
    // pack covers the case of a pack that can recognize but is not in the
    // profile's list.
    if (OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages())
        return describe(engine);
    const auto available = OcrEngine::AvailableRecognizerLanguages();
    if (available.Size() > 0)
        if (OcrEngine engine = OcrEngine::TryCreateFromLanguage(available.GetAt(0)))
            return describe(engine);
    return {};
}

// An engine that reads the other family of script, or a null one when this
// machine has only one family installed.
//
// The installed list rather than the profile's, because the profile has already
// had its say: it chose the first engine, and it has no opinion about a second
// one for a picture the first could not read.
OcrEngine engineOf(Family wanted)
{
    const auto available = OcrEngine::AvailableRecognizerLanguages();
    for (unsigned index = 0; index < available.Size(); ++index) {
        const auto language = available.GetAt(index);
        if (familyOf(QString::fromWCharArray(language.LanguageTag().c_str())) != wanted)
            continue;
        if (OcrEngine engine = OcrEngine::TryCreateFromLanguage(language))
            return engine;
    }
    return nullptr;
}

// One reading of one picture: the text recognize() has always reported, and the
// word boxes recognizeLayout() adds. Both come out of the same RecognizeAsync call
// on purpose -- the two callers must not be able to disagree about what was read.
struct Reading
{
    QString text;
    QVector<Ocr::LayoutLine> lines;
};

// One engine, one picture, the words it found -- line by line, not Text(): the
// whole-picture string joins with \r\n, and the lines are also what keeps the
// caller free to reflow them.
//
// `pixelsPerOriginal` undoes the shrinking describeBitmap() does for a picture the
// service would refuse: the boxes OcrWord reports are in the bitmap it was given,
// and a caller placing text on the original picture needs its own coordinates back.
Reading readLines(const OcrEngine &engine, const SoftwareBitmap &bitmap,
                  double pixelsPerOriginal)
{
    const auto recognized = engine.RecognizeAsync(bitmap).get();
    Reading reading;
    QStringList lines;
    for (const auto &line : recognized.Lines()) {
        const QString said = QString::fromWCharArray(line.Text().c_str());
        lines.append(said);

        Ocr::LayoutLine shaped;
        shaped.text = said;
        for (const auto &word : line.Words()) {
            Ocr::LayoutWord one;
            one.text = QString::fromWCharArray(word.Text().c_str());
            const auto rect = word.BoundingRect();
            one.box = QRectF(rect.X * pixelsPerOriginal, rect.Y * pixelsPerOriginal,
                             rect.Width * pixelsPerOriginal, rect.Height * pixelsPerOriginal)
                          .normalized();
            shaped.words.append(one);
            shaped.box |= one.box;
        }
        reading.lines.append(shaped);
    }
    reading.text = lines.join(QLatin1Char('\n'));
    return reading;
}

// The engine's answer, or the other family's when the first says it was the wrong
// tool.
//
// The second pass is asked only of a picture the first engine's own answer says it
// was the wrong tool for. Two shapes say that, and both are in the measurement
// above: it said *nothing* at all, which is what en-US answers for Chinese
// characters, and a CJK engine said something with not one CJK character in it,
// which is what zh-Hans answers for a line of Latin words -- and that answer is the
// one that spells X as ")(".
//
// What is deliberately not here: a search for the best of several engines. One
// extra pass is the whole cost, and on this machine the second engine is the only
// other family there is. A reader whose Windows has one pack installed gets exactly
// the answer this feature gave before, because there is nobody else to ask.
Reading readWithFallback(const Engine &engine, const SoftwareBitmap &bitmap,
                         double pixelsPerOriginal)
{
    Reading reading = readLines(engine.handle, bitmap, pixelsPerOriginal);
    const bool saidNothing = reading.text.trimmed().isEmpty();
    const bool wrongScript = engine.family == Family::Cjk && !hasCjkCharacter(reading.text);
    if (saidNothing || wrongScript) {
        const Family wanted = engine.family == Family::Cjk ? Family::Latin : Family::Cjk;
        if (const OcrEngine other = engineOf(wanted)) {
            const Reading again = readLines(other, bitmap, pixelsPerOriginal);
            // An empty second answer never replaces one that said something.
            //
            // Measured, and the honest half of that: on this machine the trigger
            // above already keeps a picture of Chinese from ever reaching this line,
            // so deleting only this condition reddens no test at all. What it covers
            // is the other shape -- a first engine that guessed something and a
            // second that found nothing.
            if (!again.text.trimmed().isEmpty())
                reading = again;
        }
    }
    return reading;
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

// The service refuses images larger than this outright. Rather than fail on a
// huge crop, bring it down to a size it accepts: the text that survives a halving
// is still text, and a refusal nobody can recover from is the worst answer
// available.
//
// `pixelsPerOriginal` is 1.0 when nothing was changed, and otherwise the factor
// that turns a box in the smaller picture back into the caller's -- the boxes
// OcrWord reports belong to the bitmap the engine was handed, not to the image
// the caller knows.
QImage limitedForService(QImage image, double *pixelsPerOriginal)
{
    const unsigned maximum = OcrEngine::MaxImageDimension();
    if (unsigned(image.width()) <= maximum && unsigned(image.height()) <= maximum)
        return image;
    const QImage scaled = image.scaled(
        QSize(image.width(), image.height()).boundedTo(QSize(int(maximum), int(maximum))),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!scaled.isNull() && scaled.width() > 0)
        *pixelsPerOriginal = double(image.width()) / double(scaled.width());
    return scaled;
}

// Pool threads arrive with no apartment at all, and this is what makes blocking
// .get() legal on one. A thread that already has an apartment throws here; that is
// fine, because it was already the way the work needs it.
void joinApartment()
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error &) {
        // Already initialised on this thread.
    }
}

Ocr::Result recognizeWorker(QImage image)
{
    joinApartment();

    try {
        const Engine engine = pickEngine();
        if (!engine.handle)
            return {false, {},
                    sentence(QT_TRANSLATE_NOOP("Ocr", "这台 Windows 没有安装文字识别的语言包，无法识别文字。"))};

        double pixelsPerOriginal = 1.0;
        image = limitedForService(std::move(image), &pixelsPerOriginal);
        const auto bitmap = toSoftwareBitmap(image);

        // Both engine passes live in readWithFallback(); the note there says which
        // two shapes of answer ask for the second one.
        return {true, readWithFallback(engine, bitmap, pixelsPerOriginal).text, {}};
    } catch (const winrt::hresult_error &error) {
        return {false, {},
                sentence(QT_TRANSLATE_NOOP("Ocr", "识别时出错：%1")).arg(QString::fromWCharArray(error.message().c_str()))};
    } catch (const std::exception &error) {
        return {false, {}, sentence(QT_TRANSLATE_NOOP("Ocr", "识别时出错：%1")).arg(QString::fromLocal8Bit(error.what()))};
    } catch (...) {
        // Anything else, rather than letting it leave the pool thread. QtConcurrent
        // would wrap it as a QUnhandledException and the tab's finished handler would
        // rethrow it on the UI thread, where nothing catches it -- the process dies
        // for a picture the engine could not read (handoff-02 F4 caught that shape
        // with a debugger: QUnhandledException, what=[Unknown exception]).
        return {false, {}, sentence(QT_TRANSLATE_NOOP("Ocr", "识别时出错了。"))};
    }
}

Ocr::LayoutResult recognizeLayoutWorker(QImage image)
{
    joinApartment();

    try {
        const Engine engine = pickEngine();
        if (!engine.handle)
            return {false, {},
                    sentence(QT_TRANSLATE_NOOP("Ocr", "这台 Windows 没有安装文字识别的语言包，无法识别文字。"))};

        double pixelsPerOriginal = 1.0;
        image = limitedForService(std::move(image), &pixelsPerOriginal);
        const auto bitmap = toSoftwareBitmap(image);

        Ocr::LayoutResult result;
        result.ok = true;
        result.lines = readWithFallback(engine, bitmap, pixelsPerOriginal).lines;
        return result;
    } catch (const winrt::hresult_error &error) {
        return {false, {},
                sentence(QT_TRANSLATE_NOOP("Ocr", "识别时出错：%1")).arg(QString::fromWCharArray(error.message().c_str()))};
    } catch (const std::exception &error) {
        return {false, {}, sentence(QT_TRANSLATE_NOOP("Ocr", "识别时出错：%1")).arg(QString::fromLocal8Bit(error.what()))};
    } catch (...) {
        return {false, {}, sentence(QT_TRANSLATE_NOOP("Ocr", "识别时出错了。"))};
    }
}

} // namespace

bool Ocr::isAvailable()
{
    return onMtaThread([] {
        try {
            return static_cast<bool>(pickEngine().handle);
        } catch (...) {
            return false;
        }
    });
}

Ocr::Result Ocr::recognize(const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return {false, {}, sentence(QT_TRANSLATE_NOOP("Ocr", "框选的区域里没有可以识别的内容。"))};

    // Moved in, so the caller's copy is untouched and no second full image is
    // made. QImage is explicitly shared across threads; the detach happens
    // here, in this thread, before the worker takes over.
    QImage copy = image;
    return onMtaThread([copy = std::move(copy)]() mutable {
        return recognizeWorker(std::move(copy));
    });
}

Ocr::LayoutResult Ocr::recognizeLayout(const QImage &image)
{
    // A sentence of its own rather than recognize()'s: the caller here is a page,
    // not a box the reader dragged, and telling them the dragged region was empty
    // would name something they never did.
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return {false, {}, sentence(QT_TRANSLATE_NOOP("Ocr", "这一页没有可以识别的内容。"))};

    QImage copy = image;
    return onMtaThread([copy = std::move(copy)]() mutable {
        return recognizeLayoutWorker(std::move(copy));
    });
}
