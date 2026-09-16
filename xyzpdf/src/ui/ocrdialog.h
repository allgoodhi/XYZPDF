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

private:
    QLabel *status = nullptr;
    QPlainTextEdit *text = nullptr;
    QPushButton *copyButton = nullptr;
};
