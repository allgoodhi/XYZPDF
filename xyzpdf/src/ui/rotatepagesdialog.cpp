#include "rotatepagesdialog.h"

#include "../core/windowdialogs.h"
#include "../edit/documenteditor.h"
#include "../print/printjob.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

RotatePagesDialog::RotatePagesDialog(QWidget *parent, int currentPage,
                                     const DocumentEditor *document)
    : QDialog(parent)
    , currentPageIndex(currentPage)
    , pageTotal(document ? document->sourcePageCount() : 0)
    , editor(document)
{
    setWindowTitle(tr("旋转页面"));
    WindowDialogs::makeWindowModal(this, parent);
    resize(440, 320);

    auto *layout = new QVBoxLayout(this);

    auto *directionGroup = new QGroupBox(tr("转向"), this);
    auto *directions = new QVBoxLayout(directionGroup);
    rightTurn = new QRadioButton(tr("向右转 90°"), directionGroup);
    rightTurn->setChecked(true);
    upsideDown = new QRadioButton(tr("旋转 180°"), directionGroup);
    leftTurn = new QRadioButton(tr("向左转 90°"), directionGroup);
    directions->addWidget(rightTurn);
    directions->addWidget(upsideDown);
    directions->addWidget(leftTurn);
    layout->addWidget(directionGroup);

    auto *group = new QGroupBox(tr("要旋转的页面"), this);
    auto *choices = new QVBoxLayout(group);

    currentChoice = new QRadioButton(tr("当前页（第 %1 页）").arg(currentPage + 1), group);
    choices->addWidget(currentChoice);

    auto *customRow = new QHBoxLayout;
    customChoice = new QRadioButton(tr("指定页码"), group);
    customChoice->setChecked(true);
    customEdit = new QLineEdit(group);
    customEdit->setPlaceholderText(tr("例如 1-3,5,8-10"));
    customRow->addWidget(customChoice);
    customRow->addWidget(customEdit, 1);
    choices->addLayout(customRow);

    connect(customEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty() && !customChoice->isChecked()) {
            QSignalBlocker guard(customChoice);
            customChoice->setChecked(true);
        }
        updateSummary();
    });
    connect(customChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(currentChoice, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(rightTurn, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(upsideDown, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(leftTurn, &QRadioButton::toggled, this, [this] { updateSummary(); });

    layout->addWidget(group);

    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("旋转这些页"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    okayButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateSummary();
    customEdit->setFocus();
}

void RotatePagesDialog::updateSummary()
{
    const QVector<int> pages = pagesToRotate();
    // Rotation has no floor like deletion does -- turning every page is a
    // legitimate document -- so the only thing that can make this unusable is a
    // range that names no page still in the document.
    const int present = editor ? editor->pagesStillThere(pages) : 0;

    QString line;
    if (pages.isEmpty())
        line = tr("还没有说出要转哪几页。");
    else if (present == 0)
        line = tr("这些页在前面的修改里已经删掉了，没有可以转的。");
    else if (present < pages.size())
        // Not "you asked for pages that don't exist": they do exist in the file,
        // it is the unsaved changes that took them, and the reader needs to know
        // which of the two they are looking at.
        line = tr("%1。说好的 %2 页里有 %3 页还在，只转那 %3 页。")
                   .arg(directionName())
                   .arg(pages.size())
                   .arg(present);
    else
        line = tr("%1：%2 页。").arg(directionName()).arg(pages.size());
    // Same sentence the delete dialog ends with, for the same reason: nothing
    // here has written a file yet.
    summaryLabel->setText(tr("%1\n\n原来那份文件不会改动。这一步只是记下修改，"
                             "要它成为一个文件，还得走保存。")
                              .arg(line));

    okayButton->setEnabled(present > 0);
}

QString RotatePagesDialog::directionName() const
{
    if (upsideDown->isChecked())
        return tr("旋转 180°");
    if (leftTurn->isChecked())
        return tr("向左转 90°");
    return tr("向右转 90°");
}

QVector<int> RotatePagesDialog::pagesToRotate() const
{
    if (currentChoice->isChecked())
        return {currentPageIndex};
    return Print::parsePageRange(customEdit->text(), pageTotal);
}

int RotatePagesDialog::quarterTurns() const
{
    if (upsideDown->isChecked())
        return 2;
    if (leftTurn->isChecked())
        return 3;
    return 1;
}
