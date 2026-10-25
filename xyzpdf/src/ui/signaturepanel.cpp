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
    header()->setStretchLastSection(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // A line longer than the panel ends in an ellipsis and carries its whole text as a tooltip
    // (set per item below). Wrapping does not do it on its own here: the tree sizes a child row
    // for the column's width without its indent, so the wrapped text comes out one line short and
    // is cut anyway (seen on the screen in §130).
    setTextElideMode(Qt::ElideRight);
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
        row->setToolTip(0, line);
        row->setIcon(0, iconFor(one.verdict));
        row->setData(0, VerdictRole, one.verdictLabel());
        row->setData(0, PageRole, one.page);
        // The verdict line is what the reader sees, so it stays collapsed-out; the detail lines
        // are children, which is how one signature's evidence does not push the next one off the
        // panel.
        for (const QString &detail : one.detailLines()) {
            auto *child = new QTreeWidgetItem(row);
            child->setText(0, detail);
            child->setToolTip(0, detail);
        }
        // Selectable as well as enabled: a press has to show which signature it was about. With
        // ItemIsEnabled alone the click set the current item (and the jump worked) but drew no
        // selection, so on a one-page document the press looked like it did nothing -- found on
        // the screen by handoff-08 J1, invisible to a test that pressed the row in code.
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    // The row that could not be placed on a page is still a row the reader can read and select,
    // but pressing it must not move the page they are on: rowPressed() checks the page it carries.
    //
    // No resizeColumnToContents(0) here: it made the one column as wide as the longest line
    // (687 px in a 262 px sidebar, measured in §130), so setWordWrap() never had a width to wrap
    // at and every detail line ran off the panel behind a horizontal scroll bar. The column is
    // the viewport's width (the header stretches it) and the lines wrap inside it.
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
