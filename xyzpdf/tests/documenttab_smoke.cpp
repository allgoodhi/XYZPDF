// The wiring between the view and the tab.
//
// ---- why this test exists ----
//
// Every part of this had a test and the whole of it did not. render_smoke
// checks PageRenderer on its own, layout_smoke and zoom_smoke check arithmetic,
// and none of them noticed that zooming and rotating left the page grey
// forever: PageView asked for a render *before* telling the tab to make the
// work in flight pointless, so every result came back for a generation that had
// already been superseded and was dropped -- and the page was recorded as "on
// its way" for the size that had just been asked for, so nothing ever asked
// again.
//
// The assertion therefore has to be about what ends up on screen, not about
// signals being emitted. "A request went out" is exactly what was true while
// this was broken.
//
// It runs on the offscreen platform: the point is the plumbing, and a window
// appearing on somebody's desktop while their tests run is not.

#include "../documenttab.h"
#include "../src/edit/documenteditor.h"
#include "../src/theme/thememanager.h"
#include "../src/ui/bookmarkdialog.h"
#include "../src/ui/deletepagesdialog.h"
#include "../src/ui/findbar.h"
#include "../src/ui/mergepagesdialog.h"
#include "../src/ui/movepagesdialog.h"
#include "../src/ui/ocrdialog.h"
#include "../src/ui/protectpagesdialog.h"
#include "../src/ui/propertiesdialog.h"
#include "../src/ui/rotatepagesdialog.h"
#include "../src/ui/splitpagesdialog.h"
#include "../src/ui/thumbnailpanel.h"
#include "../src/ui/watermarkdialog.h"
#include "../src/view/pageview.h"
#include "../src/view/zoomcontroller.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QThreadPool>
#include <QTabWidget>
#include <QTimer>

// The overwrite measurement asks the same question the save's last step does, and that is a Win32
// call rather than a Qt one.
//
// The #undef is not tidiness: rpcndr.h (pulled in by windows.h) does `#define small char`, and this
// file already had a local named `small` several hundred lines below the include -- so adding the
// header rewrote a line nobody was working on, and the compiler complained about a QString two
// variables away from the change. undef'ing it here keeps the pre-existing code meaning what it
// said; the alternative was to rename something this slice has no business renaming.
#include <windows.h>
#undef small

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <functional>
#include <memory>
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

int measurements = 0;

// Printed rather than asserted, and counted -- §78's rule: a reading a later slice compares against
// a recorded number has to go through the counter, or the "N measurement(s)" line and the lines the
// run printed stop describing the same run.
void note(const QString &what)
{
    std::cout << "note: " << what.toStdString() << '\n';
    ++measurements;
}

// Pumps the event loop until `ready` says so, or the time runs out. The
// renders are on other threads and come back through queued signals, so there
// is nothing to wait on but the event loop itself.
bool waitFor(const std::function<bool()> &ready, int timeoutMilliseconds = 30000)
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

PageView *viewOf(DocumentTab &tab)
{
    return tab.view();
}

// How many rows of the thumbnail strip have a picture in them.
//
// Read off the model rather than off the panel, because what is being asked is
// what a reader would see: a row with no decoration is a page number on an
// empty tile. An icon that exists but is null counts as nothing, which is what
// QIcon::isNull() says.
int thumbnailsRendered(const QAbstractItemView *strip)
{
    const QAbstractItemModel *model = strip->model();
    if (!model)
        return 0;
    int rendered = 0;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QVariant decoration = model->index(row, 0).data(Qt::DecorationRole);
        if (!qvariant_cast<QIcon>(decoration).isNull())
            ++rendered;
    }
    return rendered;
}

// Everything the labels of a dialog say, run together.
//
// The properties dialog is a form of labels and nothing else, so "what does it
// show" is exactly this string -- and asserting on it rather than on row
// indices means a row added above does not move the test.
QString labelsOf(const QWidget *dialog)
{
    QStringList said;
    for (const QLabel *label : dialog->findChildren<QLabel *>())
        said.append(label->text());
    return said.join(QLatin1Char(0x0a));
}

// Answers one of the tab's own dialogs the way a reader would: waits for it to
// open, types `pages` into its page box, presses the button that accepts it.
//
// Armed *before* the menu action is called, because the action ends in exec()
// and nothing after that line runs until the dialog is gone. Bounded both ways:
// a dialog that never appears costs a few seconds rather than the run, and one
// that appears and cannot be filled in is rejected, so the exec() underneath
// always returns.
template <typename Dialog>
void answerNextDialog(const QString &pages)
{
    auto *timer = new QTimer;
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
            timer->deleteLater();
            return;
        }
        if (++*ticks > 100)
            timer->deleteLater(); // never appeared; nothing to unblock
    });
    timer->start(50);
}

// One mouse event, straight to the viewport.
//
// Sent to the viewport rather than to the view because that is where Qt
// delivers a mouse to a scroll area from: QAbstractScrollArea filters its
// viewport's events and hands them on with the coordinates already in the
// viewport's system, which is the one pageRectInViewport() reports in.
void sendMouse(QWidget *viewport, QEvent::Type type, const QPointF &position,
               bool buttonHeld = false, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const Qt::MouseButton button = Qt::LeftButton;
    const Qt::MouseButtons buttons =
        type == QEvent::MouseMove
            ? (buttonHeld ? Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(Qt::NoButton))
            : Qt::MouseButtons(Qt::LeftButton);
    QMouseEvent event(type, position, viewport->mapToGlobal(position), button, buttons, modifiers);
    QApplication::sendEvent(viewport, &event);
}

} // namespace

