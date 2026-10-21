// The window: everything that belongs to the program rather than to a document.
//
// Menus, toolbar, status bar, recent files, the theme, the language, settings
// and about. Each open document is a DocumentTab in the strip in the middle,
// and nothing about a document -- its page, zoom, rotation -- is kept here.
// That division is the plan's §7.1 and it is what makes a second document
// possible; see documenttab.h for the other half of it.
#pragma once

#include "src/cli/commandline.h"
#include "src/view/pageview.h"
#include "src/view/viewchrome.h"

class WrappingToolBarArea;
struct WordExportReport;

#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPointer>
#include <QTimer>

#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressDialog;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class QWidget;
QT_END_NAMESPACE

class DocumentTab;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Handles what this launch was asked to do.
    //
    // False means there is nothing to show: the window is not made visible, and
    // the caller is expected to end the process rather than sit in an event
    // loop with nothing in it. True covers both "opened the file" and "opened
    // empty because no file was named".
    bool applyStartupCommandLine(const CommandLine::Request &request);

    // The same, for a request that arrived from a second launch. Never returns
    // false: the window is already up by definition.
    void applyForwardedCommandLine(const CommandLine::Request &request);

    // Whether anything is still going on that the window does not show -- a
    // render in flight, a document being read. main.cpp's idle check asks, so
    // that a launch with no window does not exit in the middle of its work.
    bool hasPendingWork() const;

private:
    void buildCentralWidget();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void rebuildRecentMenu();
    void retranslateUi();
    void updateForCurrentTab();
    void updateWindowTitle();
    void setStatusMessage(const QString &message, int timeoutMilliseconds = 0);

    // Both return the tab, or nullptr when the document could not be opened --
    // the caller in that case has already been told why.
    //
    // `password` is for the documents this program just wrote: an edit that ended with a
    // 加密 produced a copy the reader holds the key to, and asking for it again in the next
    // breath is not a prompt but a retry. Every other caller leaves it empty, and a file
    // that turns out to need a key still gets the prompt (promptForPassword).
    DocumentTab *openDocument(const QString &path, int page = -1,
                              const QString &password = QString());
    DocumentTab *currentTab() const;
    void openFileDialog();
    // Asks where to put the Word file, and tells the tab in front to write it.
    // The answer comes back as a signal -- the writing happens on a worker -- and
    // is said in a message box: this is a thing that produced a file somewhere,
    // and "it worked" has to come with where.
    void exportCurrentToWord();
    // Says what the export did, in a message box that names the file it wrote.
    void reportWordExport(const WordExportReport &report);
    // The progress box, put up when the export reports its first page and taken
    // down when it reports its last. Driven by the tab's own signals rather than
    // by the menu item, so that it describes work that is happening rather than
    // work that was asked for -- and so that an export started any other way gets
    // one too.
    void showExportProgress(DocumentTab *tab, int done, int total);
    void closeExportProgress();
    void closeTab(int index);
    // False when the reader said they did not mean to lose the edits this tab is
    // holding -- which is the only thing closeTab and the drag hand-off can do with
    // them, since nothing else writes them anywhere.
    bool confirmDiscardingEdits(DocumentTab *tab);
    // The same question asked once for the window, counting the tabs that hold
    // something. Used by closeEvent.
    bool confirmDiscardingEditsInAllTabs();
    void closeCurrentTab();
    // The tab-drag gestures wired from DocumentTabBar: reorder within this
    // window, detach a tab into a new window, accept a tab merged in from another
    // window, and drop a tab that another window has taken over.
    void reorderTab(int from, int to);
    void detachTabToNewWindow(int index, QPoint globalPos);
    void mergeTabIn(const QString &path, int page, int index);
    // A whole window was pulled along by its blank strip. Either another window
    // takes every tab it holds (this one then goes away), or nothing does and it
    // stays the window it already was.
    void mergeWindowIntoTargetAt(QPoint globalPos);
    // Every tab has been opened by another window: shed them all. Closing the
    // last one closes the window, which is the same route a single handed-off tab
    // takes.
    void handOffWholeWindow();
    // The 窗口 menu: bring every other window's tabs into this one and close them.
    void mergeAllWindows();
    // What this window has open, as the drag carries it: path and page per tab.
    QList<QPair<QString, int>> openDocumentRefs() const;
    // A window opened from a detached tab closes itself when its last tab goes.
    void setDetachedWindow(bool detached);
    // Asks for the password of an encrypted document, and retries.
    bool promptForPassword(DocumentTab *tab, const QString &path);

    void changeLanguage(const QString &code);
    void showSettings();
    void showAbout();
    void showThirdParty();

    // What a drag on a page does, from here on: applied to the tab in front,
    // remembered for the tabs opened after it, and shown in the toolbar.
    void chooseTool(PageView::Tool tool);

    // The toolbar's count of hits: which one is in front and how many there
    // are. Reads the tab in front rather than remembering anything, so that it
    // cannot disagree with the document it describes.
    void updateSearchCount();

    void zoomIn();
    void zoomOut();
    void zoomTo(double zoom);
    void fitWidth();
    void fitWholePage();
    void rotateBy(int degrees);
    void goToPageDialog();
    void stepPage(int delta);
    void jumpTo(int zeroBasedPage);

    // ---- fullscreen and presentation ----
    //
    // Which of the window's own furniture is on screen, and what a click and a
    // key mean while it is not. The decisions themselves are in viewchrome.h as
    // plain functions; everything here is the applying of them, which is the part
    // no offscreen test can see.
    void setChromeMode(ViewChrome::Mode mode);
    void toggleFullScreen();
    void togglePresentation();
    // Hides or shows one part of the window, and nothing else: entering a mode,
    // leaving one, and having the tab in front change all need the same answer
    // recomputed, so it is said once.
    void applyChrome();
    // Brings the toolbars back while fullscreen, because the pointer reached the
    // top edge -- and takes them away again when it has been away long enough.
    void revealToolBars(bool reveal);

