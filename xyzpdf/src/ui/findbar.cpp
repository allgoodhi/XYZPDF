#include "findbar.h"

#include <QCheckBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findBar"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);

    auto *caption = new QLabel(tr("查找："), this);
    layout->addWidget(caption);

    field = new QLineEdit(this);
    field->setObjectName(QStringLiteral("findField"));
    field->setPlaceholderText(tr("在本文档中查找"));
    field->setClearButtonEnabled(true);
    layout->addWidget(field, 1);

    caseBox = new QCheckBox(tr("区分大小写"), this);
    caseBox->setObjectName(QStringLiteral("findCaseBox"));
    layout->addWidget(caseBox);

    count = new QLabel(this);
    count->setObjectName(QStringLiteral("findCount"));
    // Room for the longest thing it ever says ("第 9999 / 9999 个"), so that the
    // field does not change width as the count grows and shrinks while typing.
    count->setMinimumWidth(110);
    layout->addWidget(count);

    auto *previous = new QToolButton(this);
    previous->setText(tr("上一个"));
    previous->setToolTip(tr("上一个匹配（Shift+Enter）"));
    layout->addWidget(previous);

    auto *next = new QToolButton(this);
    next->setText(tr("下一个"));
    next->setToolTip(tr("下一个匹配（Enter）"));
    layout->addWidget(next);

    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("关闭查找栏（Esc）"));
    layout->addWidget(close);

    // Typing searches: the scan is off the UI thread and a page of text is
    // cheap, so waiting for Enter would be waiting for nothing.
    connect(field, &QLineEdit::textChanged, this, [this] {
        Q_EMIT searchRequested(field->text(), caseBox->isChecked());
    });
    connect(caseBox, &QCheckBox::toggled, this, [this] {
        Q_EMIT searchRequested(field->text(), caseBox->isChecked());
    });
    connect(next, &QToolButton::clicked, this, [this] { Q_EMIT stepRequested(1); });
    connect(previous, &QToolButton::clicked, this, [this] { Q_EMIT stepRequested(-1); });
    connect(close, &QToolButton::clicked, this, &FindBar::closed);

    // Enter inside the field, which is where the caret is: the field consumes
    // the key, so the modifier is read from the keyboard rather than from an
    // event that never arrives here.
    connect(field, &QLineEdit::returnPressed, this, [this] {
        const bool backwards = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        Q_EMIT stepRequested(backwards ? -1 : 1);
    });

    setResultCount(-1, 0, false);
}

void FindBar::focusField()
{
    show();
    field->setFocus(Qt::ShortcutFocusReason);
    field->selectAll();
}

QString FindBar::text() const
{
    return field->text();
}

bool FindBar::caseSensitive() const
{
    return caseBox->isChecked();
}

void FindBar::setText(const QString &text)
{
    if (field->text() == text)
        return;
    // Blocked: this is the bar being *told* what is being searched, and the
    // field's own change signal would send it straight back as a request to
    // search for it again -- a scan started by the answer to a scan.
    QSignalBlocker blocker(field);
    field->setText(text);
}

void FindBar::setResultCount(int current, int total, bool searching)
{
    if (searching) {
        count->setText(tr("搜索中…"));
        return;
    }
    if (total <= 0) {
        count->setText(field && !field->text().isEmpty() ? tr("没有找到") : QString());
        return;
    }
    count->setText(tr("第 %1 / %2 个").arg(current + 1).arg(total));
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        Q_EMIT closed();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        Q_EMIT stepRequested(event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}
