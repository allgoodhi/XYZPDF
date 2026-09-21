// The tab strip, with the drag behaviour a plain QTabBar does not have.
//
// QTabWidget's own bar can reorder tabs within one window, but it cannot take a
// tab out into a new window or accept one from another -- and that is what "drag
// a tab between windows" needs. So this subclass owns the whole drag gesture and
// asks its window, through signals, to do the moving. It never touches a
// DocumentTab itself: it reports what was dragged and where it landed, and
// MainWindow decides. Because the bar owns the drag, the tab widget's built-in
// reordering is turned off and reorder is routed back through reorderRequested.
//
// ---- why a drag carries a path, not a live document ----
//
// Moving a DocumentTab widget from one top-level window to another would move it
// across the thread that renders it and the PDFium document it holds -- exactly
// the overlap the rest of this program goes out of its way to avoid (see
// DocumentTab::drainRenders). So a drag carries the file's *path* and page, and
// the receiving window opens it fresh. The reader sees the tab move; under the
// hood it is closed here and opened there, which is safe in a way that hauling a
// live renderer between windows is not.
#pragma once

#include <QList>
#include <QPair>
#include <QPoint>
#include <QString>
#include <QTabBar>
#include <QTabWidget>

#include <functional>

class QMimeData;
class QPixmap;

// A tab as a drag carries it: which file, and where in it the reader was. A
// whole-window drag is simply one of these per open tab.
using TabRef = QPair<QString, int>;

class DocumentTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

    // How the strip asks its window what a tab is showing -- the file and the
    // page -- so a drag can carry them. Set by MainWindow.
    std::function<QString(int)> pathForIndex;
    std::function<int(int)> pageForIndex;

    // Reading a tab drag from outside the strip. A merge has to work when the
    // tab is let go anywhere over the target window, but Qt delivers the drop to
    // the widget under the cursor -- the page, not this bar -- so the window
    // recognises the drag with these and hands it back through takeMergedTabs.
    static bool isTabDrag(const QMimeData *mime);
    static bool readTabs(const QMimeData *mime, QList<TabRef> &docs);

    // Take tabs dropped on this window rather than on its strip: one for a tab
    // dragged from another window, all of them for a whole window pulled in by
    // its blank strip. Each opens here, and the source is told to let go of its
    // copy. When the drag came from this very bar it is a drop back home: nothing
    // moves.
    void takeMergedTab(const QString &path, int page);
    void takeMergedTabs(const QList<TabRef> &docs);

    // Paints where an in-flight tab drag would land, so the drag says what
    // releasing now would do. -1 clears it. The strip sets it as the cursor moves
    // over it; the window sets it to the append slot for a drop over the body.
    void setInsertIndicator(int index);

Q_SIGNALS:
    // A tab was dropped on this same strip at `to`: reorder the window's tabs.
    void reorderRequested(int from, int to);
    // A tab was dragged out and let go over nothing: open its path in a new
    // window (at `globalPos`) and remove it here.
    void detachRequested(int index, QPoint globalPos);
    // A tab dragged from another window was dropped here at `index`: open the
    // path there. The source closes its own tab once the drop is accepted.
    void mergeRequested(const QString &path, int page, int index);
    // This window's tab was accepted by a *different* window during a drag-out:
    // the other window has opened it, so remove this tab (do not open a new one).
    void handedOff(int index);
    // The whole window was taken by another one -- its blank strip was dragged
    // onto that window. Every tab is open there now, so this window goes away.
    void handedOffWindow();
    // A whole-window drag ended over nothing of ours. `globalPos` is where the
    // button came up: the window looks there for a target, and otherwise the
    // window simply stays the window it already was.
    void windowMergeRequested(QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void beginDrag(int index, const QPoint &globalPos);
    // Carry every document this window holds, started from a press on the blank
    // strip beside the tabs. `globalPos` is where the pointer is as the drag
    // begins.
    void beginWindowDrag(const QPoint &globalPos);
    // The shared shape of both drags: carry these tabs, run the modal drag, and
    // decide from the result what the source window has to do afterwards.
    // `sourceIndex` is the tab that was grabbed, or -1 for a whole-window drag.
    void runDrag(const QList<TabRef> &docs, int sourceIndex, const QPixmap &ghost,
                 const QPoint &hotSpot);

    int pressIndex = -1;
    bool pressWholeWindow = false;
    // Held between a press on the blank strip and the drag (or release) that
    // follows it, so the gesture is not lost when the pointer leaves the row.
    bool holdingPressGrab = false;
    QPoint pressGlobal;
    int insertIndicator = -1;
};

// A QTabWidget that installs a DocumentTabBar. setTabBar is protected, so the
// only place a custom bar can be put in is a subclass's own constructor -- before
// any tab is added, which is what QTabWidget requires of it.
class DocumentTabWidget : public QTabWidget
{
    Q_OBJECT

public:
    explicit DocumentTabWidget(QWidget *parent = nullptr);
};
