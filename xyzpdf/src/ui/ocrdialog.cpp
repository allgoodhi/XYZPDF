#include "ocrdialog.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

OcrDialog::OcrDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("文字识别"));
    // Not application-modal: the document underneath stays readable while the
    // recognition runs, which is the whole point of doing it off the UI thread.
    setModal(false);
    resize(440, 340);

    auto *layout = new QVBoxLayout(this);

    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);

    text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    // A placeholder for the empty case, which is a real outcome (a picture
    // with no words in it) rather than an error.
    text->setPlaceholderText(tr("识别出的文字会出现在这里。"));
    layout->addWidget(text, 1);

    auto *buttons = new QHBoxLayout;
    copyButton = new QPushButton(tr("复制文字"), this);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(text->toPlainText());
    });
    buttons->addWidget(copyButton);
    // The region itself, as a picture. Enabled by setCropImage rather than by
    // the recognition result -- a chart or a stamp with nothing to read off it
    // is still the thing the reader wanted to grab.
    copyImageButton = new QPushButton(tr("复制图片"), this);
    copyImageButton->setObjectName(QStringLiteral("copyImageButton"));
    copyImageButton->setEnabled(false);
    connect(copyImageButton, &QPushButton::clicked, this, [this] {
        if (crop.isNull())
            return;
        QGuiApplication::clipboard()->setPixmap(QPixmap::fromImage(crop));
    });
    buttons->addWidget(copyImageButton);
    // Translate the recognized words -- the reason a scan or a picture can be
    // read into another language, not just into text. Emitted rather than done
    // here: this window knows nothing about services or the network.
    translateButton = new QPushButton(tr("翻译"), this);
    connect(translateButton, &QPushButton::clicked, this, [this] {
        Q_EMIT translateRequested(text->toPlainText());
    });
    buttons->addWidget(translateButton);
    buttons->addStretch(1);
    auto *closeButton = new QPushButton(tr("关闭"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    setPending();
}

void OcrDialog::setCropImage(const QImage &rendered, bool matchScreen)
{
    if (rendered.isNull()) {
        crop = QImage();
        copyImageButton->setEnabled(false);
        return;
    }

    // The paper first. What comes back from the renderer is ink over
    // transparency -- the white of a page is put down by whoever shows the
    // result -- and copying that straight would paste a cut-out whose background
    // is whatever the reader pasted into, which is not the picture they looked
    // at and chose.
    QImage paper(rendered.size(), QImage::Format_RGB32);
    paper.fill(Qt::white);
    QPainter onPaper(&paper);
    onPaper.drawImage(0, 0, rendered);
    onPaper.end();

    // Then the night. Inverting the composited picture rather than the renderer's
    // output is what makes the two agree: the view inverts the same way as it
    // paints, so this is the screen's image, not a second opinion about it.
    if (matchScreen)
        paper.invertPixels(QImage::InvertRgb);

    crop = paper;
    copyImageButton->setEnabled(true);
}

void OcrDialog::setPending()
{
    status->setText(tr("正在识别框选区域内的文字…"));
    text->clear();
    text->setEnabled(false);
    copyButton->setEnabled(false);
    translateButton->setEnabled(false);
}

void OcrDialog::setResult(const Ocr::Result &result)
{
    text->setEnabled(true);
    if (!result.ok) {
        status->setText(result.error.isEmpty() ? tr("识别失败。") : result.error);
        text->clear();
        copyButton->setEnabled(false);
        translateButton->setEnabled(false);
        return;
    }
    if (result.text.isEmpty()) {
        // Said rather than shown as an empty window: "it worked and there was
        // nothing there" and "it did not work" are different news.
        status->setText(tr("框选的区域里没有识别出文字。"));
        text->clear();
        copyButton->setEnabled(false);
        translateButton->setEnabled(false);
        return;
    }
    status->setText(tr("识别完成，可以复制或翻译："));
    text->setPlainText(result.text);
    copyButton->setEnabled(true);
    translateButton->setEnabled(true);
}