protected:
    // One filter, one job: the tab strip does not get the wheel. See
    // buildCentralWidget() for why.
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Files dragged in from Explorer, which is how a reader is handed a
    // document by somebody who is looking at it in a folder. The window takes
    // the drop rather than the page surface, so a drag anywhere over the
    // window works -- including over the tab strip and over the empty state,
    // which is where a window with no documents in it spends its time.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    // Escape leaves fullscreen or presentation. It is asked here rather than by a
    // shortcut on the action, so that a search box holding the focus still gets
    // its Escape first -- see eventFilter().
    void keyPressEvent(QKeyEvent *event) override;

    // The last chance to stop a window closing while a tab in it is holding edits
    // no file has. Closing a single tab asks in closeTab; this covers the X, 关闭,
    // and anything else that takes the window whole.
    void closeEvent(QCloseEvent *event) override;

private:
    // What a drag in progress is carrying: the local files among its URLs,
    // decided once when the drag arrives rather than on every move -- the
    // answer costs a stat() per URL, and a drag move arrives continuously.
    // Empty means the drag is not one this window can do anything with.
    QStringList pendingDrop;

    // Set while a tab dragged from another window is over this one, so the body
    // (not just the thin strip) takes the merge and the exit knows to clear the
    // cue. The tab drag is a different animal from a file drop and never touches
    // pendingDrop.
    bool tabDragOver = false;


    Ui::MainWindow *ui;
    QTabWidget *tabs = nullptr;
    QStackedWidget *stack = nullptr;
    QWidget *emptyState = nullptr;
    QToolBar *toolBar = nullptr;
    // The strip the toolbars live in, which wraps them onto extra rows rather
    // than letting them hide their own tails: see buildToolBar().
    WrappingToolBarArea *toolBarArea = nullptr;
    // The export that is running, and the tab it is reading. Both null when
    // nothing is being exported, which is the state the box is shown in.
    QProgressDialog *exportProgress = nullptr;
    QPointer<DocumentTab> exportTab;
    // The last page count reported, so that a box left saying "正在停止…" still
    // shows how far it got rather than snapping back to nothing.
    int exportDone = 0;

    QComboBox *pageBox = nullptr;
    QComboBox *zoomBox = nullptr;
    QLabel *statusPage = nullptr;
    QLabel *statusZoom = nullptr;

    QMenu *recentMenu = nullptr;
    QMenu *languageMenu = nullptr;

    QAction *openAction = nullptr;
    QAction *viewToolAction = nullptr;
    QAction *selectToolAction = nullptr;
    QAction *recognizeToolAction = nullptr;
    QAction *translateToolAction = nullptr;
    QAction *copyAction = nullptr;
    QAction *translateAction = nullptr;
    QAction *selectAllAction = nullptr;
    QAction *exportWordAction = nullptr;
    QAction *printAction = nullptr;
    QAction *exportImagesAction = nullptr;
    QAction *propertiesAction = nullptr;
    // 文档: the list of edits this tab is holding, seen from its three ends --
    // what can be recorded, what the list can be moved back to, and the one
    // command that writes a file.
    QAction *deletePagesAction = nullptr;
    QAction *rotatePagesAction = nullptr;
    QAction *movePagesAction = nullptr;
    QAction *mergeFileAction = nullptr;
    QAction *splitFileAction = nullptr;
    QAction *protectFileAction = nullptr;
    QAction *watermarkAction = nullptr;
    QAction *bookmarkAction = nullptr;
    QAction *undoEditAction = nullptr;
    QAction *redoEditAction = nullptr;
    QAction *saveEditsAction = nullptr;
    QAction *findAction = nullptr;
    QAction *findNextAction = nullptr;
    QAction *findPreviousAction = nullptr;
    QLineEdit *searchBox = nullptr;
    QLabel *searchCount = nullptr;
    QAction *closeTabAction = nullptr;
    QAction *firstPageAction = nullptr;
    QAction *previousPageAction = nullptr;
    QAction *nextPageAction = nullptr;
    QAction *lastPageAction = nullptr;
    QAction *goToPageAction = nullptr;
    QAction *zoomInAction = nullptr;
    QAction *zoomOutAction = nullptr;
    QAction *fitWidthAction = nullptr;
    QAction *fitPageAction = nullptr;
    QAction *actualSizeAction = nullptr;
    QAction *rotateLeftAction = nullptr;
    QAction *rotateRightAction = nullptr;
    QAction *continuousAction = nullptr;
    QAction *singlePageAction = nullptr;
    QAction *twoPageAction = nullptr;
    QAction *bookModeAction = nullptr;
    QAction *outlineSidebarAction = nullptr;
    QAction *nightReadingAction = nullptr;
    QAction *fullScreenAction = nullptr;
    QAction *presentationAction = nullptr;

    // Which of the window's furniture is currently on screen, and why.
    ViewChrome::Mode chromeMode = ViewChrome::Mode::Normal;
    // The toolbars are back because the pointer asked for them. Only ever true
    // while chromeMode is FullScreen.
    bool chromeRevealed = false;
    // Started when the pointer leaves the reveal band, so that a toolbar does not
    // vanish under a hand that is still reaching for it.
    QTimer chromeHideTimer;
    // Maximized windows come back maximized and plain ones come back plain, so
    // the mode has to remember which this window was.
    bool chromeWasMaximized = false;
    // Presentation overrides the tab in front, and gives them back on the way out.
    PageLayout::Mode presentationPreviousLayout = PageLayout::Mode::Continuous;

    // The tool the user chose, for the tabs that are opened after this. The tab
    // in front has its own copy -- this is what a new one is started with, so
    // that a mode does not reset itself every time a file is opened.
    PageView::Tool rememberedTool = PageView::Tool::Select;

    // Set while this window is putting its own state into the toolbar, so that
    // the widget's change signals do not come back in as commands.
    bool updatingControls = false;
    // Set while closing a tab on purpose, so that "the last tab is gone" closes
    // the window instead of showing the empty state.
    bool closingLastTabClosesWindow = true;
    // A window born from dragging a tab out. It is allocated on the heap and
    // deletes itself on close, and it has no single-instance gateway (that stays
    // with the first window) -- so it is just another top-level of the process.
    bool detachedWindow = false;
};
