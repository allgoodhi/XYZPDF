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
#include "../src/core/languages.h"
#include "../src/ui/deletepagesdialog.h"
#include "../src/ui/documenttabbar.h"
#include "../src/ui/findbar.h"
#include "../src/ui/wrappingtoolbararea.h"
#include "../src/view/pageview.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMap>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QStatusTipEvent>
#include <QTemporaryDir>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QCryptographicHash>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QPointer>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QUrl>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

// To build the three form shapes the status line branches on. The window itself reaches qpdf only
// through DocumentEditor, which is compiled into this test for that reason, so the include is not
// a new dependency of the target -- it is the same one documenteditor.cpp carries.
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

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

// ---- the two ways this run answers a box a window opened ----
//
// Both are armed *before* the click that opens the box, because the action ends in
// exec() and nothing after that line runs until the box is gone. Both keep one waiter
// alive at a time: a waiter that found nothing keeps ticking towards its own limit,
// and if the next box opens inside that span the old waiter answers it -- with the old
// call's answer and the old call's record. documenttab_smoke measured exactly that
// when two refusals were reported as "no box".

namespace {
QTimer *dialogWaiter = nullptr;

void retireWaiter(QTimer *timer)
{
    if (dialogWaiter == timer)
        dialogWaiter = nullptr;
    timer->stop();
    timer->deleteLater();
}

void armWaiter(QTimer *timer)
{
    if (dialogWaiter)
        retireWaiter(dialogWaiter);
    dialogWaiter = timer;
}
} // namespace

// Answers one of the document dialogs the way a reader would: waits for it to open,
// types `pages` into its page box, presses 确定. Same shape as documenttab_smoke's.
template <typename Dialog>
void answerNextDialog(const QString &pages)
{
    auto *timer = new QTimer;
    armWaiter(timer);
    auto ticks = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, ticks, pages] {
        Dialog *open = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (auto *candidate = qobject_cast<Dialog *>(top); candidate && candidate->isVisible())
                open = candidate;
            for (Dialog *child : top->findChildren<Dialog *>()) {
                if (child->isVisible())
                    open = child;
            }
        }
        if (open) {
            if (auto *box = open->template findChild<QLineEdit *>())
                box->setText(pages);
            auto *buttons = open->template findChild<QDialogButtonBox *>();
            QPushButton *accept = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
            if (accept && accept->isEnabled())
                accept->click();
            else
                open->reject();
            retireWaiter(timer);
            return;
        }
        if (++*ticks > 100)
            retireWaiter(timer); // never appeared; nothing to unblock
    });
    timer->start(50);
}

// Answers the next message box by its standard-button role rather than by its label.
// F8's close question is 保存 / 不保存 / 取消, and the labels are translated: matching
// on the Chinese spelling would pass here and tell nobody when the English form moves.
// The box's own text is recorded, so the caller can assert what it said.
// The label the last answered box gave its Discard button, empty when it had none.
// Read before the click, because the click is what takes the box away.
QString lastDiscardLabel;

void answerNextBox(QMessageBox::StandardButton which, bool *seen, QString *text)
{
    auto *timer = new QTimer;
    armWaiter(timer);
    auto ticks = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, ticks, which, seen, text] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box) {
            if (seen)
                *seen = true;
            if (text)
                *text = box->text();
            QAbstractButton *discard = box->button(QMessageBox::Discard);
            lastDiscardLabel = discard ? discard->text() : QString();
            if (auto *button = box->button(which))
                button->click();
            else
                box->reject();
            retireWaiter(timer);
            return;
        }
        if (++*ticks > 250)
            retireWaiter(timer);
    });
    timer->start(20);
}

// Counts the viewport resizes a scrollbar coming or going causes. That is the
// wire the fit-width feedback loop ran around: fitting a narrower page made a
// wider one overflow, the horizontal bar appeared, the viewport lost its
// height, the page under the middle changed, and the fit followed it back.
class ResizeCounter : public QObject
{
public:
    int count = 0;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        if (event->type() == QEvent::Resize)
            ++count;
        return false;
    }
};