int main(int argc, char **argv)
{
    // Before QApplication: the platform has to be chosen before it starts.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    const QString path = directory + QStringLiteral("/text.pdf");

    DocumentTab tab;
    tab.resize(900, 700);
    tab.show();

    if (tab.open(path) != Document::Status::Ready) {
        std::cerr << "FAIL: could not open the fixture " << path.toStdString() << ": "
                  << tab.lastError().toStdString() << '\n';
        return 1;
    }

    PageView *view = viewOf(tab);

    // ---- the first page appears ----
    {
        check(waitFor([view] { return view->hasImageFor(0); }),
              "the page on screen is rendered and drawn");
        check(tab.pageCount() == 2, "the document's pages are known to the tab");
    }

    // ---- the thumbnails wait for the sidebar to be opened ----
    //
    // docs/review-03.md §2.1: opening a document used to start a walk over
    // every one of its pages, rendering each into the strip, whether or not the
    // sidebar had ever been shown -- and every one of those renders holds the
    // global PDFium lock the page the reader is waiting for needs. On a scanned
    // page a 120 px thumbnail costs 17 ms against the whole page's 30 (measured
    // with tools/pdfprobe.cpp), so a 500-page scan spent about 8.5 seconds of
    // that lock on pictures for a panel nobody had opened.
    //
    // Both halves are asserted, because only one of them is visible in the
    // pictures: nothing is rendered while the sidebar is shut, *and* it is
    // rendered once it is opened. A deferral that never ends would pass the
    // first check on its own.
    {
        auto *strip = tab.findChild<ThumbnailPanel *>();
        check(strip != nullptr, "the tab has a thumbnail strip");
        if (strip) {
            check(!strip->isVisible(), "the sidebar starts shut");
            check(strip->waiting(), "so the strip is holding a job rather than running one");
            // Through the base class: ThumbnailPanel has a member called `model`
            // of its own, which hides QAbstractItemView::model() from anyone
            // holding the derived type.
            const auto *stripView = static_cast<const QAbstractItemView *>(strip);
            check(stripView->model() != nullptr
                      && stripView->model()->rowCount() == tab.pageCount(),
                  "the strip has a row per page even before any of them is rendered");

            // Given every chance to render: the walk is a worker and its results
            // arrive as queued events, so a loop pumped for a while is what the
            // claim "nothing happened" has to survive.
            waitFor([] { return false; }, 400);
            check(thumbnailsRendered(strip) == 0, "and no page has been rendered into it");

            tab.setSidebarVisible(true);
            check(strip->isVisible(),
                  "opening the sidebar puts the strip on screen (text.pdf has no bookmarks, so "
                  "it is the tab in front)");
            check(waitFor([strip, &tab] { return thumbnailsRendered(strip) == tab.pageCount(); }),
                  "and that is when the pages are rendered into it");
            check(!strip->waiting(), "the job it was holding has been taken on");

            // ---- and only the strip that can be seen is rendered ----
            //
            // The other half of the same argument (docs/review-03.md §2.2):
            // the walk used to render every page of the document and keep
            // every picture for as long as it was open -- 120x170 of ARGB
            // each, about 81 KB, so 49 MB on a 600-page book, outside the
            // render cache's budget entirely. A strip shows a handful of rows;
            // holding six hundred is the same mistake review-02 found in
            // PageView::images one layer down.
            {
                const QString book = directory + QStringLiteral("/huge.pdf");
                check(tab.open(book) == Document::Status::Ready, "the 600-page book opens");
                check(tab.pageCount() == 600, "and it really is 600 pages");
                check(waitFor([strip] { return strip->heldCount() > 0; }),
                      "its strip renders the rows at the top");

                // Given every chance to render the rest, the way the old walk
                // would have: if the band were not bounding this, six hundred
                // pages of a text document render in well under a second
                // (0 ms each, measured with pdfprobe).
                waitFor([] { return false; }, 1500);
                const int held = strip->heldCount();
                check(held < 60,
                      QStringLiteral("but it holds only the rows near the viewport, not the "
                                     "document (holding %1 of 600)").arg(held));
                check(held == thumbnailsRendered(strip),
                      QStringLiteral("and what it says it holds is what is in the model "
                                     "(%1 against %2)").arg(held).arg(thumbnailsRendered(strip)));

                // Not bounded by rendering nothing: the far end of the
                // document gets its pictures when it is the far end that is
                // being looked at.
                auto *bar = strip->verticalScrollBar();
                bar->setValue(bar->maximum());
                check(waitFor([strip] {
                          const QAbstractItemModel *rows =
                              static_cast<const QAbstractItemView *>(strip)->model();
                          return !qvariant_cast<QIcon>(
                                      rows->index(rows->rowCount() - 1, 0).data(Qt::DecorationRole))
                                      .isNull();
                      }),
                      "scrolling to the end renders the last page into the strip");
                check(strip->heldCount() < 60,
                      QStringLiteral("and the count is still bounded after the scroll (%1)")
                          .arg(strip->heldCount()));

                check(tab.open(path) == Document::Status::Ready, "and the first fixture is back");
            }

            // Back to where the rest of this file expects to find the tab.
            tab.setSidebarVisible(false);
        }
    }

    // ---- a fit mode fits the document, not the page being read ----
    //
    // The decision that broke a loop which spun at 100% of a core for as long
    // as a mixed-size document was open (docs/store-submission.md §100, found
    // while measuring something else): fit width used to fit the current page,
    // so on this fixture -- 595x842 then 400x500 -- the zoom followed the
    // scroll position. The rest of the loop is the horizontal scrollbar:
    // narrow page fitted -> the wide one overflows -> scrollbar appears -> the
    // viewport loses height -> a different page is current -> fit the wide one
    // -> everything fits -> the scrollbar goes -> round again, forever.
    //
    // What is asserted here is only that the fitted zoom is the widest page's.
    // **The cascade itself is not pinned by this file**: it needs the window,
    // the strip and a geometry where the two states disagree, and when it is
    // reintroduced mainwindow_smoke does not go red -- it stops finishing
    // (measured: 253,000 resize events, killed at 300 s). That is an owed test,
    // not a covered one.
    {
        tab.setZoomMode(ZoomController::Mode::FitWidth);
        // A width of its own, so that a resize really is delivered: the block
        // above showed and hid the sidebar, so the viewport the zoom was last
        // computed against is not the one this block is about.
        tab.resize(880, 700);
        waitFor([] { return false; }, 300);
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "page 1 is drawn at fit width");

        // The widest page of this fixture is page 1 at 595 pt. Fit width is
        // that page's fit whatever else is on screen, so the zoom times 595
        // is the viewport's width less the margin the fit leaves.
        const double zoom = tab.zoom();
        const double fittedWidth = 595.0 * zoom;
        const double viewportWidth = view->viewport()->width();
        check(fittedWidth <= viewportWidth && fittedWidth > viewportWidth - 40,
              QStringLiteral("fit width fits the widest page of the document (595 pt at %1 is "
                             "%2 px in a %3 px viewport)")
                  .arg(zoom).arg(fittedWidth).arg(viewportWidth));

        tab.resize(900, 700);
        waitFor([] { return false; }, 200);
        check(waitFor([view] { return view->isRendered(0); }), "and back");
    }

    // ---- reading the file again (docs/review-03.md §3.4) ----
    //
    // For the document that is being regenerated while it is being read. The
    // three things worth pinning are not "it reopens": they are that the
    // reader is put back where they were, that a file which has gone does not
    // take the document off the screen, and that a job standing on this object
    // says no rather than pulling the floor out (§100's ForegroundJob).
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for the reload");
        const QString copy = scratch.filePath(QStringLiteral("reload-me.pdf"));
        check(QFile::copy(path, copy), "setup: a copy of the fixture to read and then delete");

        check(tab.open(copy) == Document::Status::Ready, "the copy opens");
        view->goToPage(1);
        tab.setZoom(1.5);
        tab.rotateBy(90);
        check(waitFor([&tab] { return tab.currentPage() == 1; }), "the reader is on page 2 of it");

        QString error;
        check(tab.reload(&error), QStringLiteral("it reads again (%1)").arg(error));
        check(tab.isOpen(), "and is still open");
        check(tab.currentPage() == 1, "on the page the reader was on");
        check(qFuzzyCompare(tab.zoom(), 1.5),
              QStringLiteral("at the zoom they had set (%1)").arg(tab.zoom()));
        check(tab.rotation() == 90,
              QStringLiteral("and the way up they had turned it (%1)").arg(tab.rotation()));
        check(waitFor([view] { return view->isRendered(1); }), "and the page is drawn again");

        // A job standing on the tab: the same guard the window uses.
        {
            const DocumentTab::ForegroundJob job(&tab);
            QString busy;
            check(!tab.reload(&busy), "a reload is refused while a foreground job is running");
            check(!busy.isEmpty(), "and says why");
        }

        // ---- and the file cannot go, which is the other half of §3.4 ----
        //
        // This started out as "delete it under the reader and check the reload
        // refuses without emptying the tab". The delete fails: PDFium opens
        // the file without FILE_SHARE_DELETE, so for as long as a document is
        // on screen Windows will not let anyone remove or replace it -- §66
        // measured that for the preview pane and fixed it there by reading a
        // copy; the main window still holds the reader's own file.
        //
        // So the branch in reload() that answers "it is not there any more" is
        // **not reachable from here**, and the assertion below is the reason
        // why, pinned. If somebody makes the main window read a copy too, this
        // line goes red -- and that is the moment to come back and test the
        // branch properly.
        check(!QFile::remove(copy),
              "the open document cannot be deleted out from under the reader (PDFium holds the "
              "handle; see §66 and docs/review-03.md §3.4)");
        check(tab.isOpen() && tab.pageCount() == 2, "and it is still open and whole");

        check(tab.open(path) == Document::Status::Ready, "the fixture is back for what follows");
        tab.setZoomMode(ZoomController::Mode::FitWidth);
        view->setRotation(0);
    }

    // ---- the strip shows what a save would write (docs/review-03.md §1.3) ----
    //
    // Every structural edit used to be invisible: delete a page and the page was
    // still there, turn one and it was not turned, until the edits were written
    // to a new file and that file opened in another tab. He chose the cheapest
    // of the ways out: the main view goes on showing the file, and the thumbnail
    // strip shows the pages a save would write -- in that order, that way up.
    //
    // Driven through the dialogs a reader uses, because the edit list belongs to
    // the tab and the tab only lets it be changed that way.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        check(tab.open(book) == Document::Status::Ready, "the 40-page fixture opens");
        const int pages = tab.pageCount();
        check(pages == 40, QStringLiteral("and it is 40 pages (%1)").arg(pages));

        auto *strip = tab.findChild<ThumbnailPanel *>();
        auto *bar = tab.findChild<QTabWidget *>();
        tab.setSidebarVisible(true);
        if (bar && strip)
            bar->setCurrentWidget(strip);
        const int stripTab = bar && strip ? bar->indexOf(strip) : -1;
        const QString plainTitle = stripTab >= 0 ? bar->tabText(stripTab) : QString();

        if (strip && stripTab >= 0) {
            check(strip->rowCount() == pages, "before any edit, a row per page");
            check(!tab.thumbnailsShowPendingEdits(), "and the strip is the file as it is");

            // Delete page 2.
            answerNextDialog<DeletePagesDialog>(QStringLiteral("2"));
            tab.deletePages();
            check(tab.hasUnsavedEdits(), "the deletion is recorded");
            check(strip->rowCount() == pages - 1,
                  QStringLiteral("and the strip has one row fewer (%1 of %2)")
                      .arg(strip->rowCount())
                      .arg(pages));
            check(strip->rowAt(1).sourcePage == 2,
                  QStringLiteral("the second row is now the third page of the file (row 2 shows "
                                 "page %1)").arg(strip->rowAt(1).sourcePage + 1));
            check(tab.thumbnailsShowPendingEdits(), "the tab knows the strip is a preview");
            check(bar->tabText(stripTab) != plainTitle,
                  QStringLiteral("and the strip's own tab says so (\"%1\")")
                      .arg(bar->tabText(stripTab)));
            check(tab.pageCount() == pages, "while the document on screen still has every page");

            // Turn page 1 right.
            answerNextDialog<RotatePagesDialog>(QStringLiteral("1"));
            tab.rotatePages();
            check(strip->rowAt(0).rotation == 90,
                  QStringLiteral("the first row is shown turned a quarter right (%1)")
                      .arg(strip->rowAt(0).rotation));
            check(waitFor([strip] {
                      const QAbstractItemModel *m =
                          static_cast<const QAbstractItemView *>(strip)->model();
                      const QIcon icon = qvariant_cast<QIcon>(m->index(0, 0).data(Qt::DecorationRole));
                      const QList<QSize> sizes = icon.availableSizes();
                      return !sizes.isEmpty() && sizes.first().width() > sizes.first().height();
                  }),
                  "and its picture is landscape, which is what a portrait page turned is");

            // A row is a page of the file, not a position: clicking the second row
            // goes to page 3 of what is on screen.
            Q_EMIT strip->clicked(static_cast<const QAbstractItemView *>(strip)->model()->index(1, 0));
            check(waitFor([&tab] { return tab.currentPage() == 2; }),
                  QStringLiteral("clicking the second row goes to the page it shows (now on %1)")
                      .arg(tab.currentPage() + 1));

            // And back, all the way.
            tab.undoEdit();
            tab.undoEdit();
            check(!tab.hasUnsavedEdits(), "two undos take both edits back");
            check(strip->rowCount() == pages && strip->rowAt(1).sourcePage == 1
                      && strip->rowAt(0).rotation == 0,
                  "and the strip is the file as it is again");
            check(!tab.thumbnailsShowPendingEdits() && bar->tabText(stripTab) == plainTitle,
                  "with its tab back to its plain name");
        }

        tab.setSidebarVisible(false);
        check(tab.open(path) == Document::Status::Ready, "the first fixture is back");
    }

    // ---- the properties dialog (PLAN M2) ----
    //
    // Two halves, because they fail apart: what the tab gathers out of the
    // document, and what the dialog makes of it. docs/review-03.md §3.1 is why
    // this is new -- DocMetadata, the backend's metadata() and its test all
    // existed, and no window had ever shown any of it.
    //
    // The gathering is asserted through propertiesInfo() rather than by opening
    // the dialog: showProperties() ends in exec(), and a test that has to find a
    // modal window and press a button on it hangs for the full timeout on the
    // day the window does not appear.
    {
        view->goToPage(1);
        const PropertiesDialog::Info info = tab.propertiesInfo();
        check(QFileInfo(info.path).fileName() == QStringLiteral("text.pdf"),
              "the properties name the file that is open");
        check(info.pageCount == tab.pageCount(), "and its page count");
        check(info.fileBytes == QFileInfo(info.path).size(), "and its size on disk");
        check(info.sizedPage == 1,
              "the page measured is the one being read, not page 1 -- a PDF may mix sizes");
        view->goToPage(0);
        const PropertiesDialog::Info onFirst = tab.propertiesInfo();
        check(onFirst.sizedPage == 0, "reading page 1 measures page 1");
        // This fixture's two pages are deliberately different sizes, which is
        // the whole reason the row names a page instead of claiming one size
        // for the document.
        check(onFirst.pagePoints != info.pagePoints,
              QStringLiteral("and the two pages of this fixture measure differently "
                             "(%1x%2 against %3x%4)")
                  .arg(onFirst.pagePoints.width())
                  .arg(onFirst.pagePoints.height())
                  .arg(info.pagePoints.width())
                  .arg(info.pagePoints.height()));

        PropertiesDialog::Info made = info;
        // A4 by hand, so that the millimetre arithmetic is checked against a
        // number everyone knows rather than against whatever the fixture is.
        made.pagePoints = QSizeF(595, 842);
        // Every field filled except the author, and both dates valid, so that
        // there is exactly one empty value in the dialog. Counted rather than
        // searched for: with several fields empty, "it says （无） somewhere"
        // passes even when the rule that puts it there has been taken out --
        // measured, that is what the first control of this block did.
        made.metadata.title = QStringLiteral("A Title");
        made.metadata.subject = QStringLiteral("A Subject");
        made.metadata.keywords = QStringLiteral("a, b");
        made.metadata.creator = QStringLiteral("A Creator");
        made.metadata.producer = QStringLiteral("A Producer 9.9");
        made.metadata.creationDate = QDateTime(QDate(2026, 1, 2), QTime(3, 4));
        made.metadata.modificationDate = made.metadata.creationDate;
        made.metadata.author.clear();
        PropertiesDialog filled(nullptr, made);
        const QString shown = labelsOf(&filled);
        check(shown.contains(QStringLiteral("text.pdf")), "the dialog shows the file name");
        check(shown.contains(QStringLiteral("A Producer 9.9")),
              "and the producer string, which is the field people copy out");
        check(shown.contains(QStringLiteral("595")) && shown.contains(QStringLiteral("210")),
              "and the page in points and in millimetres (595 pt is 210 mm)");
        check(shown.count(QStringLiteral("（无）")) == 1,
              QStringLiteral("the one empty field says so rather than being blank (counted %1)")
                  .arg(shown.count(QStringLiteral("（无）"))));
        check(!shown.contains(QStringLiteral("没有填写任何")),
              "and a file with one field filled in is not called empty");

        PropertiesDialog bare(nullptr, info);
        check(labelsOf(&bare).contains(QStringLiteral("没有填写任何")),
              "a file that says nothing about itself is called that, once, at the top");
    }

    // ---- zooming still shows a page ----
    //
    // The regression. Changing the zoom drops the images, so the check is that
    // a page comes back afterwards -- and it is checked from nothing, because
    // the "has an image" answer is what a user sees as "not a grey rectangle".
    {
        const double before = view->zoom();
        tab.setZoom(before * 2.0);

        check(!view->hasImageFor(0),
              "changing the zoom drops the image that no longer fits the page");
        check(waitFor([view] { return view->hasImageFor(0); }),
              "and a page is rendered again at the new size -- this is what was broken");
        check(std::fabs(view->zoom() - before * 2.0) < 0.001,
              "at the zoom that was asked for");
    }

    // ---- rotating still shows a page ----
    {
        const int before = view->rotation();
        tab.rotateBy(90);
        check(view->rotation() == (before + 90) % 360, "the rotation was taken");

        check(!view->hasImageFor(0), "the old image is dropped, because it is the wrong way up");
        check(waitFor([view] { return view->hasImageFor(0); }),
              "and a page is rendered again the right way up");
    }

    // ---- a factor chosen by hand survives a resize ----
    //
    // The second regression: setZoom() used to leave the controller in "fit
    // width", so the next resize (or one notch of the wheel) replaced the
    // factor the user had typed with whatever fitted.
    {
        view->setZoom(2.0);
        check(view->zoomMode() == ZoomController::Mode::Percent,
              "setting a factor by hand takes the view out of a fit mode");
        check(waitFor([view] { return view->hasImageFor(0); }), "and the page is rendered");

        tab.resize(700, 520);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(std::fabs(view->zoom() - 2.0) < 0.001,
              "and a resize does not throw it away");

        // While a fit mode is the one in force, a resize does change the zoom --
        // that is the whole point of a fit mode, and the check above would pass
        // just as well if the fit had stopped working altogether.
        view->setZoomMode(ZoomController::Mode::FitWidth);
        const double fitted = view->zoom();
        check(view->zoomMode() == ZoomController::Mode::FitWidth, "fit width is in force");
        tab.resize(520, 700);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(std::fabs(view->zoom() - fitted) > 0.001,
              "and a narrower window fits a narrower page");
    }

    // ---- a jump announces one page, not two ----
    //
    // goToPage() moves the scrollbar, which used to run the scroll handler,
    // which recomputed the current page from the middle of the viewport and
    // announced that as well -- so the page box and the status bar flickered
    // through two numbers, the second of which was often the page after the one
    // that was asked for.
    {
        int announcements = 0;
        int last = -1;
        const QMetaObject::Connection listening =
            QObject::connect(view, &PageView::currentPageChanged, view, [&](int page) {
                ++announcements;
                last = page;
            });

        tab.setCurrentPage(1);
        check(announcements == 1,
              QStringLiteral("jumping announces the page once, not once per scroll event (got %1)")
                  .arg(announcements));
        check(last == 1, "and announces the page that was asked for");

        // Off again before the locals go out of scope: the connection lives as
        // long as the view does, and a lambda that captures locals by reference
        // is a use-after-free the moment anything else scrolls.
        QObject::disconnect(listening);
    }

    // ---- a page larger than the screen is rendered as a band ----
    //
    // At 800% a page is 7140 by 10000 device pixels -- 288 MB for the one page,
    // more than the entire render budget, on a machine where "fit width" is
    // already 33 MB a page. A page bigger than a few viewports is therefore
    // rendered as the band that is on screen, and this is the assertion that it
    // happens rather than being described in a comment.
    {
        QVector<RenderToken> asked;
        const QMetaObject::Connection listening = QObject::connect(
            view, &PageView::pageImageNeeded, view,
            [&asked](const RenderToken &token) { asked.append(token); });

        view->setZoom(4.0);
        check(waitFor([view] { return view->hasImageFor(0); }), "a banded page arrives");

        check(!asked.isEmpty(), "the zoomed page was asked for");
        const RenderToken band = asked.last();
        check(!band.clip.isNull(), "as a band rather than as a whole page");
        check(band.imageSize() != band.pixels,
              "so the image is smaller than the page it came from");

        const qint64 viewportPixels = qint64(view->viewport()->width())
                                      * view->viewport()->height();
        check(qint64(band.imageSize().width()) * band.imageSize().height()
                  <= 4 * viewportPixels,
              QStringLiteral("and no bigger than a few screens (%1 px against a %2 px screen)")
                  .arg(band.imageSize().width() * band.imageSize().height())
                  .arg(viewportPixels));

        // And a small scroll stays inside the band that was already rendered. A
        // band that changed on every pixel of scrolling would be a render per
        // frame, which is worse than the memory banding saves.
        const auto requestsForFrontPage = [&asked] {
            int count = 0;
            for (const RenderToken &token : asked) {
                if (token.page == 0)
                    ++count;
            }
            return count;
        };
        const int before = requestsForFrontPage();
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + 40);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        check(requestsForFrontPage() == before, "a small scroll does not ask for it again");

        // See the note above the jump test: the connection outlives this block.
        QObject::disconnect(listening);
    }

    // ---- the images held do not grow with how far somebody has scrolled ----
    //
    // This is the one that mattered: the view used to keep every page it had
    // ever been given, so scrolling grew memory without limit while the target
    // file's pages were 33 to 52 MB each. Forty pages, scrolled to the end, has
    // to leave a handful held rather than forty.
    {
        const QString manyPages = directory + QStringLiteral("/many-pages.pdf");
        if (QFileInfo::exists(manyPages)) {
            check(tab.open(manyPages) == Document::Status::Ready, "the long fixture opens");
            check(waitFor([view] { return view->hasImageFor(0); }), "its first page is drawn");

            for (int page = 1; page < tab.pageCount(); ++page) {
                view->goToPage(page);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            check(waitFor([view, &tab] { return view->hasImageFor(tab.pageCount() - 1); }),
                  "and so is the last one");

            // One page, the neighbour above and the neighbour below: three, and
            // at most a couple more if the prefetch allowance is generous. Forty
            // would be the unbounded version.
            check(view->heldImageCount() <= 5,
                  QStringLiteral("only the pages in view and nearby are held (holding %1 of %2)")
                      .arg(view->heldImageCount())
                      .arg(tab.pageCount()));
        } else {
            std::cerr << "FAIL: " << manyPages.toStdString() << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        }
    }

    // ---- a render that produced nothing does not leave a page grey ----
    //
    // An out-of-memory render comes back as a null image, and the view used to
    // treat that as no news: the page stayed recorded as "asked for", so nothing
    // ever asked again and the placeholder was permanent. Every page that failed
    // once stayed grey for the rest of the session.
    {
        const QString path2 = directory + QStringLiteral("/text.pdf");
        check(tab.open(path2) == Document::Status::Ready, "the small fixture reopens");

        RenderToken outstanding;
        int requestsForPage0 = 0;
        const QMetaObject::Connection listening = QObject::connect(
            view, &PageView::pageImageNeeded, view,
            [&outstanding, &requestsForPage0](const RenderToken &token) {
                if (token.page == 0) {
                    ++requestsForPage0;
                    outstanding = token;
                }
            });

        // A fresh request, and then failure for it before the event loop can
        // deliver the real one.
        view->setZoom(2.0);
        check(!outstanding.isValid(), "the token captured from the signal has no generation yet");

        const int before = requestsForPage0;
        view->setPageImage(outstanding, QImage());
        check(waitFor([&requestsForPage0, before] { return requestsForPage0 > before; }),
              "the page is asked for again after a render that produced nothing");
        check(waitFor([view] { return view->hasImageFor(0); }), "and ends up with an image");

        // Off again before `outstanding` and the counter go out of scope: the
        // next block scrolls and renders, and a live connection into locals
        // that no longer exist is a crash rather than a test failure.
        QObject::disconnect(listening);
    }

    // ---- the page itself is white, not whatever is behind it ----
    //
    // A PDF page has no background: the renderer draws the content, and the
    // white paper is supplied by whoever shows the result. Draw the content
    // straight onto the surround and the page comes out the surround's colour
    // -- a grey page under perfectly correct text, which is exactly what the
    // report was. Asserted off the widget rather than out of the code, because
    // the colour is the thing the user is looking at.
    {
        view->goToPage(0);
        // Rendered, not merely held: a band from the other end of the page is
        // an image the view is about to replace, and sampling before it lands
        // is sampling the placeholder.
        check(waitFor([view] { return view->isRendered(0); }),
              "the page on screen is fully rendered");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "and it has somewhere to be");

        const QImage shot = view->grab().toImage();
        check(!shot.isNull(), "the view can be sampled");

        // Inside the page, near its top-left corner, where an ordinary document
        // has margin: paper, and nothing else. The failure message carries
        // everything that decides what that pixel can be, because "not white"
        // is the symptom of five different things.
        const QPoint inside(int(page.left()) + 8, int(page.top()) + 8);
        const QColor onPage = shot.pixelColor(inside);
        check(onPage == QColor(255, 255, 255),
              QStringLiteral("the page is paper-coloured, not the backdrop (%1,%2,%3 at %4,%5; "
                             "page %6,%7 %8x%9; viewport %10x%11; zoom %12; image %13)")
                  .arg(onPage.red())
                  .arg(onPage.green())
                  .arg(onPage.blue())
                  .arg(inside.x())
                  .arg(inside.y())
                  .arg(page.left())
                  .arg(page.top())
                  .arg(page.width())
                  .arg(page.height())
                  .arg(view->viewport()->width())
                  .arg(view->viewport()->height())
                  .arg(view->zoom())
                  .arg(view->hasImageFor(0) ? QStringLiteral("held")
                                            : QStringLiteral("missing")));

        // Beside the page, which is where the surround is supposed to show --
        // the same grey it has always been, now that the page is not drawn on
        // top of it.
        const QColor beside = shot.pixelColor(qMax(0, int(page.left()) - 4),
                                             int(page.top()) + 8);
        const QColor expected = ThemeManager::currentColors().pageBackdrop;
        check(beside == expected,
              QStringLiteral("and beside it is still the backdrop (%1,%2,%3, expected %4,%5,%6)")
                  .arg(beside.red())
                  .arg(beside.green())
                  .arg(beside.blue())
                  .arg(expected.red())
                  .arg(expected.green())
                  .arg(expected.blue()));
    }

    // ---- every stylesheet rule points at the asset it means ----
    //
    // A stylesheet is a string, so a rule can name the wrong picture and only a screenshot would
    // notice. One did: the tab's close button was given the combo box's down chevron (the same token
    // two rules away), which reads as "there is a menu here", closes nothing, and stays plausible
    // looking until someone clicks it.
    for (const AppTheme theme : { AppTheme::Default, AppTheme::DefaultDark }) {
        const QString sheet = ThemeManager::styleSheetFor(theme);
        const int at = sheet.indexOf(QStringLiteral("QTabBar::close-button"));
        const int end = at < 0 ? -1 : sheet.indexOf(QLatin1Char('}'), at);
        const QString rule = (at < 0 || end < 0) ? QString() : sheet.mid(at, end - at).trimmed();
        check(rule.contains(QStringLiteral("tab_close_")) && !rule.contains(QStringLiteral("chevron")),
              QStringLiteral("the tab close button names a close image, not another widget's arrow (%1)")
                  .arg(rule.isEmpty() ? QStringLiteral("no rule found") : rule));
    }

    // ---- dragging over the text selects it ----
    //
    // The end-to-end version of textlayer_smoke: real mouse events, on a real
    // page, turned into a real selection. What it checks is not the exact
    // characters -- those depend on the font's metrics, which are not this
    // program's business -- but that a drag selects part of the document's own
    // text, that the same drag backwards selects the same thing, and that a
    // click selects nothing.
    {
        // The tool this block is about. It is also the default, and saying so
        // here is the point: what a drag does is now a mode, and a test that
        // does not name the mode it is exercising is a test that reads as
        // though the old implicit rule were still in force.
        view->setTool(PageView::Tool::Select);

        const QString path3 = directory + QStringLiteral("/text.pdf");
        check(tab.open(path3) == Document::Status::Ready, "the fixture reopens for selecting");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "the page is drawn");

        // The document's own text, to compare a drag against. Select-all is
        // also the one case where the selection covers every page, so it is
        // worth checking on its own.
        tab.selectAll();
        const QString whole = tab.selectedText();
        check(whole.contains(QStringLiteral("XYZPDF one")), "select all reaches the first page");
        check(whole.contains(QLatin1Char('\n')), "and separates the second page from it");
        check(tab.hasSelection(), "select all leaves a selection");
        tab.clearSelection();
        check(!tab.hasSelection(), "and clearing it leaves none");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "the page is on screen");

        // Across the first line. The fixture draws its text near the top left
        // of the page; these fractions of it are inside that line, so the
        // assertions below do not depend on the font being any set size.
        const QPointF left(page.left() + page.width() * 0.12, page.top() + page.height() * 0.09);
        const QPointF right(page.left() + page.width() * 0.30, page.top() + page.height() * 0.09);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, left, true);
        sendMouse(view->viewport(), QEvent::MouseMove, right, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, right);

        const QString dragged = tab.selectedText();
        check(!dragged.isEmpty(),
              QStringLiteral("dragging across the text selects some of it (got \"%1\")")
                  .arg(dragged));
        check(whole.contains(dragged), "and what it selected is part of the document's text");
        check(dragged != whole, "a drag along one line is not the whole document");

        // Backwards over the same span: the same text. This is the case that
        // gets written wrong, and the one a user hits the moment they overshoot
        // a word and drag back over it.
        view->clearSelection();
        sendMouse(view->viewport(), QEvent::MouseButtonPress, right, true);
        sendMouse(view->viewport(), QEvent::MouseMove, left, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, left);
        check(tab.selectedText() == dragged, "and the same text dragged backwards");

        // A click, which is not a selection.
        view->clearSelection();
        sendMouse(view->viewport(), QEvent::MouseButtonPress, left);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, left);
        check(!tab.hasSelection(), "a click selects nothing");

        // ---- the pointer says what is under it ----
        //
        // An I-beam over a line of text and an arrow everywhere else, which is
        // how every other program says "this can be selected". The tracking
        // flag is checked as well: without it the handler is never called in
        // the real program, and this test -- which sends its events by hand --
        // would pass anyway.
        check(view->viewport()->hasMouseTracking(),
              "the view is told about moves with no button held");

        sendMouse(view->viewport(), QEvent::MouseMove, left);
        check(view->viewport()->cursor().shape() == Qt::IBeamCursor,
              QStringLiteral("the pointer is an I-beam over text (shape %1 at %2,%3 of a page "
                             "at %4,%5 %6x%7)")
                  .arg(int(view->viewport()->cursor().shape()))
                  .arg(left.x())
                  .arg(left.y())
                  .arg(page.left())
                  .arg(page.top())
                  .arg(page.width())
                  .arg(page.height()));

        sendMouse(view->viewport(), QEvent::MouseMove, QPointF(4, 2));
        check(view->viewport()->cursor().shape() == Qt::ArrowCursor,
              "and an arrow away from it");

        // And it is visible. The wash is the only part of this a user can see,
        // and a selection that is correct in the clipboard but invisible on the
        // page is a selection nobody would believe in.
        tab.selectAll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QImage shot = view->grab().toImage();
        const QColor washed = shot.pixelColor(left.toPoint());
        check(washed != QColor(255, 255, 255),
              QStringLiteral("the selection is drawn on the page (%1,%2,%3)")
                  .arg(washed.red())
                  .arg(washed.green())
                  .arg(washed.blue()));
        check(washed.blue() > washed.red(), "in the selection colour rather than the page's");
    }

    // ---- the two drag tools do not answer to each other's gestures ----
    //
    // What this milestone was about. Until the tools existed, a drag meant
    // whichever of the two things the pointer was pointing at, and the two
    // features fought over the same gesture: selecting text on a scan was a
    // box, and a box over a line was a selection. Now the mode decides, and
    // this is the pair of assertions that says so -- the same drag, over the
    // same page, doing one thing and then the other.
    //
    // blank.pdf has no text layer, so a selection drag there has nothing to
    // select: in Select mode it must do nothing at all, and in Recognize mode
    // it must draw a box.
    {
        const QString blankPath = directory + QStringLiteral("/blank.pdf");
        if (!QFileInfo::exists(blankPath)) {
            std::cerr << "FAIL: " << blankPath.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            check(tab.open(blankPath) == Document::Status::Ready, "the blank fixture opens");
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

            int captures = 0;
            int capturedPage = -1;
            QRectF captured;
            const QMetaObject::Connection listening = QObject::connect(
                view, &PageView::regionCaptured, view,
                [&](int page, const QRectF &points) {
                    ++captures;
                    capturedPage = page;
                    captured = points;
                });

            const QRectF page = view->pageRectInViewport(0);
            const QPointF from(page.left() + page.width() * 0.3,
                               page.top() + page.height() * 0.3);
            const QPointF to(page.left() + page.width() * 0.6,
                             page.top() + page.height() * 0.6);

            // ---- and first: the selection tool, where the same drag does nothing ----
            view->setTool(PageView::Tool::Select);
            sendMouse(view->viewport(), QEvent::MouseMove, QPointF(4, 2));
            check(view->viewport()->cursor().shape() == Qt::ArrowCursor,
                  "the selection tool's pointer is not a hand or a crosshair");

            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);
            check(captures == 0, "a selection drag on a page of scans draws no box");
            check(!tab.hasSelection(), "and selects nothing, there being no text to select");

            // ---- then the recognition tool, where it draws the box ----
            view->setTool(PageView::Tool::Recognize);
            sendMouse(view->viewport(), QEvent::MouseMove, from);
            check(view->viewport()->cursor().shape() == Qt::CrossCursor,
                  "the recognition tool's pointer is a crosshair");

            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

            check(captures == 1, "the drag reports exactly one region");
            check(capturedPage == 0, "from the page it was dragged on");
            // The page's own points, not the screen's: a 595x842 page dragged
            // from 30% to 60% is those fractions of 595 and 842, whatever the
            // zoom and the window did to the pixels in between.
            const QSizeF points(595, 842);
            const QRectF expected(0.3 * points.width(), 0.3 * points.height(),
                                  0.3 * points.width(), 0.3 * points.height());
            check(std::fabs(captured.left() - expected.left()) < 1.5
                      && std::fabs(captured.top() - expected.top()) < 1.5
                      && std::fabs(captured.width() - expected.width()) < 1.5
                      && std::fabs(captured.height() - expected.height()) < 1.5,
                  QStringLiteral("in the page's own coordinates (got %1,%2 %3x%4, wanted %5,%6 "
                                 "%7x%8)")
                      .arg(captured.left())
                      .arg(captured.top())
                      .arg(captured.width())
                      .arg(captured.height())
                      .arg(expected.left())
                      .arg(expected.top())
                      .arg(expected.width())
                      .arg(expected.height()));

            // A click is not a region: it is how a box drag is cancelled, and
            // it must not open a window asking to be recognized.
            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, from);
            check(captures == 1, "a click reports no region");

            // ---- and a box over text, which is the case the tool exists for ----
            //
            // A picture inside a page of text, a table read as a picture, a
            // heading in a scan: the box is drawn over whatever is there and
            // nothing is selected. This is what the mode buys over the old
            // "the pointer decides" rule, where a drag here was a selection.
            check(tab.open(directory + QStringLiteral("/text.pdf"))
                      == Document::Status::Ready,
                  "the text fixture reopens");
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

            const QRectF textPage = view->pageRectInViewport(0);
            const QPointF onLine(textPage.left() + textPage.width() * 0.12,
                                 textPage.top() + textPage.height() * 0.09);
            const QPointF across(textPage.left() + textPage.width() * 0.30,
                                 textPage.top() + textPage.height() * 0.09);

            // A box has two dimensions: these two corners are above and below
            // the line, not just either side of it. A drag along one line is a
            // selection gesture, and in this tool it is a box with no height,
            // which the minimum size throws away.
            const QPointF boxFrom(textPage.left() + textPage.width() * 0.08,
                                  textPage.top() + textPage.height() * 0.05);
            const QPointF boxTo(textPage.left() + textPage.width() * 0.40,
                                textPage.top() + textPage.height() * 0.13);

            sendMouse(view->viewport(), QEvent::MouseButtonPress, boxFrom, true);
            sendMouse(view->viewport(), QEvent::MouseMove, boxTo, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, boxTo);
            check(captures == 2, "a box over a line of text is still a box");
            check(!tab.hasSelection(), "and selects nothing");

            // ---- and the selection tool selects the very same line ----
            view->setTool(PageView::Tool::Select);
            sendMouse(view->viewport(), QEvent::MouseButtonPress, onLine, true);
            sendMouse(view->viewport(), QEvent::MouseMove, across, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, across);
            check(captures == 2, "while the selection tool draws no box");
            check(tab.hasSelection(), "and selects the text instead");

            // Off again before the locals go out of scope: the connection
            // outlives this block, and it captures them by reference.
            QObject::disconnect(listening);
        }
    }

    // ---- the hand drags the page and touches nothing on it ----
    //
    // The third tool, and the one with the least to get wrong -- which is why
    // it is worth asserting rather than assuming: a pan that also selected
    // something, or a pan in the wrong direction, is a tool nobody would use
    // twice.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "a document is open for the hand");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for the hand");

        view->setTool(PageView::Tool::View);
        // Scrolled somewhere with room in both directions, so that a pan that
        // ran off the end of the document would still be visible as a value
        // that did not move.
        view->horizontalScrollBar()->setValue(100);
        view->verticalScrollBar()->setValue(200);
        const int beforeX = view->horizontalScrollBar()->value();
        const int beforeY = view->verticalScrollBar()->value();
        const QRectF beforeRect = view->pageRectInViewport(0);

        const QRectF page = view->pageRectInViewport(0);

        const QPointF from(page.left() + page.width() * 0.5, page.top() + page.height() * 0.5);
        const QPointF to(from.x() + 40, from.y() + 30);

        sendMouse(view->viewport(), QEvent::MouseMove, from);
        check(view->viewport()->cursor().shape() == Qt::OpenHandCursor,
              "the hand tool's pointer is an open hand");

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        check(view->viewport()->cursor().shape() == Qt::ClosedHandCursor,
              "which closes while the page is being dragged");
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);
        check(view->viewport()->cursor().shape() == Qt::OpenHandCursor, "and opens again");

        // The paper follows the pointer: dragging right and down shows what is
        // to the left and above, so the scroll position moves against the drag
        // by exactly its own amount.
        check(view->horizontalScrollBar()->value() == beforeX - 40
                  && view->verticalScrollBar()->value() == beforeY - 30,
              QStringLiteral("the page followed the pointer (%1,%2 from %3,%4)")
                  .arg(view->horizontalScrollBar()->value())
                  .arg(view->verticalScrollBar()->value())
                  .arg(beforeX)
                  .arg(beforeY));
        check(qRound(view->pageRectInViewport(0).left() - beforeRect.left()) == 40
                  && qRound(view->pageRectInViewport(0).top() - beforeRect.top()) == 30,
              "which moved the page on screen by the same amount");
        check(!tab.hasSelection(), "and selected nothing");
    }

    // ---- a drag over the second line of a page selects that line ----
    //
    // The reported bug, end to end. On a document with more than one line per
    // page, dragging over any line but the first used to select nothing a reader
    // could see: the index it anchored at was found by bisecting on a predicate
    // a line break makes non-monotone, and came back from somewhere else in the
    // document. It was reported as "selection works in English and not in
    // Chinese" -- the document was Chinese, and every fixture here had exactly
    // one line per page, so the line break was in none of them.
    //
    // The second line of this fixture is at y between 93 and 110 points of a
    // 595x842 page, which is where the drag below is aimed.
    {
        view->setTool(PageView::Tool::Select);
        check(tab.open(directory + QStringLiteral("/lines.pdf")) == Document::Status::Ready,
              "the multi-line fixture opens");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

        const QRectF page = view->pageRectInViewport(0);
        const QPointF from(page.left() + page.width() * 0.15, page.top() + page.height() * 0.115);
        const QPointF to(page.left() + page.width() * 0.35, page.top() + page.height() * 0.125);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        const QString selected = tab.selectedText();
        check(!selected.isEmpty(),
              QStringLiteral("dragging over the second line selects something (got \"%1\")")
                  .arg(selected));
        check(QStringLiteral("second line here").contains(selected),
              QStringLiteral("and it is that line's own text (got \"%1\")").arg(selected));
    }

    // ---- the same box, with the snapshot tool: pixels, and no recognition ----
    //
    // docs/review-03.md §3.3. Copying a crop was reachable before this tool
    // existed -- through the recognition window's 复制图片 button -- which
    // means a reader who wants a picture has to ask for 识别 and wait for the
    // engine to read something nobody will look at. Both halves are asserted:
    // the picture arrives on the clipboard, and no recognition window opens.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a snapshot");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for it");

        view->setTool(PageView::Tool::Snapshot);
        view->clearSelection();
        const int dialogsBefore = tab.findChildren<OcrDialog *>().size();
        QGuiApplication::clipboard()->clear();

        int copiedWidth = 0;
        int copiedHeight = 0;
        QObject::connect(&tab, &DocumentTab::regionCopied, &tab,
                         [&copiedWidth, &copiedHeight](int width, int height) {
                             copiedWidth = width;
                             copiedHeight = height;
                         });

        const QRectF page = view->pageRectInViewport(0);
        const QPointF from(page.left() + page.width() * 0.05,
                           page.top() + page.height() * 0.05);
        const QPointF to(page.left() + page.width() * 0.45, page.top() + page.height() * 0.13);
        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        check(waitFor([&copiedWidth] { return copiedWidth > 0; }),
              "a box drawn with the snapshot tool reports a picture");
        const QImage onClipboard = QGuiApplication::clipboard()->image();
        check(!onClipboard.isNull(), "and the clipboard has one in it");
        check(onClipboard.width() == copiedWidth && onClipboard.height() == copiedHeight,
              QStringLiteral("of the size the window is told to say (%1x%2 against %3x%4)")
                  .arg(onClipboard.width())
                  .arg(onClipboard.height())
                  .arg(copiedWidth)
                  .arg(copiedHeight));
        // A crop of about two fifths of the width and the better part of a
        // tenth of the height, rendered larger than the screen shows it: the
        // numbers are loose because the scale is the recognizer's, but a crop
        // that came out a pixel wide or the whole page would fail this.
        check(onClipboard.width() > 200 && onClipboard.width() < onClipboard.height() * 6,
              QStringLiteral("and a shape that is the box rather than the page (%1x%2)")
                  .arg(onClipboard.width())
                  .arg(onClipboard.height()));

        // The half that is the point of the tool existing.
        waitFor([] { return false; }, 300);
        check(tab.findChildren<OcrDialog *>().size() == dialogsBefore,
              "and no recognition window was opened for a picture nobody asked to read");
        check(!tab.hasSelection(), "nor was any text selected");

        view->setTool(PageView::Tool::Select);
    }

    // ---- a box over the text: read, not selected ----
    //
    // The case the whole feature is for, and the one its own test did not
    // cover: the drag on the blank fixture proves a window opens and says
    // nothing about what is in it. Here the box is drawn around the fixture's
    // line of text, and what has to come back is the words that are in it.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a box over its text");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for the box");

        view->setTool(PageView::Tool::Recognize);
        view->clearSelection();
        const int dialogsBefore = tab.findChildren<OcrDialog *>().size();

        const QRectF page = view->pageRectInViewport(0);
        // Around the line of text, with room to spare: the fixture draws it
        // near the top left of a 595x842 page, at y between 63 and 80 points.
        const QPointF from(page.left() + page.width() * 0.05,
                           page.top() + page.height() * 0.05);
        const QPointF to(page.left() + page.width() * 0.45, page.top() + page.height() * 0.13);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        check(!tab.hasSelection(), "a box over text does not select it");

        OcrDialog *dialog = nullptr;
        check(waitFor([&tab, dialogsBefore, &dialog] {
                  const QList<OcrDialog *> dialogs = tab.findChildren<OcrDialog *>();
                  if (dialogs.size() <= dialogsBefore)
                      return false;
                  dialog = dialogs.last();
                  return true;
              }),
              "the box opens a recognition window");

        QPlainTextEdit *recognized = dialog ? dialog->findChild<QPlainTextEdit *>() : nullptr;
        check(recognized != nullptr, "which has somewhere to show the text");
        if (recognized) {
            check(waitFor([recognized] { return recognized->isEnabled(); }, 30000),
                  "and the recognition finishes");
            const QString text = recognized->toPlainText().simplified();
            check(text.contains(QStringLiteral("XYZPDF")),
                  QStringLiteral("with the text that was inside the box (got \"%1\")").arg(text));
        }
    }

    // ---- searching the document ----
    //
    // The whole way round: a needle typed into the bar, a scan that runs off the
    // UI thread, the count that comes back, stepping through the hits (wrapping
    // at both ends), the highlight on the page, and the search being forgotten
    // when the document it belonged to is closed.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the two-page fixture opens for searching");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn for the search");

        // ---- the bar is shown where the search belongs ----
        tab.showFindBar();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        check(tab.findBarWidget() && tab.findBarWidget()->isVisible(),
              "ask for the find bar and it appears");
        if (tab.findBarWidget()) {
            auto *field = tab.findBarWidget()->findChild<QLineEdit *>(QStringLiteral("findField"));
            // The bar's own focus widget rather than hasFocus(), which is false
            // unless the window is active -- and on the offscreen platform it is
            // not. What is being checked is that the caret has been given to the
            // field, which is what Ctrl+F twice is for.
            check(field && tab.findBarWidget()->focusWidget() == field,
                  "with the caret in the field, ready to be typed in");
        }

        // ---- a needle in two pages ----
        //
        // The fixture says "XYZPDF one" on page 1 and "XYZPDF two" on page 2, so
        // this is a search that has to cross a page boundary to be right.
        tab.startSearch(QStringLiteral("XYZPDF"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
              QStringLiteral("two hits in the document (found %1)").arg(tab.searchMatchCount()));
        check(tab.currentSearchMatch() == 0, "and the first of them is in front");
        check(view->currentPage() == 0, "which is on the page that was being read");

        // ---- and the bar says so ----
        //
        // The label was in the bar from the day it was written and nothing ever
        // filled it in: the count was only reachable through this tab's API,
        // which is not where a reader looks for it. Asserted on the text rather
        // than on the API for that reason, and loosely -- the numbers are the
        // requirement, the wording is the bar's business.
        if (tab.findBarWidget()) {
            auto *count = tab.findBarWidget()->findChild<QLabel *>(QStringLiteral("findCount"));
            check(count != nullptr, "the bar has somewhere to show the count");
            if (count) {
                const QString shown = count->text();
                check(shown.contains(QStringLiteral("1")) && shown.contains(QStringLiteral("2")),
                      QStringLiteral("and it says which of how many (got \"%1\")").arg(shown));
            }
        }

        // ---- it is on the page, and visible ----
        {
            const QRectF page = view->pageRectInViewport(0);
            // The fixture draws its text near the top left of a 595x842 page; the
            // view has just been scrolled to the match, so the box is sampled
            // where the match is rather than where it was.
            const QPointF inside(page.left() + page.width() * 0.12,
                                 page.top() + page.height() * 0.09);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QImage shot = view->grab().toImage();
            const QColor marked = shot.pixelColor(inside.toPoint());
            check(marked != QColor(255, 255, 255),
                  QStringLiteral("the hit is washed on the page (%1,%2,%3 at %4,%5)")
                      .arg(marked.red())
                      .arg(marked.green())
                      .arg(marked.blue())
                      .arg(inside.x())
                      .arg(inside.y()));
            check(marked.red() > marked.blue(), "in the highlight colour, not the page's");
        }

        // ---- stepping, and wrapping at both ends ----
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == 1, "the next hit is the one on page 2");
        check(view->currentPage() == 1, "and the view went to page 2 with it");
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == 0, "stepping past the last hit wraps to the first");
        tab.stepSearch(-1);
        check(tab.currentSearchMatch() == 1, "and stepping back from the first wraps to the last");

        // ---- case, both ways ----
        tab.startSearch(QStringLiteral("xyzpdf"), true);
        check(waitFor([&tab] { return !tab.isSearching(); }), "a case-sensitive search finishes");
        check(tab.searchMatchCount() == 0,
              QStringLiteral("and finds nothing in a document that has it in capitals (found %1)")
                  .arg(tab.searchMatchCount()));
        tab.startSearch(QStringLiteral("xyzpdf"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
              "while the same needle without case finds both");

        // ---- nothing to find ----
        tab.startSearch(QStringLiteral("nothing like this is in the document"), false);
        check(waitFor([&tab] { return !tab.isSearching(); }), "a needle that is not there finishes");
        check(tab.searchMatchCount() == 0, "with no hits");
        check(tab.currentSearchMatch() == -1, "and nothing in front");
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == -1, "and stepping does nothing rather than crashing");

        tab.startSearch(QString(), false);
        check(waitFor([&tab] { return !tab.isSearching(); }), "an empty needle finishes");
        check(tab.searchMatchCount() == 0, "and matches nothing rather than everything");

        // ---- the same thing on a document with more than one line per page ----
        //
        // Which is where selection used to break, and a search that highlights
        // the wrong box is the same bug wearing a hat.
        check(tab.open(directory + QStringLiteral("/lines.pdf")) == Document::Status::Ready,
              "the multi-line fixture opens");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

        tab.startSearch(QStringLiteral("second"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 1; }),
              QStringLiteral("one hit for the word on the second line (found %1)")
                  .arg(tab.searchMatchCount()));

        // ---- whole words, through the box a reader ticks ----
        //
        // The rule itself is textsearch_smoke's; what is checked here is that
        // the box reaches the scan at all. docs/review-03.md §3.2: PLAN M2 asks
        // for 大小写与全词选项 and only the first of the two existed.
        {
            tab.startSearch(QStringLiteral("secon"), false, false);
            check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 1; }),
                  QStringLiteral("part of a word is found when whole words are not asked for "
                                 "(found %1)").arg(tab.searchMatchCount()));
            tab.startSearch(QStringLiteral("secon"), false, true);
            check(waitFor([&tab] { return !tab.isSearching(); }), "and the same with the option on");
            check(tab.searchMatchCount() == 0,
                  QStringLiteral("finds nothing, because it is part of \"second\" (found %1)")
                      .arg(tab.searchMatchCount()));
            check(tab.searchWholeWord(), "and the tab remembers which it was asked for");

            // The box itself, since that is the thing that is new on screen: it
            // starts clear, and ticking it is what asks again.
            auto *whole = tab.findBarWidget()
                              ? tab.findBarWidget()->findChild<QCheckBox *>(
                                    QStringLiteral("findWholeBox"))
                              : nullptr;
            check(whole != nullptr, "the find bar has a whole-word box");
            if (whole) {
                check(!whole->isChecked(), "which starts clear, like every other reader's");
                whole->setChecked(true);
                check(waitFor([&tab] { return tab.searchWholeWord(); }),
                      "and ticking it is what asks the tab again");
                whole->setChecked(false);
                check(waitFor([&tab] { return !tab.searchWholeWord(); }), "and clearing it, back");
            }
            tab.startSearch(QStringLiteral("second"), false, false);
            check(waitFor([&tab] { return !tab.isSearching(); }), "and the state is put back");
        }

        // ---- and a search does not outlive its document ----
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "another document opens");
        check(tab.searchMatchCount() == 0,
              QStringLiteral("and the search of the previous one is gone (found %1)")
                  .arg(tab.searchMatchCount()));
    }

    // ---- a document is replaced while its pages are still rendering ----
    //
    // The race, made reproducible rather than left to luck: open a document,
    // ask for pages, and replace the document before those renders come back --
    // five times over. A render that is still running is inside the document
    // that QPdfDocument::load() is about to replace, and Qt's global PDFium lock
    // does not cover the reload itself: this used to end in a fail-fast inside
    // open(), which the test saw as "a page is drawn" timing out and then the
    // process dying.
    //
    // A reader hits it by opening a second document while the first is still
    // painting -- rare on a two-page file, ordinary on a five-hundred-page scan.
    // DocumentTab::open() now waits for the renders in flight before it reloads.
    {
        const QString manyPages = directory + QStringLiteral("/many-pages.pdf");
        const QString small = directory + QStringLiteral("/text.pdf");
        if (QFileInfo::exists(manyPages)) {
            for (int attempt = 0; attempt < 5; ++attempt) {
                check(tab.open(manyPages) == Document::Status::Ready,
                      QStringLiteral("round %1: the long fixture opens").arg(attempt + 1));
                // Deliberately not waited for: the renders are what the next
                // open has to survive.
                view->goToPage(attempt + 3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

                check(tab.open(small) == Document::Status::Ready,
                      QStringLiteral("round %1: and the next document opens over the renders "
                                     "still in flight")
                          .arg(attempt + 1));
            }
            // Back to the first page before waiting: the loops above left the
            // view scrolled wherever the last document's page 4 was, and a page
            // that is not on screen is never rendered -- which is not the
            // question being asked here.
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }),
                  "and a page is drawn afterwards, so nothing was left wedged");
        }
    }

    // The recognition a box drag kicks off runs on the global pool. Whatever
    // it decided to do, the test does not leave work behind at exit: a process
    // teardown under a running WinRT call is a hang, not a failure.
    QThreadPool::globalInstance()->waitForDone(30000);

    // ---- 导出为 Word：the tab's own half of it ----
    //
    // The conversion itself is wordexport_smoke's business. What is this test's
    // is the wiring around it: that the work runs off the UI thread, that the
    // answer comes back as a signal, and that the tab knows it is busy -- the
    // last of which is what keeps a document from being reloaded under it.
    {
        const QString path = directory + QStringLiteral("/text.pdf");
        check(tab.open(path) == Document::Status::Ready, "the fixture opens for export");

        QTemporaryDir output;
        check(output.isValid(), "there is somewhere to export to");
        const QString file = output.filePath(QStringLiteral("tab.docx"));

        bool answered = false;
        WordExportReport answer;
        int progressReports = 0;
        int lastReported = 0;
        const QMetaObject::Connection listening = QObject::connect(
            &tab, &DocumentTab::wordExportFinished, &tab, [&](const WordExportReport &report) {
                answered = true;
                answer = report;
            });
        // The progress the window puts in the status bar, which is the only sign
        // of life a long export has -- and the only part of this that has to
        // cross from the worker thread to this one.
        const QMetaObject::Connection progress = QObject::connect(
            &tab, &DocumentTab::wordExportProgress, &tab, [&](int done, int total) {
                ++progressReports;
                lastReported = total > 0 ? done : lastReported;
            });

        tab.exportToWord(file);
        check(waitFor([&answered] { return answered; }, 30000), "the export answers");
        check(progressReports > 0, QStringLiteral("having reported its progress (%1 reports)")
                                       .arg(progressReports));
        check(progressReports == 0 || lastReported == 2,
              QStringLiteral("up to the last page (%1)").arg(lastReported));
        QObject::disconnect(progress);

        check(answer.ok, QStringLiteral("with a file written (%1)").arg(answer.error));
        check(answer.path == file, "at the path it was given");
        check(answer.pages == 2, QStringLiteral("of both pages (%1)").arg(answer.pages));
        check(QFileInfo::exists(file) && QFileInfo(file).size() > 0, "and it is on disk");
        check(!tab.isExporting(), "and the tab is no longer busy");

        QObject::disconnect(listening);
    }

    // ---- night reading ----
    //
    // Asserted off the pixels because the pixels are the feature. The setting
    // existed for milestones with nothing reading it -- a stored preference that
    // changed nothing on screen -- and that is precisely the defect that reading
    // the code cannot catch and sampling the widget can.
    {
        tab.setNightInverted(false);
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }),
              "the first page is drawn for the day");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "and it is somewhere that can be sampled");

        // The viewport's own centre, not a corner of the page: this fixture is
        // laid out far larger than the test viewport, so the page's top-left
        // corner sits at a negative x and a point taken from it falls outside the
        // grabbed image -- where pixelColor answers with an invalid colour whose
        // channels all read 0, which looks exactly like a black page.
        const QPoint paper = view->viewport()->rect().center();
        check(page.contains(paper),
              "the point sampled is on the page, not the surround");
        const QImage dayShot = view->grab().toImage();
        const QColor day = dayShot.pixelColor(paper);
        check(day.isValid(), QStringLiteral("and the sample is a real pixel (%1)")
                                 .arg(day.isValid() ? day.name() : QStringLiteral("invalid")));

        tab.setNightInverted(true);
        check(view->nightInverted(), "the view reports itself inverted");
        check(waitFor([view] { return view->isRendered(0); }),
              "the page is re-rendered rather than left as a placeholder");
        const QColor nightColour = view->grab().toImage().pixelColor(paper);

        // Asserted as a relationship between the two samples rather than against
        // a colour, because by this point in the file the tab holds whichever
        // fixture the last block opened, and what that pixel is -- paper, ink, or
        // a black bar in a graphic -- is that fixture's business. What must hold
        // for any pixel is that inversion takes each channel to its complement.
        check(day != nightColour,
              QStringLiteral("the toggle changed what is on screen (%1,%2,%3 became %4,%5,%6)")
                  .arg(day.red()).arg(day.green()).arg(day.blue())
                  .arg(nightColour.red()).arg(nightColour.green()).arg(nightColour.blue()));
        check(day.red() + nightColour.red() == 255
                  && day.green() + nightColour.green() == 255
                  && day.blue() + nightColour.blue() == 255,
              QStringLiteral("and every channel went to its complement (%1,%2,%3 / %4,%5,%6)")
                  .arg(day.red()).arg(day.green()).arg(day.blue())
                  .arg(nightColour.red()).arg(nightColour.green()).arg(nightColour.blue()));

        // Back the way it was, so that anything added after this sees a day page.
        tab.setNightInverted(false);
        check(waitFor([view] { return view->isRendered(0); }), "and it comes back");
    }

    // ---- the region snapshot ----
    //
    // The first time this suite touches the clipboard at all, so it is worth
    // stating plainly: if the offscreen platform has no clipboard, the readback
    // below comes back empty and this fails for a reason that is not the
    // feature's fault. It stays because "a button that silently copies nothing"
    // is exactly what the row exists to catch, and the only way to know which of
    // the two happened is to have looked.
    {
        OcrDialog dialog;
        auto *copyImage = dialog.findChild<QPushButton *>(QStringLiteral("copyImageButton"));
        check(copyImage != nullptr, "the recognition window offers a picture to copy");
        if (copyImage) {
            check(!copyImage->isEnabled(),
                  "and it is off before there are any pixels to copy");

            // A crop as the renderer actually returns it: ink over transparency,
            // because the white of a page is put down by whoever shows the image.
            QImage crop(120, 60, QImage::Format_ARGB32_Premultiplied);
            crop.fill(Qt::transparent);
            {
                QPainter ink(&crop);
                ink.fillRect(10, 10, 20, 20, Qt::black);
            }

            // By day: the paper has to be in the picture, or pasting gives a
            // cut-out that borrows whatever background it lands on.
            dialog.setCropImage(crop, false);
            check(copyImage->isEnabled(), "handing it the crop enables it");
            copyImage->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const QImage pasted = QGuiApplication::clipboard()->image();
            check(!pasted.isNull(), "clicking it puts an image on the clipboard");
            check(pasted.size() == crop.size(),
                  QStringLiteral("at the size that was cropped (%1x%2 against %3x%4)")
                      .arg(pasted.size().width())
                      .arg(pasted.size().height())
                      .arg(crop.size().width())
                      .arg(crop.size().height()));
            check(pasted.pixelColor(0, 0) == QColor(Qt::white),
                  QStringLiteral("with paper under the ink, not transparency (%1)")
                      .arg(pasted.pixelColor(0, 0).name()));
            check(pasted.pixelColor(20, 20) == QColor(Qt::black),
                  QStringLiteral("and the ink still where it was (%1)")
                      .arg(pasted.pixelColor(20, 20).name()));

            // At night: what the reader is looking at is the inverted page, and a
            // snapshot that comes back white-on-black is the complement of the
            // thing they chose to copy.
            dialog.setCropImage(crop, true);
            copyImage->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const QImage nightPasted = QGuiApplication::clipboard()->image();
            check(nightPasted.pixelColor(0, 0) == QColor(Qt::black),
                  QStringLiteral("at night the paper pastes black (%1)")
                      .arg(nightPasted.pixelColor(0, 0).name()));
            check(nightPasted.pixelColor(20, 20) == QColor(Qt::white),
                  QStringLiteral("and the ink white (%1)")
                      .arg(nightPasted.pixelColor(20, 20).name()));
        }
    }

    // ---- the merge dialog's order promise ----
    //
    // 合并文件进来 is the first dialog here whose answer is a sequence rather than a
    // number, and the sequence is worked out inside the dialog: a merge lands beside
    // the target the reader named, not beside the block the row before it added, so
    // for 最前面 and 指定页之后 the rows have to be recorded in reverse for the list to
    // be the order the file gets (MergePagesDialog::mergesToRecord). The editor cannot
    // catch that from the other side -- it is handed one file at a time -- so it is
    // checked here, with the file picker replaced by the half of 添加文件… that is not
    // a window.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: the editor opened %1: %2").arg(path, error));
        const int onScreen = editor.pageCount();

        MergePagesDialog dialog(&tab, &editor);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QPushButton *okay = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        auto *table = dialog.findChild<QTableWidget *>();
        check(okay && table, "the merge window has its list and its button");

        auto pickTarget = [&dialog](const QString &words) {
            const auto radios = dialog.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };
        auto summary = [&dialog] {
            const auto labels = dialog.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        auto baseName = [](const QString &file) { return QFileInfo(file).fileName(); };

        check(okay && !okay->isEnabled(),
              "with an empty list the button is off, and says why");
        check(summary().contains(QStringLiteral("还没有加上要合并进来的文件")),
              QStringLiteral("the empty list is described, not just greyed: %1").arg(summary()));

        const QStringList donors = {directory + QStringLiteral("/many-pages.pdf"),
                                    directory + QStringLiteral("/dense.pdf"),
                                    directory + QStringLiteral("/mixed.pdf")};
        const QStringList refused = dialog.addPaths(donors);
        check(refused.isEmpty(),
              QStringLiteral("three ordinary files are taken: %1").arg(refused.join(QStringLiteral("; "))));
        const QVector<MergePagesDialog::Entry> listed = dialog.mergesToRecord();
        check(listed.size() == donors.size(), "and one row per file");
        for (int row = 0; row < listed.size(); ++row) {
            check(baseName(listed.at(row).path) == baseName(donors.at(row)),
                  QStringLiteral("row %1 is the file picked at that place: got %2")
                      .arg(row + 1)
                      .arg(baseName(listed.at(row).path)));
            check(listed.at(row).pageCount > 0
                      && listed.at(row).pages.size() == listed.at(row).pageCount,
                  QStringLiteral("and a new row says the whole of its own file (%1 of %2)")
                      .arg(listed.at(row).pages.size())
                      .arg(listed.at(row).pageCount));
        }

        check(pickTarget(QStringLiteral("最前面")) && pickTarget(QStringLiteral("指定页之后"))
                  && pickTarget(QStringLiteral("最后面")),
              "the three places the reader can aim at are on the dialog");

        // The reversal itself, and what it is for: recorded in list order at 最前面 the
        // three land 3,2,1, because each one goes in front of the two already there.
        pickTarget(QStringLiteral("最前面"));
        QStringList recordOrder;
        for (const MergePagesDialog::Entry &entry : dialog.mergesToRecord())
            recordOrder << baseName(entry.path);
        check(recordOrder == QStringList({baseName(donors.at(2)), baseName(donors.at(1)),
                                          baseName(donors.at(0))}),
              QStringLiteral("pointed at 最前面 the rows are recorded so the file gets them "
                             "in list order: got %1")
                  .arg(recordOrder.join(QStringLiteral(","))));

        DocumentEditor landing;
        check(landing.open(path, QString(), &error), "setup: a second editor on the same file");
        for (const MergePagesDialog::Entry &entry : dialog.mergesToRecord()) {
            check(landing.mergeFile(entry.path, entry.pages, DocumentEditor::MoveTarget::Front,
                                    -1, &error),
                  QStringLiteral("%1 comes in at the front: %2").arg(baseName(entry.path), error));
        }
        const QVector<DocumentEditor::PageRef> front = landing.pageOrder();
        check(front.size() == onScreen + listed.at(0).pages.size() + listed.at(1).pages.size()
                    + listed.at(2).pages.size(),
              "and the document is every one of those pages longer");
        // Each merge carries its own identity, and the last one recorded is the first
        // of them -- so the front of the file is row 1, then row 2, then row 3.
        int walking = 0;
        for (int row = 0; row < listed.size(); ++row) {
            const int identity = listed.size() - row;
            for (int page = 0; page < listed.at(row).pages.size(); ++page, ++walking) {
                check(front.at(walking).source == identity
                          && front.at(walking).page == page,
                      QStringLiteral("page %1 of the file is merge %2's page %3, which is row %4 "
                                     "of the list")
                          .arg(walking + 1)
                          .arg(identity)
                          .arg(page + 1)
                          .arg(row + 1));
            }
        }

        pickTarget(QStringLiteral("最后面"));
        table->item(0, 2)->setText(QStringLiteral("2"));
        check(dialog.mergesToRecord().at(0).pages == QVector<int>({1}),
              "a row's range cell is read as that file's own page numbers");
        table->item(1, 2)->setText(QStringLiteral(" "));
        check(okay && !okay->isEnabled(),
              "and a row left blank stops the button -- blank is nothing said, not everything");
        check(summary().contains(baseName(donors.at(1))),
              QStringLiteral("naming which row: %1").arg(summary()));
        table->item(1, 2)->setText(QStringLiteral("1"));

        pickTarget(QStringLiteral("指定页之后"));
        check(okay && !okay->isEnabled(),
              "an 指定页之后 with no number in the box is not an answer yet");
        check(summary().contains(QStringLiteral("要说合并到第几页之后")),
              QStringLiteral("and it says that: %1").arg(summary()));
        auto *anchorBox = dialog.findChild<QLineEdit *>();
        check(anchorBox != nullptr, "the box beside 指定页之后");
        if (anchorBox) {
            anchorBox->setText(QString::number(onScreen + 5));
            check(okay && !okay->isEnabled(), "a number past the last page of the screen is refused");
            check(summary().contains(QStringLiteral("没有第 %1 页").arg(onScreen + 5)),
                  QStringLiteral("and named as the number that was typed: %1").arg(summary()));
            anchorBox->setText(QStringLiteral("2"));
            check(okay && okay->isEnabled(), "a page that is there lets the button go back");
        }

        // The sentence 删除页面 needs too, said where the pages come from: an imported
        // page has no number on the screen, so a reader has to be told before merging.
        check(editor.mergeFile(donors.at(0), {0}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("setup: one page recorded into the editor the dialog reads: %1")
                  .arg(error));
        pickTarget(QStringLiteral("最后面"));
        check(summary().contains(QStringLiteral("已经有 %1 页是合并进来的").arg(1)),
              QStringLiteral("the summary counts what the screen never showed: %1").arg(summary()));
        check(summary().contains(QStringLiteral("只能撤销那次合并")),
              QStringLiteral("and says the only way back out: %1").arg(summary()));
    }

    // ---- the merge box refuses a pending split before the click ----
    //
    // §20 caught this dialog lighting 合并这些文件 and printing an arithmetic line while
    // DocumentEditor::mergeFile() was quietly going to refuse whatever it promised, with
    // 拆分文件 having done it the other way round (greyed button, reason in the summary)
    // since §16. So both ends of that mutual exclusion now answer the same two questions
    // of the box itself: does the button stay off, and is the reason said in the model's
    // own words rather than a paraphrase that can drift. A fresh box per click, the way
    // the menu builds one, so what it says is read off the list as it stands now -- which
    // is also what makes the request pass again once the split has been undone.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        const auto saidByMergeBox = [](MergePagesDialog &box, const QString &donor) {
            check(box.addPaths({donor}).isEmpty(), "setup: the donor file is taken");
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto okayOfMergeBox = [](MergePagesDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const QString donor = directory + QStringLiteral("/dense.pdf");

        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("setup: with no split pending, this same request is one the "
                                 "button lights for: %1")
                      .arg(said));
        }

        check(editor.splitInto(15, &error),
              QStringLiteral("setup: a split of every 15 pages is pending: %1").arg(error));
        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && !okay->isEnabled(),
                  "with a split pending the merge box's own button is off before the click");
            check(said.contains(QStringLiteral("已经说了要拆成每 15 页一份")),
                  QStringLiteral("and the summary names the pending split, in the model's "
                                 "sentence: %1")
                      .arg(said));
            // The number the refusal replaces: where a batch would land is an answer
            // about a file that will not be written, which is the same kind of blank as
            // §20's "一个没有指称的数".
            check(!said.contains(QStringLiteral("连着排在第")),
                  QStringLiteral("and no page numbers are promised for a merge that cannot "
                                 "be recorded: %1")
                      .arg(said));
            // The box and the editor refuse the same request -- the rule §21 states,
            // asked from this end.
            check(!editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error)
                      && error.contains(QStringLiteral("先撤销那一步")),
                  QStringLiteral("and the editor refuses it too: %1").arg(error));
        }

        check(editor.undo(), "setup: the split is taken back out of the list");
        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("undo that split and the very same request is lit again: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("已经说了要拆成")),
                  QStringLiteral("and the refusal is gone with it: %1").arg(said));
        }
    }

    // ---- one page is not a range: the two landing sentences ----
    //
    // §20 fixed this for 拆分's file names and pinned it there ("a one-page piece says 21,
    // not 21-21"), while 移动页面 and 合并文件进来 kept printing 5-5 and 40-40 -- and kept
    // saying 它们连着 / "they end up together" about a single page, which is the same thing:
    // a number deciding the words around it. Both dialogs now read the number from
    // DocumentEditor::pageSpan() and pick the wording by the count, so this asks each box the
    // one-page question and the several-page question, and looks for the dash on the right
    // side of that line only.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document to move pages around in: %1").arg(error));
        const int onScreen = editor.pageCount();

        const auto saidBy = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto pickTarget = [](QDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };

        // 合并文件进来 first: a one-page donor needs no range typed, because a fresh row takes
        // the whole of its own file, and rotated.pdf is exactly one page.
        const auto rangeCell = [](QDialog &box) {
            auto *cells = box.findChild<QTableWidget *>();
            return cells && cells->item(0, 2) ? cells->item(0, 2)->text() : QString();
        };
        {
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({directory + QStringLiteral("/rotated.pdf")}).isEmpty(),
                  "setup: a one-page donor is taken");
            check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("一共取 1 页")),
                  QStringLiteral("the count says one page: %1").arg(said));
            check(said.contains(QStringLiteral("排在第 %1 页").arg(onScreen + 1)),
                  QStringLiteral("and it lands on one number, not a range: %1").arg(said));
            check(!said.contains(QStringLiteral("连着")),
                  QStringLiteral("one page is not 连着 either, which is the half the English "
                                 "reader sees as \"they end up together\": %1").arg(said));
            // The fifth exit, and the only one that was an *input*: the cell a fresh row seeds
            // with its own file's range held "1-1" for a one-page file. It parses the same, so
            // nothing downstream noticed -- it is only ever a reader reading the number back.
            check(rangeCell(box) == QStringLiteral("1"),
                  QStringLiteral("a one-page donor seeds the range cell with 1, not 1-1: [%1]")
                      .arg(rangeCell(box)));
        }
        {
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({directory + QStringLiteral("/dense.pdf")}).isEmpty(),
                  "setup: a two-page donor is taken");
            check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("连着排在第 %1-%2 页")
                                     .arg(onScreen + 1)
                                     .arg(onScreen + 2)),
                  QStringLiteral("two pages keep the run and the wording that goes with it: %1")
                      .arg(said));
            check(rangeCell(box) == QStringLiteral("1-2"),
                  QStringLiteral("and a two-page donor does seed a run, so the cell is not just "
                                 "always the first number: [%1]").arg(rangeCell(box)));
        }

        // 移动页面: the same two questions, typed into its own range box -- picked by the
        // placeholder, because this dialog has a second line edit beside it.
        {
            MovePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = nullptr;
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    range = edit;
            }
            check(range != nullptr, "setup: the box that names which pages move");
            if (range) {
                check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
                range->setText(QStringLiteral("7"));
                const QString one = saidBy(box);
                check(one.contains(QStringLiteral("把 1 页移到")),
                      QStringLiteral("one page moved is not a block of them: %1").arg(one));
                check(one.contains(QStringLiteral("移完它就排在第 40 页")),
                      QStringLiteral("page 7 alone takes the last place, which is 40: %1")
                          .arg(one));
                check(!one.contains(QStringLiteral("它们")),
                      QStringLiteral("and the plural pronoun went with the dash: %1").arg(one));

                range->setText(QStringLiteral("7,9"));
                const QString two = saidBy(box);
                check(two.contains(QStringLiteral("连着排在第 39-40 页")),
                      QStringLiteral("two pages come over together, at 39 and 40: %1").arg(two));
            }
        }

        // 拆分文件: the fourth exit, and the one whose rule had a private copy of its own
        // until this slice. Its list is cut short after six pieces, so 每 1 页一份 of this
        // 40-page document is the shape where every number in it stands alone -- a dash
        // anywhere in that list would be the 40-40 bug wearing a different hat. Nothing in
        // any test read this box's summary before now, which is how two correct private
        // copies of one rule stayed unasked while two missing ones reached the screen.
        {
            SplitPagesDialog box(&tab, &editor);
            auto *size = box.findChild<QSpinBox *>();
            check(size != nullptr, "setup: the box that says how many pages go in a piece");
            if (size) {
                size->setValue(1);
                const QString said = saidBy(box);
                check(said.contains(QStringLiteral("会写成 40 份，每份最多 1 页")),
                      QStringLiteral("forty pieces of one page: %1").arg(said));
                const QString list
                    = said.section(QStringLiteral("页："), 1).section(QStringLiteral("。"), 0, 0);
                // Six pieces are listed, then the cut, then the last one -- and the cut is
                // joined to the numbers with `、` too, so the list splits into nine.
                // Split on U+3001 by its code point: QLatin1Char cannot hold it (the source
                // is UTF-8, so it would silently take one byte of a three-byte character).
                check(list.split(QChar(0x3001)).size() == 8,
                      QStringLiteral("six of them listed, then the cut, then the last one: [%1]")
                          .arg(list));
                check(list.startsWith(QStringLiteral("1、2")) && list.endsWith(QStringLiteral("40")),
                      QStringLiteral("bare numbers at both ends of the list: [%1]").arg(list));
                check(!list.contains(QLatin1Char('-')),
                      QStringLiteral("and no piece of it is a range, because none of them holds "
                                     "more than one page: [%1]").arg(list));
            }
        }
    }

    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----
    //
    // The dialog greys 确定 for exactly the requests DocumentEditor::protectWith() and
    // ::removeProtection() turn down, and it does that by reading the same two answers the
    // save reads. That sharing is the arrangement, and it is the kind that rots quietly: let
    // one of the refusals through and the reader meets a warning box after pressing 确定;
    // grey one the editor would have taken and the feature is unreachable. So both sides of
    // each refusal are asked here. The box is also rebuilt between them the way the menu
    // rebuilds it on every click -- which is why a dialog can never be seen agreeing with an
    // older version of the list.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: an unprotected document opens: %1").arg(error));

        const auto okayOf = [](ProtectPagesDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](ProtectPagesDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto choose = [](ProtectPagesDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };
        const auto groupStartingWith = [](ProtectPagesDialog &box, const QString &words) {
            const auto groups = box.findChildren<QGroupBox *>();
            for (QGroupBox *group : groups) {
                if (group->title().startsWith(words))
                    return group;
            }
            return static_cast<QGroupBox *>(nullptr);
        };

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            QGroupBox *keys = groupStartingWith(box, QStringLiteral("打开口令"));
            QGroupBox *limits = groupStartingWith(box, QStringLiteral("允许的用途"));
            check(keys && limits, "the box asks for a口令 and offers the six permissions");
            check(okay && okay->isEnabled() && keys->isEnabled() && limits->isEnabled(),
                  "nothing is pending and a file that has never been locked can still be locked, "
                  "so it opens on 加密 with every question live");

            check(choose(box, QStringLiteral("去掉口令和权限")), "the other direction is on the box");
            check(okay && !okay->isEnabled(),
                  "over a file that was never locked, 去掉口令 greys the button");
            check(saidBy(box).contains(QStringLiteral("没有东西可去掉")),
                  QStringLiteral("and says that rather than only greying: %1").arg(saidBy(box)));
            check(!editor.removeProtection(&error),
                  "the editor refuses the same request the box greyed");
            check(!keys->isEnabled() && !limits->isEnabled(),
                  "a口令 and six permissions that no answer will read are put away, not left on show");

            check(choose(box, QStringLiteral("加上口令和权限")), "back to 加密");
            check(keys->isEnabled() && limits->isEnabled(), "and the two questions are back");
        }

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            const auto edits = box.findChildren<QLineEdit *>();
            check(edits.size() == 2,
                  "the口令 is asked twice, because nothing here can report it back later");
            // The name a field answers to when nothing points at it is its group's title, so
            // a reader that cannot see the screen asked both fields 打开口令 -- measured in a
            // real window through UI Automation, which is also the only way anyone would ever
            // notice. The labels are buddied to the fields so the two answer apart.
            const auto nameOf = [](QLineEdit *edit) {
                QAccessibleInterface *face = QAccessible::queryAccessibleInterface(edit);
                return face ? face->text(QAccessible::Name) : QString();
            };
            check(nameOf(edits.at(0)) == QStringLiteral("口令") &&
                      nameOf(edits.at(1)) == QStringLiteral("再输一遍"),
                  QStringLiteral("the two fields are named apart by whatever only reads names: "
                                 "[%1] [%2]")
                      .arg(nameOf(edits.at(0)), nameOf(edits.at(1))));
            edits.at(0)->setText(QStringLiteral("hunter2"));
            check(okay && !okay->isEnabled(), "one box filled and the other empty is two different口令");
            check(saidBy(box).contains(QStringLiteral("两次输入的口令不一样")),
                  QStringLiteral("and it names that as the reason: %1").arg(saidBy(box)));
            edits.at(1)->setText(QStringLiteral("hunter2"));
            check(okay && okay->isEnabled(), "agreeing lets it through");
            const DocumentEditor::Protection asked = box.request();
            check(asked.what == DocumentEditor::Protection::What::Encrypt
                      && asked.password == QStringLiteral("hunter2"),
                  "and what the box hands over is the direction plus the key as typed");
            check(editor.protectWith(asked, &error),
                  QStringLiteral("the editor takes it: %1").arg(error));
        }

        {
            // A reader who opens the box a second time finds the six boxes as they were
            // recorded and the口令 fields empty -- showing the key back is the thing the key
            // is for. So typing it again is asking for the request that is already in the
            // list, which is the second refusal, and changing one box is not.
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            const auto edits = box.findChildren<QLineEdit *>();
            edits.at(0)->setText(QStringLiteral("hunter2"));
            edits.at(1)->setText(QStringLiteral("hunter2"));
            check(okay && !okay->isEnabled(), "the same request asked again is greyed");
            check(saidBy(box).contains(QStringLiteral("不会多改什么")),
                  QStringLiteral("with the reason: %1").arg(saidBy(box)));
            check(!editor.protectWith(box.request(), &error),
                  "and the editor turns the same request down");

            const auto permissionBoxes = box.findChildren<QCheckBox *>();
            check(permissionBoxes.size() == 6,
                  "six permissions, one per thing PDF keeps a bit for");
            permissionBoxes.first()->click();
            check(okay && okay->isEnabled(), "clearing one of them is a change, so the button goes back");
            check(editor.protectWith(box.request(), &error),
                  QStringLiteral("and the editor records it: %1").arg(error));
        }

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            check(choose(box, QStringLiteral("去掉口令和权限")),
                  "the way back out of a 加密 that was just recorded is this same box");
            check(okay && okay->isEnabled(), "with a 加密 pending there is something to take off");
            check(saidBy(box).contains(QStringLiteral("会被这一步顶掉")),
                  QStringLiteral("and it says so: %1").arg(saidBy(box)));
            check(box.request().what == DocumentEditor::Protection::What::Decrypt,
                  "the box says which way it went");
            check(editor.removeProtection(&error),
                  QStringLiteral("the editor records that too: %1").arg(error));
            check(editor.passwordOfWrittenFile().isEmpty(),
                  "and the copy a save would write from here asks for no key at all");
        }
    }

    // ---- 加水印: the box offers what the model means, and greys what it refuses ----
    //
    // The third inert recorder. Two things a model test cannot see are checked here. One is what
    // the box shows before anybody has touched it: the first version opened offering 1% 不透明度,
    // because a QSpinBox whose range starts at 1 sits at 1 until someone asks for the default --
    // found in a real window, and a watermark at 1% is a watermark nobody can see, so the number
    // is now read from the same struct the writer reads. The other is §22's rule from the 加密
    // box: the button is off for exactly the requests addWatermark() turns down, so the reader
    // learns 再要一次不会多改什么 from a grey button and one sentence, not from a warning box
    // after a press. A fresh box per click, the way the menu builds one.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        const auto okayOf = [](WatermarkDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](WatermarkDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        // The two boxes are picked by the hint they carry, not by counting: a QSpinBox owns a
        // QLineEdit of its own, so the second line edit in creation order is the opacity field
        // and writing a page range into it changes nothing anyone can see.
        const auto editHintedWith = [](WatermarkDialog &box, const QString &hint) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText() == hint)
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };
        const auto wordEdit = [&editHintedWith](WatermarkDialog &box) {
            return editHintedWith(box, QStringLiteral("例如 机密、作废、内部资料"));
        };
        const auto rangeEdit = [&editHintedWith](WatermarkDialog &box) {
            return editHintedWith(box, QStringLiteral("例如 1-3,5,8-10"));
        };
        const auto radio = [](WatermarkDialog &box, const QString &starts) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *choice : radios) {
                if (choice->text().startsWith(starts))
                    return choice;
            }
            return static_cast<QRadioButton *>(nullptr);
        };

        {
            WatermarkDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            auto *spin = box.findChild<QSpinBox *>();
            check(spin != nullptr, "the box asks how solid the marks should be");
            check(spin && spin->value() == DocumentEditor::Watermark {}.opacityPercent,
                  QStringLiteral("and it opens on the model's own default, not on its minimum: %1%")
                      .arg(spin ? spin->value() : -1));
            check(okay && !okay->isEnabled(),
                  "a box nobody has typed a word into is off, because an empty word is a smudge");
            check(saidBy(box).contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("and says which half is missing: %1").arg(saidBy(box)));
            QRadioButton *all = radio(box, QStringLiteral("全部"));
            check(all && all->isChecked(),
                  "it opens on 全部, because that is what a reader who came to stamp every page means");
            const DocumentEditor::Watermark asked = box.request();
            check(asked.text.isEmpty() && asked.pages.size() == 40,
                  "and what it would hand over is every page and no word");
        }

        {
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled() && okay->text() == QStringLiteral("盖这 40 页"),
                  QStringLiteral("one word and the button counts the pages it lands on: %1")
                      .arg(okay ? okay->text() : QStringLiteral("(none)")));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("「机密」")) && said.contains(QStringLiteral("20%"))
                      && said.contains(QStringLiteral("盖在 40 页上")),
                  QStringLiteral("the summary repeats all three answers back: %1").arg(said));
            check(said.contains(QStringLiteral("画上去的一张图")),
                  "and warns that the marks are a drawing, which is the one fact here a reader "
                  "cannot discover by trying");
        }

        {
            // 空 is not 全部. The print parser reads an empty range as the whole document, and
            // this box has a radio for saying that on purpose -- a second way to say it would let
            // Enter stamp four hundred pages because a box was left blank.
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            radio(box, QStringLiteral("指定"))->click();
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "choosing 指定页码 and naming nothing is off");
            check(saidBy(box).contains(QStringLiteral("还没有说出要盖哪几页")),
                  QStringLiteral("and is named as the missing half: %1").arg(saidBy(box)));
            rangeEdit(box)->setText(QStringLiteral("2-3,7"));
            check(okay->isEnabled() && okay->text() == QStringLiteral("盖这 3 页"),
                  "three pages named, three pages counted");
            const DocumentEditor::Watermark asked = box.request();
            QStringList spelled;
            for (int page : asked.pages)
                spelled << QString::number(page + 1);
            check(asked.pages == QVector<int>({1, 2, 6}),
                  QStringLiteral("and the numbers handed over are the ones the save reads: %1")
                      .arg(spelled.join(QLatin1Char(','))));
        }

        {
            // A page an earlier 删页 took out is not inked, and the box says the number the save
            // will draw rather than the number of boxes ticked -- pagesStillThere() is the one
            // function both ends ask.
            check(editor.deletePages({1}, &error),
                  QStringLiteral("setup: the second page goes: %1").arg(error));
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            rangeEdit(box)->setText(QStringLiteral("2-3,7"));
            check(radio(box, QStringLiteral("指定"))->isChecked(),
                  "a range typed in moves the choice to it without being clicked");
            check(saidBy(box).contains(QStringLiteral("说好的 3 页里有 2 页还在")),
                  QStringLiteral("and the box says which two: %1").arg(saidBy(box)));
            check(okayOf(box)->text() == QStringLiteral("盖这 2 页"),
                  "the button counts what is still there");
            check(editor.pagesStillThere(box.request().pages) == 2,
                  "read off the same one function the save will read");
            check(editor.addWatermark(box.request(), &error),
                  QStringLiteral("setup: the word is recorded: %1").arg(error));
        }

        {
            // Reopening is what the menu does on every click. The recorded answer is offered back
            // rather than a blank box, and asking for exactly that again is the request the editor
            // refuses -- so the button is off before the press, in the same words.
            WatermarkDialog box(&tab, &editor);
            check(wordEdit(box)->text() == QStringLiteral("机密"),
                  "the box comes back saying what is already on the list");
            check(rangeEdit(box)->text() == QStringLiteral("3,7"),
                  QStringLiteral("with the pages a save would actually ink spelled back: %1")
                      .arg(rangeEdit(box)->text()));
            // 3,7 and not 2,3,7: addWatermark() dropped the page the earlier 删页 had taken out
            // before it recorded anything, so the list holds only pages that exist to be drawn on
            // and a box opened against it cannot offer a second way to ask for a ghost.
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and the very same ask again is off");
            check(saidBy(box).contains(QStringLiteral("一模一样")),
                  QStringLiteral("said, not just greyed: %1").arg(saidBy(box)));
            check(!editor.addWatermark(box.request(), &error)
                      && error.contains(QStringLiteral("再要一次不会多改什么")),
                  QStringLiteral("and the editor refuses the identical request: %1").arg(error));

            wordEdit(box)->setText(QStringLiteral("作废"));
            check(okay->isEnabled() && okay->text() == QStringLiteral("盖这 2 页"),
                  "a different word is a change, so the button lights again");
            check(saidBy(box).contains(QStringLiteral("会把前面记下的「机密」换掉")),
                  QStringLiteral("and says it replaces rather than adds: %1").arg(saidBy(box)));
            check(editor.addWatermark(box.request(), &error),
                  QStringLiteral("the editor takes that one: %1").arg(error));
            check(editor.watermarkToWrite().text == QStringLiteral("作废"),
                  "one ask at a time: the list carries the newest word, not two layers");

            check(editor.undo(), "setup: the replacement is taken back");
            check(editor.watermarkToWrite().text == QStringLiteral("机密"),
                  "and the earlier answer is what a save would write now");
            WatermarkDialog again(&tab, &editor);
            QPushButton *againOkay = okayOf(again);
            check(againOkay && !againOkay->isEnabled(),
                  "a box built after the undo offers the undone-away answer back and greys it too, "
                  "because it reads the list as it stands now");
        }
    }

    // ---- who speaks first: the sentence a dialog picks when two of its guards hold ----
    //
    // Every edit dialog answers with ONE sentence out of a chain, and each link in that chain
    // has a test somewhere -- but a chain is a sequence, and nothing asked what the reader
    // gets when two guards are true at once. That is not a corner: 拆分挂着 and 列表是空的 are
    // both facts about the same box, and reordering the two links changes what a person reads
    // while every single-branch check stays green. §29 wrote this exact case down from a real
    // window and left it unpinned, so each pin below is built by making both guards true.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        const QString donor = directory + QStringLiteral("/dense.pdf");
        auto baseName = [](const QString &file) { return QFileInfo(file).fileName(); };
        QString error;
        // DocumentEditor is not copyable (its QPdfDocument is), so each case opens its own:
        // this fills one rather than returning one.
        const auto openBook = [&book, &error](DocumentEditor &editor) {
            check(editor.open(book, QString(), &error),
                  QStringLiteral("setup: a 40-page document to refuse in: %1").arg(error));
        };

        const auto saidOf = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto okayOf = [](QDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        // The range box, picked by its placeholder: 移动页面 has a second line edit beside it
        // (the anchor number), and taking the first child would read out the wrong one.
        const auto rangeOf = [](QDialog &box) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };
        const auto pick = [](QDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };

        // 合并 with nothing in the list AND a split pending: the list is the more basic
        // nothing-to-do, and §29 read exactly this pair on a real screen (with an empty list
        // the pending split is never mentioned) without any test saying so.
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.splitInto(15, &error),
                  QStringLiteral("setup: a split of every 15 pages is pending: %1").arg(error));
            MergePagesDialog box(&tab, &editor);
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有加上要合并进来的文件")),
                  QStringLiteral("an empty list answers first, even with a split pending: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("已经说了要拆成每")),
                  QStringLiteral("and the pending split is not mentioned while there is nothing "
                                 "to merge in: %1")
                      .arg(said));
        }

        // 合并 with a row that has not said its pages AND a split pending: here the split wins,
        // because it invalidates the whole request rather than one row of it. The order is the
        // decision, so it needs both sentences in the same run to be worth anything.
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.splitInto(15, &error), "setup: a split is pending");
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({donor}).isEmpty(), "setup: the donor row is taken");
            auto *table = box.findChild<QTableWidget *>();
            check(table != nullptr && table->item(0, 2) != nullptr, "setup: the range cell");
            table->item(0, 2)->setText(QStringLiteral(" "));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("已经说了要拆成每 15 页一份")),
                  QStringLiteral("a silent row does not outrank a request the model will refuse: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("还没有说出要合入哪几页")),
                  QStringLiteral("and the row is not named while the split is what stops it: %1")
                      .arg(said));
        }

        // 合并 with a silent row and no split: now the row is the only thing wrong, and the
        // sentence names which row-- so the same box moves from the split's answer to the
        // row's answer by undoing one list entry.
        {
            DocumentEditor editor;
            openBook(editor);
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({donor}).isEmpty(), "setup: the donor row is taken");
            auto *table = box.findChild<QTableWidget *>();
            table->item(0, 2)->setText(QStringLiteral(" "));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要合入哪几页"))
                      && said.contains(baseName(donor)),
                  QStringLiteral("without a pending split the silent row is what gets said, "
                                 "with its file named: %1")
                      .arg(said));
            // and the anchor question is further down the chain than the row question
            pick(box, QStringLiteral("指定页之后"));
            check(saidOf(box).contains(baseName(donor))
                      && !saidOf(box).contains(QStringLiteral("要说合并到第几页之后")),
                  QStringLiteral("a blank anchor does not cut in front of a silent row: %1")
                      .arg(saidOf(box)));
        }

        // 删除 with nothing typed: blank is nothing said, and the button waits (§13's finding,
        // fixed by making a blank box mean nothing rather than everything).
        {
            DocumentEditor editor;
            openBook(editor);
            DeletePagesDialog box(&tab, 0, &editor);
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要删哪几页")),
                  QStringLiteral("a box nobody typed into says that and does not count: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and the button is off, so nothing can be pressed");
        }

        // 删除 with pages typed that an earlier pending 删页 already took: those numbers are
        // original page numbers (§15), so typing 3 after deleting original page 3 names a page
        // that is not there to delete -- and the answer must not be "you said nothing".
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.deletePages({2}, &error),
                  QStringLiteral("setup: original page 3 is already taken out: %1").arg(error));
            DeletePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = rangeOf(box);
            check(range != nullptr, "setup: the box that takes the numbers");
            range->setText(QStringLiteral("3"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("这些页在前面的修改里已经删掉了")),
                  QStringLiteral("contradicting the reader is the thing to avoid, so this names "
                                 "the earlier change: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("还没有说出要删哪几页")),
                  QStringLiteral("and not the sentence for a blank box, which this is not: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and nothing is left to press");
        }

        // 删除 naming everything that is left: the floor is its own answer, and it is said
        // before the arithmetic that would otherwise report 剩下 0 页.
        {
            DocumentEditor editor;
            openBook(editor);
            DeletePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = rangeOf(box);
            range->setText(QStringLiteral("1-40"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("这样一页也不剩了")),
                  QStringLiteral("the floor answers before the arithmetic does: %1").arg(said));
            check(!said.contains(QStringLiteral("剩下 0 页")),
                  QStringLiteral("and no line of numbers is offered for a document that cannot "
                                 "exist: %1")
                      .arg(said));
        }

        // §77: 删除页面 says, before the press, what taking a page out does to a form answer -- and
        // only when there is a form to say it about. The book above has no /AcroForm at all, which is
        // the half that must stay silent; this makes a copy of it with one field on the first page.
        // Written into a scratch directory, never beside the fixtures: that directory is globbed by
        // other tests (§17's rule, restated in AGENTS.md).
        {
            QTemporaryDir forms;
            check(forms.isValid(), "setup: a scratch folder for the form-bearing copy");
            const QString withForm = forms.filePath(QStringLiteral("one-field.pdf"));
            QString made;
            bool built = false;
            try {
                QPDF qpdf;
                qpdf.processFile(book.toUtf8().constData());
                QPDFObjectHandle page =
                    QPDFPageDocumentHelper(qpdf).getAllPages().front().getObjectHandle();
                QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
                field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
                field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
                field.replaceKey("/T", QPDFObjectHandle::newString("onOne"));
                field.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
                QPDFObjectHandle rect = QPDFObjectHandle::newArray();
                rect.appendItem(QPDFObjectHandle::newReal(60));
                rect.appendItem(QPDFObjectHandle::newReal(600));
                rect.appendItem(QPDFObjectHandle::newReal(260));
                rect.appendItem(QPDFObjectHandle::newReal(630));
                field.replaceKey("/Rect", rect);
                field = qpdf.makeIndirectObject(field);
                field.replaceKey("/P", page);
                QPDFObjectHandle annots = QPDFObjectHandle::newArray();
                annots.appendItem(field);
                page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
                QPDFObjectHandle fields = QPDFObjectHandle::newArray();
                fields.appendItem(field);
                QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
                form.replaceKey("/Fields", fields);
                qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
                QPDFWriter writer(qpdf, withForm.toUtf8().constData());
                writer.write();
                built = true;
            } catch (const std::exception &e) {
                made = QString::fromUtf8(e.what());
            }
            check(built, QStringLiteral("setup: a copy of the book carrying one form field: %1")
                             .arg(made));

            DocumentEditor formBook;
            check(formBook.open(withForm, QString(), &error),
                  QStringLiteral("setup: the editor opens it: %1").arg(error));
            check(formBook.hasFormFields(), "and the model answers that there is a form here");
            DeletePagesDialog inForm(&tab, 0, &formBook);
            check(saidOf(inForm).contains(QStringLiteral("字段填的答案属于它画在哪一页上")),
                  QStringLiteral("so the box says what a deletion does to an answer before the "
                                 "press: %1").arg(saidOf(inForm)));

            // The same question asked of the tab, which is what decides whether
            // the reader is told anything at all. docs/review-03.md 四.1: this
            // program draws no field contents anywhere, so a filled-in form
            // looks like an empty one -- on screen, in an export and on paper
            // (§94). Until the fields are drawn, the open has to say so, and it
            // can only say so if it knows.
            {
                const QString wasOpen = tab.documentPath();
                check(tab.open(withForm) == Document::Status::Ready,
                      "the form-bearing copy opens in the tab");
                check(tab.hasFormFields(),
                      "and the tab knows there are form fields in it, which is what the status "
                      "line says on the way past");
                check(tab.open(wasOpen) == Document::Status::Ready,
                      "the book without a form opens again");
                check(!tab.hasFormFields(), "and the tab does not claim a form that is not there");
            }

            DocumentEditor plain;
            openBook(plain);
            check(!plain.hasFormFields(), "the same book with no form answers no");
            DeletePagesDialog inPlain(&tab, 0, &plain);
            check(!saidOf(inPlain).contains(QStringLiteral("表单字段")),
                  QStringLiteral("and the sentence keeps out of a box that has nothing to say about "
                                 "forms: %1").arg(saidOf(inPlain)));
        }

        // 移动 with nothing named AND no anchor number: the pages are the more basic question.
        // Getting there is itself a reading: the box opens on 当前页, which always names one
        // page, so the "no pages named" branch only exists behind the 指定页码 choice -- a box
        // left as it opened asks the anchor question first, and that is the right order.
        {
            DocumentEditor editor;
            openBook(editor);
            MovePagesDialog box(&tab, 0, &editor);
            pick(box, QStringLiteral("指定页码"));
            pick(box, QStringLiteral("指定页之后"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要移动哪几页")),
                  QStringLiteral("no pages named is said before no anchor: %1").arg(said));
            check(!said.contains(QStringLiteral("要说移到第几页之后")),
                  QStringLiteral("and the anchor question waits its turn: %1").arg(said));
            pick(box, QStringLiteral("当前页（第 1 页）"));
            check(saidOf(box).contains(QStringLiteral("要说移到第几页之后")),
                  QStringLiteral("name the page another way and the anchor is the only thing "
                                 "left missing: %1")
                      .arg(saidOf(box)));
        }

        // 水印 with neither words nor pages: the text is the first question the box asks, and
        // the page question is not raised while the marks themselves are still unsaid.
        {
            DocumentEditor editor;
            openBook(editor);
            WatermarkDialog box(&tab, &editor);
            pick(box, QStringLiteral("指定页码"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("words first: %1").arg(said));
            check(!said.contains(QStringLiteral("还没有说出要盖哪几页")),
                  QStringLiteral("and the page question is not asked yet: %1").arg(said));
        }

        // 水印 with no words but a page range typed, on a document that carries imported pages:
        // the imported-pages aside is about pages that will not be inked, so it stays out of a
        // refusal that already says nothing was named. (It used to be appended here, and the
        // reason this is a pin rather than a comment is that the aside is gated on the pages,
        // not on the answer the box just gave.)
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: one imported page: %1").arg(error));
            WatermarkDialog box(&tab, &editor);
            QLineEdit *range = rangeOf(box);
            check(range != nullptr, "setup: the box that takes the numbers");
            range->setText(QStringLiteral("3"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("a typed range with no words is still no words: %1").arg(said));
            check(!said.contains(QStringLiteral("前面合并进来的那")),
                  QStringLiteral("and nothing is explained about pages that were never asked to "
                                 "be inked: %1")
                      .arg(said));
        }
    }

    // ---- 编辑书签: the box that shows a tree as a list, and greys what would change nothing ----
    //
    // The eighth recorder, and the only one whose subject is a structure rather than a number or a
    // range. Three things are checked here that no model test can see. One is what the box opens on:
    // the sidebar's own tree, so the reader edits the list they can see rather than an empty grid.
    // Two is the rule the model does *not* have: pressing 确定 on a box nobody touched would record a
    // request, dirty the tab and hand out an undo row that puts the file back exactly as it was --
    // setOutline() only refuses an identical *pending* tree (§69), and nothing but this box can see
    // the document's own. Three is §70's owed sentence: a bookmark naming a page an earlier 删页 took
    // out is said out loud *before* the press and still let through, because the model's answer to it
    // is a heading, which is the same answer it gives for a page lost after the ask.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");

        // A tree the shape of a real chapter list: five bookmarks, three depths, one of them a
        // heading that names no page.
        const QVector<OutlineRow> sample = {
            {QStringLiteral("第一章"), 1, 0, QPointF()},
            {QStringLiteral("1.1"), 2, 2, QPointF()},
            {QStringLiteral("1.1.1"), 3, 4, QPointF()},
            {QStringLiteral("1.2"), 2, 6, QPointF()},
            {QStringLiteral("第二章"), 1, -1, QPointF()},
        };
        const Outline tree = outlineFromRows(sample);

        const auto gridOf = [](BookmarkDialog &box) { return box.findChild<QTableWidget *>(); };
        const auto okayOf = [](BookmarkDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](BookmarkDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto button = [](BookmarkDialog &box, const QString &name) {
            const auto buttons = box.findChildren<QPushButton *>();
            for (QPushButton *one : buttons) {
                if (one->text() == name)
                    return one;
            }
            return static_cast<QPushButton *>(nullptr);
        };
        const auto orderOf = [](BookmarkDialog &box) {
            QStringList titles;
            const QVector<OutlineRow> rows = box.rows();
            for (const OutlineRow &row : rows)
                titles << QStringLiteral("%1/%2/%3").arg(row.title).arg(row.level).arg(row.page);
            return titles.join(QStringLiteral(", "));
        };
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        {
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            check(grid != nullptr, "the box shows the bookmarks as a grid");
            check(grid && grid->rowCount() == 5,
                  QStringLiteral("it opens on the tree the sidebar is showing, not on an empty "
                                 "list (%1 rows)")
                      .arg(grid ? grid->rowCount() : -1));
            // 1-based in the cell: this is the number the reader counts on screen (§70), so a page
            // index of 0 has to read as 第 1 页 or every number in the box is one off from the bar.
            check(grid && grid->item(0, 2) && grid->item(0, 2)->text() == QStringLiteral("1"),
                  QStringLiteral("and the page column is the numbering on the screen: %1")
                      .arg(grid && grid->item(0, 2) ? grid->item(0, 2)->text() : QStringLiteral("(none)")));
            check(grid && grid->item(4, 2) && grid->item(4, 2)->text().isEmpty(),
                  "a bookmark with no page opens with an empty cell rather than a 0 or a 1");
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(),
                  "an untouched box is off: pressing it would record a change that is not one");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("一模一样")) && said.contains(QStringLiteral("不会多改什么")),
                  QStringLiteral("and it says that in those words: %1").arg(said));
            check(said.contains(QStringLiteral("其中 1 条不指向任何一页")) == false,
                  QStringLiteral("the untouched box says nothing about headings, because the "
                                 "sentence about a change belongs to a change: %1")
                      .arg(said));
        }

        {
            // One keystroke and the box becomes a request: the count, the heading aside, and the
            // family's ending all have to arrive together, or the reader agrees to a sentence that
            // stopped being true when they typed.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            check(grid && grid->item(0, 0), "setup: the first row has a name cell");
            grid->item(0, 0)->setText(QStringLiteral("第一章 改名"));
            QPushButton *okay = okayOf(box);
            const QString renamed = saidBy(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("renaming one row is a change, and the button knows: %1")
                      .arg(renamed));
            check(okay && okay->text() == QStringLiteral("写这 5 条书签"),
                  QStringLiteral("and the button counts them: %1")
                      .arg(okay ? okay->text() : QStringLiteral("(none)")));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("会有 5 条书签")) && said.contains(QStringLiteral("原来那份文件")),
                  QStringLiteral("the summary repeats the count and keeps the save promise: %1")
                      .arg(said));
            check(said.contains(QStringLiteral("其中 1 条不指向任何一页")),
                  QStringLiteral("and says which of the five is a heading: %1").arg(said));
        }

        {
            // §70's owed sentence, on the only path that can produce it: the page was taken out by an
            // edit that has not been saved, so the reader's number still names a page on screen and
            // nothing at all in the file a save would write.
            DocumentEditor deleted;
            QString e;
            check(deleted.open(book, QString(), &e), QStringLiteral("setup: opens again: %1").arg(e));
            check(deleted.deletePages({2}, &e), QStringLiteral("setup: the third page is pending-deleted: %1").arg(e));
            const QVector<OutlineRow> naming = {
                {QStringLiteral("还在一起"), 1, 0, QPointF()},
                {QStringLiteral("被删那页"), 1, 2, QPointF()},
            };
            BookmarkDialog box(&tab, &deleted, outlineFromRows(naming));
            QTableWidget *grid = gridOf(box);
            // A change first, because an untouched box is not a request at all (that is the rule two
            // blocks above pin): what is being asked here is whether the box tells the reader about
            // the page that is already gone *while* they are making an edit that will write it.
            grid->item(0, 0)->setText(QStringLiteral("还在一起 改名"));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("renaming is a change, so the press is allowed through: %1")
                      .arg(saidBy(box)));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("在前面的修改里已经删掉了")),
                  QStringLiteral("and the box says which kind of bookmark they are about to write: %1")
                      .arg(said));
            check(said.contains(QStringLiteral("有 1 条")),
                  QStringLiteral("counted, not vaguely mentioned: %1").arg(said));
            check(box.rows().at(1).page == 2,
                  "the row keeps the number the reader typed rather than being renumbered here");
        }

        {
            // Sharing a page is not the same as losing one, and this is the case that was missed
            // until a real book was opened in the box: 184 bookmarks over fewer pages, with several
            // rows on the same page. Asking "how many of my rows are still on a page" of a function
            // that counts *distinct pages* subtracts the duplicates and reports them as deleted --
            // which is how the box told a reader who had deleted nothing that 77 bookmarks pointed at
            // pages that were gone (docs/store-submission.md §72).
            DocumentEditor shared;
            QString e;
            check(shared.open(book, QString(), &e), QStringLiteral("setup: opens for the shared page: %1").arg(e));
            const QVector<OutlineRow> threeOnTwo = {
                {QStringLiteral("One"), 1, 5, QPointF()},
                {QStringLiteral("Two"), 1, 5, QPointF()},
                {QStringLiteral("Three"), 1, 6, QPointF()},
            };
            BookmarkDialog box(&tab, &shared, outlineFromRows(threeOnTwo));
            box.findChild<QTableWidget *>()->item(0, 0)->setText(QStringLiteral("One 改名"));
            const QString said = saidBy(box);
            check(!said.contains(QStringLiteral("在前面的修改里已经删掉了")),
                  QStringLiteral("three bookmarks on two pages, nothing deleted, so nothing is "
                                 "reported as taken out: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled(), "and the change itself is still a change");

            // The positive half: the same box, with a page genuinely pending-deleted behind it, says
            // the sentence about the one row that lost its page -- so the check above is not green
            // because the sentence can never appear.
            check(shared.deletePages({6}, &e), QStringLiteral("setup: page 7 is now pending-deleted: %1").arg(e));
            box.findChild<QTableWidget *>()->item(1, 0)->setText(QStringLiteral("Two 改名"));
            const QString gone = saidBy(box);
            check(gone.contains(QStringLiteral("在前面的修改里已经删掉了")) && gone.contains(QStringLiteral("有 1 条")),
                  QStringLiteral("counted as rows, not as pages: %1").arg(gone));
        }

        {
            // The two ways a page cell can be wrong, and that both stop the press.
            DocumentEditor strict;
            QString e;
            check(strict.open(book, QString(), &e), QStringLiteral("setup: opens for the bounds: %1").arg(e));
            const QVector<OutlineRow> one = {
                {QStringLiteral("Only"), 1, 5, QPointF()},
            };
            BookmarkDialog box(&tab, &strict, outlineFromRows(one));
            QTableWidget *grid = gridOf(box);
            grid->item(0, 2)->setText(QStringLiteral("999"));
            QPushButton *okay = okayOf(box);
            const QString said = saidBy(box);
            check(okay && !okay->isEnabled(), "a page the file never had is refused before the press");
            check(said.contains(QStringLiteral("第 1 行")) && said.contains(QStringLiteral("40 页")),
                  QStringLiteral("naming the row and the file's own page count: %1").arg(said));
            grid->item(0, 2)->setText(QStringLiteral("第三" /* not a number */));
            const QString again = saidBy(box);
            check(again.contains(QStringLiteral("不是数字")) && again.contains(QStringLiteral("留空")),
                  QStringLiteral("and a cell that is not a number is told apart from a blank one, "
                                 "with the way to mean no page: %1")
                      .arg(again));
        }

        {
            // The 层级 column is a text cell, and that swap is the whole of §74: the first version put a
            // spin box *inside* the cell, and §72 read this box off the real window and found that cell
            // empty in the UIA tree -- a number on the screen and nothing for a screen reader. A text
            // cell is reachable, but it can now hold "0", "深" or nothing at all, so the bound the spin
            // enforced silently is said out loud before the press instead. Both halves are pinned here:
            // the shape the accessibility tree can reach, and the refusal that replaced the clamp.
            DocumentEditor levels;
            QString e;
            check(levels.open(book, QString(), &e),
                  QStringLiteral("setup: opens for the level column: %1").arg(e));
            const QVector<OutlineRow> nested = {
                {QStringLiteral("章"), 1, 0, QPointF()},
                {QStringLiteral("节"), 2, 2, QPointF()},
            };
            BookmarkDialog box(&tab, &levels, outlineFromRows(nested));
            QTableWidget *grid = gridOf(box);
            check(grid->item(1, 1) && grid->item(1, 1)->text() == QStringLiteral("2"),
                  QStringLiteral("the level is the cell's own text, which is what the tree reads: %1")
                      .arg(grid->item(1, 1) ? grid->item(1, 1)->text() : QStringLiteral("(none)")));
            check(grid->cellWidget(0, 1) == nullptr && grid->cellWidget(1, 1) == nullptr,
                  "and no widget sits in those cells, because a cell widget is absent from UIA (§72)");

            QPushButton *okay = okayOf(box);
            // A real request first. Without this the button would be grey for the *other* reason -- an
            // untouched box (§71) -- and a refusal asserted as "grey" would be green whether or not the
            // level check exists at all.
            grid->item(0, 0)->setText(QStringLiteral("章 改名"));
            check(okay && okay->isEnabled(), "setup: renaming made it a request");

            grid->item(0, 1)->setText(QStringLiteral("0"));
            QString said = saidBy(box);
            check(okay && !okay->isEnabled(),
                  QStringLiteral("a typed 0 is refused before the press, not clamped to 1: %1").arg(said));
            check(said.contains(QStringLiteral("第 1 行的层级")) && said.contains(QStringLiteral("1 或更大")),
                  QStringLiteral("naming the row, the column and what a level looks like: %1").arg(said));
            check(box.rows().at(0).level == 0,
                  "and the list keeps the number that was typed, so nothing is quietly re-nested");

            grid->item(0, 1)->setText(QString());
            said = saidBy(box);
            check(!okay->isEnabled() && said.contains(QStringLiteral("第 1 行的层级")),
                  QStringLiteral("a blank level is the same refusal, because a blank is not a level: %1")
                      .arg(said));

            grid->item(0, 1)->setText(QStringLiteral("深"));
            said = saidBy(box);
            check(!said.contains(QStringLiteral("不是数字")),
                  QStringLiteral("and it does not borrow the page column's non-number sentence, whose "
                                 "second half tells the reader to leave the cell empty -- here empty is "
                                 "not an answer: %1").arg(said));

            grid->item(0, 1)->setText(QStringLiteral("3"));
            said = saidBy(box);
            check(okay->isEnabled(), QStringLiteral("a level of 3 is a request again: %1").arg(said));
            check(box.rows().at(0).level == 3, "and that is the number the model gets");

            grid->item(0, 1)->setText(QStringLiteral("0"));
            grid->item(0, 2)->setText(QStringLiteral("999"));
            said = saidBy(box);
            check(said.contains(QStringLiteral("层级")) && !said.contains(QStringLiteral("不在这份文件里")),
                  QStringLiteral("a row broken in two columns is asked about left first: %1").arg(said));

            {
                // Writing back the digits a row already had is not an edit: §71's "untouched box" rule
                // compares the parsed number, so a cell re-typed as "1" has to land back on the level
                // it opened with rather than reading as a change.
                grid->item(0, 0)->setText(QStringLiteral("章"));
                grid->item(0, 1)->setText(QStringLiteral("1"));
                grid->item(0, 2)->setText(QStringLiteral("1"));
                grid->item(1, 1)->setText(QStringLiteral("2"));
                const QString back = saidBy(box);
                check(!okay->isEnabled(),
                      QStringLiteral("back to exactly what it opened with, so the press is off again: %1")
                          .arg(back));
                check(back.contains(QStringLiteral("一模一样")),
                      QStringLiteral("and it says the untouched sentence, not a count of what would be "
                                     "written: %1").arg(back));
            }
        }

        {
            // Which message wins when two rows are broken: the topmost row. A box that answered by
            // *kind* would name row 5's page while the reader is looking at row 2's empty name.
            const QVector<OutlineRow> two = {
                {QStringLiteral("First"), 1, 0, QPointF()},
                {QString(), 1, 3, QPointF()},
                {QStringLiteral("Last"), 1, 999, QPointF()},
            };
            BookmarkDialog box(&tab, &editor, outlineFromRows(two));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("第 2 行还没有名字")),
                  QStringLiteral("the earlier row is the one it asks about: %1").arg(said));
            check(!said.contains(QStringLiteral("第 3 行")),
                  QStringLiteral("and it does not talk about the third row yet: %1").arg(said));
        }

        {
            // 加一条 puts the new row beside the selected one -- after its whole subtree, which is
            // the one question outlineSubtreeEnd() answers. A row inserted directly under a chapter
            // would become its child and rearrange a tree the reader did not touch.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            grid->setCurrentCell(0, 0);
            QPushButton *add = button(box, QStringLiteral("加一条"));
            check(add && add->isEnabled(), "a box with rows in it can take one more");
            add->click();
            const QVector<OutlineRow> after = box.rows();
            check(after.size() == 6, QStringLiteral("one row was added, not one child: %1")
                                          .arg(after.size()));
            check(after.at(4).title == QStringLiteral("新书签") && after.at(4).level == 1,
                  QStringLiteral("and it landed after 第一章's whole block, at the top level (%1)")
                      .arg(orderOf(box)));
            check(after.at(5).title == QStringLiteral("第二章") && after.at(5).page == -1,
                  QStringLiteral("with the file's own last bookmark still last and still without a "
                                 "page (%1)")
                      .arg(orderOf(box)));
        }

        {
            // 删一条 takes the subtree with it. The children have nowhere else to go: left behind,
            // outlineFromRows would hang them on whatever row precedes them, which is a different
            // document than the one the reader was looking at.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            grid->setCurrentCell(0, 0);
            QPushButton *remove = button(box, QStringLiteral("删一条"));
            check(remove && remove->isEnabled(), "a selected row can be taken out");
            remove->click();
            check(box.rows().size() == 1,
                  QStringLiteral("taking the chapter out took its three descendants with it: %1")
                      .arg(orderOf(box)));
        }

        {
            // 上移/下移 move a block past a sibling and are off where there is no sibling to pass.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            grid->setCurrentCell(1, 0); // 1.1, a level-2 row with a child and one sibling (1.2)
            QPushButton *up = button(box, QStringLiteral("上移"));
            QPushButton *down = button(box, QStringLiteral("下移"));
            check(up && !up->isEnabled(),
                  "a first child cannot move above its parent, and the button says so");
            check(down && down->isEnabled(), "but it has a sibling below to swap with");
            down->click();
            check(orderOf(box) == QStringLiteral(
                      "第一章/1/0, 1.2/2/6, 1.1/2/2, 1.1.1/3/4, 第二章/1/-1"),
                  QStringLiteral("and the swap carried the child along with its parent: %1")
                      .arg(orderOf(box)));
        }

        {
            // Opening on what is already pending, and the difference between "same as the file" and
            // "same as the last request": both grey the button, and they have to say different things,
            // because the first is nothing to undo and the second is an edit that is already recorded.
            DocumentEditor pending;
            QString e;
            check(pending.open(book, QString(), &e), QStringLiteral("setup: opens for the pending tree: %1").arg(e));
            check(pending.setOutline(sample, &e), QStringLiteral("setup: a tree is recorded: %1").arg(e));
            // The file's own tree, deliberately a different size, so that "it opened on the recorded
            // list" and "it opened on the bookmarks in the file" cannot both be true of one reading.
            const QVector<OutlineRow> asInFile = {
                {QStringLiteral("旧封面"), 1, 0, QPointF()},
                {QStringLiteral("旧版权"), 1, 1, QPointF()},
            };
            BookmarkDialog box(&tab, &pending, outlineFromRows(asInFile));
            QTableWidget *grid = gridOf(box);
            check(grid && grid->rowCount() == 5,
                  QStringLiteral("the box opens on the recorded tree, not on the file's own bookmarks "
                                 "(%1 rows)")
                      .arg(grid ? grid->rowCount() : -1));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and untouched, it is still not a change");
            grid->item(0, 0)->setText(QStringLiteral("第一章 又改"));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("这会把前面记下的那一条")),
                  QStringLiteral("now it says which list is being replaced: %1").arg(said));
        }
    }

    // ---- the box says what a save will cost the file's own bookmarks (§80) ----
    //
    // §75 measured that every bookmark this program writes comes out as /Fit, so a document whose
    // bookmarks name a place inside a page loses that layer on a save -- and nothing on this screen
    // can show it, because the tree being edited holds a title, a level and a page number and no
    // position at all. §75's third route is the one taken: say it before the press. Both halves are
    // pinned -- it is said for a file that carries positions, and *not* said for one that does not,
    // for §77's reason that a warning about a loss which cannot happen here trains the reader to
    // ignore the warnings that can.
    //
    // The sample is hand-built for the same reason §75's was: our own writer cannot produce a
    // bookmark with a position in it, so no file written by this program ever reaches this branch.
    {
        QTemporaryDir dir;
        check(dir.isValid(), "setup: a temporary folder for the bookmark sample");
        const QString sample = dir.filePath(QStringLiteral("positional-bookmarks.pdf"));
        bool built = false;
        QString why;
        try {
            QPDF qpdf;
            qpdf.processFile((directory + QStringLiteral("/many-pages.pdf")).toUtf8().constData());
            const std::vector<QPDFPageObjectHelper> pages =
                QPDFPageDocumentHelper(qpdf).getAllPages();
            auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };
            QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
            root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
            root = qpdf.makeIndirectObject(root);
            auto item = [&qpdf](const std::string &title, QPDFObjectHandle parent) {
                QPDFObjectHandle handle = QPDFObjectHandle::newDictionary();
                handle.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
                handle.replaceKey("/Parent", parent);
                return qpdf.makeIndirectObject(handle);
            };
            auto addDest = [](QPDFObjectHandle handle, std::vector<QPDFObjectHandle> parts) {
                QPDFObjectHandle dest = QPDFObjectHandle::newArray();
                for (QPDFObjectHandle &part : parts)
                    dest.appendItem(part);
                handle.replaceKey("/Dest", dest);
            };
            const QPDFObjectHandle xyz = item("Xyz spot", root);
            addDest(xyz, { pageAt(1), QPDFObjectHandle::newName("/XYZ"),
                           QPDFObjectHandle::newReal(0), QPDFObjectHandle::newReal(700),
                           QPDFObjectHandle::newReal(0) });
            const QPDFObjectHandle fitHorizontal = item("FitH spot", root);
            addDest(fitHorizontal, { pageAt(4), QPDFObjectHandle::newName("/FitH"),
                                     QPDFObjectHandle::newReal(650) });
            const QPDFObjectHandle fit = item("Fit page", root);
            addDest(fit, { pageAt(7), QPDFObjectHandle::newName("/Fit") });
            const QPDFObjectHandle heading = item("Heading", root);
            std::vector<QPDFObjectHandle> row = { xyz, fitHorizontal, fit, heading };
            for (size_t i = 0; i < row.size(); ++i) {
                if (i > 0)
                    row[i].replaceKey("/Prev", row[i - 1]);
                if (i + 1 < row.size())
                    row[i].replaceKey("/Next", row[i + 1]);
            }
            root.replaceKey("/First", row.front());
            root.replaceKey("/Last", row.back());
            root.replaceKey("/Count", QPDFObjectHandle::newInteger(int(row.size())));
            qpdf.getRoot().replaceKey("/Outlines", root);
            QPDFWriter writer(qpdf, sample.toUtf8().constData());
            writer.write();
            built = true;
        } catch (const std::exception &e) {
            why = QString::fromUtf8(e.what());
        }
        check(built,
              QStringLiteral("setup: a file whose bookmarks name a spot inside a page: %1").arg(why));

        const auto saidByBox = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };

        DocumentEditor carrying;
        QString e;
        check(carrying.open(sample, QString(), &e),
              QStringLiteral("setup: the positional file opens for the editor: %1").arg(e));
        check(carrying.sourcePositionalBookmarks() == 2,
              QStringLiteral("and the editor counts two of its four bookmarks as carrying a "
                             "position: %1").arg(carrying.sourcePositionalBookmarks()));

        const QVector<OutlineRow> one = { { QStringLiteral("改一下"), 1, 0, QPointF() } };
        BookmarkDialog box(&tab, &carrying, outlineFromRows(one));
        // A real request first: the clause sits with the other "what pressing will do" sentences, and
        // an untouched box answers 一模一样 instead (§71) -- without this the check below could be
        // reading a summary that never reached the branch it claims to test.
        box.findChild<QTableWidget *>()->item(0, 0)->setText(QStringLiteral("改一下名字"));
        const QString said = saidByBox(box);
        // §99 changed what this number counts, and the test has to follow it. The file carries two
        // positional bookmarks; a request that replaces the whole tree with one row of our own
        // matches none of them, so only the row standing in slot 0 -- where the file had /XYZ -- is
        // reported. Saying 2 here would be §80's old claim ("这一版一条都留不住"), which stopped
        // being true the moment the destinations started riding along with the rows.
        QVector<OutlineRow> justRenamed = one;
        justRenamed[0].title = QStringLiteral("改一下名字");
        check(carrying.positionalAtRiskFor(justRenamed) == 2,
              QStringLiteral("setup: replacing the whole tree with one new row leaves both of the "
                             "file's positions behind: %1")
                  .arg(carrying.positionalAtRiskFor(justRenamed)));
        check(said.contains(QStringLiteral("其中 2 条书签指着页面内的某个位置")),
              QStringLiteral("the box says, before the press, how many positions THIS request "
                             "will leave behind: %1")
                  .arg(said));
        check(said.contains(QStringLiteral("整页适配")) && said.contains(QStringLiteral("对不上")),
              QStringLiteral("and says what those become, and why this time it is the request and "
                             "not the version: %1")
                  .arg(said));

        // The half §99 bought, and the reason the warning is not decoration: ask for the file's own
        // tree, change something that carries no position, and nothing is at risk -- so the sentence
        // is not said about a loss that is not happening (§77's rule, now with a live case where it
        // applies). One row's title is edited, so this is still a real request and not the untouched
        // 一模一样 answer §71 pins.
        const QVector<OutlineRow> asTheFileHas = {
            { QStringLiteral("Xyz spot"), 1, 1, QPointF() },
            { QStringLiteral("FitH spot"), 1, 4, QPointF() },
            { QStringLiteral("Fit page"), 1, 7, QPointF() },
            { QStringLiteral("Heading"), 1, -1, QPointF() },
        };
        BookmarkDialog carried(&tab, &carrying, outlineFromRows(asTheFileHas));
        carried.findChild<QTableWidget *>()->item(3, 0)->setText(QStringLiteral("Heading, renamed"));
        const QString carriedSaid = saidByBox(carried);
        QVector<OutlineRow> renamedHeading = asTheFileHas;
        renamedHeading[3].title = QStringLiteral("Heading, renamed");
        check(carrying.positionalAtRiskFor(renamedHeading) == 0,
              QStringLiteral("setup: renaming a bookmark that had no position puts none at risk: %1")
                  .arg(carrying.positionalAtRiskFor(renamedHeading)));
        check(!carriedSaid.contains(QStringLiteral("页面内的某个位置")),
              QStringLiteral("and nothing is said about positions, though this file has two of "
                             "them: %1")
                  .arg(carriedSaid));

        // The negative half, same box, a document with nothing at risk.
        DocumentEditor plain;
        check(plain.open(path, QString(), &e),
              QStringLiteral("setup: a document with no bookmarks at all opens: %1").arg(e));
        check(plain.sourcePositionalBookmarks() == 0,
              QStringLiteral("and the count there is zero: %1").arg(plain.sourcePositionalBookmarks()));
        BookmarkDialog quiet(&tab, &plain, outlineFromRows(one));
        quiet.findChild<QTableWidget *>()->item(0, 0)->setText(QStringLiteral("改一下名字"));
        const QString kept = saidByBox(quiet);
        check(!kept.contains(QStringLiteral("页面内的某个位置")),
              QStringLiteral("nothing is said about positions for a file that has none: %1").arg(kept));
    }

    //
    // §13 wrote this down from a real window: after a zoom-mode change the pages in
    // front were 5 and 6 while the bar still read "2 / 40", and pressing a paging key
    // once made the two agree. The reason is that only scrolling ever works out which
    // page is current -- see scrollContentsBy -- and the changes that re-lay the pages
    // out can move a page boundary across the middle of the viewport without moving the
    // scrollbar by a pixel. No scroll event, so nothing re-derived the number, and what
    // the reader was told named a page that was not in front of them. The same number
    // feeds the page box and the six 文档 dialogs' "当前页（第 %1 页）", so it is not
    // only the corner of the window that was wrong.
    //
    // The arithmetic below is worked out by hand the way layout_smoke's is: the
    // fixture's pages are 595x842 points and the gap is 12 pixels (kSpacing), so at 100%
    // page k starts at 12 + k * 854 content pixels, and the middle of the window is the
    // scroll position plus half a viewport.
    {
        check(tab.open(directory + QStringLiteral("/many-pages.pdf")) == Document::Status::Ready,
              "the forty-page fixture opens");
        check(tab.pageCount() == 40,
              QStringLiteral("forty pages, the document the note was read on (got %1)")
                  .arg(tab.pageCount()));

        // Which page is in front, worked out from the rectangles the view hands out
        // rather than from the answer it reports: a check that asked the view both
        // questions could be answered by a view that was wrong about both. The rule is
        // the one a reader uses without noticing -- the page the middle of the window is
        // on -- and the hand-worked numbers below are what pin it, since this lambda and
        // PageLayout::pageAtViewportCenter could both be quietly wrong.
        const auto pageOnScreen = [view] {
            const double middle = view->viewport()->height() / 2.0;
            const int count = view->pageCount();
            for (int page = 0; page < count; ++page) {
                const QRectF rect = view->pageRectInViewport(page);
                if (!rect.isNull() && middle < rect.bottom())
                    return page;
            }
            return count - 1;
        };

        // The status bar is not polled; it moves when the view says so, through
        // DocumentTab's pageChanged. A fix that settled the internal page and stayed
        // quiet would leave the number where it was, so the announcement is what is
        // checked, not just the field.
        int announcements = 0;
        int lastAnnounced = -1;
        const QMetaObject::Connection listening =
            QObject::connect(view, &PageView::currentPageChanged, view, [&](int page) {
                ++announcements;
                lastAnnounced = page;
            });

        // Back to one place every case starts from: 100%, one page per row, page 11's top
        // edge at the top of the window. A case that began anywhere else would have to
        // argue about where it began as well as about what changed.
        const auto startAtPageEleven = [&](const QString &forCase) {
            view->setRotation(0);
            view->setLayoutMode(PageLayout::Mode::Continuous);
            view->setZoomMode(ZoomController::Mode::Percent);
            view->setZoom(1.0);
            view->goToPage(10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(view->verticalScrollBar()->value() == 8552,
                  QStringLiteral("%1 starts with page 11's top at the top of the window "
                                 "(scroll is %2)")
                      .arg(forCase)
                      .arg(view->verticalScrollBar()->value()));
            check(view->currentPage() == 10 && pageOnScreen() == 10,
                  QStringLiteral("%1 begins in agreement: bar %2, screen %3")
                      .arg(forCase)
                      .arg(view->currentPage() + 1)
                      .arg(pageOnScreen() + 1));
            announcements = 0;
            lastAnnounced = -1;
        };

        // The promise, said once and then asked after every one of the changes that can
        // break it.
        const auto agreesWithTheScreen = [&](const QString &after) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const int shown = pageOnScreen();
            check(shown != 10,
                  QStringLiteral("%1 moved a page boundary across the middle of the window, "
                                 "so there was something to get wrong (still page %2)")
                      .arg(after)
                      .arg(shown + 1));
            check(view->currentPage() == shown,
                  QStringLiteral("%1 names the page that is in front (bar says page %2, the "
                                 "screen shows page %3)")
                      .arg(after)
                      .arg(view->currentPage() + 1)
                      .arg(shown + 1));
            check(announcements > 0 && lastAnnounced == shown,
                  QStringLiteral("%1 says it out loud, which is the only way the bar and the "
                                 "page box can hear (announcements %2, last %3)")
                      .arg(after)
                      .arg(announcements)
                      .arg(lastAnnounced + 1));
        };

        // ---- scrolling, which is the path that already worked ----
        //
        // Here so that the one place that decides the answer has a check over every way of
        // getting there: the older test of this asks how many times the view said a page
        // changed, which a view that said the wrong one would pass just as well.
        {
            startAtPageEleven(QStringLiteral("scrolling to page 2"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(866 - middle + 10);
            check(pageOnScreen() == 1,
                  QStringLiteral("page 2 is what the middle of the window is on now (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("scrolling to page 2"));
        }

        // ---- a factor, chosen by hand or typed into the box ----
        //
        // At 400% a page occupies 3368 pixels and starts at 12 + k * 3380. The middle of
        // the window is between 8552 and 9394 for any window under 1684 px tall, and that
        // whole range is inside page 3 (6772 .. 10140).
        {
            startAtPageEleven(QStringLiteral("zooming to 400%"));
            const int scrolledTo = view->verticalScrollBar()->value();
            view->setZoom(4.0);
            check(view->verticalScrollBar()->value() == scrolledTo,
                  "and the zoom did not scroll anywhere -- which is the whole reason: there "
                  "was no scroll event to re-derive the page from");
            check(pageOnScreen() == 2,
                  QStringLiteral("page 3 is what is in front at four times the size (says %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("zooming to 400%"));
        }

        // ---- a fit mode, which is what the note was about ----
        //
        // 适应整页 shrinks the factor until a whole page fits the window's height, so the
        // pages get shorter and one of them ends up higher than the middle of the window
        // than it was. No hand number here: the factor depends on the window's height,
        // which the test reads rather than assumes.
        {
            startAtPageEleven(QStringLiteral("适应整页"));
            view->setZoomMode(ZoomController::Mode::FitPage);
            agreesWithTheScreen(QStringLiteral("适应整页"));
        }

        // ---- the same fit mode in a window wide enough for a page ----
        //
        // This case exists because the one above does not, on its own, cover the mode
        // change. Fitting a page makes the pages narrower as well as shorter, and in a
        // window that was too narrow for a page that takes the sideways scrollbar away --
        // which resizes the viewport a moment later, and that resize recomputes the fit and
        // re-takes the number. Cut the call site in setZoomMode() and the case above stays
        // green: the number is settled by the second pass, not the first.
        //
        // Wide enough for a page to hang nothing sideways, there is no second pass: the mode
        // change is the only event, so this is the case that asks it.
        {
            const QSize sizeBefore = tab.size();
            tab.resize(1400, 620);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            startAtPageEleven(QStringLiteral("适应整页 in a wide window"));
            check(view->horizontalScrollBar()->maximum() == 0,
                  QStringLiteral("the window is wide enough that nothing hangs sideways at "
                                 "100% either (scrollbar maximum %1)")
                      .arg(view->horizontalScrollBar()->maximum()));

            const int heightBefore = view->viewport()->height();
            view->setZoomMode(ZoomController::Mode::FitPage);
            check(view->viewport()->height() == heightBefore,
                  QStringLiteral("and the fitted pages are narrower still, so no scrollbar "
                                 "came or went and no resize arrives afterwards to do it "
                                 "instead (viewport height %1 -> %2)")
                      .arg(heightBefore)
                      .arg(view->viewport()->height()));
            agreesWithTheScreen(QStringLiteral("适应整页 in a wide window"));
            tab.resize(sizeBefore);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }

        // ---- two pages per row ----
        //
        // Twenty spreads of 854 pixels each, so the middle of the window (8552 .. 9394)
        // lands in spread 11, which is pages 21 and 22 -- and the layout's rule names the
        // left one of a spread first.
        {
            startAtPageEleven(QStringLiteral("turning on two-page mode"));
            view->setLayoutMode(PageLayout::Mode::TwoPage);
            check(pageOnScreen() == 20,
                  QStringLiteral("the spread in front is pages 21 and 22 (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("turning on two-page mode"));
        }

        // ---- a quarter turn ----
        //
        // The pages become 842 wide by 595 tall, so every page above the middle of the
        // window gets 247 pixels shorter and the middle reaches further into the
        // document. Which page exactly depends on the window's height, so this one is the
        // invariant alone -- with the check inside agreesWithTheScreen that a boundary
        // really did cross, which is what keeps it from passing by standing still.
        {
            startAtPageEleven(QStringLiteral("rotating"));
            view->setRotation(90);
            agreesWithTheScreen(QStringLiteral("rotating"));
        }

        // ---- a shorter window ----
        //
        // The clearest case of all, because nothing about the document changed at all: the
        // middle of the window moved up by half the difference in height. A taller window
        // makes the same point, but this screen is only so tall -- growing the window by
        // two page periods grew the viewport by one, which is why the measurement goes the
        // other way -- and a shorter window starts by parking page 11's top edge a known
        // ten pixels above the middle, so the distance to the boundary it has to cross is
        // fixed rather than a function of how tall the window happens to be.
        {
            startAtPageEleven(QStringLiteral("making the window shorter"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(8552 - middle + 10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(pageOnScreen() == 10,
                  QStringLiteral("the middle of the window is ten pixels inside page 11 "
                                 "(says page %1)")
                      .arg(pageOnScreen() + 1));

            const int heightBefore = view->viewport()->height();
            tab.resize(tab.width(), tab.height() - 200);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const int lost = heightBefore - view->viewport()->height();
            // The middle rose by half of that, so anything between 21 and 1752 pixels
            // leaves it inside page 10 -- the ask is 200, and however much of it the
            // window's own minimum size gave back, the answer is a page number rather
            // than a measurement of the platform.
            check(lost > 20 && lost < 1752,
                  QStringLiteral("the window lost %1 pixels of page, which is more than twice "
                                 "the ten pixels to spare and less than a whole page")
                      .arg(lost));
            check(pageOnScreen() == 9,
                  QStringLiteral("a shorter window reaches back over the edge the middle was "
                                 "inside (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("making the window shorter"));
        }

        // ---- opening a second document in the same tab ----
        //
        // The same fact, with the document changing instead of the window: which page the
        // middle of the window sits on is a property of the geometry, and the geometry
        // includes what is laid out. setPageSizes() answered it by writing page 1 down,
        // which is where a reader expects to start rather than where they end up -- the
        // scroll position is not reset for a new document, so a two-page file opened in a
        // tab that was showing page 2 of a forty-page one is showing page 2, and the bar
        // read "1 / 2" while it did.
        //
        // text.pdf's pages are the same 595x842 as the forty-page fixture's, which is what
        // puts the position below inside the new document rather than past its end -- a
        // position past the end would be clamped, the clamp would scroll, and the scrolling
        // path would have quietly fixed it. That is why the number is asked for before and
        // compared after: this case is the half of the situation that no scroll event ever
        // covered. Where a new document *should* start is a different question, and is
        // recorded as still open in docs/store-submission.md §25.
        {
            startAtPageEleven(QStringLiteral("opening a two-page document"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(866 - middle + 10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(pageOnScreen() == 1,
                  QStringLiteral("the middle of the window is ten pixels inside page 2 of the "
                                 "forty-page fixture (says page %1)")
                      .arg(pageOnScreen() + 1));
            const int kept = view->verticalScrollBar()->value();

            check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
                  "the two-page fixture opens in the same tab");
            check(tab.pageCount() == 2,
                  QStringLiteral("of two pages (got %1)").arg(tab.pageCount()));
            check(view->verticalScrollBar()->value() == kept,
                  QStringLiteral("and the new document is still showing the same content "
                                 "pixels as the old one (scroll %1 -> %2), so no scroll event "
                                 "happened -- which is the shape of this bug")
                      .arg(kept)
                      .arg(view->verticalScrollBar()->value()));
            agreesWithTheScreen(QStringLiteral("opening a two-page document"));
        }

        QObject::disconnect(listening);
    }

    // ---- what a save does to a name another tab of this program is already reading ----
    //
    // The last step of DocumentEditor::saveAs is one MoveFileExW per file with
    // MOVEFILE_REPLACE_EXISTING, and the name the save dialog hands back can perfectly well be a PDF
    // this very window has open in a different tab -- the editor refuses its own source file and any
    // page it merged in from (documenteditor.cpp:2201, :2215), and nothing refuses a tab. Two bad
    // endings are possible and which one happens has never been asked of a file: the move fails and
    // the reader is told the name is "locked by another program" while the program is us, or the move
    // succeeds and the other tab goes on showing bytes that no longer exist on disk. Printing both
    // answers here, before choosing a fix, is §58's rule -- a suspicion written as a run rather than
    // as a sentence.
    {
        QTemporaryDir dir;
        check(dir.isValid(), "setup: a temporary folder for the overwrite measurement");
        const QString beingRead = dir.filePath(QStringLiteral("being-read.pdf"));
        const QString stagedFile = dir.filePath(QStringLiteral("staged-overwrite.pdf"));
        check(QFile::copy(path, beingRead),
              "setup: the name another tab will read is a copy in the scratch folder");
        check(QFile::copy(directory + QStringLiteral("/many-pages.pdf"), stagedFile),
              "setup: the file that will be moved onto it is the many-page one");

        DocumentTab other;
        other.resize(700, 500);
        other.show();
        check(other.open(beingRead) == Document::Status::Ready,
              QStringLiteral("setup: another tab has that name open: %1").arg(other.lastError()));
        PageView *otherView = viewOf(other);
        check(waitFor([otherView] { return otherView->hasImageFor(0); }),
              "setup: and its first page is drawn, so the tab is idle the way a reader's would be");
        const int pagesWhileOpen = other.pageCount();

        const std::wstring from = QDir::toNativeSeparators(stagedFile).toStdWString();
        const std::wstring to = QDir::toNativeSeparators(beingRead).toStdWString();
        const BOOL moved = MoveFileExW(from.c_str(), to.c_str(),
                                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        const DWORD lastError = moved ? 0 : GetLastError();

        int pagesOnDisk = -1;
        try {
            QPDF probe;
            probe.processFile(beingRead.toUtf8().constData());
            pagesOnDisk = int(QPDFPageDocumentHelper(probe).getAllPages().size());
        } catch (const std::exception &e) {
            std::cerr << "FAIL: could not read the name the move left at: " << e.what() << '\n';
            ++failures;
        }
        note(QStringLiteral("sec79 move onto a name another tab reads: moved=%1 win32=%2, the tab "
                            "says %3 pages, the file at that name now says %4, the staged file is "
                            "still there: %5")
                 .arg(moved ? 1 : 0)
                 .arg(lastError)
                 .arg(pagesWhileOpen)
                 .arg(pagesOnDisk)
                 .arg(QFile::exists(stagedFile) ? 1 : 0));

        // The premise §79's early refusal rests on, pinned as checks rather than left as printed
        // numbers: a name one tab is reading cannot be replaced, and the failed attempt leaves both
        // files whole. If a future Qt starts sharing the delete, these redden -- and the refusal
        // that cites this measurement should be relaxed in the same change, not left telling a
        // reader a name is unavailable when it has become available.
        check(!moved,
              QStringLiteral("a name another tab of this program is reading cannot be replaced "
                             "(win32 %1)").arg(lastError));
        check(QFile::exists(stagedFile),
              "and the file that was to be moved into its place is still where it was");
        check(pagesOnDisk == pagesWhileOpen,
              QStringLiteral("and the name still holds the document the reader was looking at "
                             "(disk says %1, the tab said %2)").arg(pagesOnDisk).arg(pagesWhileOpen));
        check(other.pageCount() == pagesWhileOpen,
              "and the reading tab is left showing that same document, not a half-applied one");

        // The second half, which only matters if the move went through: does the tab that is still
        // looking at that name notice the change? Asked twice -- what it says now, and what it says
        // after being told to open the same path again.
        const int pagesStillSaid = other.pageCount();
        const Document::Status reopened = other.open(beingRead);
        note(QStringLiteral("sec79 that tab says %1 pages after the move; opening the same path "
                            "again returns status=%2 and then says %3 pages")
                 .arg(pagesStillSaid)
                 .arg(static_cast<int>(reopened))
                 .arg(other.pageCount()));
    }

    if (failures == 0) {
        std::cout << "the view and the tab hold together (" << measurements
                  << " measurement(s) printed above)\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
