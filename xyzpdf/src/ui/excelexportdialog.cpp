#include "excelexportdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ExcelExportDialog::ExcelExportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("导出为 Excel"));
    setWindowModality(Qt::WindowModal);
    resize(430, 0);

    auto *layout = new QVBoxLayout(this);

    auto *explanation = new QLabel(
        tr("这份 PDF 里每张表格会写进一张工作表：合并的格子、列宽行高、边框和底纹都按原样。"),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    textBox = new QCheckBox(tr("表格之外的文字也导出"), this);
    textBox->setToolTip(tr("没有表格的页面，把它的文字按行写进一张工作表"));
    layout->addWidget(textBox);

    auto *detail = new QLabel(tr("没有表格的页面默认跳过。勾选上面这一项，它们的文字会一行一行写进"
                                 "一张“文字”工作表；一行里有制表符的，写到相邻的列。"),
                              this);
    detail->setWordWrap(true);
    detail->setEnabled(false);
    layout->addWidget(detail);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("导出"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool ExcelExportDialog::exportText() const
{
    return textBox->isChecked();
}
