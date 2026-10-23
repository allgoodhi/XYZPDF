// Which recognizer, handed which picture, reads this machine's text correctly.
//
// ---- why this is a probe and not a test ----
//
// Every line it prints is another library's answer, and on this machine that
// answer is the open question: since F7 put PDFium behind the renderer, the
// Windows recognizer reads text.pdf's first line as ")(YZPDF one" rather than
// "XYZPDF one" (docs/store-submission.md sec.116). The main session had already
// ruled out the two easy suspects -- the render is clean and a white margin
// around the crop does not help -- so what is left is the pair this probe can
// separate: WHICH engine gets the picture, and WHAT the picture looks like when
// it arrives. `pickEngine()` takes the user's own language list first, which on
// a Chinese Windows means a zh-CN engine reading Latin letters.
//
// So: one crop, produced the way the product produces it, handed to every
// recognizer installed here, after each of several preprocessings. The table is
// printed; it is not asserted. Which cell reads the line correctly is what the
// product change is then made to match -- choosing the fix before measuring it
// is how this row came to be a loosened assertion instead of a fixed one.
//
// Run it from the xyzpdf directory:
//
//     build-local-release/tests/xyzpdf_ocrprobe.exe [fixture] [输出目录]
//
// It prints a UTF-8 table and writes the pictures it recognized into that
// directory (default build-local-release/ocr-probe-out, which is inside the
// ignored build tree rather than beside the sources), because the numbers alone
// do not show what a misread glyph looked like at the pixel level and the
// pictures do.
//
// Like the other probes (encryptprobe, outlineprobe, watermarkprobe) its name
// keeps tools/build_all_tests.py from running it: it prints numbers rather than
// passing or failing, and it spends tens of seconds in a system service.

#include "../src/document/document.h"
#include "../src/document/documentfactory.h"
#include "../src/document/pagemetrics.h"
#include "../src/ocr/ocrengine.h"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.UserProfile.h>

#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using winrt::Windows::Globalization::Language;
using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
using winrt::Windows::Graphics::Imaging::BitmapDecoder;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::Ocr::OcrEngine;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::InMemoryRandomAccessStream;

// The two lines being read. The first is the fixture's, and the shipped misread
// is about it; the second is what a Chinese page looks like, and it is there so
// that a change made for Latin letters is seen to cost something or nothing on
// the other script. Both are compared against the whole line, not a piece of it,
// because "almost right" is what ")(" is -- but the Chinese comparison ignores
// the spaces a CJK pack puts between its characters, which is that pack
// reporting correctly (see squeezed()).
const wchar_t *kExpectedLatin = L"XYZPDF one";
const wchar_t *kExpectedChinese = L"中文识别测试";

// The box tests/documenttab_smoke.cpp drags around that line, kept as fractions
// of the page in both files so that they cannot drift apart into two different
// boxes -- the viewport rect maps onto the whole page, so a fraction of one is
// the same fraction of the other.
constexpr double kBoxLeft = 0.05;
constexpr double kBoxTop = 0.05;
constexpr double kBoxRight = 0.45;
constexpr double kBoxBottom = 0.13;

// The three constants from src/services/ocrsession.cpp, copied rather than
// included because they live in that file's anonymous namespace. The "shipped"
// row is built with them, so if one of them moves the probe has to move too --
// and the row that stops matching the product is the notice.
constexpr double kOcrTargetPixels = 1600.0;
constexpr qint64 kOcrMaximumPagePixels = 4LL * 1024 * 1024;
constexpr int kOcrMinimumCropPixels = 480;

QString simplify(const QString &text)
{
    return QString(text).simplified();
}

// Every character of the answer, with the whitespace taken out.
//
// A CJK engine writes one character per word and puts a space between them: the
// zh-Hans pack answers "中 文 识 别 测 试" for a picture of 中文识别测试. The
// characters are the reading; the spaces are how that pack reports them, so for
// the Chinese row they are not a mistake to count.
QString squeezed(const QString &text)
{
    QString out;
    for (const QChar character : text)
        if (!character.isSpace())
            out.append(character);
    return out;
}

bool isExact(const QString &text, const wchar_t *expected)
{
    return simplify(text) == QString::fromStdWString(expected);
}

