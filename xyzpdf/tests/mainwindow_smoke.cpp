// Dropping a file on the window.
//
// ---- why this needs a test rather than a reading ----
//
// The handlers for a drop are private, and a widget that never called
// setAcceptDrops() has them all and is never called. Qt also decides where a
// drag goes: it is delivered to the widget under the pointer and walks up the
// parents of anything that does not take drops, which is a rule this window
// depends on -- the drop has to arrive whether the pointer is over the page,
// over the tab strip, or over the empty state. Neither of those is visible in
// the code that was written for it, so this drives the real window with real
// drag and drop events and then looks at what is open.
//
// The failure it would catch: the empty state has said "把 PDF 拖到这里" since
// the first build, and for several milestones nothing was listening. Nobody
// reported it, which is what a missing gesture looks like -- no error, no log,
// just a hint on screen that is not true.
//
// It runs on the offscreen platform: the point is the plumbing.

#include "../documenttab.h"
#include "../mainwindow.h"
#include "../src/ui/documenttabbar.h"
#include "../src/ui/findbar.h"
#include "../src/ui/wrappingtoolbararea.h"
#include "../src/view/pageview.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QPointF>
#include <QString>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include <functional>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

void putUrls(QMimeData &mime, const QStringList &urls)
{
    QList<QUrl> list;
    for (const QString &url : urls)
        list.append(QUrl(url));
    mime.setUrls(list);
}

