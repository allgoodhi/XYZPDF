// The answer to "what do these selected words mean".
//
// The same shape as OcrDialog on purpose: a small, non-modal window that is
// shown while the work is happening and filled in when it comes back, so the
// document underneath stays readable and the reader can move the result aside
// and keep reading. It holds both halves of the answer -- the words that were
// asked about and their translation -- because a translation with nothing to
// compare it against is hard to trust at a glance.
#pragma once

#include "../translate/translator.h"

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;

class TranslateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TranslateDialog(QWidget *parent = nullptr);

    // While the request is in flight. Copy is off: there is nothing to copy yet.
    void setPending(const QString &source);

    // The answer, whatever it is: a translation, an honest failure, or the
    // service's silence on an empty selection.
    void setResult(const Translate::Result &result);

private:
    QLabel *status = nullptr;
    QPlainTextEdit *source = nullptr;
    QPlainTextEdit *translation = nullptr;
    QPushButton *copyButton = nullptr;
};