bool isExactIgnoringSpaces(const QString &text, const wchar_t *expected)
{
    return squeezed(text) == QString::fromStdWString(expected);
}

// ---- the recognition, one engine at a time ----
//
// Deliberately NOT Ocr::recognize: that one picks the engine itself, so calling
// it cannot answer "which engine". This mirrors what it does downstream -- the
// same PNG round trip into a SoftwareBitmap, the same line join -- with the
// language pinned by the caller. The last row of the Latin table calls the real
// Ocr::recognize on the same bytes, which is the receipt that this file's route
// and the product's route give the same answer.

struct Answer
{
    bool ok = false;
    QString text;
    QString error;
};

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
    writer.WriteBytes(winrt::array_view<const uint8_t>(first, first + png.size()));
    writer.StoreAsync().get();
    writer.FlushAsync().get();
    writer.DetachStream();
    stream.Seek(0);

    return BitmapDecoder::CreateAsync(stream)
        .get()
        .GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied)
        .get();
}

// Runs one recognition on its own thread and waits.
//
// The WinRT calls block, and blocking on a thread that has not chosen an
// apartment is the shape that bites "sometimes"; a fresh MTA thread per call
// costs a millisecond here, and the product does the same on a pool of its own.
Answer recognizeWith(const QString &languageTag, const QImage &image)
{
    Answer answer;
    std::thread worker([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error &) {
            // Already has one.
        }
        try {
            const OcrEngine engine = OcrEngine::TryCreateFromLanguage(Language(languageTag.toStdWString()));
            if (!engine) {
                answer.error = QStringLiteral("no engine installed for this language");
                return;
            }
            const auto recognized = engine.RecognizeAsync(toSoftwareBitmap(image)).get();
            QStringList lines;
            for (const auto &line : recognized.Lines())
                lines.append(QString::fromWCharArray(line.Text().c_str()));
            answer.ok = true;
            answer.text = lines.join(QLatin1Char('\n'));
        } catch (const winrt::hresult_error &error) {
            answer.error = QString::fromWCharArray(error.message().c_str());
        } catch (const std::exception &error) {
            answer.error = QString::fromLocal8Bit(error.what());
        }
    });
    worker.join();
    return answer;
}

// ---- the pictures ----

// The crop, rendered the way OcrSession::captureRegion renders it: the box is
// in the page's own points, and the pixel grid it is cut from is the whole page
// at `scale`.
QImage renderCrop(Document &document, int page, double scale, bool lcd)
{
    const QSizeF pagePoints = document.pageSize(page);
    const QRectF box(pagePoints.width() * kBoxLeft, pagePoints.height() * kBoxTop,
                     pagePoints.width() * (kBoxRight - kBoxLeft),
                     pagePoints.height() * (kBoxBottom - kBoxTop));
    const QSize full = PageMetrics::pixelsFor(pagePoints, scale, 1.0);
    const QRect crop(QRect(int(std::floor(box.left() * scale)), int(std::floor(box.top() * scale)),
                           int(std::ceil(box.width() * scale)), int(std::ceil(box.height() * scale)))
                         .intersected(QRect(QPoint(0, 0), full)));

    RenderOptions options;
    options.rotationDegrees = 0;
    options.annotations = true;
    options.optimizedForLcd = lcd;
    options.clip = crop;
    options.scaledSize = full;
    return document.render(page, crop.size(), options);
}

