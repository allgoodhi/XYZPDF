#include "propertiesdialog.h"

#include "../core/windowdialogs.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace {

// 72 points to the inch is the PDF user space unit, fixed by the format.
constexpr double kMillimetresPerPoint = 25.4 / 72.0;

} // namespace

PropertiesDialog::PropertiesDialog(QWidget *parent, const Info &info)
    : QDialog(parent)
{
    // Window modal, like every other dialog here: reading one document's
    // properties must not freeze the window next to it. AGENTS.md's 所有对话框
    // 走 WindowDialogs.
    WindowDialogs::makeWindowModal(this, parent);
    setWindowTitle(tr("文档属性"));

    auto *form = new QFormLayout;
    // The labels are the fixed half and the values are the half that varies in
    // length, so the form is told not to let a long producer string push the
    // labels around.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // One empty-field spelling, written once: a blank value next to a label
    // reads as "the dialog failed to fill this in" rather than as "the file
    // does not say".
    const QString nothing = tr("（无）");
    const auto valueOf = [&nothing](const QString &text) {
        return text.trimmed().isEmpty() ? nothing : text.trimmed();
    };
    const auto addRow = [this, form](const QString &label, const QString &value) {
        auto *field = new QLabel(value, this);
        field->setTextInteractionFlags(Qt::TextSelectableByMouse);
        field->setWordWrap(true);
        form->addRow(label, field);
    };

    const QFileInfo file(info.path);
    addRow(tr("文件名"), valueOf(file.fileName()));
    addRow(tr("位置"), valueOf(file.absolutePath()));
    // Qt's own formatting, so that the units are the ones the rest of the
    // machine uses in the reader's language rather than a number of bytes.
    addRow(tr("大小"), QLocale().formattedDataSize(info.fileBytes));
    addRow(tr("页数"), QLocale().toString(info.pageCount));
    // A count, not "signed": the file says how many signature dictionaries it carries,
    // and nothing in this program checks the cryptography behind them -- 文档 says that
    // half at open, and this row is the number. Left out entirely for the ordinary file
    // with none: every other row of this box names a thing the document has, so a row
    // that always reads （无） would make the empty ones ambiguous instead of informative.
    if (info.signatureCount > 0)
        addRow(tr("数字签名"), tr("%n 个", nullptr, info.signatureCount));

    if (info.pagePoints.width() > 0 && info.pagePoints.height() > 0) {
        // Both units: points are what the file is written in and what every
        // other tool reports, millimetres are what the paper in the printer is.
        addRow(tr("第 %1 页的大小").arg(QLocale().toString(info.sizedPage + 1)),
               tr("%1 × %2 点（%3 × %4 毫米）")
                   .arg(QString::number(info.pagePoints.width(), 'f', 0),
                        QString::number(info.pagePoints.height(), 'f', 0),
                        QString::number(info.pagePoints.width() * kMillimetresPerPoint, 'f', 0),
                        QString::number(info.pagePoints.height() * kMillimetresPerPoint, 'f', 0)));
    }

    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    form->addRow(rule);

    const DocMetadata &data = info.metadata;
    addRow(tr("标题"), valueOf(data.title));
    addRow(tr("作者"), valueOf(data.author));
    addRow(tr("主题"), valueOf(data.subject));
    addRow(tr("关键词"), valueOf(data.keywords));
    addRow(tr("创建者"), valueOf(data.creator));
    addRow(tr("生成器"), valueOf(data.producer));

    const auto dateText = [&nothing](const QDateTime &when) {
        return when.isValid() ? QLocale().toString(when, QLocale::ShortFormat) : nothing;
    };
    addRow(tr("创建日期"), dateText(data.creationDate));
    addRow(tr("修改日期"), dateText(data.modificationDate));
    // Not "encrypted": this is true when a password was needed to open the file
    // and given, which is the only thing the reader can honestly be told. A PDF
    // that is encrypted with an empty user password opens without one, and
    // nothing short of the owner password says what it permits.
    addRow(tr("打开时需要口令"), data.encrypted ? tr("是") : tr("否"));

    auto *layout = new QVBoxLayout(this);
    if (data.isEmpty()) {
        // Said once, at the top, rather than leaving the reader to work it out
        // from every field below saying the same thing.
        auto *empty = new QLabel(tr("这个文件没有填写任何文档信息。"), this);
        empty->setWordWrap(true);
        layout->addWidget(empty);
    }
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Wide enough that a producer string has somewhere to go before it wraps;
    // the height follows the rows.
    resize(520, sizeHint().height());
}
