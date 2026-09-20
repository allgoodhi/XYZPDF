#include "translatedialog.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

TranslateDialog::TranslateDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("翻译"));
    // Not application-modal, exactly like the recognition window: the reading
    // that asked for a translation should not be interrupted by it.
    setModal(false);
    resize(460, 380);

    auto *layout = new QVBoxLayout(this);

    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *sourceLabel = new QLabel(tr("原文"), this);
    layout->addWidget(sourceLabel);
    source = new QPlainTextEdit(this);
    source->setReadOnly(true);
    // Two lines of the original: enough to see what was asked without the
    // window growing tall enough to cover the paragraph it came from.
    source->setMaximumHeight(72);
    layout->addWidget(source);

    auto *translationLabel = new QLabel(tr("译文"), this);
    layout->addWidget(translationLabel);
    translation = new QPlainTextEdit(this);
    translation->setReadOnly(true);
    translation->setPlaceholderText(tr("译文会出现在这里。"));
    layout->addWidget(translation, 1);

    auto *buttons = new QHBoxLayout;
    copyButton = new QPushButton(tr("复制译文"), this);
    connect(copyButton, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(translation->toPlainText()); });
    buttons->addWidget(copyButton);
    buttons->addStretch(1);
    auto *closeButton = new QPushButton(tr("关闭"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);
}

void TranslateDialog::setPending(const QString &text)
{
    status->setText(tr("正在翻译…"));
    source->setPlainText(text);
    translation->clear();
    translation->setEnabled(false);
    copyButton->setEnabled(false);
}

void TranslateDialog::setResult(const Translate::Result &result)
{
    translation->setEnabled(true);
    if (!result.source.isEmpty())
        source->setPlainText(result.source);
    if (!result.ok) {
        status->setText(result.error.isEmpty() ? tr("翻译失败。") : result.error);
        translation->clear();
        copyButton->setEnabled(false);
        return;
    }
    status->setText(tr("译文（识别为 %1）：").arg(result.detectedLang.isEmpty() ? tr("未知语言")
                                                                                : result.detectedLang));
    translation->setPlainText(result.text);
    copyButton->setEnabled(true);
}
