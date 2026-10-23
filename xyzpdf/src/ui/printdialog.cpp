#include "printdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

PrintDialog::PrintDialog(QWidget *parent, QPrinter *printer, int currentPage, int pageCount)
    : QDialog(parent)
    , printer(printer)
    , pageTotal(pageCount)
{
    currentPageIndex = currentPage;
    setWindowTitle(tr("打印"));
    // Window-modal, like every dialog here (AGENTS.md): it blocks the reading
    // window it belongs to, not the whole program.
    setWindowModality(Qt::WindowModal);
    resize(440, 360);

    auto *layout = new QVBoxLayout(this);

    // ---- 页面范围 ----
    {
        auto *group = new QGroupBox(tr("页面范围"), this);
        auto *box = new QVBoxLayout(group);

        allRange = new QRadioButton(tr("全部（%n 页）", nullptr, pageCount), group);
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

        // Typing into the range box is taken as meaning "custom", which is what
        // every print dialog does and saves a click.
        connect(customEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (!text.trimmed().isEmpty())
                customRange->setChecked(true);
        });
        connect(customRange, &QRadioButton::toggled, this,
                [this](bool on) { customEdit->setEnabled(on); });
        connect(allRange, &QRadioButton::toggled, this, [this] { customEdit->setEnabled(false); });

        layout->addWidget(group);
    }

    // ---- 排版 ----
    {
        auto *group = new QGroupBox(tr("排版"), this);
        auto *form = new QFormLayout(group);

        scaleBox = new QComboBox(group);
        scaleBox->addItem(tr("适应可打印区域"), int(Print::Settings::Scale::Fit));
        scaleBox->addItem(tr("实际大小（可能裁切）"), int(Print::Settings::Scale::Actual));
        form->addRow(tr("缩放"), scaleBox);

        nUpBox = new QComboBox(group);
        for (int n : {1, 2, 4, 6, 9, 16})
            nUpBox->addItem(tr("%n 页/张", nullptr, n), n);
        form->addRow(tr("每页版数"), nUpBox);

        layout->addWidget(group);
    }

    // ---- 打印机 ----
    {
        auto *row = new QHBoxLayout;
        printerLabel = new QLabel(printer->printerName().isEmpty()
                                      ? tr("打印机：（默认）")
                                      : tr("打印机：%1").arg(printer->printerName()),
                                  this);
        row->addWidget(printerLabel, 1);
        auto *pickButton = new QPushButton(tr("选择打印机 / 份数 / 双面…"), this);
        connect(pickButton, &QPushButton::clicked, this, &PrintDialog::choosePrinter);
        row->addWidget(pickButton);
        layout->addLayout(row);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("打印"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PrintDialog::choosePrinter()
{
    // The system dialog owns the printer choice, the copy count, collation and
    // duplex -- it writes them onto `printer`, which the job then uses. What it
    // also offers (its own page-range box) is deliberately not relied on; the
    // range here is the richer one above.
    QPrintDialog dialog(printer, this);
    if (dialog.exec() == QDialog::Accepted) {
        printerLabel->setText(printer->printerName().isEmpty()
                                  ? tr("打印机：（默认）")
                                  : tr("打印机：%1").arg(printer->printerName()));
    }
}

Print::Settings PrintDialog::settings() const
{
    Print::Settings out;
    if (allRange->isChecked())
        out.range = Print::Settings::Range::All;
    else if (currentRange->isChecked())
        out.range = Print::Settings::Range::Current;
    else
        out.range = Print::Settings::Range::Custom;
    out.custom = customEdit->text();
    out.currentPage = currentPageIndex;
    out.scale = Print::Settings::Scale(scaleBox->currentData().toInt());
    out.nUp = nUpBox->currentData().toInt();
    return out;
}
