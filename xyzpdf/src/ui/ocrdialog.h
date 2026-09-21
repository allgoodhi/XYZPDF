// The result of a box the user dragged over a picture.
//
// A small window on purpose, and non-modal: recognition can take a second, and
// the reading the user was doing should not be interrupted by anything more
// than a window they can move aside. It is created when the drag ends (showing
// what is happening), and filled in when the answer arrives -- one window per
// drag, because two drags are two questions.
#pragma once

// Relative, like every other include from inside src/: quoted includes resolve
// against this file's own directory first, and the project root is not on the
// include path.
#include "../ocr/ocrengine.h"

#include <QDialog>
#include <QImage>

class QLabel;
class QPlainTextEdit;
class QPushButton;

class OcrDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OcrDialog(QWidget *parent = nullptr);

    // While the crop is being recognized. Copy is off: there is nothing to
    // copy yet, and a button that copies nothing is a lie.
    void setPending();

    // The answer, whatever it is: text, an honest "nothing there", or the
    // engine's sentence about what went wrong.
    void setResult(const Ocr::Result &result);

    // The box the reader dragged, as pixels. Offered beside the words because
    // they are two different asks: a figure, a chart or a foreign stamp is often
    // wanted as a picture to paste somewhere even when it holds no readable
    // text at all -- so this stays available when setResult reports nothing.
    //
    // `matchScreen` is whether the reader is reading at night. The renderer's
    // image carries no paper -- the white is put down by whoever shows it -- so
    // what gets stored here is the crop laid over paper and, at night, inverted:
    // the same picture the reader is looking at rather than its complement.
    void setCropImage(const QImage &rendered, bool matchScreen);

Q_SIGNALS:
    // The reader asked to translate what was just recognized. The dialog has no
    // business with a translation service -- it hands the text out and the tab
    // that owns it does the asking, the same way the page view's menu does.
    void translateRequested(const QString &text);

private:
    QLabel *status = nullptr;
    QPlainTextEdit *text = nullptr;
    QPushButton *copyButton = nullptr;
    QPushButton *copyImageButton = nullptr;
    QPushButton *translateButton = nullptr;
    QImage crop;
};
