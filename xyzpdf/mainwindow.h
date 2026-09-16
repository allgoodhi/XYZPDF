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

#include <QMainWindow>

#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class QAction;
class QComboBox;
class QLabel;
class QMenu;
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
    DocumentTab *openDocument(const QString &path, int page = -1);
    DocumentTab *currentTab() const;
    void openFileDialog();
    void closeTab(int index);
    void closeCurrentTab();
    // Asks for the password of an encrypted document, and retries.
    bool promptForPassword(DocumentTab *tab, const QString &path);

    void changeLanguage(const QString &code);
    void showSettings();
    void showAbout();
    void showThirdParty();

    // What a drag on a page does, from here on: applied to the tab in front,
    // remembered for the tabs opened after it, and shown in the toolbar.
    void chooseTool(PageView::Tool tool);

    void zoomIn();
    void zoomOut();
    void zoomTo(double zoom);
    void fitWidth();
    void fitWholePage();
    void rotateBy(int degrees);
    void goToPageDialog();
    void stepPage(int delta);
    void jumpTo(int zeroBasedPage);

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

private:
    // What a drag in progress is carrying: the local files among its URLs,
    // decided once when the drag arrives rather than on every move -- the
    // answer costs a stat() per URL, and a drag move arrives continuously.
    // Empty means the drag is not one this window can do anything with.
    QStringList pendingDrop;


    Ui::MainWindow *ui;
    QTabWidget *tabs = nullptr;
    QStackedWidget *stack = nullptr;
    QWidget *emptyState = nullptr;
    QToolBar *toolBar = nullptr;

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
    QAction *copyAction = nullptr;
    QAction *selectAllAction = nullptr;
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
};