// ---- the run has to end, even when the regression does not ----
//
// docs/store-submission.md §100 measured the fit-width cascade at 253,000
// resize events in 20 s; when it came back during that slice, this test did
// not fail, it hung, and was killed by hand after 300 s with nothing printed.
// A gate timeout is a red run, but one that names no test and no reason.
//
// This thread turns any overrun into a named FAIL and a non-zero exit. The
// default is generous (120 s; this test takes a few seconds) and can be
// shortened for experiments with XYZPDF_SMOKE_TIMEOUT_MS -- the control run
// for the cascade below uses that.
void armFinishWatchdog()
{
    int limitMilliseconds = 120000;
    bool configured = false;
    const int fromEnvironment =
        qEnvironmentVariableIntValue("XYZPDF_SMOKE_TIMEOUT_MS", &configured);
    if (configured && fromEnvironment > 0)
        limitMilliseconds = fromEnvironment;

    std::thread([limitMilliseconds] {
        std::this_thread::sleep_for(std::chrono::milliseconds(limitMilliseconds));
        std::fprintf(stderr, "FAIL: mainwindow_smoke did not finish within %.1f s\n",
                     limitMilliseconds / 1000.0);
        std::fflush(stderr);
        // The test is still on its way through an event loop that will not
        // come back; waiting for any cleanup is exactly what never happens.
        std::_Exit(1);
    }).detach();
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    armFinishWatchdog();
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    const QString pdf = directory + QStringLiteral("/text.pdf");

    MainWindow window;
    window.resize(900, 700);
    window.show();

    // ---- the window takes drops at all ----
    check(window.acceptDrops(), "the window is a drop target");

    // ---- with nothing open, no tool can be chosen ----
    //
    // Every tool acts on a page, so in a window with no document all five are
    // grey. 翻译 was the one that was not: it was left off the list in
    // updateForCurrentTab() when it was added as the fourth tool, and it stayed
    // lit in an empty window until somebody read the installed copy's menu and
    // saw four grey items and one that was not (docs/store-submission.md §106).
    {
        for (const char *name : {"viewToolAction", "selectToolAction", "recognizeToolAction",
                                 "translateToolAction", "snapshotToolAction"}) {
            auto *tool = window.findChild<QAction *>(QString::fromLatin1(name));
            check(tool != nullptr, QStringLiteral("the %1 exists").arg(QLatin1String(name)));
            if (tool)
                check(!tool->isEnabled(),
                      QStringLiteral("and with no document open, %1 is grey")
                          .arg(QLatin1String(name)));
        }
    }

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

    // ---- what the status bar says about a form the reader cannot see ----
    //
    // The three sentences 打开 puts on the bar for a document whose form this version does not
    // draw: the ordinary AcroForm one (§94's, already shipped), the XFA one, and the dynamic XFA
    // one. Only one of the three can be said for a given file, so what is being pinned here is as
    // much the order as the words -- and the plain file beside them is the control: a bar that
    // says "已打开" for a document with nothing in it is what makes the other three mean anything.
    //
    // The tab is closed after every reading, so the count the blocks below expect is undisturbed.
    {
        QTemporaryDir samples;
        check(samples.isValid(), "setup: a scratch folder for the form-bearing copies");
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        const QString plain = samples.filePath(QStringLiteral("plain-form.pdf"));
        const QString packeted = samples.filePath(QStringLiteral("with-xfa.pdf"));
        const QString dynamic = samples.filePath(QStringLiteral("dynamic-xfa.pdf"));

        // One generator, three shapes: no form at all, an AcroForm with a field, and the same with
        // a packet bolted on (/NeedsRendering and no /Fields turns it dynamic).
        const auto build = [&book](const QString &file, bool withFields, bool withPacket,
                                   bool needsRendering, QString *why,
                                   bool withScript = false) -> bool {
            const auto rect = [](QPDF &qpdf, double left, double top, double right,
                                 double bottom) {
                QPDFObjectHandle value = QPDFObjectHandle::newArray();
                value.appendItem(QPDFObjectHandle::newReal(left));
                value.appendItem(QPDFObjectHandle::newReal(top));
                value.appendItem(QPDFObjectHandle::newReal(right));
                value.appendItem(QPDFObjectHandle::newReal(bottom));
                return value;
            };
            try {
                QPDF qpdf;
                qpdf.processFile(book.toUtf8().constData());
                QPDFObjectHandle page =
                    QPDFPageDocumentHelper(qpdf).getAllPages().front().getObjectHandle();
                if (withFields) {
                    QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
                    field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                    field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
                    field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
                    field.replaceKey("/T", QPDFObjectHandle::newString("onOne"));
                    field.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
                    field.replaceKey("/Rect", rect(qpdf, 60, 600, 260, 630));
                    if (withScript) {
                        // /AA is the additional-actions dictionary, and a field carrying one
                        // is a form that asks for arithmetic or validation on every keystroke
                        // -- which a build with no V8 never runs. The shape is table 226's:
                        // event keys (/K typing, /F losing the focus) each naming an action.
                        QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
                        action.replaceKey("/S", QPDFObjectHandle::newName("/JavaScript"));
                        action.replaceKey("/JS",
                                          QPDFObjectHandle::newString("event.value = 1;"));
                        QPDFObjectHandle actions = QPDFObjectHandle::newDictionary();
                        actions.replaceKey("/K", action);
                        actions.replaceKey("/F", action);
                        field.replaceKey("/AA", actions);
                    }
                    field = qpdf.makeIndirectObject(field);
                    field.replaceKey("/P", page);
                    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
                    annots.appendItem(field);
                    page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
                }
                if (withFields || withPacket) {
                    QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
                    if (withFields) {
                        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
                        fields.appendItem(page.getKey("/Annots").getArrayItem(0));
                        form.replaceKey("/Fields", fields);
                    }
                    if (needsRendering) // the catalog's key, not the form's (PDF 32000-1, table 28)
                        qpdf.getRoot().replaceKey("/NeedsRendering", QPDFObjectHandle::newBool(true));
                    if (withPacket) {
                        const QString xml = QStringLiteral(
                            "<?xml version=\"1.0\"?><xdp:xdp><template/></xdp:xdp>");
                        QPDFObjectHandle stream
                            = QPDFObjectHandle::newStream(&qpdf, xml.toUtf8().constData());
                        stream.getDict().replaceKey("/Subtype",
                                                    QPDFObjectHandle::newName("/XML"));
                        QPDFObjectHandle xfa = QPDFObjectHandle::newArray();
                        xfa.appendItem(QPDFObjectHandle::newString(""));
                        xfa.appendItem(stream);
                        form.replaceKey("/XFA", xfa);
                    }
                    qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
                }
                QPDFWriter writer(qpdf, file.toUtf8().constData());
                writer.write();
            } catch (const std::exception &e) {
                *why = QString::fromUtf8(e.what());
                return false;
            }
            return true;
        };
        QString made;
        check(build(plain, false, false, false, &made),
              QStringLiteral("setup: a copy of the book with no form in it: %1").arg(made));
        check(build(packeted, true, true, false, &made),
              QStringLiteral("setup: a copy carrying an XFA packet: %1").arg(made));
        check(build(dynamic, false, true, true, &made),
              QStringLiteral("setup: and a dynamic one: %1").arg(made));

        auto *closer = window.findChild<QAction *>(QStringLiteral("closeTabAction"));
        check(closer != nullptr, "setup: the tab can be closed again");
        const int tabsBefore = openTabs(window);
        // Closing a tab is a deleteLater(), and processEvents() does not flush deferred deletes
        // -- so a count taken right after 关闭标签 still says the tab is there. This is the same
        // three steps the window itself needs to let go of one: run the loop, send the deferred
        // deletes, run it again for whatever those set off.
        const auto closeOneTab = [&window, &closer] {
            if (!closer)
                return;
            closer->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        };
        // Each reading opens its own tab and leaves it up: the status message is set by
        // openDocument() as that document arrives, so the newest tab is the newest sentence and no
        // closing is needed between them. The tabs are let go of at the end of the block.
        int count = tabsBefore;
        const auto saidFor = [&window, &count](const QString &file) {
            dropFiles(window, {QUrl::fromLocalFile(file).toString()}, Qt::CopyAction);
            if (!waitFor([&window, &count] { return openTabs(window) == count + 1; }, 20000))
                return QStringLiteral("<no tab opened>");
            ++count;
            // Read in the same breath the tab lands: 12 s is the shorter of the two notices asked
            // for here, and waiting past it would answer "no message" about a file the bar did
            // describe.
            return window.statusBar()->currentMessage();
        };
        const QString packetSaid = saidFor(packeted);
        std::cout << "MEASURE status xfa: [" << packetSaid.toStdString() << "]\n";
        check(packetSaid.contains(QStringLiteral("XFA")),
              QStringLiteral("a packet document is named on the bar as one: %1").arg(packetSaid));
        check(!packetSaid.contains(QStringLiteral("动态 XFA")),
              QStringLiteral("and not as a dynamic one, which is the stronger of the two: %1")
                  .arg(packetSaid));

        const QString dynamicSaid = saidFor(dynamic);
        std::cout << "MEASURE status dynamic: [" << dynamicSaid.toStdString() << "]\n";
        check(dynamicSaid.contains(QStringLiteral("动态 XFA")),
              QStringLiteral("the dynamic copy gets its own sentence: %1").arg(dynamicSaid));
        check(dynamicSaid.contains(QStringLiteral("请稍候")),
              QStringLiteral("which says what another reader would show: %1").arg(dynamicSaid));

        // ---- and the pointer crossing the toolbar does not take the sentence away ----
        //
        // Found on screen (handoff-03 F9), not by any test here: a status tip -- the pointer
        // over a toolbar button -- replaced this sentence and stopped its timer, and the empty
        // tip that follows when the pointer leaves cleared the bar. So a reader who moved the
        // mouse over the toolbar never saw why their document looks the way it does. Sent to the
        // window, which is where a tip propagates to and where QMainWindow puts it on the bar.
        {
            QStatusTipEvent covering(QStringLiteral("a toolbar button's tip"));
            QApplication::sendEvent(&window, &covering);
            check(window.statusBar()->currentMessage() == QStringLiteral("a toolbar button's tip"),
                  QStringLiteral("setup: the tip shows while the pointer is over the button (%1)")
                      .arg(window.statusBar()->currentMessage()));
            QStatusTipEvent leaving{QString()};
            QApplication::sendEvent(&window, &leaving);
            check(window.statusBar()->currentMessage() == dynamicSaid,
                  QStringLiteral("and when the pointer leaves, the document's sentence is back (%1)")
                      .arg(window.statusBar()->currentMessage()));
        }

        const QString plainSaid = saidFor(plain);
        std::cout << "MEASURE status plain: [" << plainSaid.toStdString() << "]\n";
        check(!plainSaid.contains(QStringLiteral("XFA")) && !plainSaid.contains(QStringLiteral("表单")),
              QStringLiteral("a document with neither gets no notice at all: %1").arg(plainSaid));

        // The order the three are tried in still matters for what is left of them: a file with
        // fields and a packet says the packet, because that is the copy a reader without XFA falls
        // back to once the page collection moves. What does not get said any more is 表单字段 on
        // its own -- §94's sentence described an engine that drew no field contents anywhere, and
        // F7-C drew them (formpage measured 24,705 pixels inside a field's rectangle, 0 with the
        // form layer switched off). A document with only fields now gets the ordinary 已打开, and
        // this is the line that goes red if the notice comes back.
        const QString withFields = samples.filePath(QStringLiteral("only-fields.pdf"));
        check(build(withFields, true, false, false, &made),
              QStringLiteral("setup: a copy with fields and no packet: %1").arg(made));
        const QString fieldsSaid = saidFor(withFields);
        std::cout << "MEASURE status fields: [" << fieldsSaid.toStdString() << "]\n";
        check(!fieldsSaid.contains(QStringLiteral("表单")) && !fieldsSaid.contains(QStringLiteral("XFA")),
              QStringLiteral("fields alone get no notice: %1").arg(fieldsSaid));
        check(fieldsSaid.startsWith(QStringLiteral("已打开")),
              QStringLiteral("and are opened with the ordinary sentence: %1").arg(fieldsSaid));

        // handoff-04 G1 5's sentence: a form whose fields carry actions. This build has no V8
        // (docs/pdfium-plan.md sec.2 reads args.gn's pdf_enable_v8=false), so a calculation or
        // a validation the author wrote never happens, and a reader whose total did not move
        // has no way to find that out except being told. Fields alone still get nothing -- the
        // line above is the control for this one, and it is the pair that makes the row mean
        // something: a notice that also fires on a plain form would pass this check too.
        const QString withScript = samples.filePath(QStringLiteral("fields-with-script.pdf"));
        check(build(withScript, true, false, false, &made, true),
              QStringLiteral("setup: a copy whose field carries an action: %1").arg(made));
        const QString scriptSaid = saidFor(withScript);
        std::cout << "MEASURE status script: [" << scriptSaid.toStdString() << "]\n";
        check(scriptSaid.contains(QStringLiteral("脚本")),
              QStringLiteral("a scripted form is named on the bar: %1").arg(scriptSaid));
        check(scriptSaid.contains(QStringLiteral("不运行")),
              QStringLiteral("and the sentence says what will not happen: %1").arg(scriptSaid));
        check(!scriptSaid.contains(QStringLiteral("XFA")),
              QStringLiteral("it is not mixed up with the packet notice: %1").arg(scriptSaid));

        while (count > tabsBefore) {
            closeOneTab();
            --count;
        }
        check(waitFor([&window, tabsBefore] { return openTabs(window) == tabsBefore; }, 8000),
              QStringLiteral("and the window is left with the tabs it had (%1 now)")
                  .arg(openTabs(window)));
    }

    // ---- no two rows of one menu answer to the same Alt letter ----
    //
    // §116 merged a new 保存(&S) into 文档 while 拆分文件 was still written
    // "&Split Files": two rows of one menu taking the letter S, so the reader who
    // presses Alt then S gets whichever of the two Qt reaches first. The eye found
    // it during that merge and no machine could have. check_translations.py's fourth
    // rule judges how one English form writes *its own* letter -- `&Print…`, not the
    // source's `Print(&P)…` -- and says nothing about its neighbours; the mnemonics
    // screen mode opens the top-level menus and reads their row names, so it never
    // looks inside a menu. A menu grows by one row at a time, which makes this the
    // class of defect that arrives with every feature. That is why it is a test now
    // rather than a review comment.
    //
    // Both interface languages, because the letters are chosen per catalogue: the
    // Chinese rows put one on a pinyin initial, the English rows inside an English
    // word, and the two sets are free to collide in only one of them. Reading a live
    // window rather than the .ts also covers the labels composed at runtime --
    // 撤销 %1(&U) exists only after updateForCurrentTab() has run, and the
    // constructor calls it, so even a window with nothing open carries U and O.
    // The menu bar counts as a menu of its own: two top-level menus sharing a letter
    // is the same collision one level up.
    {
        struct Row
        {
            QString label;      // the row's own text, ampersand included
            QString menu;       // which menu it sits in, for the message
            unsigned letter = 0; // 0 for a row that takes no letter
        };

        // QKeySequence::mnemonic() answers with a key sequence rather than a key: empty when
        // the label carries no ampersand, and one combination when it does, with the
        // modifiers masked off by QKeyCombination::key(). Alt+f and Alt+F are one press with
        // Shift added, so a letter is folded onto its capital before anything is compared --
        // that is the identity the reader actually has.
        const auto letterOf = [](const QString &text) -> unsigned {
            const QKeySequence mnemonic = QKeySequence::mnemonic(text);
            if (mnemonic.isEmpty())
                return 0;
            const unsigned taken = static_cast<unsigned>(mnemonic[0].key());
            if (taken == static_cast<unsigned>(Qt::Key_unknown))
                return 0;
            return taken >= 'a' && taken <= 'z' ? taken - 32 : taken;
        };

        // Whether a label still has Chinese in it. Used as the receipt that a round
        // read the language it claims to have read: a catalogue that failed to load
        // would leave the English round walking the Chinese rows, both rounds green and
        // one of them testing nothing.
        const auto hasChinese = [](const QString &text) {
            for (const QChar &character : text) {
                const ushort code = character.unicode();
                if (code >= 0x3400 && code <= 0x9FFF) // the CJK ideograph blocks
                    return true;
            }
            return false;
        };

        // Every visible row under the bar, in walk order, each carrying the name of the
        // menu it came out of. Separators carry no letter and are not rows; a hidden
        // row is not one the reader can Alt to.
        const auto rowsOf = [&letterOf](MainWindow &window) {
            QList<Row> rows;
            std::function<void(const QString &, const QList<QAction *> &)> walk =
                [&](const QString &menu, const QList<QAction *> &actions) {
                    for (QAction *action : actions) {
                        if (!action || action->isSeparator() || !action->isVisible())
                            continue;
                        const QString label = action->text();
                        rows.append({label, menu, letterOf(label)});
                        if (QMenu *inside = action->menu())
                            walk(menu + QStringLiteral(" > ") + label, inside->actions());
                    }
                };
            walk(QStringLiteral("menu bar"), window.menuBar()->actions());
            return rows;
        };

        // One round's readings and verdicts. The letters are grouped per menu so that a
        // failure names the menu, the letter and the rows sharing it, which is the whole of
        // what it takes to go and fix one.
        const auto judge = [](const QString &language, const QList<Row> &rows) {
            QStringList menus;
            for (const Row &row : rows) {
                if (!menus.contains(row.menu))
                    menus.append(row.menu);
            }

            int lettered = 0;
            QStringList carryingLetters;
            for (const QString &menu : std::as_const(menus)) {
                int visible = 0;
                QMap<unsigned, QStringList> byLetter;
                for (const Row &row : rows) {
                    if (row.menu != menu)
                        continue;
                    ++visible;
                    if (row.letter == 0)
                        continue;
                    ++lettered;
                    byLetter[row.letter].append(row.label);
                }

                QStringList letters;
                for (auto taken = byLetter.constBegin(); taken != byLetter.constEnd(); ++taken)
                    letters.append(QString(1, QChar(static_cast<ushort>(taken.key()))));
                if (!byLetter.isEmpty())
                    carryingLetters.append(menu);
                std::cout << "MEASURE mnemonics [" << language.toStdString() << "] menu ["
                          << menu.toStdString() << "] rows=" << visible << " letters="
                          << letters.join(QStringLiteral(" ")).toStdString() << '\n';

                for (auto taken = byLetter.constBegin(); taken != byLetter.constEnd(); ++taken)
                    check(taken.value().size() == 1,
                          QStringLiteral("[%1] menu '%2': the letter '%3' is taken by %4 of its "
                                         "rows -- %5")
                              .arg(language, menu,
                                   QString(1, QChar(static_cast<ushort>(taken.key()))))
                              .arg(taken.value().size())
                              .arg(taken.value().join(QStringLiteral(" | "))));
            }

            std::cout << "MEASURE mnemonics [" << language.toStdString() << "]: "
                      << menus.size() << " menus, " << rows.size() << " visible rows, " << lettered
                      << " of them carrying a letter, in " << carryingLetters.size()
                      << " menus\n";

            // The walk has to get inside the menus, not only read the bar: 文档 is exactly
            // where §116's collision was, and a round that never descended would be a green
            // saying nothing was wrong. A floor rather than a count, because a menu gaining a
            // row or a whole new menu arriving is an ordinary change -- today it is seven, and
            // three menus carry no letter at all (最近打开, 转到 and Language).
            check(carryingLetters.size() >= 6,
                  QStringLiteral("[%1] setup: only %2 of its menus carried a letter, so the walk "
                                 "did not reach inside them -- it saw %3")
                      .arg(language)
                      .arg(carryingLetters.size())
                      .arg(carryingLetters.join(QStringLiteral(" | "))));
        };

        // The menu bar's own rows, which are the ones every language has to translate.
        const auto barRows = [](const QList<Row> &rows) {
            QStringList labels;
            for (const Row &row : rows) {
                if (row.menu == QStringLiteral("menu bar"))
                    labels.append(row.label);
            }
            return labels;
        };

        // Chinese first, and with no catalogue installed: the source strings are the
        // interface, so this is what a reader who never opened the Language menu sees.
        QList<Row> chineseRows;
        {
            MainWindow chinese;
            chineseRows = rowsOf(chinese);
            judge(QStringLiteral("zh_CN"), chineseRows);
        }

        // The catalogue is a build product compiled into the binary, which is why this target
        // is listed beside i18n_smoke among the translation targets: without that, the load
        // below fails and the English round walks Chinese rows while reporting English.
        QList<Row> englishRows;
        {
            QTranslator catalogue;
            const QString resource = Languages::catalogueResource(QStringLiteral("en"));
            check(catalogue.load(resource),
                  QStringLiteral("setup: the English catalogue loads (%1)").arg(resource));
            check(QCoreApplication::installTranslator(&catalogue),
                  "setup: and the English catalogue installs");

            MainWindow inEnglish;
            englishRows = rowsOf(inEnglish);
            judge(QStringLiteral("en"), englishRows);

            // Out before the next block, which reads Chinese sentences off the window this
            // test was built around.
            QCoreApplication::removeTranslator(&catalogue);
        }

        // The receipt that the second round read a second language rather than the first one
        // twice: the bar has the same rows in both, each said differently, and each English
        // one free of Chinese -- except "Language", which the product leaves untranslated on
        // purpose, because whoever needs that entry is looking at an interface they cannot
        // read. A catalogue that did not load would have left both rounds green over the same
        // labels, which is how §92's one-spelling lookup silently tested one locale.
        {
            const QStringList chineseBar = barRows(chineseRows);
            const QStringList englishBar = barRows(englishRows);
            check(!chineseBar.isEmpty() && chineseBar.size() == englishBar.size(),
                  QStringLiteral("setup: both rounds saw the whole menu bar (%1 and then %2 "
                                 "rows)")
                      .arg(chineseBar.size())
                      .arg(englishBar.size()));

            int moved = 0;
            const int pairs = qMin(chineseBar.size(), englishBar.size());
            for (int row = 0; row < pairs; ++row) {
                if (chineseBar.at(row) == englishBar.at(row)) {
                    check(englishBar.at(row) == QStringLiteral("Language"),
                          QStringLiteral("setup: the menu bar row '%1' is word for word the "
                                         "same in both languages")
                              .arg(englishBar.at(row)));
                    continue;
                }
                ++moved;
                check(!hasChinese(englishBar.at(row)),
                      QStringLiteral("setup: the English menu bar row '%1' still has Chinese "
                                     "in it")
                          .arg(englishBar.at(row)));
            }

            std::cout << "MEASURE mnemonics setup: " << moved << " of " << pairs
                      << " menu bar rows changed language\n";
            check(pairs > 0 && moved == pairs - 1,
                  QStringLiteral("setup: every menu bar row but 'Language' changed language "
                                 "(%1 of %2 did)")
                      .arg(moved)
                      .arg(pairs));
        }
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
            // All five, in the order the toolbar puts them: the list used to
            // stop at three, so 翻译 and then 截图 were added to the product
            // without this test noticing either of them (docs/review-03.md
            // §3.3 added the last one).
            const char *names[] = {"viewToolAction", "selectToolAction", "recognizeToolAction",
                                   "translateToolAction", "snapshotToolAction"};
            for (const char *name : names)
                tools.append(toolAction(name));
            check(!tools.contains(nullptr), "the five tool actions exist");
        }

        if (bar && !tools.contains(nullptr)) {
            const QList<QAction *> actions = bar->actions();
            for (QAction *action : std::as_const(tools))
                check(actions.contains(action), "and each of them is in the toolbar");
            check(actions.size() >= tools.size() && actions.mid(0, tools.size()) == tools,
                  "as the first buttons on it, in the order they were asked for");
            for (QAction *action : std::as_const(tools))
                check(action->isCheckable(), "and each of them is a checkable button");
        }

        // Exclusive: choosing the last one takes the tick off the others.
        if (tools.size() == 5) {
            tools.at(2)->trigger();
            check(tools.at(2)->isChecked() && !tools.at(0)->isChecked()
                      && !tools.at(1)->isChecked() && !tools.at(3)->isChecked()
                      && !tools.at(4)->isChecked(),
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
                // And the newest of them, which is the one this slice added.
                front->setTool(PageView::Tool::Snapshot);
                check(tools.at(4)->isChecked(), "including the snapshot tool");
                front->setTool(PageView::Tool::Select);
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

            // Where the reader is decides where a search starts: the first hit at
            // or after the page in view, which is what a reader expects and not a
            // detail the test gets to ignore. This fixture has one hit per page,
            // so without pinning the page the count below is "1 / 2" or "2 / 2"
            // depending on which earlier block last scrolled this document -- a
            // fact about the order of the file rather than about the search, and
            // the reason this assertion used to fail about half the runs.
            front->setCurrentPage(0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(front->currentPage() == 0, "the view is on the first page before searching");

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
            // The shape this is here for is the manual a reader reported: nine
            // seconds a page, and a stop only noticed between pages leaves them
            // watching. It used to be driven by dense.pdf, whose seventy-line page
            // took seconds through the old wrapper; the same page now reads in about
            // a millisecond, so a two-page document is over before a box can be
            // offered and the test would be asserting a race. What makes a job long
            // now is pages rather than lines, so this is huge.pdf: six hundred pages
            // of one line each, which is a second or so of export and long enough
            // that the box, its numbers and its button can all be read.
            const QString slowPath = directory + QStringLiteral("/huge.pdf");
            dropFiles(window, {QUrl::fromLocalFile(slowPath).toString()}, Qt::CopyAction);
            check(waitFor([&window, &slowPath] {
                      DocumentTab *front = currentDocumentTab(window);
                      return front && sameFile(front->documentPath(), slowPath);
                  }),
                  "the six-hundred-page document is in front");
            if (DocumentTab *dense = currentDocumentTab(window)) {
                const QString unwritten = output.filePath(QStringLiteral("huge.docx"));
                const int pages = dense->pageCount();
                check(pages == 600,
                      QStringLiteral("setup: and it has the pages the box will count (%1)")
                          .arg(pages));
                dense->exportToWord(unwritten);
                QProgressDialog *third = nullptr;
                check(waitFor([&window, &third] {
                          third = window.findChild<QProgressDialog *>(
                              QStringLiteral("exportProgress"));
                          return third && third->isVisible();
                      }),
                      "its export offers the box while the document is still being read");
                if (third) {
                    check(third->maximum() == pages,
                          QStringLiteral("with the whole document in it (%1 of %2 pages)")
                              .arg(third->maximum())
                              .arg(pages));
                    // The first report is the one that says the export has started,
                    // with a way out still ahead of it.
                    check(third->value() < third->maximum(),
                          QStringLiteral("and how far it has got (%1 of %2)")
                              .arg(third->value())
                              .arg(third->maximum()));
                    check(third->labelText().contains(QString::number(pages)),
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
                              QStringLiteral("within one page of where it was (%1 ms; this "
                                             "fixture's page is a few)")
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

    // ---- fit width is the document's, so changing page cannot restart it ----
    //
    // docs/store-submission.md §100's cascade. text.pdf is 595x842 followed by
    // 400x500; in a window around 900 px wide, "fit width" used to fit the page
    // being read. Fitting the narrow page made the wide one overflow, the
    // horizontal scrollbar appeared, the viewport lost 14 px of height, the
    // page at the middle of the viewport changed back to the wide one, the bar
    // disappeared -- and the two states fed each other for as long as the
    // document stayed open. §100 counted 253,000 resize events in 20 s.
    //
    // The fix fits the largest target in the document, so the page being read
    // is no longer an input. This puts the view in the state that used to
    // start the loop (second page current, then one resize event) and asks it
    // to go quiet. The watchdog above is the other half: a regression that
    // never returns still fails with a named line instead of a gate timeout.
    {
        MainWindow cascade;
        cascade.resize(900, 700);
        cascade.show();

        const QString mixed = directory + QStringLiteral("/text.pdf");
        dropFiles(cascade, {QUrl::fromLocalFile(mixed).toString()}, Qt::CopyAction);
        check(waitFor([&cascade] { return currentDocumentTab(cascade) != nullptr; }),
              "the mixed-size document is open for the fit-width cascade");

        DocumentTab *tab = currentDocumentTab(cascade);
        PageView *view = tab ? tab->view() : nullptr;
        check(view != nullptr, "and it has a page view");
        if (view) {
            check(view->pageCount() == 2,
                  QStringLiteral("with both pages laid out (%1)").arg(view->pageCount()));
            check(waitFor([view] { return view->isRendered(0); }), "the first page is drawn");
            // Asked for, not assumed: MainWindow restores the last session for a
            // file it has seen, and an earlier block in this same run has
            // already put text.pdf into fit page. The cascade needs fit width.
            view->setZoomMode(ZoomController::Mode::FitWidth);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            check(view->zoomMode() == ZoomController::Mode::FitWidth,
                  "and it is in fit width, the mode the cascade lives in");
            check(view->horizontalScrollBar()->maximum() == 0,
                  QStringLiteral("fit width leaves nothing to scroll sideways on the first page "
                                 "(range %1)")
                      .arg(view->horizontalScrollBar()->maximum()));
            const double widestFit = view->zoom();

            // The narrow page is the one whose fit used to overflow the other.
            view->goToPage(1);
            check(waitFor([view] { return view->currentPage() == 1; }),
                  QStringLiteral("the second, narrow page is current (page %1)")
                      .arg(view->currentPage()));

            // A real geometry change is what the loop rode: the window is
            // resized, the view is laid out again, the fit is recomputed for the
            // page being read, and the bar that answer brings with it takes 14 px
            // off the viewport -- which changes the page being read again.
            ResizeCounter viewportResizes;
            view->viewport()->installEventFilter(&viewportResizes);
            cascade.resize(900, 701);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            cascade.resize(900, 700);

            // Bounded on both sides, because the regression is "never settles":
            // the measurement stops asking after two seconds, then listens for
            // 400 ms of quiet. The watchdog above is the other half of this --
            // a processEvents() that never returns cannot be reached by an
            // assertion in the test that is stuck inside it.
            QElapsedTimer settling;
            settling.start();
            while (settling.elapsed() < 2000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const int afterSettling = viewportResizes.count;
            QElapsedTimer quiet;
            quiet.start();
            while (quiet.elapsed() < 400)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const int duringQuiet = viewportResizes.count - afterSettling;

            check(duringQuiet == 0,
                  QStringLiteral("the view stops resizing itself once fitted (%1 resize event(s) "
                                 "in a quiet 400 ms, %2 while settling)")
                      .arg(duringQuiet)
                      .arg(afterSettling));
            check(view->horizontalScrollBar()->maximum() == 0,
                  QStringLiteral("and no sideways scrollbar is left over (range %1)")
                      .arg(view->horizontalScrollBar()->maximum()));
            check(qFuzzyCompare(view->zoom(), widestFit),
                  QStringLiteral("fit follows the widest page, not the one being read (%1 "
                                 "against %2)")
                      .arg(view->zoom())
                      .arg(widestFit));
            view->viewport()->removeEventFilter(&viewportResizes);
        }
        cascade.hide();
    }

    // ---- the three answers, asked about a filled field ----
    //
    // handoff-04 G1's mainwindow item: 关窗 must treat a changed field the way it treats
    // a pending page operation, because the work is in the same place -- this tab and
    // nowhere else. The block below is the F8 half of that question rewritten for an
    // answer that came from a keyboard instead of a dialog: the same three buttons, and
    // three different visible outcomes, on a document whose only change is what somebody
    // typed into it.
    //
    // The file is a copy in a scratch folder, and every round compares its sha256 before
    // and after: 取消 and 不保存 must both leave the bytes alone, and only 保存 may move
    // them. That is AGENTS.md's rule read from the window's side.
    {
        // The window comes back to a size where a field is a clickable area rather than a
        // few pixels: earlier blocks leave it whatever they last asked it to be, and a
        // page drawn 74 points wide puts that field in an 8x4 box -- a click that misses
        // by rounding is a test reading its own geometry, not the program's.
        window.resize(1100, 800);
        window.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        QTemporaryDir formDir;
        check(formDir.isValid(), "setup: a scratch folder for the window's form");
        const QString formFile = formDir.filePath(QStringLiteral("window-form.pdf"));
        {
            QPDF qpdf;
            qpdf.processFile((directory + QStringLiteral("/text.pdf")).toUtf8().constData());
            std::vector<QPDFPageObjectHelper> pages =
                QPDFPageDocumentHelper(qpdf).getAllPages();
            QPDFObjectHandle page = pages.front().getObjectHandle();
            QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
            field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
            field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
            field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
            field.replaceKey("/T", QPDFObjectHandle::newString("name"));
            field.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
            field.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 10 Tf 0 g "));
            QPDFObjectHandle rect = QPDFObjectHandle::newArray();
            rect.appendItem(QPDFObjectHandle::newReal(60));
            rect.appendItem(QPDFObjectHandle::newReal(600));
            rect.appendItem(QPDFObjectHandle::newReal(260));
            rect.appendItem(QPDFObjectHandle::newReal(630));
            field.replaceKey("/Rect", rect);
            QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
            font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
            font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
            font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
            font = qpdf.makeIndirectObject(font);
            QPDFObjectHandle resources = page.getKey("/Resources");
            if (!resources.isDictionary()) {
                resources = QPDFObjectHandle::newDictionary();
                page.replaceKey("/Resources", resources);
            }
            QPDFObjectHandle fonts = resources.getKey("/Font");
            if (!fonts.isDictionary()) {
                fonts = QPDFObjectHandle::newDictionary();
                resources.replaceKey("/Font", fonts);
            }
            fonts.replaceKey("/Helv", font);
            field = qpdf.makeIndirectObject(field);
            field.replaceKey("/P", page);
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(field);
            page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            fields.appendItem(field);
            QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
            form.replaceKey("/Fields", fields);
            form.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 10 Tf 0 g "));
            QPDFObjectHandle dr = QPDFObjectHandle::newDictionary();
            QPDFObjectHandle drFonts = QPDFObjectHandle::newDictionary();
            drFonts.replaceKey("/Helv", font);
            dr.replaceKey("/Font", drFonts);
            form.replaceKey("/DR", dr);
            qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
            QPDFWriter writer(qpdf, formFile.toUtf8().constData());
            writer.write();
        }
        check(QFileInfo::exists(formFile), "setup: a form-bearing copy is on disk");

        auto *closeTabAction = window.findChild<QAction *>(QStringLiteral("closeTabAction"));
        check(closeTabAction, "setup: the close-tab item is in the window");
        const auto pump = [] {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        };
        const auto shaOf = [](const QString &file) {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly))
                return QByteArray();
            return QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha256);
        };
        const auto emptyWindow = [&window, &closeTabAction, &pump] {
            for (int guard = 0; openTabs(window) > 0 && guard < 20; ++guard) {
                bool asked = false;
                answerNextBox(QMessageBox::Discard, &asked, nullptr);
                closeTabAction->trigger();
                pump();
            }
        };

        // Open the form and change one field.
        //
        // The change is made through the form surface with an explicit page frame rather
        // than by clicking the pixels, and that is deliberate: the window has been through
        // a dozen other blocks by now and its tab area is whatever the last of them left it
        // (measured here: 100x30, in which the field is a box four pixels tall), so a click
        // would be a test reading the window's leftover layout. The route this block is
        // about is what the *window* does with a tab whose fields changed, and the click
        // itself is measured end to end in documenttab_smoke.
        const auto fillOneField = [&window, &formFile, &pump]() -> DocumentTab * {
            dropFiles(window, {QUrl::fromLocalFile(formFile).toString()}, Qt::CopyAction);
            if (!waitFor([&window, &formFile] {
                    DocumentTab *front = currentDocumentTab(window);
                    return front && front->isOpen() && front->documentPath() == formFile;
                }))
                return nullptr;
            pump();
            DocumentTab *tab = currentDocumentTab(window);
            FormFill *surface = tab->formSurface();
            if (!surface || !surface->fillable())
                return tab;
            FormFill::Frame frame;
            frame.pixels = QSize(595, 842);
            frame.quarterTurns = 0;
            // The field's /Rect is 60,600 to 260,630 in user space with the origin at the
            // page's bottom-left, so its centre is (160, 615) -- and the frame above is a
            // page drawn one pixel to the point, which makes the device point (160, 227).
            const QPointF field(160.0, 227.0);
            surface->mousePressed(0, frame, field, Qt::NoModifier);
            surface->mouseReleased(0, frame, field, Qt::NoModifier);
            surface->characterTyped(QChar(QStringLiteral("Z")[0]), Qt::NoModifier);
            surface->releaseFocus();
            pump();
            return tab;
        };

        // ---- 取消: nothing closes and nothing is written ----
        {
            emptyWindow();
            DocumentTab *tab = fillOneField();
            check(tab != nullptr, "form close: the filled document opened in the window");
            check(tab && tab->hasUnsavedEdits(),
                  "form close: and the window's tab counts a typed field as an edit");
            check(tab && tab->title().endsWith(QStringLiteral(" *")),
                  QStringLiteral("form close: the tab's title carries the star (%1)")
                      .arg(tab ? tab->title() : QString()));
            const QByteArray before = shaOf(formFile);
            bool asked = false;
            QString question;
            answerNextBox(QMessageBox::Cancel, &asked, &question);
            closeTabAction->trigger();
            pump();
            check(asked, "form close: closing the tab asks first");
            std::cout << "MEASURE form close question: [" << question.toStdString() << "]\n";
            check(question.contains(QStringLiteral("字段")),
                  "and it names the field the reader changed");
            check(openTabs(window) == 1, "form close: 取消 left the tab where it was");
            check(shaOf(formFile) == before,
                  "form close: 取消 wrote nothing to the reader's file");

            // ---- 不保存: the tab goes, the file does not change ----
            asked = false;
            answerNextBox(QMessageBox::Discard, &asked, nullptr);
            closeTabAction->trigger();
            pump();
            check(asked, "form close: the question is asked again, and 不保存 answers it");
            check(openTabs(window) == 0,
                  "form close: 不保存 closed the tab");
            check(shaOf(formFile) == before,
                  "form close: and 不保存 wrote nothing either");
        }

        // ---- 保存: the answer goes into the file, and the tab closes ----
        {
            emptyWindow();
            DocumentTab *tab = fillOneField();
            check(tab != nullptr && tab->hasUnsavedEdits(),
                  "form close: a second round, filled the same way");
            const QByteArray before = shaOf(formFile);
            bool asked = false;
            answerNextBox(QMessageBox::Save, &asked, nullptr);
            closeTabAction->trigger();
            pump();
            check(asked, "form close: 保存 is one of the three answers");
            check(openTabs(window) == 0,
                  "form close: 保存 wrote and then let the tab close");
            const QByteArray after = shaOf(formFile);
            std::cout << "MEASURE form close sha256: before "
                      << before.left(6).toHex().toStdString() << " after "
                      << after.left(6).toHex().toStdString()
                      << " (first 6 bytes of each digest; the files differ wholly)"
                      << '\n';
            check(after != before,
                  "form close: 保存 is the one answer that writes the reader's file");
            emptyWindow();
        }
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

    // ---- closing a tab while a foreground job is pumping the loop ----
    //
    // docs/review-03.md §2.4 asked whether DocumentTab::print() and
    // exportImages() have a reentrancy hole, and the answer measured here is
    // yes. Both are foreground: they render page by page on this thread and
    // call QApplication::processEvents() between pages so the progress box
    // repaints and its Cancel button works. Anything queued runs in there --
    // including 关闭标签, which takes the tab out of the strip and deletes it.
    //
    // The first version of this measurement had no guard in the way, and the
    // tab was gone before the pump's second turn: deleteLater() inside a
    // processEvents() is run by that processEvents(), which is the opposite of
    // what the comment here first claimed. So the job now says it is running
    // and MainWindow refuses to take the tab apart underneath it.
    //
    // Both halves are asserted, because a refusal that never lifts would pass
    // the first on its own: refused while the job is standing, and the same
    // close going through the moment it is not.
    {
        const QString path = directory + QStringLiteral("/text.pdf");
        window.show();
        dropFiles(window, {QUrl::fromLocalFile(path).toString()}, Qt::CopyAction);
        check(waitFor([&window] { return currentDocumentTab(window) != nullptr; }),
              "a document is open for the close-during-pump measurement");

        auto *strip = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"));
        auto *closeTab = window.findChild<QAction *>(QStringLiteral("closeTabAction"));
        QPointer<DocumentTab> tab = currentDocumentTab(window);
        check(strip != nullptr && closeTab != nullptr && tab != nullptr,
              "and the strip and its 关闭标签 are where the test expects them");

        if (strip && closeTab && tab) {
            const int before = strip->count();
            {
                // Stands in for print() or exportImages(): the same object
                // those two put on their own stack, for the same span.
                const DocumentTab::ForegroundJob job(tab);

                // Queued, so that it lands inside the pump below rather than
                // here. This is the action the menu and Ctrl+W trigger.
                QTimer::singleShot(0, &window, [closeTab] { closeTab->trigger(); });

                bool diedDuringThePump = false;
                for (int turn = 0; turn < 40 && !diedDuringThePump; ++turn) {
                    QApplication::processEvents(); // the call the export's callback makes
                    diedDuringThePump = tab.isNull();
                }
                check(!diedDuringThePump,
                      "a tab that a foreground job is standing in is not destroyed by a close "
                      "that arrives inside the job's own event pump");
                check(strip->count() == before,
                      "and it stays in the strip rather than half-leaving it");
            }

            // The job is over: the same close now goes through, so what was
            // measured above is a refusal and not a close that never arrived.
            QTimer::singleShot(0, &window, [closeTab] { closeTab->trigger(); });
            check(waitFor([strip, before] { return strip->count() == before - 1; }),
                  "once the job is done the same close takes the tab out");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(tab.isNull(), "and the tab is deleted");
        }
    }

    // ---- F8 at the window: 保存, 另存为, and the three answers on the way out ----
    //
    // The save itself is measured in documenttab_smoke, on copies, step by step. What
    // belongs to this window is the two menu items that reach it, the sentences that
    // tell a reader what the tools they just used actually did, and the question asked
    // when a window with edits is closed -- which is the one place where 取消, 不保存
    // and 保存 have to be three different visible outcomes.
    //
    // Every file here is a copy in a temporary folder: 保存 writes, and the fixtures
    // this run has been dropping all the way through are shared with every other block.
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for the window's save");

        auto *closeTabAction = window.findChild<QAction *>(QStringLiteral("closeTabAction"));
        auto *deletePages = window.findChild<QAction *>(QStringLiteral("deletePagesAction"));
        auto *save = window.findChild<QAction *>(QStringLiteral("saveEditsAction"));
        auto *saveAs = window.findChild<QAction *>(QStringLiteral("saveEditsAsAction"));
        auto *strip = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"));
        check(closeTabAction && deletePages && save && saveAs && strip,
              "setup: the close, the delete, the two save items and the strip are all in the window");

        const auto pump = [] {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        };
        // Everything the earlier blocks left open is taken out first, because what
        // follows counts tabs and answers questions for the whole window. None of it is
        // edited, so nothing asks on the way out -- the waiter below is only the safety
        // net for a tab that does.
        for (int guard = 0; openTabs(window) > 0 && guard < 40; ++guard) {
            bool asked = false;
            answerNextBox(QMessageBox::Discard, &asked, nullptr);
            closeTabAction->trigger();
            pump();
        }
        window.show();
        pump();
        check(openTabs(window) == 0,
              QStringLiteral("setup: the window starts this block empty (%1 tabs)")
                  .arg(openTabs(window)));

        const QByteArray pristine = [&directory] {
            QFile in(directory + QStringLiteral("/text.pdf"));
            if (!in.open(QIODevice::ReadOnly))
                return QByteArray();
            return QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha256);
        }();
        check(!pristine.isEmpty(), "setup: the two-page fixture is read for comparison");

        const auto shaOf = [](const QString &file) {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly))
                return QByteArray();
            return QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha256);
        };
        const auto openCopy = [&window, &scratch, &directory, &pump](const QString &name) {
            const QString file = scratch.filePath(name);
            if (!QFile::copy(directory + QStringLiteral("/text.pdf"), file))
                return QString();
            dropFiles(window, {QUrl::fromLocalFile(file).toString()}, Qt::CopyAction);
            if (!waitFor([&window, &file] {
                    DocumentTab *front = currentDocumentTab(window);
                    return front && front->isOpen() && sameFile(front->documentPath(), file);
                }))
                return QString();
            pump();
            return file;
        };
        // One page taken out: the cheapest way to hand a tab a pending list, because the
        // dialog opens, is filled in and accepted, and what it recorded is then what the
        // save is asked to write.
        const auto makeDirty = [&deletePages, &pump](const QString &pages) {
            answerNextDialog<DeletePagesDialog>(pages);
            deletePages->trigger();
            pump();
        };
        // The tabs this block opens are let go of between the halves, so that each one
        // starts with a known number of them. An edited tab asks again on the way out;
        // the answer here is always 不保存, because these tabs are the setup of a
        // sentence, not the thing being measured.
        const auto emptyWindow = [&window, &closeTabAction, &pump] {
            for (int guard = 0; openTabs(window) > 0 && guard < 40; ++guard) {
                bool asked = false;
                answerNextBox(QMessageBox::Discard, &asked, nullptr);
                closeTabAction->trigger();
                pump();
            }
            window.show();
            pump();
        };

        const QString one = openCopy(QStringLiteral("window-save-one.pdf"));
        check(!one.isEmpty(), "setup: a copy of the two-page fixture opens in the window");
        QPointer<DocumentTab> tab = currentDocumentTab(window);
        check(tab && tab->pageCount() == 2, "setup: it is the two-page document");

        // ---- the two items, what they say, and when they are offered ----
        {
            check(save && save->text() == QStringLiteral("保存(&S)"),
                  QStringLiteral("保存 carries its mnemonic inside the word (%1)")
                      .arg(save ? save->text() : QStringLiteral("<missing>")));
            check(save && save->shortcut() == QKeySequence(QStringLiteral("Ctrl+S")),
                  "and Ctrl+S reaches it");
            check(saveAs && saveAs->text() == QStringLiteral("另存为(&A)…"),
                  QStringLiteral("另存为… carries its own letter (%1)")
                      .arg(saveAs ? saveAs->text() : QStringLiteral("<missing>")));
            check(saveAs && saveAs->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                  "and Ctrl+Shift+S reaches that one");
            // Not in 文件 with the things done to a file: these two write the edits the
            // 文档 menu records, so they sit beside the tools that made them.
            bool inDocumentMenu = false;
            for (QAction *menu : window.menuBar()->actions()) {
                if (menu->menu() && menu->menu()->actions().contains(save)
                    && menu->menu()->actions().contains(saveAs))
                    inDocumentMenu = menu->text().contains(QStringLiteral("文档"));
            }
            check(inDocumentMenu, "both of them are in the 文档 menu, where the edits come from");
            check(save && !save->isEnabled() && saveAs && !saveAs->isEnabled(),
                  "and with nothing to write both are grey");

            // The row this replaces promised a new file for every edit; F8 puts the
            // writing back into the file being read. So no sentence left in this window
            // may still say 保存修改为 -- the name that row had -- and the tools that
            // record an edit have to say that it waits for 保存. Seven of them do:
            // 删除/旋转/移动/合并/加密/水印/书签.
            QString stale;
            int waitsForSave = 0;
            for (QAction *action : window.findChildren<QAction *>()) {
                if (action->toolTip().contains(QStringLiteral("保存修改为"))
                    || action->text().contains(QStringLiteral("保存修改为")))
                    stale = action->text().isEmpty() ? action->toolTip() : action->text();
                if (action->toolTip().contains(QStringLiteral("按保存")))
                    ++waitsForSave;
            }
            check(stale.isEmpty(),
                  QStringLiteral("and nothing left in the window still offers 保存修改为 (%1)")
                      .arg(stale));
            check(waitsForSave == 7,
                  QStringLiteral("seven tools say their edits wait for 保存 (%1 do)")
                      .arg(waitsForSave));
        }

        // ---- 保存 from the menu writes into the file the tab is reading ----
        {
            const QByteArray before = shaOf(one);
            makeDirty(QStringLiteral("1"));
            check(tab && tab->hasUnsavedEdits(), "the tab has a deletion to write");
            check(tab && tab->title().endsWith(QStringLiteral(" *")),
                  QStringLiteral("and the tab's own name says so (%1)")
                      .arg(tab ? tab->title() : QStringLiteral("<gone>")));
            check(save && save->isEnabled(), "so 保存 is offered");

            bool boxSeen = false;
            QString boxText;
            answerNextBox(QMessageBox::Cancel, &boxSeen, &boxText);
            save->trigger();
            pump();
            check(!boxSeen,
                  QStringLiteral("the write went through without asking anything (%1)").arg(boxText));
            check(shaOf(one) != before, "the file on disk is not the file that was there");
            check(tab && tab->pageCount() == 1 && tab->isOpen(),
                  QStringLiteral("and the tab is reading the one-page document it just wrote (%1)")
                      .arg(tab ? tab->pageCount() : -1));
            check(tab && !tab->hasUnsavedEdits(), "with nothing pending");
            check(tab && !tab->title().endsWith(QStringLiteral(" *")), "and no star");
            check(save && !save->isEnabled(), "so 保存 is grey again");
            check(window.statusBar()->currentMessage().contains(QStringLiteral("已保存")),
                  QStringLiteral("and the bar says where the bytes went (%1)")
                      .arg(window.statusBar()->currentMessage()));
            emptyWindow();
        }

        // ---- 取消 keeps the tab and writes nothing ----
        {
            const QString three = openCopy(QStringLiteral("window-cancel.pdf"));
            check(!three.isEmpty(), "setup: a fresh copy for the close question");
            QPointer<DocumentTab> asked = currentDocumentTab(window);
            const QByteArray before = shaOf(three);
            makeDirty(QStringLiteral("1"));
            check(asked && asked->hasUnsavedEdits(), "the tab is edited for the way out");

            bool boxSeen = false;
            QString boxText;
            answerNextBox(QMessageBox::Cancel, &boxSeen, &boxText);
            window.close();
            pump();
            check(boxSeen, "closing the window asks about the edits");
            check(boxText.contains(QStringLiteral("还带着没有写进任何文件的修改")),
                  QStringLiteral("and names the edits rather than saying \"unsaved changes\" (%1)")
                      .arg(boxText));
            check(window.isVisible(), "取消 leaves the window where it was");
            check(openTabs(window) == 1,
                  QStringLiteral("with its tab (%1)")
                      .arg(openTabs(window)));
            check(asked && asked->hasUnsavedEdits(), "and the pending list still there");
            check(shaOf(three) == before, "and not one byte written");

            // ---- and 不保存 closes, still writing nothing ----
            bool discardSeen = false;
            answerNextBox(QMessageBox::Discard, &discardSeen, nullptr);
            window.close();
            pump();
            check(discardSeen, "the same question, answered 不保存 this time");
            // Found on screen by handoff-04 G2: the button was Qt's stock 丢弃 / Discard
            // ("throw away"), not the Windows answer to "save first?".
            check(lastDiscardLabel
                      == QCoreApplication::translate("WindowDialogs", "不保存(&N)"),
                  QStringLiteral("and that answer's button says 不保存 / Don't Save (it said \"%1\")")
                      .arg(lastDiscardLabel));
            check(!window.isVisible(), "and the window goes");
            check(shaOf(three) == before,
                  "with the file exactly as it was -- which is what separates this answer from "
                  "the one below");
            emptyWindow();
        }

        // ---- 保存 on the way out writes first, then closes ----
        {
            const QString four = openCopy(QStringLiteral("window-save-close.pdf"));
            check(!four.isEmpty(), "setup: a copy for the third answer");
            QPointer<DocumentTab> closing = currentDocumentTab(window);
            const QByteArray before = shaOf(four);
            makeDirty(QStringLiteral("1"));
            check(closing && closing->hasUnsavedEdits(), "with a deletion pending");

            bool boxSeen = false;
            answerNextBox(QMessageBox::Save, &boxSeen, nullptr);
            window.close();
            pump();
            check(boxSeen, "the question is answered 保存");
            check(shaOf(four) != before, "and the file was written before the window went");
            check(!window.isVisible(), "then the window closed");
            check(closing && !closing->hasUnsavedEdits(), "with nothing left pending");
            emptyWindow();
        }

        // ---- two edited tabs are one question for the window, with a count ----
        //
        // The counted sentence is why this half is here at all: three tabs popping
        // three questions tells a reader nothing about what they are giving up, and the
        // number is a %n sentence, which is the shape i18n_smoke's probe covers.
        {
            const QString first = openCopy(QStringLiteral("window-count-one.pdf"));
            const QString second = openCopy(QStringLiteral("window-count-two.pdf"));
            check(!first.isEmpty() && !second.isEmpty(),
                  "setup: two copies open for the counted question");
            check(openTabs(window) == 2,
                  QStringLiteral("setup: and the window has two tabs (%1)").arg(openTabs(window)));

            for (int index = 0; index < strip->count(); ++index) {
                strip->setCurrentIndex(index);
                pump();
                auto *each = qobject_cast<DocumentTab *>(strip->widget(index));
                if (each && !each->hasUnsavedEdits())
                    makeDirty(QStringLiteral("1"));
            }
            int dirtyTabs = 0;
            for (int index = 0; index < strip->count(); ++index) {
                auto *each = qobject_cast<DocumentTab *>(strip->widget(index));
                if (each && each->hasUnsavedEdits())
                    ++dirtyTabs;
            }
            check(dirtyTabs == 2,
                  QStringLiteral("setup: both tabs carry something unwritten (%1 do)")
                      .arg(dirtyTabs));

            bool boxSeen = false;
            QString boxText;
            answerNextBox(QMessageBox::Cancel, &boxSeen, &boxText);
            window.close();
            pump();
            check(boxSeen, "closing asks once, not twice");
            check(boxText.contains(QStringLiteral("2 个标签带着没有写进任何文件的修改")),
                  QStringLiteral("and says how many tabs it is about (%1)").arg(boxText));
            check(window.isVisible(), "取消 leaves both of them alone");
            check(openTabs(window) == 2,
                  QStringLiteral("with both tabs still in the strip (%1)").arg(openTabs(window)));
            check(shaOf(first) == pristine && shaOf(second) == pristine,
                  "and neither of the two files was written by a question that was refused");

            bool discardSeen = false;
            answerNextBox(QMessageBox::Discard, &discardSeen, nullptr);
            window.close();
            pump();
            check(discardSeen && !window.isVisible(), "不保存 then lets the window go");
            check(shaOf(first) == pristine && shaOf(second) == pristine,
                  "and giving up the two lists wrote neither file");
        }
    }


    if (failures == 0) {
        std::cout << "files dropped on the window open, the three tools are wired, and so is Find\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
