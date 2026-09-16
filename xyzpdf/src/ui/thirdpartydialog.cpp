#include "thirdpartydialog.h"

#include "../core/thirdpartynotices.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

ThirdPartyDialog::ThirdPartyDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(ThirdPartyNotices::title());
    resize(820, 560);

    auto *summary = new QLabel(ThirdPartyNotices::summary(), this);
    summary->setWordWrap(true);

    list = new QListWidget(this);
    list->setMinimumWidth(220);

    heading = new QLabel(this);
    heading->setWordWrap(true);
    // The links are the part LGPL actually asks for, so they have to be
    // clickable rather than something to copy out by hand.
    heading->setTextFormat(Qt::RichText);
    heading->setOpenExternalLinks(true);
    heading->setTextInteractionFlags(Qt::TextBrowserInteraction);

    text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    // Monospace, because every one of these was written to be read that way --
    // the formal licences are hard-wrapped at about seventy columns and their
    // numbered clauses line up on it.
    //
    // Wrapped to the widget all the same. Not every notice here is a formal
    // licence: Qt's runs to paragraph-long lines, and without wrapping the
    // right-hand half of its text sits off the edge behind a scrollbar nobody
    // will think to drag. Wrapping costs the pre-wrapped ones nothing.
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setPointSizeF(font().pointSizeF() * 0.95);
    text->setFont(monospace);

    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(heading);
    rightLayout->addWidget(text, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(list);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(summary);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    for (const ThirdPartyNotices::Component &component : ThirdPartyNotices::components())
        list->addItem(component.name);
    connect(list, &QListWidget::currentRowChanged, this, &ThirdPartyDialog::showComponent);
    if (list->count() > 0)
        list->setCurrentRow(0);
}

void ThirdPartyDialog::showComponent(int row)
{
    const QVector<ThirdPartyNotices::Component> all = ThirdPartyNotices::components();
    if (row < 0 || row >= all.size())
        return;
    const ThirdPartyNotices::Component &component = all.at(row);

    heading->setText(tr("<b>%1</b><br>许可证：%2<br>随包文件：%3<br>"
                        "源码：<a href=\"%4\">%4</a><br>%5")
                         .arg(component.name.toHtmlEscaped(),
                              component.licence.toHtmlEscaped(),
                              component.libraryFile.toHtmlEscaped(),
                              component.source.toHtmlEscaped(),
                              component.note.toHtmlEscaped()));
    text->setPlainText(ThirdPartyNotices::licenceText(component));
}
