// The recognition engine, against text the test drew itself.
//
// ---- what a pass means, and what it costs ----
//
// The engine is a Windows service, not code in this repository, so what this
// test can assert is that the chain works on the machine it runs on: an image
// with known words in it comes back with those words in it. The two failure
// modes that are about the machine rather than the code -- no OCR language
// packs installed, no service -- are reported as a skip: a pass would claim the
// feature works here when it cannot have been exercised, and a fail would send
// somebody to debug a machine from a test report.
//
// It runs real recognition, so it is a second or two, not a millisecond.
//
// It asks in two scripts now, not one: digits for the plumbing, and a line of
// Chinese for the choice the engine makes between the packs this machine has.
// That choice is the whole of the accuracy fix, and a test that only ever asked
// in Latin would not notice it being made the wrong way round.

#include "../src/ocr/ocrengine.h"

#include <QColor>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QString>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

// The picture the recognition is asked to read: black digits on white paper,
// drawn the way a screenshot draws them. Digits because they are the one thing
// every language pack reads correctly, and this test is about the plumbing
// rather than about any particular script.
QImage sampleImage()
{
    QImage image(640, 240, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPixelSize(140);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("2026"));
    painter.end();
    return image;
}

// The same question in the other script, and this row is a guard rather than a
// curiosity.
//
// What makes this machine read text.pdf's "XYZPDF one" correctly is a second
// engine being asked when the first one's answer says it was the wrong script
// for the picture (src/ocr/ocrengine.cpp). Measured in tests/ocrprobe.cpp: the
// en-US pack reads that line exactly in all fourteen pictures it was handed,
// the zh-Hans pack -- which is what this machine's own language list puts
// first -- in four of them. So a fix that only ever preferred the Latin pack is
// one row away from working here and quietly returning *nothing* for a page of
// Chinese, which is exactly what en-US answers for the picture drawn below.
// This row is the one that goes red if that happens.
//
// A CJK pack answers one character per word with a space between them --
// "中 文 识 别 测 试" -- and the characters, in order, are the reading. The
// spaces are how that pack reports and are not counted as a mistake.
QImage chineseSample()
{
    QImage image(900, 260, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::black);
        QFont font = painter.font();
        font.setPixelSize(150);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("中文识别测试"));
        std::cout << "MEASURE chinese sample drawn with: ["
                  << QFontInfo(painter.font()).family().toStdString() << "]\n";
    }
    int ink = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 128)
                ++ink;
    std::cout << "MEASURE chinese sample ink: " << ink << " dark pixels\n";
    // The number the drawing really has to be checked against, because there is
    // a way to draw nothing and still run: the six characters at 150 px cover
    // roughly forty thousand dark pixels, and the same call on a Qt with no font
    // database -- which is what the offscreen platform is, measured at
    // "font families known to Qt: 0" -- covers 3,480, the outlines of six hollow
    // boxes. An OCR engine reads the second one as an empty picture, and a row
    // that only looked at the answer would have called that the engine losing
    // Chinese.
    if (ink < 10000) {
        std::cout << "SKIP the picture came out with no glyphs in it; the Chinese row cannot run\n";
        return QImage();
    }
    return image;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);

    if (!Ocr::isAvailable()) {
        std::cout << "no OCR language packs on this machine; the recognition test is a skip\n";
        return 0;
    }

    // The whole chain: image in, words out.
    const Ocr::Result result = Ocr::recognize(sampleImage());
    check(result.ok, QStringLiteral("the sample is recognized (error: %1)").arg(result.error));
    check(result.text.contains(QStringLiteral("2026")),
          QStringLiteral("the digits it drew are what comes back (got \"%1\")").arg(result.text));

    // The same chain, asked of the other script, and the reason this file draws
    // a second picture at all. See chineseSample() for what it guards.
    const QImage chinese = chineseSample();
    if (chinese.isNull()) {
        std::cout << "    (the Chinese row is a skip, and says why above)\n";
    } else {
        const Ocr::Result chineseResult = Ocr::recognize(chinese);
        check(chineseResult.ok,
              QStringLiteral("the Chinese picture is recognized (error: %1)").arg(chineseResult.error));
        QString characters;
        for (const QChar character : chineseResult.text)
            if (!character.isSpace())
                characters.append(character);
        std::cout << "MEASURE chinese answer: [" << chineseResult.text.toStdString() << "]\n";
        check(characters == QStringLiteral("中文识别测试"),
              QStringLiteral("and every character of it comes back, in order (got \"%1\")")
                  .arg(QString(chineseResult.text).simplified()));
    }

    // An image with nothing in it is an honest "nothing", not an error and not
    // a crash: the dialog has to be able to tell the two apart.
    QImage blank(300, 300, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    const Ocr::Result nothing = Ocr::recognize(blank);
    check(nothing.ok, "a blank picture is recognized successfully");
    check(nothing.text.isEmpty(), "and reports no text in it");

    // Nonsense in, a sentence out: a null image cannot be recognized, and the
    // answer to trying is a message rather than an empty success.
    const Ocr::Result nonsense = Ocr::recognize(QImage());
    check(!nonsense.ok, "a null image is refused");
    check(!nonsense.error.isEmpty(), "and refused with a sentence");

    if (failures == 0) {
        std::cout << "the engine reads what is in front of it\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
