// Text recognition, from the one engine a Windows program already ships with.
//
// ---- why Windows.Media.Ocr ----
//
// The feature is "drag a box around a picture and read the text in it". That
// needs an OCR engine, and the choices are Tesseract (a hundred megabytes of
// models and DLLs to carry, version and licence and all) or the one Windows
// itself provides: Windows.Media.Ocr, an offline WinRT service that has been in
// every Windows 10 and 11 install since 2015 and reads whatever scripts the
// user's language packs cover. Choosing it means the feature costs no download,
// no installation step and no network -- and that on a machine with no language
// pack the feature says so honestly instead of failing somewhere inside a
// library this program would then have to debug.
//
// It is a system service, not a linked library: nothing about it goes into the
// third-party notices, because nothing about it is redistributed.
//
// ---- threading ----
//
// recognize() and isAvailable() may be called from any thread, including the
// GUI one. They do their WinRT work on a global-pool thread and block the
// caller for the duration -- see onMtaThread() for why the work is not simply
// done on the calling thread.
#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

class QImage;

namespace Ocr {

struct Result
{
    // False means nothing usable came out, and error says why -- as a sentence
    // to show the user, already translated.
    bool ok = false;
    // What was read, one line of the image per line here. Empty when ok is
    // false, and empty-when-ok means the picture genuinely had no text in it.
    QString text;
    QString error;
};

// Whether this machine can recognize text at all: the service needs at least
// one installed language pack, and a Windows that has none reports itself here
// rather than failing on first use.
bool isAvailable();

// Reads the text in an image. Blocking; off the calling thread is where the
// caller is expected to put it, and DocumentTab does.
//
// The image is copied, so the caller's copy is theirs to change or drop while
// this runs.
//
// Give it *opaque* pixels. The recognizer composites nothing: a transparent
// pixel is black to it, so a page rendered straight out of a PDF -- which is
// ink over transparent black, because a PDF page has no background of its own
// -- arrives as a black rectangle with black text on it and comes back empty.
// See the flattening in OcrSession::captureRegion(), which is what the first
// version of this feature was missing.
Result recognize(const QImage &image);

// One word, where the recognizer found it.
struct LayoutWord
{
    QString text;
    // In the pixels of the image that was handed to recognizeLayout(), top-left
    // origin -- also when the service made the picture smaller than the one it was
    // given (see the size limit in recognizeWorker): the box is scaled back, because
    // a caller drawing something at the word's place needs the caller's coordinates.
    QRectF box;
};

struct LayoutLine
{
    QString text;
    QVector<LayoutWord> words;
    QRectF box;
};

// The same recognition, with the place of every word.
//
// This is what a searchable PDF is made of: the invisible text an OCR program lays
// over a scan has to sit where the ink is, or selecting it drags a highlight across
// the wrong part of the page and copying it reads in an order nobody wrote.
// Windows.Media.Ocr hands out OcrLine and OcrWord bounding rectangles, so the
// position is the service's own answer rather than a guess made from pixels.
//
// The engine choice is recognize()'s, including the second pass for a picture the
// first engine's answer says was the wrong script -- the two calls share the code
// that makes it, so they cannot drift apart on which pack reads what.
//
// Blocking, and the same expectations as recognize(): an image, a copied QImage,
// opaque pixels.
struct LayoutResult
{
    bool ok = false;
    QVector<LayoutLine> lines;
    QString error;
};

LayoutResult recognizeLayout(const QImage &image);

} // namespace Ocr