// Onto white, exactly as src/services/ocrsession.cpp does it, and with the same
// small-crop doubling. This is "the picture the product actually sends".
QImage asShipped(const QImage &crop)
{
    QImage input(crop.size(), QImage::Format_RGB32);
    input.fill(Qt::white);
    {
        QPainter painter(&input);
        painter.drawImage(0, 0, crop);
    }
    const int longerSide = qMax(input.width(), input.height());
    if (longerSide > 0 && longerSide < kOcrMinimumCropPixels) {
        input = input.scaled(longerSide * 2, longerSide * 2, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    return input;
}

QImage scaledBy(const QImage &image, int factor, bool smooth)
{
    return image.scaled(image.width() * factor, image.height() * factor, Qt::KeepAspectRatio,
                        smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
}

// Black or white, nothing in between. `midpoint` splits at the middle of the
// luminance the picture actually uses; the fixed one splits at 160, which is
// where a lightly antialiased stroke stops counting as a stroke.
//
// Pixel by pixel rather than over constBits(): a Grayscale8 QImage's buffer
// layout is Qt's business, and these pictures are a few hundred thousand pixels.
QImage binarize(const QImage &source, bool midpoint)
{
    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    int low = 255;
    int high = 0;
    for (int y = 0; y < gray.height(); ++y) {
        for (int x = 0; x < gray.width(); ++x) {
            const int value = qGray(gray.pixel(x, y));
            low = qMin(low, value);
            high = qMax(high, value);
        }
    }
    const int threshold = midpoint ? (low + high) / 2 : 160;
    QImage out(gray.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < gray.height(); ++y) {
        for (int x = 0; x < gray.width(); ++x)
            out.setPixel(x, y, qGray(gray.pixel(x, y)) <= threshold ? 0 : 255);
    }
    return out;
}

QImage pad(const QImage &image, int percent)
{
    const int marginX = qMax(4, image.width() * percent / 100);
    const int marginY = qMax(4, image.height() * percent / 100);
    QImage out(image.size() + QSize(marginX * 2, marginY * 2), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.drawImage(marginX, marginY, image);
    return out;
}

// The Chinese sample, drawn with the system's own font rather than read out of a
// PDF: the question this row answers is whether a change made for Latin letters
// costs the other script, and that is a question about the engine, not about the
// renderer.
//
// It reports what it actually drew, because drawing is not a given: under the
// offscreen platform the font database this program's tests run against came back
// with no family at all, and six characters of 150 px became six hollow boxes --
// 3,480 dark pixels rather than the tens of thousands real glyphs cover. That
// reads to an OCR engine as nothing, and a test that did not look would have
// reported it as the engine losing Chinese.
QImage chineseImage(int &ink, QStringList &notes)
{
    QImage image(900, 260, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::black);
        QFont font = painter.font();
        font.setPixelSize(150);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, QString::fromStdWString(kExpectedChinese));
        const QString family = QFontInfo(painter.font()).family();
        notes = {QStringLiteral("application font: %1").arg(QGuiApplication::font().family()),
                 QStringLiteral("resolved to: %1").arg(family.isEmpty() ? QStringLiteral("(nothing)") : family),
                 QStringLiteral("font families known to Qt: %1").arg(QFontDatabase::families().size())};
    }
    ink = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 128)
                ++ink;
    return image;
}

struct Variant
{
    const char *name;
    QImage image;
};

// One line to the terminal and one into the report. The terminal goes through
// wprintf because a UTF-8 byte stream on a GBK console is how a measurement of
// Chinese comes back as mojibake; the file is written as UTF-8 at the end, from
// the same list, so the two cannot disagree about what was printed.
QStringList lines;

void report(const QString &line)
{
    std::fflush(stdout);
    const std::wstring wide = line.toStdWString();
    std::wprintf(L"%s\n", wide.c_str());
    std::fflush(stdout);
    lines.append(line);
}

QString verdict(bool exact)
{
    return exact ? QStringLiteral("   OK") : QStringLiteral("  FAIL");
}

// A table cell: the flag, the engine, the preprocessing, and what came back.
QString cell(const QString &flag, const QString &engine, const QString &variant, const QString &text,
             bool isError)
{
    return QStringLiteral("%1  %2  %3  [%4]%5")
        .arg(flag)
        .arg(engine.leftJustified(10))
        .arg(variant.leftJustified(22))
        .arg(simplify(text).replace(QLatin1Char('\n'), QStringLiteral(" / ")))
        .arg(isError ? QStringLiteral("  (error)") : QString());
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);

    const QString fixture = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("tests/fixtures/text.pdf");
    const QString outputDirectory = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                             : QStringLiteral("build-local-release/ocr-probe-out");
    QDir().mkpath(outputDirectory);

    // ---- what this machine offers ----
    report(QStringLiteral("== the machine =="));
    report(QStringLiteral("product Ocr::isAvailable(): %1")
               .arg(Ocr::isAvailable() ? QStringLiteral("yes") : QStringLiteral("no")));
    QStringList profile;
    QStringList installed;
    QString pickedToday;
    unsigned maximumDimension = 0;
    {
        std::thread reader([&] {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            } catch (const winrt::hresult_error &) {
            }
            for (const auto &tag :
                 winrt::Windows::System::UserProfile::GlobalizationPreferences::Languages())
                profile.append(QString::fromWCharArray(tag.c_str()));
            for (const auto &language : OcrEngine::AvailableRecognizerLanguages())
                installed.append(QString::fromWCharArray(language.LanguageTag().c_str()));
            // Which engine the product's own choice actually lands on: the profile
            // list is the user's, but the first entry of it that has a recognizer
            // is the one that gets the picture.
            if (const OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages())
                pickedToday = QString::fromWCharArray(engine.RecognizerLanguage().LanguageTag().c_str());
            maximumDimension = OcrEngine::MaxImageDimension();
        });
        reader.join();
    }
    report(QStringLiteral("user profile languages: %1")
               .arg(profile.isEmpty() ? QStringLiteral("(none)") : profile.join(QStringLiteral(", "))));
    report(QStringLiteral("installed recognizer languages: %1")
               .arg(installed.isEmpty() ? QStringLiteral("(none)") : installed.join(QStringLiteral(", "))));
    report(QStringLiteral("the engine pickEngine() uses today: %1")
               .arg(pickedToday.isEmpty() ? QStringLiteral("(none)") : pickedToday));
    report(QStringLiteral("MaxImageDimension: %1").arg(maximumDimension));

    // ---- the pictures ----
    std::unique_ptr<Document> document = createDocument();
    if (document->load(fixture, {}) != Document::Status::Ready) {
        report(QStringLiteral("the fixture did not open: %1 (%2)")
                   .arg(fixture, document->lastError()));
        return 2;
    }
    const QSizeF pagePoints = document->pageSize(0);
    const double boxWidth = pagePoints.width() * (kBoxRight - kBoxLeft);
    const double uncapped = kOcrTargetPixels / boxWidth;
    const double pageCap = std::sqrt(double(kOcrMaximumPagePixels))
        / std::sqrt(double(pagePoints.width()) * double(pagePoints.height()));
    const double shippedScale = qBound(1.0, uncapped, pageCap);
    report(QStringLiteral("\n== the pictures =="));
    report(QStringLiteral("page 0 is %1 x %2 points; the box is %3 points wide")
               .arg(pagePoints.width())
               .arg(pagePoints.height())
               .arg(boxWidth, 0, 'f', 2));
    report(QStringLiteral("scale: wanted %1, the page-pixel cap is %2, the product uses %3")
               .arg(uncapped, 0, 'f', 3)
               .arg(pageCap, 0, 'f', 3)
               .arg(shippedScale, 0, 'f', 3));

    const QImage shippedCrop = renderCrop(*document, 0, shippedScale, false);
    const QImage sharpCrop = renderCrop(*document, 0, uncapped, false);
    const QImage lcdCrop = renderCrop(*document, 0, uncapped, true);
    const QImage smallCrop = renderCrop(*document, 0, 1.68, false);
    if (shippedCrop.isNull() || sharpCrop.isNull()) {
        report(QStringLiteral("the crop did not render"));
        return 2;
    }

    const QImage shipped = asShipped(shippedCrop);
    const QImage sharp = asShipped(sharpCrop);
    shipped.save(outputDirectory + QStringLiteral("/01-shipped.png"));
    sharp.save(outputDirectory + QStringLiteral("/02-uncapped.png"));
    asShipped(lcdCrop).save(outputDirectory + QStringLiteral("/03-uncapped-lcd.png"));
    asShipped(smallCrop).save(outputDirectory + QStringLiteral("/04-small.png"));
    binarize(sharp, false).save(outputDirectory + QStringLiteral("/05-uncapped-mono.png"));
    int chineseInk = 0;
    QStringList chineseFontNotes;
    const QImage chinese = chineseImage(chineseInk, chineseFontNotes);
    for (const QString &note : chineseFontNotes)
        report(note);
    report(QStringLiteral("the Chinese picture covers %1 dark pixels").arg(chineseInk));
    chinese.save(outputDirectory + QStringLiteral("/10-chinese.png"));

    report(QStringLiteral("shipped %1x%2, uncapped %3x%4, uncapped+lcd %5x%6, small %7x%8")
               .arg(shippedCrop.width()).arg(shippedCrop.height())
               .arg(sharpCrop.width()).arg(sharpCrop.height())
               .arg(lcdCrop.width()).arg(lcdCrop.height())
               .arg(smallCrop.width()).arg(smallCrop.height()));

    std::vector<Variant> variants;
    variants.push_back({"shipped", shipped});
    variants.push_back({"shipped-gray", shipped.convertToFormat(QImage::Format_Grayscale8)});
    variants.push_back({"shipped-mid-mono", binarize(shipped, true)});
    variants.push_back({"shipped-160-mono", binarize(shipped, false)});
    variants.push_back({"shipped-x2-smooth", scaledBy(shipped, 2, true)});
    variants.push_back({"shipped-x2-nearest", scaledBy(shipped, 2, false)});
    variants.push_back({"shipped-x4-nearest", scaledBy(shipped, 4, false)});
    variants.push_back({"shipped-pad", pad(shipped, 8)});
    variants.push_back({"uncapped", sharp});
    variants.push_back({"uncapped-gray", sharp.convertToFormat(QImage::Format_Grayscale8)});
    variants.push_back({"uncapped-160-mono", binarize(sharp, false)});
    variants.push_back({"uncapped-x2-nearest", scaledBy(sharp, 2, false)});
    variants.push_back({"uncapped-lcd", asShipped(lcdCrop)});
    variants.push_back({"small", asShipped(smallCrop)});

    // ---- the Latin table ----
    //
    // The installed engines, each pinned by tag. The product's own choice is not
    // repeated here as a "(profile)" row: the `Ocr::recognize` line at the
    // bottom of each table IS that route, and it is the one that has to read
    // correctly for the tests to assert what they assert.
    QStringList engines = installed;

    report(QStringLiteral("\n== latin: \"%1\" ==").arg(QString::fromStdWString(kExpectedLatin)));
    int hits = 0;
    const int cells = engines.size() * int(variants.size());
    for (const QString &engine : engines) {
        for (const Variant &variant : variants) {
            const Answer answer = recognizeWith(engine, variant.image);
            const bool exact = answer.ok && isExact(answer.text, kExpectedLatin);
            if (exact)
                ++hits;
            report(cell(verdict(exact), engine, QString::fromLatin1(variant.name),
                        answer.ok ? answer.text : answer.error,
                        !answer.ok));
        }
    }
    // The product's own route, on the same bytes: a row above that says OK is a
    // row the product could have had, and this is the line that says whether it
    // does or does not today.
    {
        const Ocr::Result throughProduct = Ocr::recognize(shipped);
        report(cell(verdict(throughProduct.ok && isExact(throughProduct.text, kExpectedLatin)),
                    QStringLiteral("Ocr::recog"),
                    QStringLiteral("shipped"),
                    throughProduct.ok ? throughProduct.text : throughProduct.error,
                    !throughProduct.ok));
    }
    report(QStringLiteral("latin: %1 of %2 cells read the line exactly").arg(hits).arg(cells));

    // ---- and the other script ----
    report(QStringLiteral("\n== chinese: \"%1\" ==").arg(QString::fromStdWString(kExpectedChinese)));
    for (const QString &engine : engines) {
        const Answer answer = recognizeWith(engine, chinese);
        const bool exact = answer.ok && isExactIgnoringSpaces(answer.text, kExpectedChinese);
        report(cell(verdict(exact), engine, QStringLiteral("as-drawn"),
                    answer.ok ? answer.text : answer.error, !answer.ok));
    }
    {
        const Ocr::Result throughProduct = Ocr::recognize(chinese);
        report(cell(verdict(throughProduct.ok && isExactIgnoringSpaces(throughProduct.text, kExpectedChinese)),
                    QStringLiteral("Ocr::recog"),
                    QStringLiteral("as-drawn"),
                    throughProduct.ok ? throughProduct.text : throughProduct.error,
                    !throughProduct.ok));
    }

    {
        QFile handle(outputDirectory + QStringLiteral("/report.txt"));
        if (handle.open(QIODevice::WriteOnly | QIODevice::Truncate))
            handle.write(lines.join(QLatin1Char('\n')).toUtf8());
    }
    const std::wstring wide = outputDirectory.toStdWString();
    std::wprintf(L"\nreport and pictures are in %s\n", wide.c_str());
    return 0;
}
