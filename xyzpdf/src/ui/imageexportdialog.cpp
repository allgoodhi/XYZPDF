#include "imageexportdialog.h"

#include "../core/appsettings.h"
#include "../core/windowdialogs.h"
#include "../print/printjob.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

ImageExportDialog::ImageExportDialog(QWidget *parent, int currentPage, int pageCount)
    : QDialog(parent)
    , currentPageIndex(currentPage)
    , pageTotal(pageCount)
{
    setWindowTitle(tr("导出为图片"));
    setWindowModality(Qt::WindowModal);
    resize(440, 360);

    auto *layout = new QVBoxLayout(this);

    // ---- 页面范围 ----
    {
        auto *group = new QGroupBox(tr("页面范围"), this);
        auto *box = new QVBoxLayout(group);

        allRange = new QRadioButton(tr("全部（%1 页）").arg(pageCount), group);
        allRange->setChecked(true);
        box->addWidget(allRange);

        currentRange = new QRadioButton(tr("当前页（第 %1 页）").arg(currentPage + 1), group);
        box->addWidget(currentRange);

        auto *customRow = new QHBoxLayout;
        customRange = new QRadioButton(tr("自定义"), group);
        customEdit = new QLineEdit(group);
        customEdit->setPlaceholderText(tr("例如 1-3,5,8-10"));
        customEdit->setEnabled(false);
        customRow->addWidget(customRange);
        customRow->addWidget(customEdit, 1);
        box->addLayout(customRow);

        connect(customEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (!text.trimmed().isEmpty())
                customRange->setChecked(true);
        });
        connect(customRange, &QRadioButton::toggled, this,
                [this](bool on) { customEdit->setEnabled(on); });
        connect(allRange, &QRadioButton::toggled, this,
                [this] { customEdit->setEnabled(customRange->isChecked()); });

        layout->addWidget(group);
    }

    // ---- 图片 ----
    {
        auto *group = new QGroupBox(tr("图片"), this);
        auto *form = new QFormLayout(group);

        formatBox = new QComboBox(group);
        formatBox->addItem(tr("PNG（无损）"), int(ImageExport::Format::Png));
        formatBox->addItem(tr("JPEG（有损，较小）"), int(ImageExport::Format::Jpeg));
        form->addRow(tr("格式"), formatBox);

        dpiBox = new QSpinBox(group);
        dpiBox->setRange(72, 600);
        dpiBox->setSingleStep(50);
        dpiBox->setValue(200);
        dpiBox->setSuffix(QStringLiteral(" dpi"));
        form->addRow(tr("分辨率"), dpiBox);

        layout->addWidget(group);
    }

    // ---- 保存位置 ----
    {
        auto *form = new QFormLayout;
        dirEdit = new QLineEdit(this);
        dirEdit->setReadOnly(true);
        dirEdit->setPlaceholderText(tr("尚未选择"));
        auto *row = new QHBoxLayout;
        row->addWidget(dirEdit, 1);
        auto *browseButton = new QPushButton(tr("浏览…"), this);
        connect(browseButton, &QPushButton::clicked, this, &ImageExportDialog::browse);
        row->addWidget(browseButton);
        form->addRow(tr("保存到文件夹"), row);
        layout->addLayout(form);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("导出"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ImageExportDialog::browse()
{
    const QString start = !dirEdit->text().isEmpty() ? dirEdit->text()
                                                     : AppSettings::lastOpenDirectory();
    const QString chosen = WindowDialogs::getExistingDirectory(this, tr("选择保存文件夹"), start);
    if (!chosen.isEmpty())
        dirEdit->setText(chosen);
}

QVector<int> ImageExportDialog::pages() const
{
    if (allRange->isChecked())
        return Print::parsePageRange(QString(), pageTotal);
    if (currentRange->isChecked())
        return {currentPageIndex};
    return Print::parsePageRange(customEdit->text(), pageTotal);
}

ImageExport::Format ImageExportDialog::format() const
{
    return ImageExport::Format(formatBox->currentData().toInt());
}

int ImageExportDialog::dpi() const
{
    return dpiBox->value();
}

QString ImageExportDialog::directory() const
{
    return dirEdit->text();
}
