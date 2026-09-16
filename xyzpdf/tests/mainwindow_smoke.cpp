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
#include "../src/view/pageview.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMimeData>
#include <QPointF>
#include <QString>
#include <QTabWidget>
#include <QToolBar>
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

    if (failures == 0) {
        std::cout << "files dropped on the window open, and the three tools are wired\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