// Whether a drag of these URLs is let in at all, without dropping it. `offered`
// is the set of actions the source allows, which is what a target may choose
// from: a realistic drag from Explorer inside one volume offers
// Copy|Move|Link, and a drag that offers Move alone cannot be answered with a
// copy at all.
bool dragIsAccepted(MainWindow &window, const QStringList &urls,
                    Qt::DropActions offered = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction)
{
    QMimeData mime;
    putUrls(mime, urls);
    QDragEnterEvent enter(QPoint(60, 60), offered, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    const bool accepted = enter.isAccepted();

    // Left again, so that one refusal cannot leave its decision behind for the
    // next drag to be judged by.
    QDragLeaveEvent leave;
    QApplication::sendEvent(&window, &leave);
    return accepted;
}

struct DropOutcome
{
    bool accepted = false;
    Qt::DropActions offered = Qt::IgnoreAction;
    Qt::DropAction taken = Qt::IgnoreAction;
};

// One drag, as the platform delivers it: enter, then drop.
DropOutcome dropFiles(MainWindow &window, const QStringList &urls,
                      Qt::DropActions offered = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction)
{
    QMimeData mime;
    putUrls(mime, urls);

    QDragEnterEvent enter(QPoint(60, 60), offered, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);

    QDropEvent drop(QPointF(60, 60), offered, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    return {drop.isAccepted(), drop.possibleActions(), drop.dropAction()};
}

// The documents the window has open. A tab that failed to open is taken back
// out by openDocument(), so this is the open documents and nothing else.
int openTabs(const MainWindow &window)
{
    int open = 0;
    for (const DocumentTab *tab : window.findChildren<DocumentTab *>()) {
        if (tab->isOpen())
            ++open;
    }
    return open;
}

// The tab in front, asked of the tab strip rather than guessed from the order
// findChildren() happens to walk the object tree in -- which is depth-first,
// not creation order, and had this test poking at a tab nobody was looking at.
DocumentTab *currentDocumentTab(const MainWindow &window)
{
    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"));
    return tabs ? qobject_cast<DocumentTab *>(tabs->currentWidget()) : nullptr;
}

QString onlyOpenPath(const MainWindow &window)
{
    for (const DocumentTab *tab : window.findChildren<DocumentTab *>()) {
        if (tab->isOpen())
            return tab->documentPath();
    }
    return {};
}

bool sameFile(const QString &left, const QString &right)
{
    const QFileInfo a(left);
    const QFileInfo b(right);
    const QString canonicalA = a.canonicalFilePath().isEmpty() ? a.absoluteFilePath()
                                                              : a.canonicalFilePath();
    const QString canonicalB = b.canonicalFilePath().isEmpty() ? b.absoluteFilePath()
                                                              : b.canonicalFilePath();
    return canonicalA == canonicalB;
}

// Pumps the event loop until `ready` says so. Opening a document lays out its
// pages and asks for renders synchronously, but the tab reaches the strip
// through the window's own signal, so a turn of the loop is what makes it
// visible to a query.
bool waitFor(const std::function<bool()> &ready, int timeoutMilliseconds = 10000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready()) {
        if (elapsed.elapsed() > timeoutMilliseconds)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    const QString pdf = directory + QStringLiteral("/text.pdf");

    MainWindow window;
    window.resize(900, 700);
    window.show();

    // ---- the window takes drops at all ----
    check(window.acceptDrops(), "the window is a drop target");

    // ---- a PDF dragged in is accepted, as a copy, and stays accepted ----
    {
        QMimeData mime;
        putUrls(mime, {QUrl::fromLocalFile(pdf).toString()});

        QDragEnterEvent enter(QPoint(60, 60), Qt::CopyAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        check(enter.isAccepted(), "a PDF dragged from a folder is accepted");
        check(enter.dropAction() == Qt::CopyAction, "as a copy");

        QDragMoveEvent move(QPoint(80, 80), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
        check(move.isAccepted(), "and it stays accepted while the pointer moves inside");

        QDragLeaveEvent leave;
        QApplication::sendEvent(&window, &leave);
    }

    // ---- dropping it opens it ----
    {
        const DropOutcome outcome =
            dropFiles(window, {QUrl::fromLocalFile(pdf).toString()}, Qt::CopyAction);
        check(outcome.accepted, "the drop is accepted");
        check(waitFor([&window] { return openTabs(window) == 1; }),
              QStringLiteral("dropping a PDF opens it (open documents: %1)").arg(openTabs(window)));
        check(sameFile(onlyOpenPath(window), pdf),
              QStringLiteral("and it is the file that was dropped (opened \"%1\")")
                  .arg(onlyOpenPath(window)));

        // A drop of a file that is already open raises that tab rather than
        // opening a second one: two tabs on one file is two caches and two
        // views of the same pages.
        dropFiles(window, {QUrl::fromLocalFile(pdf).toString()}, Qt::CopyAction);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(openTabs(window) == 1, "dropping the same file again does not open it twice");
    }

    // ---- what this window cannot use is refused before it lands ----
    {
        check(!dragIsAccepted(window, {QUrl::fromLocalFile(directory).toString()}),
              "a folder is refused");
        check(!dragIsAccepted(window, {QStringLiteral("https://example.invalid/x.pdf")}),
              "a URL from a browser is refused");
        check(!dragIsAccepted(window,
                              {QUrl::fromLocalFile(directory + QStringLiteral("/gone.pdf"))
                                   .toString()}),
              "a file that is not there is refused");
        check(!dragIsAccepted(window, {}), "a drag carrying no URLs at all is refused");
    }

    // ---- a move is on offer: the answer is still a copy ----
    //
    // Explorer prefers a *move* for a drag inside one volume, and taking it
    // tells Explorer the original may be taken away -- so dropping a document
    // on the reader would be how somebody loses the file they were trying to
    // open. Out of a set that offers both, this window takes the copy.
    {
        const int before = openTabs(window);
        const DropOutcome outcome = dropFiles(window, {QUrl::fromLocalFile(pdf).toString()},
                                             Qt::CopyAction | Qt::MoveAction);
        check(outcome.offered.testFlag(Qt::MoveAction), "a move was on offer");
        check(outcome.accepted, "the drop is accepted");
        check(outcome.taken == Qt::CopyAction, "and a copy is what was taken");
        check(openTabs(window) == before, "which opened nothing new, the file already being open");
    }

    // ---- and a drag that allows nothing but a move is refused ----
    //
    // Qt will not let a target choose an action the source did not offer, so
    // "accept it but copy it" is not available here: setDropAction would be
    // ignored, the drop would go through as a move, and the original file would
    // be gone. Refusing is the only honest answer -- the cursor says no, and
    // nothing happens. Holding Ctrl in Explorer is how the user offers a copy.
    {
        const int before = openTabs(window);
        const QString other = directory + QStringLiteral("/many-pages.pdf");
        check(!dragIsAccepted(window, {QUrl::fromLocalFile(other).toString()}, Qt::MoveAction),
              "a move-only drag is refused before it is released");

        const DropOutcome outcome =
            dropFiles(window, {QUrl::fromLocalFile(other).toString()}, Qt::MoveAction);
        check(!outcome.accepted, "and refused again if it is dropped anyway");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(openTabs(window) == before, "so nothing was opened and nothing was moved");
    }

    // ---- a second document joins the first ----
    {
        const QString other = directory + QStringLiteral("/rotated.pdf");
        dropFiles(window, {QUrl::fromLocalFile(other).toString()}, Qt::CopyAction);
        check(waitFor([&window] { return openTabs(window) == 2; }),
              QStringLiteral("a second file opens a second tab (open documents: %1)")
                  .arg(openTabs(window)));
    }

    // ---- the three tools, where the request put them ----
    //
    // In the toolbar, first, and one at a time. All three are things a test can
    // only see by looking at the real window: whether the buttons exist, where
    // they sit, whether the group really is exclusive (two tools in force is a
    // drag with two meanings, which is the state this milestone exists to get
    // out of), and whether choosing one reaches the document.
    {
        QToolBar *bar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
        check(bar != nullptr, "the main toolbar exists");

        // Found by their object names: the labels are translated, and the
        // shortcuts are not a stable identity either -- the first version of
        // this used Ctrl+1, which already belonged to "fit width", and the
        // buttons were found by that name until it moved.
        const auto toolAction = [&window](const char *name) {
            return window.findChild<QAction *>(QString::fromLatin1(name));
        };

        QList<QAction *> tools;
        {
            const char *names[] = {"viewToolAction", "selectToolAction", "recognizeToolAction"};
            for (const char *name : names)
                tools.append(toolAction(name));
            check(!tools.contains(nullptr), "the three tool actions exist");
        }

        if (bar && !tools.contains(nullptr)) {
            const QList<QAction *> actions = bar->actions();
            for (QAction *action : std::as_const(tools))
                check(actions.contains(action), "and each of them is in the toolbar");
            check(actions.size() >= 3 && actions.mid(0, 3) == tools,
                  "as the first three buttons, in the order they were asked for");
            for (QAction *action : std::as_const(tools))
                check(action->isCheckable(), "and each of them is a checkable button");
        }

        // Exclusive: choosing the last one takes the tick off the others.
        if (tools.size() == 3) {
            tools.at(2)->trigger();
            check(tools.at(2)->isChecked() && !tools.at(0)->isChecked()
                      && !tools.at(1)->isChecked(),
                  "only one tool is in force at a time");

            // And the choice reaches the document, not just the button.
            DocumentTab *current = currentDocumentTab(window);
            check(current && current->tool() == PageView::Tool::Recognize,
                  "choosing the recognition tool puts the open document's view into it");

            // A document opened afterwards starts in the same tool: a mode that
            // resets itself on every open is a mode people stop using.
            const QString other = directory + QStringLiteral("/blank.pdf");
            if (QFileInfo::exists(other)) {
                dropFiles(window, {QUrl::fromLocalFile(other).toString()}, Qt::CopyAction);
                check(waitFor([&window] { return openTabs(window) == 3; }),
                      "a third document opens");
                DocumentTab *newest = currentDocumentTab(window);
                check(newest && newest != current && newest->tool() == PageView::Tool::Recognize,
                      "and it is already in the tool the user was using");
            }

            // A tool changed from somewhere else -- a shortcut, a future
            // context menu -- brings the toolbar with it. Asked of the tab in
            // front, because that is the one the toolbar describes: a tab in
            // the background changing its tool is not supposed to move the
            // buttons out from under the document being read.
            DocumentTab *front = currentDocumentTab(window);
            if (front) {
                front->setTool(PageView::Tool::View);
                check(tools.at(0)->isChecked(), "the toolbar follows the tool the view is in");
                front->setTool(PageView::Tool::Select);
                check(tools.at(1)->isChecked(), "either way it was changed");
            }
        }
    }

    // ---- finding text, from the window ----
    //
    // The bar is inside the tab rather than in the window -- a search belongs to
    // the document it is looking in -- so what the window has to do is reach the
    // tab in front and ask it. That is one line per action, and the kind of line
    // that is easy to wire to nothing.
    {
        QAction *find = window.findChild<QAction *>(QStringLiteral("findAction"));
        check(find != nullptr, "the find action exists");

        DocumentTab *front = currentDocumentTab(window);
        check(front != nullptr, "and there is a document in front to search");
        if (find && front) {
            check(front->findBarWidget() && !front->findBarWidget()->isVisible(),
                  "whose find bar starts hidden");
            find->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(front->findBarWidget() && front->findBarWidget()->isVisible(),
                  "and appears when the action is run");

            // F3 and Shift+F3 step through what the bar has found. With no
            // needle typed there is nothing to step to, so this is about the
            // actions existing and reaching the tab rather than about counting.
            QAction *next = nullptr;
            for (QAction *action : window.findChildren<QAction *>()) {
                if (action->shortcut() == QKeySequence(QStringLiteral("F3")))
                    next = action;
            }
            check(next != nullptr, "the window has a find-next action");
            if (next) {
                next->trigger();
                check(front->currentSearchMatch() == -1,
                      "which does nothing when nothing has been searched for");
            }

            front->findBarWidget()->hide();
        }
    }

    // ---- searching from the toolbar, and the count beside it ----
    //
    // The box and the count are one feature: a search box that does not say
    // where you are in its results is a box people use to search once and then
    // scroll looking for the highlight. Both are driven through the tab in
    // front, and both are views of that tab's one search.
    {
        // The two-page fixture, dropped in: it may already be open from an
        // earlier block, in which case this raises it rather than opening a
        // second tab -- which is the behaviour being relied on, so the check is
        // that it is in front rather than how many tabs there are.
        const QString pdf = directory + QStringLiteral("/text.pdf");
        dropFiles(window, {QUrl::fromLocalFile(pdf).toString()}, Qt::CopyAction);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        QToolBar *bar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
        QToolBar *searchBar = window.findChild<QToolBar *>(QStringLiteral("searchToolBar"));
        auto *box = window.findChild<QLineEdit *>(QStringLiteral("searchBox"));
        auto *count = window.findChild<QLabel *>(QStringLiteral("searchCount"));
        check(box != nullptr, "the toolbar has a search box");
        check(count != nullptr, "and a place for the count");
        if (searchBar && box)
            check(box->parentWidget() == searchBar, "and it sits in the toolbar itself");

        DocumentTab *front = currentDocumentTab(window);
        check(front != nullptr, "with a document in front to search");
        if (box && count && front) {
            check(box->isEnabled(), "the box is enabled while a document is open");

            // Typing searches, which is the whole of the box's behaviour.
            box->setText(QStringLiteral("XYZPDF"));
            check(waitFor([front] { return !front->isSearching() && front->searchMatchCount() == 2; }),
                  QStringLiteral("typing in the box searches the document in front (found %1)")
                      .arg(front->searchMatchCount()));

            const QString shown = count->text();
            check(shown.contains(QStringLiteral("1")) && shown.contains(QStringLiteral("2")),
                  QStringLiteral("and the count says which of how many (got \"%1\")").arg(shown));

            // The same search, seen from the document's own find bar: two
            // controls, one search, and neither may drift from the other.
            if (front->findBarWidget()) {
                auto *field = front->findBarWidget()->findChild<QLineEdit *>(
                    QStringLiteral("findField"));
                check(field && field->text() == QStringLiteral("XYZPDF"),
                      "the document's own find bar shows the same needle");
            }

            // Enter goes to the next hit -- the key a search box is expected to
            // answer to -- and the count moves with it.
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(box, &enter);
            check(front->currentSearchMatch() == 1,
                  QStringLiteral("Enter in the box goes to the next hit (at %1)")
                      .arg(front->currentSearchMatch()));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(count->text() != shown,
                  QStringLiteral("and the count follows (was \"%1\", now \"%2\")")
                      .arg(shown)
                      .arg(count->text()));

            // A needle that is in nothing: the box says so rather than showing
            // a count of zero hits as if it were one.
            box->setText(QStringLiteral("this phrase is in no document of mine"));
            check(waitFor([front] { return !front->isSearching() && front->searchMatchCount() == 0; }),
                  "a phrase that is not there finds nothing");
            check(!count->text().isEmpty(), "and the count says something about it");
            check(!count->text().contains(QStringLiteral("/")),
                  QStringLiteral("which is not a fraction of nothing (got \"%1\")").arg(count->text()));

            // Each document has its own search, and the toolbar describes the
            // one in front: moving to another tab shows that tab's search --
            // which is nothing, if it has never been searched.
            if (auto *strip = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"))) {
                const int here = strip->currentIndex();
                const int other = (here + 1) % strip->count();
                strip->setCurrentIndex(other);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                check(currentDocumentTab(window) != front, "another document is in front");
                check(box->text().isEmpty() && count->text().isEmpty(),
                      QStringLiteral("whose own search is shown instead (box \"%1\", count \"%2\")")
                          .arg(box->text())
                          .arg(count->text()));

                strip->setCurrentIndex(here);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                check(box->text() == QStringLiteral("this phrase is in no document of mine"),
                      QStringLiteral("and coming back finds it where it was left (box \"%1\")")
                          .arg(box->text()));
            }

            // Escape clears both, which is what Escape means in a search box.
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(box, &escape);
            check(box->text().isEmpty(), "Escape empties the box");
            check(front->searchMatchCount() == 0 && front->searchText().isEmpty(),
                  "and clears the search it was making");
            check(count->text().isEmpty(), "and the count goes with it");
        }
    }

    // ---- the chrome: one row, a "…", and an expansion that stays put ----
    //
    // The bug this pins down, in the words it was reported in: the "…" at the
    // right end opened the chrome up, and then folded it back by itself --
    // when the pointer left it, and when the search box beside it was typed in
    // (typing is a layout pass). The complaint was not about the folding as
    // such, it was that the state would not stay: it should open and close on a
    // click, and on nothing else. That is what the strip below promises, and
    // this is the test of the promise rather than of the drawing.
    {
        const int narrow = window.minimumWidth();
        check(narrow > 0,
              QStringLiteral("the window refuses to be narrower than one group needs (%1)")
                  .arg(narrow));
        window.resize(narrow, 640);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 300);

        auto *chrome = window.findChild<WrappingToolBarArea *>(QStringLiteral("toolBarStrip"));
        auto *more = window.findChild<QToolButton *>(QStringLiteral("toolBarMoreButton"));
        check(chrome != nullptr, "the toolbars live in a strip of their own");
        check(more != nullptr, "with a “…” at its right end");

        // What is not on screen, how many rows are on screen, and how many
        // chevrons the chrome is showing. All three are read from the widgets
        // rather than from a member: this is about what a reader can see.
        struct Chrome {
            int hidden = 0;
            int rows = 1;
            // Qt's own "»", the one on a toolbar that has more than its width can
            // show. It has to be zero: the strip has a "…" of its own, and two
            // buttons that mean the same thing is what this looked like.
            int chevrons = 0;
        };
        auto survey = [&window]() {
            Chrome seen;
            QList<int> rows;
            for (QToolBar *bar : window.findChildren<QToolBar *>()) {
                if (bar->isFloating() || !bar->isVisible())
                    continue;
                auto *extension =
                    bar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
                if (extension && extension->isVisible() && extension->width() > 0)
                    ++seen.chevrons;
                if (!rows.contains(bar->y()))
                    rows.append(bar->y());
                for (QAction *action : bar->actions()) {
                    QWidget *item = bar->widgetForAction(action);
                    if (item && !item->isVisible())
                        ++seen.hidden;
                }
            }
            seen.rows = int(rows.size());
            return seen;
        };

        const Chrome collapsed = survey();
        check(collapsed.rows == 1,
              QStringLiteral("a narrow window shows one row (rows %1)").arg(collapsed.rows));
        check(more && more->isVisible(), "the “…” is offered, because something does not fit");
        check(collapsed.hidden > 0,
              QStringLiteral("and that something really is behind it (hidden %1)")
                  .arg(collapsed.hidden));
        check(collapsed.chevrons == 0,
              QStringLiteral("with no second chevron of Qt's beside it (%1)").arg(collapsed.chevrons));
        if (chrome)
            check(!chrome->isExpanded(), "and the strip starts folded, which is the default");

        // And the "…" is a control, not a lid on the row: the height of a button
        // with three dots in it, sat in the middle of the row.
        if (more && chrome) {
            int rowHeight = 0;
            for (QToolBar *bar : chrome->toolBars())
                rowHeight = qMax(rowHeight, bar->height());
            check(more->height() < rowHeight,
                  QStringLiteral("and is a button rather than the height of the row (%1 of %2)")
                      .arg(more->height())
                      .arg(rowHeight));
            check(more->geometry().right() <= chrome->width(),
                  QStringLiteral("inside the strip it belongs to (right %1 of %2)")
                      .arg(more->geometry().right())
                      .arg(chrome->width()));
        }

        // The click that opens it.
        if (more) {
            more->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }
        const Chrome opened = survey();
        check(opened.hidden == 0,
              QStringLiteral("clicking it shows everything (hidden %1)").arg(opened.hidden));
        check(opened.rows > 1,
              QStringLiteral("over more than one row (rows %1)").arg(opened.rows));
        check(opened.chevrons == 0, "still with only the one button");
        check(more && more->isVisible(), "and the “…” stays, as the way back");
        if (chrome)
            check(chrome->isExpanded(), "with the strip in the expanded state");

        // ---- and it stays that way ----
        //
        // The three things that used to fold it up, one after another: the
        // pointer leaving, a field on the toolbar being typed into, and a layout
        // pass of any other kind.
        {
            QEvent leave(QEvent::Leave);
            if (chrome)
                QApplication::sendEvent(chrome, &leave);
            if (more)
                QApplication::sendEvent(more, &leave);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

            auto *box = window.findChild<QLineEdit *>(QStringLiteral("searchBox"));
            DocumentTab *front = currentDocumentTab(window);
            if (box && front) {
                box->setText(QStringLiteral("XYZPDF"));
                waitFor([front] { return !front->isSearching(); });
                box->clear();
            }
            window.resize(narrow + 1, 640);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            window.resize(narrow, 640);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }
        const Chrome afterwards = survey();
        check(afterwards.hidden == 0 && afterwards.rows == opened.rows,
              QStringLiteral("and does not fold up again by itself -- not on the pointer leaving, "
                             "not on typing, not on a layout pass (hidden %1, rows %2, was %3)")
                  .arg(afterwards.hidden)
                  .arg(afterwards.rows)
                  .arg(opened.rows));

        // The click that closes it, which is the only thing that does.
        if (more) {
            more->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }
        const Chrome closed = survey();
        check(closed.rows == 1,
              QStringLiteral("clicking it again folds the chrome back to one row (rows %1)")
                  .arg(closed.rows));
        check(closed.hidden > 0 && closed.chevrons == 0,
              "with the rest behind the one “…” again");
        if (chrome)
            check(!chrome->isExpanded(), "and the strip back in its default state");

        // A window with room for everything needs no "…" at all.
        window.resize(2400, 900);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
        const Chrome wide = survey();
        check(wide.hidden == 0 && wide.rows == 1 && wide.chevrons == 0,
              QStringLiteral("a window that fits everything shows one row (hidden %1, rows %2, "
                             "chevrons %3)")
                  .arg(wide.hidden)
                  .arg(wide.rows)
                  .arg(wide.chevrons));
        check(more && !more->isVisible(), "with no “…” to offer, because there is nothing behind it");

        window.resize(900, 700);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    }

    // ---- and the File menu offers the export ----
    //
    // Only the wiring: the conversion is wordexport_smoke's and the tab's half is
    // documenttab_smoke's. What is checked here is that a reader can reach it and
    // that it is offered when -- and only when -- there is a document to export.
    {
        auto *action = window.findChild<QAction *>(QStringLiteral("exportWordAction"));
        check(action != nullptr, "the window offers an export to Word");
        check(action && action->isEnabled(), "and offers it while a document is open");
        // In the File menu, beside the other things done to a file rather than to
        // a page: a reader looking for it looks there.
        bool inFileMenu = false;
        for (QAction *menuAction : window.menuBar()->actions()) {
            if (menuAction->menu() && menuAction->menu()->actions().contains(action))
                inFileMenu = menuAction->text().contains(QStringLiteral("文件"));
        }
        check(inFileMenu, "from the File menu");
    }

    // ---- exporting says how far it has got, and can be stopped ----
    //
    // What the status bar could not do. It is in the bottom-left corner, which is
    // not where a reader who has just asked for a conversion is looking, and it
    // cannot be clicked -- so a reader who had started converting a scanned
    // document, which is minutes of renders, had no way to stop it short of
    // closing the tab.
    {
        // The report at the end is a modal box. This test has to get past it
        // rather than be the reason it never closes.
        QTimer closer;
        closer.setInterval(20);
        QObject::connect(&closer, &QTimer::timeout, [] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    if (box->isVisible())
                        box->close();
                }
            }
        });
        closer.start();

        const QString pdf = directory + QStringLiteral("/many-pages.pdf");
        dropFiles(window, {QUrl::fromLocalFile(pdf).toString()}, Qt::CopyAction);
        check(waitFor([&window, &pdf] {
                  DocumentTab *front = currentDocumentTab(window);
                  return front && sameFile(front->documentPath(), pdf);
              }),
              "the forty-page document is in front");

        DocumentTab *tab = currentDocumentTab(window);
        check(tab && tab->isOpen(), "with a document to convert");
        QTemporaryDir output;

        if (tab && tab->isOpen()) {
            const QString file = output.filePath(QStringLiteral("many.docx"));
            tab->exportToWord(file);

            // Forty pages of one line each, which is over in a moment. No box is
            // demanded of it -- a conversion that fast has nothing to report, and
            // Qt's grace is what decides -- but nothing may be left behind either,
            // and the file has to be there.
            check(waitFor([tab] { return !tab->isExporting(); }, 8000), "the export finishes");
            check(QFileInfo::exists(file) && QFileInfo(file).size() > 0,
                  QStringLiteral("with the file written (%1 bytes)").arg(QFileInfo(file).size()));
            check(waitFor([&window] {
                      return window.findChild<QProgressDialog *>(
                                 QStringLiteral("exportProgress")) == nullptr;
                  }),
                  "and no box left behind");

            // ---- the box, on a document that takes long enough to need one ----
            //
            // The page below takes a second or more to read (two character queries
            // per line, seventy lines), which is the shape of the manual this was
            // reported from: nine seconds a page. A stop that is only noticed
            // between pages leaves the reader watching the box for all nine.
            const QString densePath = directory + QStringLiteral("/dense.pdf");
            dropFiles(window, {QUrl::fromLocalFile(densePath).toString()}, Qt::CopyAction);
            check(waitFor([&window, &densePath] {
                      DocumentTab *front = currentDocumentTab(window);
                      return front && sameFile(front->documentPath(), densePath);
                  }),
                  "the page of seventy lines is in front");
            if (DocumentTab *dense = currentDocumentTab(window)) {
                const QString unwritten = output.filePath(QStringLiteral("dense.docx"));
                dense->exportToWord(unwritten);
                QProgressDialog *third = nullptr;
                check(waitFor([&window, &third] {
                          third = window.findChild<QProgressDialog *>(
                              QStringLiteral("exportProgress"));
                          return third && third->isVisible();
                      }),
                      "its export offers the box before the first page is done");
                if (third) {
                    check(third->maximum() == 2,
                          QStringLiteral("with the whole document in it (%1 pages)")
                              .arg(third->maximum()));
                    // The first report is the one that says the export has started
                    // -- page nought of two -- because the point of it is to offer
                    // a way out while there is still time to take it.
                    check(third->value() < third->maximum(),
                          QStringLiteral("and how far it has got (%1 of %2)")
                              .arg(third->value())
                              .arg(third->maximum()));
                    check(third->labelText().contains(QStringLiteral("2")),
                          QStringLiteral("naming the size of the job (\"%1\")")
                              .arg(third->labelText()));
                    check(third->windowModality() == Qt::WindowModal,
                          "window-modal, like every other dialog in this program");

                    auto *button = third->findChild<QPushButton *>();
                    check(button != nullptr, "with a button to stop it");
                    if (button) {
                        QElapsedTimer clock;
                        clock.start();
                        button->click();
                        check(waitFor([dense] { return !dense->isExporting(); }, 8000),
                              "clicking it stops the export");
                        check(clock.elapsed() < 1500,
                              QStringLiteral("in the middle of the page it was reading (%1 ms; a "
                                             "page of that document is seconds)")
                                  .arg(clock.elapsed()));
                        check(!QFileInfo::exists(unwritten),
                              QStringLiteral("and leaves no half-written file behind (%1 bytes)")
                                  .arg(QFileInfo(unwritten).size()));
                        // Waited for rather than read at once: the task is known
                        // to have stopped before the answer it produces is
                        // delivered to this thread, so the status bar still says
                        // how far the export had got for a turn or two after it.
                        check(waitFor([&window] {
                                  return window.statusBar()->currentMessage().contains(
                                      QStringLiteral("取消"));
                              }, 3000),
                              QStringLiteral("with the status bar saying as much (\"%1\")")
                                  .arg(window.statusBar()->currentMessage()));
                        check(window.findChild<QProgressDialog *>(
                                  QStringLiteral("exportProgress")) == nullptr,
                              "and the box gone with it");
                    }
                }
            }
        }

        closer.stop();
    }

    // ---- a tab merged in from another window opens here ----
    //
    // The whole cross-window drag cannot run on the offscreen platform (it needs
    // a real modal QDrag and two live windows), but what the *receiving* window
    // does when a tab's mime lands on it can: this drives the entry and the drop
    // straight at the window, the way a file drop is driven above, and checks
    // the incoming document turns into a tab.
    {
        MainWindow target;
        target.resize(900, 700);
        target.show();
        const QString first = directory + QStringLiteral("/text.pdf");
        dropFiles(target, {QUrl::fromLocalFile(first).toString()}, Qt::CopyAction);
        check(waitFor([&target] { return openTabs(target) == 1; }),
              "the merge target starts with one document");

        const QString incoming = directory + QStringLiteral("/rotated.pdf");
        QMimeData mime;
        mime.setData(QStringLiteral("application/x-xyzpdf-tab"),
                     QStringLiteral("%1\t3").arg(incoming).toUtf8());

        // Dropped over the body: the window takes it and opens the document.
        QDragEnterEvent enter(QPoint(60, 60), Qt::MoveAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(&target, &enter);
        check(enter.isAccepted(), "a tab dragged from another window is accepted over the body");

        QDropEvent drop(QPointF(60, 60), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&target, &drop);
        check(drop.isAccepted(), "and the drop over the body is taken");
        check(waitFor([&target] { return openTabs(target) == 2; }),
              QStringLiteral("the merged document opens as a tab (open documents: %1)")
                  .arg(openTabs(target)));

        // Dropped over the tab strip. Two entries here, because they answer two
        // different questions. `takeMergedTab` is the entry the strip forwards a
        // foreign tab to, and driving it directly says that opening the document
        // works; the enter-and-drop pair below says the strip is actually reached
        // by a drop in the first place. The pair matters: an earlier version of
        // this test sent only the drop, Qt honoured none of it because a drop is
        // only honoured on a widget that was told about the drag, and that read as
        // a broken strip when the test itself was what was wrong.
        auto *strip = target.findChild<DocumentTabBar *>();
        check(strip != nullptr, "the target has a document tab strip");
        if (strip) {
            const QString stripIncoming = directory + QStringLiteral("/many-pages.pdf");
            strip->takeMergedTab(stripIncoming, 0);
            check(waitFor([&target] { return openTabs(target) == 3; }),
                  QStringLiteral("the strip's merge entry opens the incoming document (open: %1)")
                      .arg(openTabs(target)));

            // And driven as a real drop on the strip itself -- enter first, then
            // the drop, because Qt only honours a drop on a widget it has told
            // about the drag. This is the case that used to be gated on the
            // drag's shared bookkeeping and silently do nothing without it.
            const QString viaStrip = directory + QStringLiteral("/picture.pdf");
            QMimeData stripMime;
            stripMime.setData(QStringLiteral("application/x-xyzpdf-tab"),
                              QStringLiteral("%1\t0").arg(viaStrip).toUtf8());
            const QPoint onTab = strip->tabRect(0).center();
            QDragEnterEvent stripEnter(onTab, Qt::MoveAction, &stripMime, Qt::LeftButton,
                                       Qt::NoModifier);
            QApplication::sendEvent(strip, &stripEnter);
            QDropEvent stripDrop(QPointF(onTab), Qt::MoveAction, &stripMime, Qt::LeftButton,
                                 Qt::NoModifier);
            QApplication::sendEvent(strip, &stripDrop);
            check(stripDrop.isAccepted(), "a tab dropped on the strip is taken");
            check(waitFor([&target] { return openTabs(target) == 4; }),
                  QStringLiteral("and merges in through the strip (open: %1)")
                      .arg(openTabs(target)));
        }

        // A whole window travels as one line per tab, so a drag of a window with
        // two documents in it has to put two tabs down here. Same mime type, same
        // entry point: the only difference is how many records came along.
        {
            QMimeData windowMime;
            windowMime.setData(
                QStringLiteral("application/x-xyzpdf-tab"),
                QStringLiteral("%1\t0\n%2\t1")
                    .arg(directory + QStringLiteral("/colored.pdf"))
                    .arg(directory + QStringLiteral("/graphic.pdf"))
                    .toUtf8());

            QList<TabRef> docs;
            check(DocumentTabBar::readTabs(&windowMime, docs) && docs.size() == 2,
                  QStringLiteral("a two-tab payload reads back as two tabs (got %1)")
                      .arg(docs.size()));
            check(docs.at(0).second == 0 && docs.at(1).second == 1,
                  "with each tab's page kept beside it");

            // A drop is only honoured on a widget that was told about the drag
            // first, so this drives enter then drop exactly as the platform does.
            QDragEnterEvent windowEnter(QPoint(60, 60), Qt::MoveAction, &windowMime,
                                        Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&target, &windowEnter);
            check(windowEnter.isAccepted(), "a whole window dragged in is accepted over the body");

            QDropEvent windowDrop(QPointF(60, 60), Qt::MoveAction, &windowMime, Qt::LeftButton,
                                  Qt::NoModifier);
            const int pre = openTabs(target);
            QApplication::sendEvent(&target, &windowDrop);
            check(pre == 4, "the window was carrying two tabs in, then four open here");
            check(windowDrop.isAccepted(), "a whole window dropped on the body is taken");
            check(waitFor([&target] { return openTabs(target) == 6; }),
                  QStringLiteral("and both of its documents open here (open: %1)")
                      .arg(openTabs(target)));
        }
    }

    // ---- 合并所有窗口 brings another window's documents into this one ----
    //
    // The menu command needs no drag at all, so unlike the drags above it can be
    // driven end to end here: two windows, one document each side, one trigger,
    // and then who has what. This is also the guard on the one thing that would
    // make the command fatal -- merging into a window whose death ends the
    // process -- because in this program the first window built is the one
    // main.cpp is waiting on.
    {
        const int before = openTabs(window);

        MainWindow extra;
        extra.resize(700, 500);
        extra.show();
        const QString spliced = directory + QStringLiteral("/spaced.pdf");
        dropFiles(extra, {QUrl::fromLocalFile(spliced).toString()}, Qt::CopyAction);
        check(waitFor([&extra] { return openTabs(extra) == 1; }),
              "a second window has a document of its own to give");

        auto *action = window.findChild<QAction *>(QStringLiteral("mergeAllWindowsAction"));
        check(action != nullptr, "and the window menu offers a way to merge it in");
        if (action) {
            action->trigger();
            check(waitFor([&window, before] { return openTabs(window) == before + 1; }),
                  QStringLiteral("its document arrives here (open: %1, was %2)")
                      .arg(openTabs(window))
                      .arg(before));
            check(waitFor([&extra] { return !extra.isVisible(); }),
                  "and the window it came from has gone");

            // With nothing left to take, the command says so instead of sitting
            // there having done nothing.
            action->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            check(window.statusBar()->currentMessage().contains(QStringLiteral("没有")),
                  QStringLiteral("and a second run with nothing to merge says so (\"%1\")")
                      .arg(window.statusBar()->currentMessage()));
        }
    }

    // ---- a press on the blank strip is recognised as a whole-window grab ----
    //
    // The gesture nobody has managed to perform by hand: the blank row beside the
    // tabs, not a tab and not the title bar. Only the half a test can reach is
    // settled here -- that a press there is taken as "carry the whole window".
    // The drag itself is deliberately not driven: QDrag::exec runs a modal loop
    // that has no drop to wait for offscreen. What is asserted instead is the
    // grab the blank-strip branch takes, which is the whole of its decision and
    // is what a press on a tab does not do.
    {
        MainWindow blank;
        blank.resize(900, 700);
        blank.show();
        // Two documents, so the strip stays on screen: one tab under
        // setTabBarAutoHide(true) would hide the row this gesture needs.
        dropFiles(blank, {QUrl::fromLocalFile(directory + QStringLiteral("/text.pdf")).toString()},
                  Qt::CopyAction);
        dropFiles(blank,
                  {QUrl::fromLocalFile(directory + QStringLiteral("/rotated.pdf")).toString()},
                  Qt::CopyAction);
        check(waitFor([&blank] { return openTabs(blank) == 2; }),
              "a window with two tabs leaves a blank stretch of strip");

        auto *bar = blank.findChild<DocumentTabBar *>();
        check(bar != nullptr && bar->count() == 2, "and the strip is there to press");
        if (bar && bar->count() == 2) {
            const QPoint blankSpot(bar->geometry().width() - 6, bar->geometry().height() / 2);
            check(bar->tabAt(blankSpot) < 0,
                  QStringLiteral("the spot chosen is blank, not a tab (tabAt=%1)")
                      .arg(bar->tabAt(blankSpot)));

            const QPoint global = bar->mapToGlobal(blankSpot);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(blankSpot), QPointF(global),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &press);
            QObject *grabber = QWidget::mouseGrabber();
            check(grabber == bar,
                  QStringLiteral("a press on the blank strip grabs the pointer for a "
                                 "whole-window drag (grabber: %1)")
                      .arg(grabber ? QString::fromLatin1(grabber->metaObject()->className())
                                   : QStringLiteral("none")));

            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(blankSpot), QPointF(global),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &release);
            check(QWidget::mouseGrabber() != bar, "and letting go gives the pointer back");
        }
        blank.hide();
    }

    // ---- fullscreen and presentation ----
    //
    // Two halves, tested separately because only one of them can be. The
    // decisions are plain functions of the mode, so they are asserted directly;
    // the applying is a window hiding its own furniture, which offscreen does
    // show -- visibility is state, not pixels.
    {
        using namespace ViewChrome;
        check(!hidesMenu(Mode::Normal) && hidesMenu(Mode::FullScreen)
                  && hidesMenu(Mode::Presentation),
              "the menu goes away in both modes and only in them");
        check(!hidesTabStrip(Mode::FullScreen) && hidesTabStrip(Mode::Presentation),
              "the tab strip is dropped only for a presentation");
        check(toolbarsCanReveal(Mode::FullScreen) && !toolbarsCanReveal(Mode::Presentation),
              "only fullscreen brings the toolbars back under the pointer");
        check(inRevealBand(0) && inRevealBand(kRevealBandPixels - 1)
                  && !inRevealBand(kRevealBandPixels) && !inRevealBand(-1),
              "the reveal band is the few pixels at the top, and nothing above them");
        check(presentationStep(Qt::Key_Space, Qt::NoModifier) == 1
                  && presentationStep(Qt::Key_Space, Qt::ShiftModifier) == -1,
              "space goes forward and shift+space back, as the page view already does");
        check(presentationStep(Qt::Key_Right, Qt::NoModifier) == 1
                  && presentationStep(Qt::Key_Left, Qt::NoModifier) == -1
                  && presentationStep(Qt::Key_PageDown, Qt::NoModifier) == 1
                  && presentationStep(Qt::Key_PageUp, Qt::NoModifier) == -1,
              "the arrows and the page keys turn pages");
        check(presentationStep(Qt::Key_Escape, Qt::NoModifier) == 0,
              "and escape is left to the window, not spent on a page turn");
        check(presentationClickStep(0, 300) == -1 && presentationClickStep(99, 300) == -1
                  && presentationClickStep(100, 300) == 1,
              "the left third of a slide goes back, the rest forward");
    }

    {
        MainWindow chrome;
        chrome.resize(900, 700);
        chrome.show();
        dropFiles(chrome,
                  {QUrl::fromLocalFile(directory + QStringLiteral("/text.pdf")).toString()},
                  Qt::CopyAction);
        check(waitFor([&chrome] { return openTabs(chrome) == 1; }), "a document to go fullscreen with");

        auto *fullScreen = chrome.findChild<QAction *>(QStringLiteral("fullScreenAction"));
        auto *presentation = chrome.findChild<QAction *>(QStringLiteral("presentationAction"));
        auto *strip = chrome.findChild<QTabBar *>();
        check(fullScreen != nullptr && presentation != nullptr, "the view menu offers both modes");
        check(strip != nullptr, "and there is a tab strip to hide");

        if (fullScreen && presentation && strip) {
            check(chrome.menuBar()->isVisible() && chrome.statusBar()->isVisible(),
                  "to start with, the window has its furniture");

            fullScreen->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            check(!chrome.menuBar()->isVisible(), "fullscreen hides the menu");
            check(!chrome.statusBar()->isVisible(), "and the status bar");
            check(strip->isVisible(), "but keeps the tab strip, so tabs still work");
            check(fullScreen->isChecked(), "and says which mode it is in");

            // Straight from one mode into the other, which is the path that could
            // lose track of what the window looked like before any of this.
            presentation->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            check(strip->isHidden(), "presentation drops the tab strip too");
            check(!fullScreen->isChecked() && presentation->isChecked(),
                  "and only one of the two is ticked");

            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&chrome, &escape);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            check(chrome.menuBar()->isVisible() && chrome.statusBar()->isVisible(),
                  "Escape brings the whole window back");
            check(strip->isVisible(), "with the tab strip with it");
            check(!fullScreen->isChecked() && !presentation->isChecked(),
                  "and neither mode is ticked afterwards");
        }
        chrome.hide();
    }

    if (failures == 0) {
        std::cout << "files dropped on the window open, the three tools are wired, and so is Find\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
