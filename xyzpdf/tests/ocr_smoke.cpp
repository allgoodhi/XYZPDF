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

#include "../src/ocr/ocrengine.h"

#include <QColor>
#include <QFont>
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
