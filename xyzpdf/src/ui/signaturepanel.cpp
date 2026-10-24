#include "signaturepanel.h"

#include "../theme/thememanager.h"

#include <QApplication>
#include <QHeaderView>
#include <QStyle>
#include <QTreeWidgetItem>

namespace {

// The four marks. These are the system's own dialog icons rather than drawn glyphs: they follow
// the theme and the dark mode the reader chose, and a verdict that changes colour with the
// interface is a verdict the interface is not inventing.
QIcon iconFor(SignatureVerify::Verdict verdict)
{
    QStyle::StandardPixmap which = QStyle::SP_MessageBoxInformation;
    switch (verdict) {
    case SignatureVerify::Verdict::Valid:
        which = QStyle::SP_DialogApplyButton;
        break;
    case SignatureVerify::Verdict::ContentModified:
        which = QStyle::SP_MessageBoxCritical;
        break;
    case SignatureVerify::Verdict::CertificateProblem:
        which = QStyle::SP_MessageBoxWarning;
        break;
    case SignatureVerify::Verdict::Unverifiable:
        which = QStyle::SP_MessageBoxQuestion;
        break;
    }
    return QApplication::style()->standardIcon(which);
}

constexpr int VerdictRole = Qt::UserRole;

} // namespace

SignaturePanel::SignaturePanel(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    // Two columns would put the signer's name in a column of its own and leave the verdict
    // stretched over the width of the panel, which is a third of the window at most. One column,
    // one line per signature, wrapped by the reader rather than by a column width nobody chose.
    setColumnCount(1);
    setRootIsDecorated(true);
    setUniformRowHeights(false);
    setWordWrap(true);
    connect(this, &QTreeWidget::itemClicked, this, [this] { rowPressed(); });
    connect(this, &QTreeWidget::itemActivated, this, [this] { rowPressed(); });
    // A signature the reader wants details about is one they will expand with the keyboard as
    // often as with the mouse, and the tree does that on its own; the press handler must not turn
    // an expand into a page jump.
}

void SignaturePanel::setSignatures(const QVector<SignatureVerify::Signature> &signatures)
{
    clear();
    for (const SignatureVerify::Signature &one : signatures) {
        auto *row = new QTreeWidgetItem(this);
        QString line = one.verdictLabel();
        if (!one.signerName.isEmpty())
            line += QStringLiteral(" — ") + one.signerName;
        const QString when = one.timeText();
        if (!when.isEmpty())
            line += QStringLiteral(" · ") + when;
        row->setText(0, line);
        row->setIcon(0, iconFor(one.verdict));
        row->setData(0, VerdictRole, one.verdictLabel());
        row->setData(0, PageRole, one.page);
        // The verdict line is what the reader sees, so it stays collapsed-out; the detail lines
        // are children, which is how one signature's evidence does not push the next one off the
        // panel.
        for (const QString &detail : one.detailLines()) {
            auto *child = new QTreeWidgetItem(row);
            child->setText(0, detail);
        }
        row->setFlags(Qt::ItemIsEnabled);
    }
    // The row that could not be placed on a page is still a row the reader can read, but pressing
    // it must not move the page they are on. ItemIsEnabled alone says "readable", and rowPressed()
    // checks the page it carries.
    resizeColumnToContents(0);
}

bool SignaturePanel::hasSignatures() const
{
    return topLevelItemCount() > 0;
}

int SignaturePanel::signatureRowCount() const
{
    return topLevelItemCount();
}

QString SignaturePanel::rowText(int row) const
{
    if (row < 0 || row >= topLevelItemCount())
        return {};
    return topLevelItem(row)->text(0);
}

QString SignaturePanel::verdictOfRow(int row) const
{
    if (row < 0 || row >= topLevelItemCount())
        return {};
    return topLevelItem(row)->data(0, VerdictRole).toString();
}

int SignaturePanel::pageOfRow(int row) const
{
    if (row < 0 || row >= topLevelItemCount())
        return -1;
    return topLevelItem(row)->data(0, PageRole).toInt();
}

bool SignaturePanel::rowIsExpanded(int row) const
{
    return row >= 0 && row < topLevelItemCount() && topLevelItem(row)->isExpanded();
}

void SignaturePanel::selectRow(int row)
{
    if (row < 0 || row >= topLevelItemCount())
        return;
    setCurrentItem(topLevelItem(row));
}

void SignaturePanel::pressRow(int row)
{
    if (row < 0 || row >= topLevelItemCount())
        return;
    setCurrentItem(topLevelItem(row));
    rowPressed();
}

void SignaturePanel::rowPressed()
{
    QTreeWidgetItem *current = currentItem();
    if (!current)
        return;
    // Only a top-level row is a signature. A detail child pressed is the reader finishing
    // reading, not asking to be taken somewhere.
    if (current->parent())
        return;
    const int page = current->data(0, PageRole).toInt();
    if (page < 0)
        return;
    Q_EMIT jumpToPage(page);
}
