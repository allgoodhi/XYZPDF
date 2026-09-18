#pragma once

#include <QVector>
#include <QWidget>

class QToolBar;
class QToolButton;

// The strip the window's toolbars live in: one row, with a "…" at the right end
// that shows the rest when the window is too narrow for all of it.
//
// ---- why this is not the window's own toolbar area ----
//
// QMainWindow lays the toolbars of one area out in a single row and squeezes them
// when they do not fit, and a squeezed QToolBar puts its own tail away behind an
// extension button (») -- which is the part a reader wants. What is not: the
// state that button puts the toolbar into, the rows that show everything, is
// transient. It is dropped again on the next layout pass or when the pointer
// leaves, so a reader who has just clicked it open loses it while reaching for
// what it revealed, and typing in a field on the toolbar -- which is a layout
// pass, because the field's own size changes -- puts the field itself away. Qt
// has no public way to ask for that state, and no public way to make it stay.
//
// So the same reader-visible behaviour is built here, with the state held by this
// widget instead of by a flag inside a Qt layout: one row by default, a "…" while
// anything does not fit, and as many rows as it takes to show everything once --
// and only while -- the reader has asked for them. Nothing the pointer, the
// window or the contents of the chrome do changes that; a click on the "…" does,
// and it is the only thing that does.
class WrappingToolBarArea : public QWidget
{
    Q_OBJECT

public:
    explicit WrappingToolBarArea(QWidget *parent = nullptr);

    // A toolbar in this strip, in the order the groups are added. The caller
    // fills it; this class only decides where it goes.
    QToolBar *addGroup(const QString &objectName, const QString &title);
    QVector<QToolBar *> toolBars() const { return bars; }

    // True while the strip is showing every group, over as many rows as that
    // takes; false for the one row a reader sees by default.
    bool isExpanded() const { return expanded; }
    void setExpanded(bool expand);

    // What the strip would like to be -- everything in one row -- and the least
    // it will accept: the width of the single widest group, which is what a
    // window needs in order for expanding to be able to show everything.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();

    // Whether the groups come to more than one row's width, which is the same
    // question as "is there anything behind the …".
    bool overflows() const;

    // Takes the chevron a squeezed toolbar puts at its own right end out of the
    // picture: this strip has a "…" of its own a few pixels further right, and
    // two buttons saying the same thing is one too many.
    void silenceExtension(QToolBar *bar);

    QVector<QToolBar *> bars;
    QToolButton *more = nullptr;
    bool expanded = false;
};
