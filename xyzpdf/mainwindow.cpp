#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "documenttab.h"
#include "src/core/aboutinfo.h"
#include "src/core/appsettings.h"
#include "src/core/edition.h"
#include "src/core/languages.h"
#include "src/core/recentdocuments.h"
#include "src/core/windowdialogs.h"
#include "src/render/rendercache.h"
#include "src/settings/settingsdialog.h"
#include "src/theme/thememanager.h"
#include "src/ui/wrappingtoolbararea.h"
#include "src/ui/thirdpartydialog.h"
#include "src/view/pagelayout.h"
#include "src/view/zoomcontroller.h"
#ifdef Q_OS_WIN
#include "src/windows/packageidentity.h"
#include "src/windows/shellintegration.h"
#endif

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

namespace {

// Zoom presets in the toolbar's box. Percentages rather than multipliers
// because that is what every reader shows, and what a person says out loud.
const int kZoomPresets[] = {50, 75, 100, 125, 150, 200, 400};

// The local files a drag is carrying, in the order the drag lists them.
//
// Only regular files: a folder has nothing this program can open, and a URL
// from a browser is not a file at all. Both are left out rather than reported,
// because a drag that is refused before it is released is a clearer answer
// than a message box after it.
QStringList droppedFiles(const QMimeData *mime)
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile())
            files.append(path);
    }
    return files;
}

// One notch of the zoom buttons, as a ratio: the same one the wheel uses, so
// that the two cannot end up disagreeing about what a step is.
double steppedZoom(double from, int steps)
{
    return ZoomController::clamp(from * std::pow(ZoomController::Step, steps));
}

} // namespace

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The tab strip does not get the wheel. See buildCentralWidget().
    if (tabs && watched == tabs->findChild<QTabBar *>() && event->type() == QEvent::Wheel)
        return true;

    // Escape in the toolbar's search box clears the search, which is what
    // Escape means in every other search box: the highlights go, the field
    // empties, and the page is left as it was found.
    if (searchBox && watched == searchBox && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            searchBox->clear();
            if (DocumentTab *tab = currentTab())
                tab->clearSearch();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    pendingDrop.clear();

    // A copy is the only outcome this window will take, and the drag has to
    // offer one for the drop to be accepted at all.
    //
    // This is the safety rule, not a style choice. A drag from Explorer inside
    // one volume *prefers* a move, and Qt will only let a target choose an
    // action out of the set the drag offered -- setDropAction(Copy) on a drag
    // that offered move alone is silently ignored (the guard is in
    // QDropEvent::setDropAction), and the drop would then go through as a move:
    // dropping a document on the reader would be how somebody loses the file
    // they were trying to read. So a move-only drag is refused, and the cursor
    // says so before anything is released. Holding Ctrl in Explorer offers a
    // copy, which is the gesture a user who wants it already knows.
    if (!event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return;
    }

    pendingDrop = droppedFiles(event->mimeData());
    if (pendingDrop.isEmpty()) {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::CopyAction);
    event->accept();

    // What the drop would do, said while the file is still in the air. The
    // cursor already says a drop is allowed; this says what it would open.
    setStatusMessage(pendingDrop.size() == 1
                         ? tr("松开以打开 %1").arg(QFileInfo(pendingDrop.first()).fileName())
                         : tr("松开以打开 %1 个文件").arg(pendingDrop.size()));
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    // The decision was made on entry; a move is the same drag somewhere else.
    // Re-deciding here would mean a stat() per mouse move, and a drag that
    // wandered over a child that did not take drops would flicker between
    // allowed and not allowed.
    if (pendingDrop.isEmpty() || !event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    pendingDrop.clear();
    setStatusMessage(tr("就绪"));
    event->accept();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    // From the drag if there is one, and from the event otherwise: a drop can
    // arrive without an entry this window saw -- a synthetic one, or a drag
    // Qt delivers straight to the drop site -- and opening nothing because the
    // bookkeeping was empty would be the wrong answer to that.
    const QStringList files =
        pendingDrop.isEmpty() ? droppedFiles(event->mimeData()) : pendingDrop;
    pendingDrop.clear();
    setStatusMessage(tr("就绪"));

    // The same two conditions as the entry: something openable, and a copy
    // among the actions offered. A drop is delivered to a widget that accepted
    // its entry, but this window is not the only way one can arrive -- and a
    // handler that trusts its own bookkeeping is one that takes a move when it
    // means to take a copy.
    if (files.isEmpty() || !event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();

    // One tab each, by the same path the Open dialog takes: a file that is
    // already open has its tab raised rather than getting a second one, and a
    // drop onto a window with a document open adds to it instead of replacing
    // what somebody was reading.
    for (const QString &path : files)
        openDocument(path);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("mainWindow"));

    buildCentralWidget();
    buildMenus();
    buildToolBar();
    buildStatusBar();

    // The registry entries carry the path of the executable that wrote them, so
    // moving xyzpdf.exe silently empties out its menu -- and nobody connects the
    // two. The stored preference, not the registry, is what the user asked for,
    // so this also repairs entries another installer removed.
    //
    // The package is the other case: a packaged build carries the same menu
    // itself, and two identically named entries side by side is what a user
    // reports as a bug. The package wins, and the stored preference is left
    // alone so that uninstalling it brings back what was asked for.
    {
        const QString classesRoot = ShellIntegration::defaultClassesRoot();
        if (PackageIdentity::isOurPackageInstalled()) {
            if (ShellIntegration::hasAnyTrace(classesRoot))
                ShellIntegration::unregisterVerbs(classesRoot, nullptr);
        } else if (AppSettings::shellIntegrationEnabled()) {
            if (ShellIntegration::isRegistered(classesRoot)) {
                ShellIntegration::refreshIfStale(QCoreApplication::applicationFilePath(),
                                                 classesRoot);
            } else {
                ShellIntegration::registerVerbs(QCoreApplication::applicationFilePath(),
                                                classesRoot, nullptr);
            }
        } else if (ShellIntegration::hasAnyTrace(classesRoot)) {
            // Switched off, but something of ours is still in the shell. A key
            // with no values in it is not a working registration, so the
            // ordinary check says there is nothing to remove -- and Explorer may
            // still draw it.
            ShellIntegration::unregisterVerbs(classesRoot, nullptr);
        }
    }

    updateForCurrentTab();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildCentralWidget()
{
    // The central widget is the chrome and, under it, the area a document is
    // drawn in. The chrome is a widget in this layout rather than a toolbar in
    // the window's toolbar area, which is a decision about wrapping -- see
    // WrappingToolBarArea and buildToolBar().
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // A stack of two: the tab strip, and what to show when there is no tab.
    //
    // QTabWidget has no empty state of its own -- with no tabs it draws a bare
    // frame -- and the first thing a new user sees is exactly that, so the
    // empty case gets a page of its own.
    stack = new QStackedWidget(central);

    tabs = new QTabWidget(stack);
    tabs->setObjectName(QStringLiteral("documentTabs"));
    tabs->setDocumentMode(true);
    tabs->setMovable(false); // M3: dragging tabs between windows
    tabs->setTabsClosable(true);
    // One document is the common case, and a single tab adds a strip of chrome
    // over the page for nothing.
    tabs->setTabBarAutoHide(true);

    // QTabBar scrolls through its tabs when the wheel turns over it, and that
    // is off here on purpose. The strip sits directly above the page, so a
    // reader whose hand is still on the wheel from scrolling a page changes
    // document the moment the pointer drifts up -- and the page they were
    // reading is not the page they come back to. Everything else in this
    // program scrolls what is under the pointer; the one control that switches
    // documents as a side effect is not a useful exception.
    //
    // QTabWidget::tabBar() is protected, which is why the strip is found as a
    // child rather than asked for.
    if (QTabBar *bar = tabs->findChild<QTabBar *>())
        bar->installEventFilter(this);

    emptyState = new QWidget(stack);
    {
        auto *layout = new QVBoxLayout(emptyState);
        layout->addStretch(1);
        auto *hint = new QLabel(tr("把 PDF 拖到这里，或者"), emptyState);
        hint->setAlignment(Qt::AlignCenter);
        auto *open = new QPushButton(tr("打开 PDF…"), emptyState);
        open->setObjectName(QStringLiteral("emptyOpenButton"));
        open->setMinimumWidth(160);
        auto *row = new QWidget(emptyState);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(hint);
        rowLayout->addWidget(open, 0, Qt::AlignCenter);
        layout->addWidget(row);
        layout->addStretch(2);
        connect(open, &QPushButton::clicked, this, &MainWindow::openFileDialog);
    }

    stack->addWidget(tabs);
    stack->addWidget(emptyState);
    stack->setCurrentWidget(emptyState);

    // The strip goes in front of the stack, and is filled in by buildToolBar() --
    // which runs after this, because a toolbar's actions are made with the menus.
    centralLayout->addWidget(stack, 1);
    setCentralWidget(central);

    // The window is the drop target rather than the page surface, so that a
    // drag anywhere over it works -- over the tab strip, over the page, and
    // over the empty state, which has been telling people to drop a PDF on it
    // since the first build without anything listening. A drop reaches it from
    // any child that does not take drops itself, which is all of them.
    setAcceptDrops(true);

    connect(tabs, &QTabWidget::currentChanged, this, [this](int) { updateForCurrentTab(); });
    connect(tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
}

void MainWindow::buildMenus()
{
    const auto makeAction = [this](const QString &text, const QKeySequence &shortcut) {
        auto *action = new QAction(text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        return action;
    };

    // ---- 文件 ----
    QMenu *fileMenu = menuBar()->addMenu(tr("文件(&F)"));

    openAction = makeAction(tr("打开(&O)…"), QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    fileMenu->addAction(openAction);

    recentMenu = fileMenu->addMenu(tr("最近打开"));
    rebuildRecentMenu();

    // ---- 导出 ----
    //
    // Beside Open, because it is the other thing done *to* a file rather than to
    // a page: the reader has a document and wants it in another program. No
    // shortcut: every combination that means "export" means something else in
    // the editors this is used next to.
    fileMenu->addSeparator();
    exportWordAction = makeAction(tr("导出为 Word(&E)…"), QKeySequence());
    exportWordAction->setObjectName(QStringLiteral("exportWordAction"));
    exportWordAction->setToolTip(tr("把这份 PDF 的文字导出为 .docx 文档"));
    connect(exportWordAction, &QAction::triggered, this, &MainWindow::exportCurrentToWord);
    fileMenu->addAction(exportWordAction);

    printAction = makeAction(tr("打印(&P)…"), QKeySequence(QStringLiteral("Ctrl+P")));
    printAction->setObjectName(QStringLiteral("printAction"));
    printAction->setToolTip(tr("把这份 PDF 打印出来"));
    connect(printAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->print();
    });
    fileMenu->addAction(printAction);

    fileMenu->addSeparator();
    closeTabAction = makeAction(tr("关闭标签(&W)"), QKeySequence::Close);
    connect(closeTabAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);
    fileMenu->addAction(closeTabAction);

    fileMenu->addSeparator();
    QAction *quitAction = makeAction(tr("退出(&X)"), QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(quitAction);

    // ---- 编辑 ----
    //
    // Two entries, and only two, because this is a reader: everything else that
    // would go here -- cut, paste, delete -- is editing, which is M5. Copy is
    // disabled until something is selected, so the menu says what state the
    // selection is in rather than offering an action that would do nothing.
    QMenu *editMenu = menuBar()->addMenu(tr("编辑(&E)"));

    copyAction = makeAction(tr("复制(&C)"), QKeySequence::Copy);
    copyAction->setObjectName(QStringLiteral("copyAction"));
    connect(copyAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->copySelection();
    });
    editMenu->addAction(copyAction);

    translateAction = makeAction(tr("翻译(&T)"), QKeySequence(QStringLiteral("Ctrl+T")));
    translateAction->setObjectName(QStringLiteral("translateAction"));
    connect(translateAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->translateSelection();
    });
    editMenu->addAction(translateAction);

    selectAllAction = makeAction(tr("全选(&A)"), QKeySequence::SelectAll);
    selectAllAction->setObjectName(QStringLiteral("selectAllAction"));
    connect(selectAllAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->selectAll();
    });
    editMenu->addAction(selectAllAction);

    // ---- 查找 ----
    //
    // The bar lives in the tab, because a search belongs to the document it is
    // looking in -- two open documents have two searches, and moving between
    // them should find each one's where it was left.
    editMenu->addSeparator();

    findAction = makeAction(tr("查找(&F)…"), QKeySequence::Find);
    findAction->setObjectName(QStringLiteral("findAction"));
    connect(findAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->showFindBar();
    });
    editMenu->addAction(findAction);

    findNextAction = makeAction(tr("查找下一个"), QKeySequence(QStringLiteral("F3")));
    connect(findNextAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->stepSearch(1);
    });
    editMenu->addAction(findNextAction);

    findPreviousAction = makeAction(tr("查找上一个"), QKeySequence(QStringLiteral("Shift+F3")));
    connect(findPreviousAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->stepSearch(-1);
    });
    editMenu->addAction(findPreviousAction);

    // ---- 查看 ----
    QMenu *viewMenu = menuBar()->addMenu(tr("查看(&V)"));

    // ---- what a drag on a page does ----
    //
    // Three tools in one exclusive group rather than one clever pointer. A drag
    // used to mean whichever of the two things the pointer was pointing at,
    // which worked until both features existed: selecting text on a page of
    // scans was a box, and the only way to ask for the other one was a modifier
    // key nobody could guess. A tool is one more click and no guessing.
    {
        auto *toolGroup = new QActionGroup(this);
        toolGroup->setExclusive(true);

        // Ctrl+Shift+1/2/3, and the Shift is not decoration: Ctrl+1 and Ctrl+2
        // are already "fit width" and "fit page", which is exactly the kind of
        // collision that shows up as one command quietly doing another one's
        // work. A shortcut that steals a key from an existing command is worse
        // than no shortcut at all.
        const struct
        {
            QAction **action;
            const char *name;
            QString text;
            QString shortcut;
            QString hint;
        } tools[] = {
            {&viewToolAction, "viewToolAction", tr("只查看"), QStringLiteral("Ctrl+Shift+1"),
             tr("拖动页面本身，不选择页面上的内容")},
            {&selectToolAction, "selectToolAction", tr("选择"), QStringLiteral("Ctrl+Shift+2"),
             tr("选择文字并复制，指针在文字上变成 I 形")},
            {&recognizeToolAction, "recognizeToolAction", tr("识别"),
             QStringLiteral("Ctrl+Shift+3"), tr("拖出一个方框，识别框内像素里的文字")},
            {&translateToolAction, "translateToolAction", tr("翻译"),
             QStringLiteral("Ctrl+Shift+4"),
             tr("拖选文字即翻译；拖到图片上则框选识别后再翻译")},
        };

        for (const auto &tool : tools) {
            QAction *action = makeAction(tool.text, QKeySequence(tool.shortcut));
            // Named so that a test can find them without depending on a label
            // that translation changes, or on the shortcut, which this very
            // change moved once already.
            action->setObjectName(QString::fromLatin1(tool.name));
            action->setCheckable(true);
            action->setToolTip(tool.hint);
            action->setStatusTip(tool.hint);
            toolGroup->addAction(action);
            viewMenu->addAction(action);
            *tool.action = action;
        }

        connect(viewToolAction, &QAction::triggered, this,
                [this] { chooseTool(PageView::Tool::View); });
        connect(selectToolAction, &QAction::triggered, this,
                [this] { chooseTool(PageView::Tool::Select); });
        connect(recognizeToolAction, &QAction::triggered, this,
                [this] { chooseTool(PageView::Tool::Recognize); });
        connect(translateToolAction, &QAction::triggered, this,
                [this] { chooseTool(PageView::Tool::Translate); });

        viewMenu->addSeparator();
    }

    zoomInAction = makeAction(tr("放大"), QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    viewMenu->addAction(zoomInAction);

    zoomOutAction = makeAction(tr("缩小"), QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    viewMenu->addAction(zoomOutAction);

    actualSizeAction = makeAction(tr("实际大小"), QKeySequence(QStringLiteral("Ctrl+0")));
    connect(actualSizeAction, &QAction::triggered, this, [this] { zoomTo(1.0); });
    viewMenu->addAction(actualSizeAction);

    fitWidthAction = makeAction(tr("适应宽度"), QKeySequence(QStringLiteral("Ctrl+1")));
    connect(fitWidthAction, &QAction::triggered, this, &MainWindow::fitWidth);
    viewMenu->addAction(fitWidthAction);

    fitPageAction = makeAction(tr("适应整页"), QKeySequence(QStringLiteral("Ctrl+2")));
    connect(fitPageAction, &QAction::triggered, this, &MainWindow::fitWholePage);
    viewMenu->addAction(fitPageAction);

    viewMenu->addSeparator();
    rotateRightAction = makeAction(tr("顺时针旋转"), QKeySequence(QStringLiteral("Ctrl+R")));
    connect(rotateRightAction, &QAction::triggered, this, [this] { rotateBy(90); });
    viewMenu->addAction(rotateRightAction);

    rotateLeftAction =
        makeAction(tr("逆时针旋转"), QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(rotateLeftAction, &QAction::triggered, this, [this] { rotateBy(-90); });
    viewMenu->addAction(rotateLeftAction);

    viewMenu->addSeparator();
    continuousAction = makeAction(tr("连续滚动"), QKeySequence());
    continuousAction->setCheckable(true);
    connect(continuousAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->setLayoutMode(PageLayout::Mode::Continuous);
        updateForCurrentTab();
    });
    viewMenu->addAction(continuousAction);

    singlePageAction = makeAction(tr("单页"), QKeySequence());
    singlePageAction->setCheckable(true);
    connect(singlePageAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->setLayoutMode(PageLayout::Mode::SinglePage);
        updateForCurrentTab();
    });
    viewMenu->addAction(singlePageAction);

    // The bookmark sidebar. Checkable and per-tab like the layout modes: the
    // toggle drives whichever document is in front, and a document with no
    // bookmarks has nothing to show, so the item is disabled there.
    outlineSidebarAction = makeAction(tr("书签大纲侧栏"),
                                      QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    outlineSidebarAction->setCheckable(true);
    connect(outlineSidebarAction, &QAction::toggled, this, [this](bool on) {
        if (DocumentTab *tab = currentTab())
            tab->setSidebarVisible(on);
    });
    viewMenu->addAction(outlineSidebarAction);

    // ---- 转到 ----
    QMenu *goMenu = menuBar()->addMenu(tr("转到(&G)"));

    firstPageAction = makeAction(tr("第一页"), QKeySequence(QStringLiteral("Ctrl+Home")));
    connect(firstPageAction, &QAction::triggered, this, [this] { jumpTo(0); });
    goMenu->addAction(firstPageAction);

    previousPageAction = makeAction(tr("上一页"), QKeySequence(QStringLiteral("PgUp")));
    connect(previousPageAction, &QAction::triggered, this, [this] { stepPage(-1); });
    goMenu->addAction(previousPageAction);

    nextPageAction = makeAction(tr("下一页"), QKeySequence(QStringLiteral("PgDown")));
    connect(nextPageAction, &QAction::triggered, this, [this] { stepPage(1); });
    goMenu->addAction(nextPageAction);

    lastPageAction = makeAction(tr("最后一页"), QKeySequence(QStringLiteral("Ctrl+End")));
    connect(lastPageAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            jumpTo(tab->pageCount() - 1);
    });
    goMenu->addAction(lastPageAction);

    goMenu->addSeparator();
    goToPageAction = makeAction(tr("跳转到页码…"), QKeySequence(QStringLiteral("Ctrl+G")));
    connect(goToPageAction, &QAction::triggered, this, &MainWindow::goToPageDialog);
    goMenu->addAction(goToPageAction);

    // ---- 语言 ----
    //
    // "Language", deliberately not tr(): whoever needs this entry is looking at
    // an interface they cannot read, and a translated label is no use to them.
    // The entries inside are each written in their own language for the same
    // reason.
    languageMenu = menuBar()->addMenu(QStringLiteral("Language"));
    {
        const QString current = AppSettings::languageCode();
        auto *group = new QActionGroup(this);
        group->setExclusive(true);

        auto *system = languageMenu->addAction(tr("跟随系统"));
        system->setCheckable(true);
        system->setChecked(current == Languages::systemCode());
        group->addAction(system);
        connect(system, &QAction::triggered, this,
                [this] { changeLanguage(Languages::systemCode()); });

        languageMenu->addSeparator();
        for (const Languages::Entry &entry : Languages::available()) {
            auto *action = languageMenu->addAction(entry.nativeName);
            action->setCheckable(true);
            action->setChecked(current == entry.code);
            group->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, code = entry.code] { changeLanguage(code); });
        }
    }

    // ---- 帮助 ----
    QMenu *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));

    QAction *settingsAction = makeAction(tr("设置(&P)…"), QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    helpMenu->addAction(settingsAction);

    helpMenu->addSeparator();
    QAction *thirdPartyAction = makeAction(tr("第三方开源许可"), QKeySequence());
    connect(thirdPartyAction, &QAction::triggered, this, &MainWindow::showThirdParty);
    helpMenu->addAction(thirdPartyAction);

    QAction *aboutAction = makeAction(tr("关于 %1").arg(Edition::productName()), QKeySequence());
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    helpMenu->addAction(aboutAction);
}

void MainWindow::buildToolBar()
{
    // ---- one row, a "…", and an expansion that stays ----
    //
    // This was decided the hard way, and the reasoning lives with the class that
    // implements it; WrappingToolBarArea's header has the long version. The
    // short one: the window's own toolbar area does put a "»" at the end of a
    // toolbar that does not fit, and clicking it does show the rest -- but only
    // until the pointer leaves or anything re-lays out, which typing in a field
    // on the toolbar is enough to cause. A reader who opened it to reach the
    // search box lost it while reaching. So the strip holds that state itself,
    // and only a click changes it.
    toolBarArea = new WrappingToolBarArea(centralWidget());
    // Above the tab strip: the same place the toolbar area used to put it, and
    // the tab strip is inside the stack this is inserted in front of.
    if (auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout()))
        layout->insertWidget(0, toolBarArea);

    auto addGroup = [this](const QString &objectName, const QString &title) {
        return toolBarArea->addGroup(objectName, title);
    };

    // The three tools first, and that is where they belong: everything else acts
    // on a page, and these decide what happens when the page is touched. A
    // reader reaches for the hand before the page number -- so the tool group
    // and Open, which together are what a reader needs first, are one toolbar.
    toolBar = addGroup(QStringLiteral("mainToolBar"), tr("主工具栏"));
    toolBar->addAction(viewToolAction);
    toolBar->addAction(selectToolAction);
    toolBar->addAction(recognizeToolAction);
    toolBar->addAction(translateToolAction);
    toolBar->addSeparator();
    toolBar->addAction(openAction);

    QToolBar *pageBar = addGroup(QStringLiteral("pageToolBar"), tr("翻页"));
    pageBar->addAction(firstPageAction);
    pageBar->addAction(previousPageAction);

    pageBox = new QComboBox(pageBar);
    pageBox->setObjectName(QStringLiteral("pageBox"));
    pageBox->setEditable(true);
    pageBox->setInsertPolicy(QComboBox::NoInsert);
    pageBox->setMinimumWidth(96);
    pageBox->setToolTip(tr("页码"));
    // Enter in the editable box is the whole reason it is editable: typing a
    // page number and pressing Enter is faster than the picker for anything
    // past the first screenful.
    connect(pageBox->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        bool understood = false;
        const int number = pageBox->currentText().trimmed().toInt(&understood);
        if (understood && number >= 1)
            jumpTo(number - 1);
    });
    connect(pageBox, &QComboBox::activated, this, [this](int index) {
        if (!updatingControls && index >= 0)
            jumpTo(index);
    });
    pageBar->addWidget(pageBox);
    pageBar->addAction(nextPageAction);
    pageBar->addAction(lastPageAction);

    // Zoom and rotation travel together: they are the four ways to answer "I
    // cannot quite read this", and the two are asked in the same breath.
    QToolBar *viewBar = addGroup(QStringLiteral("viewToolBar"), tr("缩放与旋转"));
    viewBar->addAction(zoomOutAction);

    zoomBox = new QComboBox(viewBar);
    zoomBox->setObjectName(QStringLiteral("zoomBox"));
    zoomBox->setEditable(true);
    zoomBox->setInsertPolicy(QComboBox::NoInsert);
    zoomBox->setMinimumWidth(104);
    zoomBox->setToolTip(tr("缩放"));
    for (const int percent : kZoomPresets)
        zoomBox->addItem(tr("%1%").arg(percent), QVariant(percent));
    // The two fit modes are in the box as well as in the menu: the question
    // "why is this page not filling the window" is asked with the hand already
    // on this control.
    zoomBox->addItem(tr("适应宽度"), QVariant(QStringLiteral("fit-width")));
    zoomBox->addItem(tr("适应整页"), QVariant(QStringLiteral("fit-page")));
    connect(zoomBox->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        QString text = zoomBox->currentText().trimmed();
        text.remove(QLatin1Char('%'));
        bool understood = false;
        const double percent = text.toDouble(&understood);
        if (understood && percent > 0)
            zoomTo(percent / 100.0);
        else
            updateForCurrentTab();
    });
    connect(zoomBox, &QComboBox::activated, this, [this](int index) {
        if (updatingControls || index < 0)
            return;
        const QVariant value = zoomBox->itemData(index);
        if (value.userType() == QMetaType::QString) {
            const QString mode = value.toString();
            if (mode == QLatin1String("fit-width"))
                fitWidth();
            else
                fitWholePage();
            return;
        }
        zoomTo(value.toDouble() / 100.0);
    });
    viewBar->addWidget(zoomBox);

    viewBar->addAction(zoomInAction);
    viewBar->addAction(fitWidthAction);
    viewBar->addSeparator();
    viewBar->addAction(rotateLeftAction);
    viewBar->addAction(rotateRightAction);

    // ---- 查找 ----
    //
    // Last, where a search box is looked for, and a toolbar of its own so that
    // it is a group the area can move to a row of its own when the window is
    // narrow -- the one thing in this chrome that must never be out of reach,
    // because it is also the thing whose own use can trigger a re-layout.
    //
    // Beside it, the count that answers the question a search raises second --
    // "where am I in this?" -- as soon as it is answered first. The bar inside
    // the tab has the same two things; whichever one is typed into, both show
    // the same needle and the same count, because they are two views of one
    // search.
    QToolBar *searchBar = addGroup(QStringLiteral("searchToolBar"), tr("查找"));

    searchBox = new QLineEdit(searchBar);
    searchBox->setObjectName(QStringLiteral("searchBox"));
    searchBox->setPlaceholderText(tr("在文档中查找"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setToolTip(tr("输入即搜索；Enter 到下一个，Shift+Enter 到上一个，Esc 清除"));
    // Fixed, not merely bounded: the clear button appears and disappears with
    // the text, and a QLineEdit that reports a different size hint when it has
    // text is a widget whose typing changes the width its toolbar needs. That is
    // the shape of the bug this whole function is arranged around -- so the box
    // asks for exactly one width and keeps it.
    searchBox->setFixedWidth(220);
    searchBox->installEventFilter(this);
    connect(searchBox, &QLineEdit::textChanged, this, [this](const QString &needle) {
        // Ignored while the box is being filled in from the tab in front: that
        // is the window showing what is being searched, not a request to search
        // for it again.
        if (updatingControls)
            return;
        if (DocumentTab *tab = currentTab())
            tab->startSearch(needle, tab->searchCaseSensitive());
    });
    connect(searchBox, &QLineEdit::returnPressed, this, [this] {
        // Shift is read from the keyboard rather than from an event: the field
        // consumes the key, so no event arrives anywhere that could say.
        const bool backwards = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        if (DocumentTab *tab = currentTab())
            tab->stepSearch(backwards ? -1 : 1);
    });
    searchBar->addWidget(searchBox);

    searchCount = new QLabel(searchBar);
    searchCount->setObjectName(QStringLiteral("searchCount"));
    searchCount->setToolTip(tr("当前是第几个匹配，一共几个"));
    // Wide enough for the longest thing it ever says, asked of the font rather
    // than written down as a number of pixels: "第 1234 / 1234 个" is the
    // longest, and measuring it is what makes this right at every scale factor
    // and in every language the program is translated into. A minimum and not a
    // fixed width, so that a longer count is never clipped -- it cannot happen,
    // but a label that silently truncates its own number is a bad way to find
    // out that the longest string was wrong.
    {
        const QFontMetrics metrics(searchCount->font());
        searchCount->setMinimumWidth(
            metrics.horizontalAdvance(tr("第 1234 / 1234 个")) + 8);
    }
    searchBar->addWidget(searchCount);

    // ---- and never narrower than one group needs ----
    //
    // The "…" promises that what is behind it can be shown. That promise holds
    // as long as a single group fits on a row of its own, and the window is what
    // decides that -- so the window refuses to be narrower than the widest one.
    // Measured here rather than written down as a number: it depends on the font,
    // the theme's padding and the language of the labels, none of which this
    // function can know in advance and all of which it has just built.
    setMinimumWidth(qMax(minimumWidth(), toolBarArea->minimumSizeHint().width() + 24));
}

void MainWindow::updateSearchCount()
{
    if (!searchCount)
        return;

    DocumentTab *tab = currentTab();
    if (!tab || !tab->isOpen() || tab->searchText().isEmpty()) {
        searchCount->clear();
        return;
    }
    if (tab->isSearching()) {
        searchCount->setText(tr("搜索中…"));
        return;
    }
    const int total = tab->searchMatchCount();
    if (total <= 0) {
        searchCount->setText(tr("没有找到"));
        return;
    }
    // One-based, because that is how a reader counts, and "第 3 / 12 个" is the
    // answer to the only question the count is asked.
    searchCount->setText(tr("第 %1 / %2 个").arg(tab->currentSearchMatch() + 1).arg(total));
}

void MainWindow::buildStatusBar()
{
    statusPage = new QLabel(this);
    statusPage->setObjectName(QStringLiteral("statusPage"));
    statusPage->setMinimumWidth(90);

    statusZoom = new QLabel(this);
    statusZoom->setObjectName(QStringLiteral("statusZoom"));
    statusZoom->setMinimumWidth(64);

    // Permanent widgets sit on the right, where they do not move when a message
    // comes and goes.
    statusBar()->addPermanentWidget(statusPage);
    statusBar()->addPermanentWidget(statusZoom);
    statusBar()->showMessage(tr("就绪"));
}

DocumentTab *MainWindow::currentTab() const
{
    return qobject_cast<DocumentTab *>(tabs->currentWidget());
}

DocumentTab *MainWindow::openDocument(const QString &path, int page)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        // Checked here rather than left to the backend: the difference between
        // "not there" and "not a PDF" is the difference between "the file has
        // been moved" and "this program cannot open this", and only one of
        // those is worth looking at the file for.
        WindowDialogs::warning(this, Edition::productName(),
                               tr("找不到文件：%1").arg(QDir::toNativeSeparators(path)));
        // A recent entry that has since gone is dropped here, which is the only
        // place that knows the user asked for it.
        RecentDocuments::forget(path);
        rebuildRecentMenu();
        return nullptr;
    }

    // A document that is already open gets its tab raised rather than a second
    // one: two tabs on one file is two views of the same pages, two caches, and
    // nothing to be gained.
    const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                                                : info.canonicalFilePath();
    for (int index = 0; index < tabs->count(); ++index) {
        auto *existing = qobject_cast<DocumentTab *>(tabs->widget(index));
        if (!existing || !existing->isOpen())
            continue;
        const QFileInfo open(existing->documentPath());
        const QString openCanonical =
            open.canonicalFilePath().isEmpty() ? open.absoluteFilePath() : open.canonicalFilePath();
        if (openCanonical.compare(canonical, Qt::CaseInsensitive) == 0) {
            tabs->setCurrentIndex(index);
            if (page >= 0)
                existing->setCurrentPage(page);
            return existing;
        }
    }

    auto *tab = new DocumentTab(this);
    // The tool the user is on, rather than the tab's own default: a mode that
    // resets itself every time a document is opened is a mode people stop
    // using.
    tab->setTool(rememberedTool);
    const int index = tabs->addTab(tab, QFileInfo(path).fileName());
    tabs->setCurrentIndex(index);

    // A tab that changes the tool itself -- a shortcut, or a future "recognize
    // this picture" on a context menu -- brings the toolbar with it.
    connect(tab, &DocumentTab::toolChanged, this, [this, tab](PageView::Tool) {
        if (tab == currentTab())
            updateForCurrentTab();
    });

    // And so does a search, including one typed into the tab's own find bar:
    // the toolbar box shows the needle and the count of the document in front,
    // whichever end of the search it was started from.
    // The export's answer, which arrives from a worker thread when it arrives:
    // written to a file the reader asked for, so it is reported rather than
    // silently done.
    // How far along it is, in two places: a line in the status bar, and -- since
    // a page with a picture on it is a render, so a scanned document is minutes
    // of work -- a progress box with a way to stop it. The status bar alone was
    // what there was, and a reader who has been told only "正在导出" cannot tell a
    // long job from a program that has stopped, nor find the bottom-left corner
    // of a window when the thing they are watching is in front of them.
    connect(tab, &DocumentTab::wordExportProgress, this, [this, tab](int done, int total) {
        // A report from an export that is no longer running is stale, and it
        // arrives that way often enough to matter: the progress of a page is
        // posted to this thread while the answer is delivered by the future, and
        // nothing orders the two. The last page's report can therefore land after
        // "已导出"/"导出已取消" and overwrite it with a line saying the export is
        // still going -- which, once, was what the status bar said about a
        // conversion that had already finished.
        if (!tab->isExporting())
            return;
        setStatusMessage(tr("正在导出为 Word…（%1 / %2 页）").arg(done).arg(total));
        showExportProgress(tab, done, total);
    });
    connect(tab, &DocumentTab::wordExportFinished, this,
            [this](const WordExportReport &report) {
                closeExportProgress();
                reportWordExport(report);
            });

    connect(tab, &DocumentTab::searchChanged, this, [this, tab](int, int, bool) {
        if (tab != currentTab())
            return;
        if (searchBox && searchBox->text() != tab->searchText()) {
            // Blocked: the box is being told, not typed into -- see the
            // textChanged connection in buildToolBar().
            updatingControls = true;
            searchBox->setText(tab->searchText());
            updatingControls = false;
        }
        updateSearchCount();
    });

    const auto applyTitle = [this, tab](const QString &title) {
        const int at = tabs->indexOf(tab);
        if (at >= 0 && !title.isEmpty())
            tabs->setTabText(at, title);
    };
    connect(tab, &DocumentTab::titleChanged, this, applyTitle);
    connect(tab, &DocumentTab::pageChanged, this, [this, tab](int current, int count) {
        Q_UNUSED(current)
        Q_UNUSED(count)
        if (tab == currentTab())
            updateForCurrentTab();
    });
    connect(tab, &DocumentTab::zoomChanged, this, [this, tab](double) {
        if (tab == currentTab())
            updateForCurrentTab();
    });
    connect(tab, &DocumentTab::rotationChanged, this, [this, tab](int) {
        if (tab == currentTab())
            updateForCurrentTab();
    });
    // Selecting text changes what Copy can do, so the menu has to be told. Only
    // the current tab's selection is the one the menu acts on.
    connect(tab, &DocumentTab::selectionChanged, this, [this, tab] {
        if (tab == currentTab())
            updateForCurrentTab();
    });

    const Document::Status status = tab->open(path);
    if (status == Document::Status::Ready) {
        RecentDocuments::remember(path);
        AppSettings::setLastOpenDirectory(info.absolutePath());
        rebuildRecentMenu();
        if (page >= 0)
            tab->setCurrentPage(page);
        statusBar()->showMessage(tr("已打开 %1").arg(info.fileName()), 4000);
        updateForCurrentTab();
        // A page number on the command line is a jump, so it happens here
        // rather than in a queue: the document is loaded and measured by now.
        if (page > 0)
            jumpTo(page);
        return tab;
    }

    if (status == Document::Status::NeedsPassword) {
        if (promptForPassword(tab, path))
            return tab;
    } else {
        WindowDialogs::warning(this, Edition::productName(),
                               tr("无法打开 %1。\n\n%2")
                                   .arg(info.fileName(), tab->lastError()));
    }

    // The tab is taken back out rather than left empty under a name that is not
    // being shown: a failed open should leave no trace of itself.
    tabs->removeTab(tabs->indexOf(tab));
    tab->deleteLater();
    updateForCurrentTab();
    return nullptr;
}

bool MainWindow::promptForPassword(DocumentTab *tab, const QString &path)
{
    // Three attempts, then it is left alone. Not an endless loop: somebody
    // typing a password they do not have is a person who wants the dialog to go
    // away, and a fourth prompt is not going to help them.
    constexpr int kAttempts = 3;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        bool accepted = false;
        const QString password =
            WindowDialogs::getText(this, tr("需要密码"),
                                   tr("%1 已加密，请输入打开密码：").arg(QFileInfo(path).fileName()),
                                   QLineEdit::Password, QString(), &accepted);
        if (!accepted)
            return false;
        if (tab->open(path, password) == Document::Status::Ready) {
            RecentDocuments::remember(path);
            rebuildRecentMenu();
            updateForCurrentTab();
            return true;
        }
    }
    WindowDialogs::warning(this, Edition::productName(), tr("密码不正确，已放弃打开。"));
    return false;
}

void MainWindow::openFileDialog()
{
    const QString start = AppSettings::lastOpenDirectory().isEmpty()
                              ? QDir::homePath()
                              : AppSettings::lastOpenDirectory();
    const QStringList chosen = WindowDialogs::getOpenFileNames(
        this, tr("打开 PDF"), start, tr("PDF 文档 (*.pdf);;所有文件 (*)"));
    for (const QString &path : chosen)
        openDocument(path);
}

void MainWindow::exportCurrentToWord()
{
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isOpen() || tab->isExporting())
        return;

    // The PDF's own name with the extension swapped, in the folder it came from:
    // what makes a folder of exports recognisable next to the files they were
    // made from.
    const QFileInfo info(tab->documentPath());
    QString suggested = info.completeBaseName() + QStringLiteral(".docx");
    if (!info.absolutePath().isEmpty())
        suggested = info.absolutePath() + QLatin1Char('/') + suggested;

    const QString path = WindowDialogs::getSaveFileName(this, tr("导出为 Word"), suggested,
                                                       tr("Word 文档 (*.docx)"));
    if (path.isEmpty())
        return;

    // Said now rather than when it finishes: the conversion reads every page, and
    // a reader who has just clicked something deserves to know it was heard.
    setStatusMessage(tr("正在导出为 Word…"));
    tab->exportToWord(path);
}

void MainWindow::showExportProgress(DocumentTab *tab, int done, int total)
{
    if (!tab)
        return;

    if (!exportProgress) {
        // The box is made when the first page is done rather than when the menu
        // item is clicked, so that it is a box about work that is happening: a
        // document that turns out to have nothing in it, or a file that cannot
        // be written, never puts one on screen at all.
        auto *dialog = new QProgressDialog(this);
        dialog->setObjectName(QStringLiteral("exportProgress"));
        dialog->setWindowTitle(tr("导出为 Word — %1").arg(QFileInfo(tab->documentPath()).fileName()));
        dialog->setCancelButtonText(tr("取消"));
        dialog->setAutoClose(false);
        // A quarter of a second, and not Qt's own four: the document this was
        // reported from takes nine seconds a page, and a box that appears when
        // the job is nearly over is a box nobody can use. Not zero either, so
        // that a conversion that is over in a moment -- an export of pictures is
        // one now, at two hundred milliseconds for six pages -- does not flash a
        // dialog on screen and take it away again. Qt shows it when the timer
        // runs out; nothing here calls show().
        dialog->setMinimumDuration(250);
        WindowDialogs::makeWindowModal(dialog, this);

        // ---- the way out ----
        //
        // Cancelling is a request, not a fact: the export looks at its flag
        // between pages, and a page of a scan is a render that takes a while. So
        // the box stays, saying that it is stopping, until the export itself says
        // it has stopped -- which is the only moment at which the file is known
        // not to have been written. Taking the box away on the click would be
        // telling the reader it was finished.
        //
        // Deferred, because the click does two things: it emits this signal and
        // it puts the box away, zeroing the bar on the way out (see
        // QProgressDialog::cancel()). Anything said here is said to a widget that
        // is about to be hidden, so it is said after that has happened; and the
        // button is disabled rather than replaced, because it is in the middle of
        // emitting the very signal this is running from.
        connect(dialog, &QProgressDialog::canceled, this, [this, dialog, tab] {
            if (tab != exportTab)
                return;
            tab->cancelWordExport();
            const int stoppedAt = exportDone;
            QTimer::singleShot(0, dialog, [this, dialog, stoppedAt] {
                if (exportProgress != dialog)
                    return; // it has been taken down in the meantime
                dialog->setLabelText(tr("正在停止…"));
                dialog->setValue(stoppedAt);
                if (auto *button = dialog->findChild<QPushButton *>())
                    button->setEnabled(false);
                dialog->show();
            });
        });

        // A tab can be closed while it is exporting -- a reader shutting the
        // window rather than waiting -- and the export is then cancelled by the
        // tab's own destructor. Nothing will arrive to take this box down, so it
        // goes with the tab it belongs to.
        connect(tab, &QObject::destroyed, this, [this, tab] {
            if (tab == exportTab)
                closeExportProgress();
        });

        exportProgress = dialog;
        exportTab = tab;
        // Left to show itself: see setMinimumDuration above.
        dialog->setMaximum(total);
        dialog->setValue(0);
    }

    exportDone = done;
    if (exportProgress->wasCanceled())
        return; // stopping: further pages are not news

    exportProgress->setMaximum(total);
    exportProgress->setValue(done);
    // Zero is the report that the export has started rather than finished: "第 0
    // 页" is not a page, so it says the size of the job instead.
    exportProgress->setLabelText(done > 0
                                     ? tr("正在导出为 Word…（%1 / %2 页）").arg(done).arg(total)
                                     : tr("正在导出为 Word…（共 %1 页）").arg(total));
}

void MainWindow::closeExportProgress()
{
    if (!exportProgress)
        return;

    QProgressDialog *dialog = exportProgress;
    exportProgress = nullptr;
    exportDone = 0;
    // Cleared before the close, and that is not tidiness: closing a progress
    // dialog emits canceled(), and this one is not being cancelled -- it is
    // finished. With the tab forgotten first, that signal finds nothing to stop.
    exportTab = nullptr;
    dialog->close();
    dialog->deleteLater();
}

void MainWindow::reportWordExport(const WordExportReport &report)
{
    // Nothing to say about an export that was stopped because the document was
    // replaced: whoever replaced it is looking at the new one, and a modal box
    // about the old one would be an interruption about something they have
    // already moved on from. The status bar keeps it on the record.
    if (report.cancelled) {
        setStatusMessage(tr("导出已取消"), 5000);
        return;
    }

    if (!report.ok) {
        setStatusMessage(tr("导出失败"), 5000);
        WindowDialogs::warning(this, tr("导出为 Word"), report.error);
        return;
    }

    QString message = tr("已导出 %1 页、%2 个段落到：\n%3")
                          .arg(report.pages)
                          .arg(report.paragraphs)
                          .arg(QDir::toNativeSeparators(report.path));

    QString detail;
    if (report.tables > 0) {
        detail = tr("其中 %1 个带网格的表格已转换为可编辑 Word 表格。")
                     .arg(report.tables);
    }
    if (report.figures > 0) {
        // The figures of a page that also had words on it: cut out of the page and
        // put in where they sit, with the words beside them as words.
        if (!detail.isEmpty())
            detail += QLatin1Char('\n');
        detail += tr("其中 %1 处图形（图表、框图、照片等）已按图片插入，"
                     "其所在页面的文字仍是可以编辑的。")
                      .arg(report.figures);
    }
    if (report.imagePages > 0) {
        // Said out loud because it changes what the file is: those pages are
        // pictures, and a reader who tries to edit the text on one of them needs
        // to know that is what happened rather than wondering why the cursor
        // will not go in.
        if (!detail.isEmpty())
            detail += QLatin1Char('\n');
        detail += tr("其中 %1 页没有文字层或包含复杂图表/表格，已按整页图片还原以保持原貌，"
                     "但页内的文字不能直接编辑。需要可编辑的文字可以先用“识别”工具。")
                      .arg(report.imagePages);
    }
    if (report.emptyPages > 0) {
        // Counted and named rather than left out: a reader who exported a
        // fourteen-page document and got twelve has to be told about the two,
        // and why.
        if (!detail.isEmpty())
            detail += QLatin1Char('\n');
        detail += tr("另有 %1 页既没有文字，也没有渲染出内容，已跳过。").arg(report.emptyPages);
    }
    if (!detail.isEmpty())
        message += QStringLiteral("\n\n") + detail;
    setStatusMessage(tr("已导出为 Word"), 5000);
    WindowDialogs::information(this, tr("导出为 Word"), message);
}

void MainWindow::closeTab(int index)
{
    auto *tab = qobject_cast<DocumentTab *>(tabs->widget(index));
    if (!tab)
        return;

    tabs->removeTab(index);
    tab->deleteLater();

    if (tabs->count() == 0 && closingLastTabClosesWindow) {
        // A reader with no document and nothing else open is a window that does
        // nothing but take up space, and Qt would keep the process alive for it.
        // M3 adds more than one window; until then closing the last tab is
        // closing the program.
        close();
        return;
    }
    updateForCurrentTab();
}

void MainWindow::closeCurrentTab()
{
    if (tabs->currentIndex() >= 0)
        closeTab(tabs->currentIndex());
}

void MainWindow::chooseTool(PageView::Tool tool)
{
    rememberedTool = tool;
    if (DocumentTab *tab = currentTab())
        tab->setTool(tool);
    updateForCurrentTab();
}

void MainWindow::updateForCurrentTab()
{
    DocumentTab *tab = currentTab();
    stack->setCurrentWidget(tab ? static_cast<QWidget *>(tabs) : emptyState);

    const bool hasDocument = tab && tab->isOpen();
    for (QAction *action : {closeTabAction, firstPageAction, previousPageAction, nextPageAction,
                            lastPageAction, goToPageAction, zoomInAction, zoomOutAction,
                            fitWidthAction, fitPageAction, actualSizeAction, rotateLeftAction,
                            rotateRightAction, continuousAction, singlePageAction,
                            selectAllAction, findAction, findNextAction, findPreviousAction,
                            exportWordAction, printAction, viewToolAction, selectToolAction,
                            recognizeToolAction}) {
        if (action)
            action->setEnabled(hasDocument);
    }

    // The tool buttons show the tool the tab in front is actually using, which
    // is not necessarily the remembered one: the tab may have been given a
    // different one before this window existed, and each tab keeps its own.
    {
        const PageView::Tool current = tab ? tab->tool() : rememberedTool;
        if (viewToolAction)
            viewToolAction->setChecked(current == PageView::Tool::View);
        if (selectToolAction)
            selectToolAction->setChecked(current == PageView::Tool::Select);
        if (recognizeToolAction)
            recognizeToolAction->setChecked(current == PageView::Tool::Recognize);
        if (translateToolAction)
            translateToolAction->setChecked(current == PageView::Tool::Translate);
    }
    // Copy is the one that needs more than an open document: with nothing
    // selected it would clear the clipboard, which is worse than doing nothing.
    if (copyAction)
        copyAction->setEnabled(hasDocument && tab->hasSelection());
    // Translate needs a selection for the same reason -- there is nothing to
    // send otherwise -- and shares Copy's enable signal, so both light up when
    // the reader drags over some words.
    if (translateAction)
        translateAction->setEnabled(hasDocument && tab->hasSelection());
    pageBox->setEnabled(hasDocument);
    zoomBox->setEnabled(hasDocument);

    // The search box shows the search of the document in front, which is a
    // property of that tab: a reader moving between two documents finds each
    // one's own search where they left it.
    if (searchBox) {
        const QString needle = tab ? tab->searchText() : QString();
        if (searchBox->text() != needle) {
            updatingControls = true;
            searchBox->setText(needle);
            updatingControls = false;
        }
        searchBox->setEnabled(hasDocument);
    }
    updateSearchCount();

    updatingControls = true;
    if (hasDocument) {
        const int count = tab->pageCount();
        if (pageBox->count() != count) {
            pageBox->clear();
            for (int page = 0; page < count; ++page)
                pageBox->addItem(QString::number(page + 1));
        }
        pageBox->setCurrentIndex(tab->currentPage());

        const int percent = qRound(tab->zoom() * 100.0);
        zoomBox->setCurrentText(tr("%1%").arg(percent));

        continuousAction->setChecked(tab->layoutMode() == PageLayout::Mode::Continuous);
        singlePageAction->setChecked(tab->layoutMode() == PageLayout::Mode::SinglePage);

        // The sidebar toggle reflects the tab in front. Blocked so that reading
        // its state does not fire the toggled handler and write it back; and it
        // is only offered on a document that actually has bookmarks.
        {
            const QSignalBlocker blocker(outlineSidebarAction);
            outlineSidebarAction->setChecked(tab->sidebarVisible());
        }
        outlineSidebarAction->setEnabled(tab->hasOutline());

        statusPage->setText(tr("%1 / %2").arg(tab->currentPage() + 1).arg(count));
        statusZoom->setText(tr("%1%").arg(percent));
    } else {
        pageBox->clear();
        zoomBox->setCurrentText(QString());
        statusPage->setText(QString());
        statusZoom->setText(QString());
        outlineSidebarAction->setEnabled(false);
    }
    updatingControls = false;

    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    DocumentTab *tab = currentTab();
    const QString product = Edition::productName();
    if (tab && tab->isOpen())
        setWindowTitle(tr("%1 — %2").arg(tab->title(), product));
    else
        setWindowTitle(product);
}

void MainWindow::rebuildRecentMenu()
{
    if (!recentMenu)
        return;
    recentMenu->clear();

    const QStringList recent = RecentDocuments::all();
    for (const QString &path : recent) {
        QAction *action = recentMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { openDocument(path); });
    }

    if (recent.isEmpty()) {
        QAction *none = recentMenu->addAction(tr("（没有）"));
        none->setEnabled(false);
        return;
    }

    recentMenu->addSeparator();
    QAction *clear = recentMenu->addAction(tr("清除列表"));
    connect(clear, &QAction::triggered, this, [this] {
        RecentDocuments::clear();
        rebuildRecentMenu();
    });
}

void MainWindow::changeLanguage(const QString &code)
{
    if (AppSettings::languageCode() == code)
        return;
    AppSettings::setLanguageCode(code);
    // Said rather than applied: retranslating a running window means rebuilding
    // every string already built, and the ones living in model data would not
    // be. A restart is honest and cannot half-work.
    WindowDialogs::information(this, QStringLiteral("Language"),
                               tr("语言更改将在下次启动时生效。"));
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this] {
        // The cache limit belongs to each tab, and there is no signal that
        // reaches them -- so the window applies it to the ones that exist.
        const qint64 limit = AppSettings::renderCacheBytes(RenderCache::DefaultMaximumBytes);
        for (int index = 0; index < tabs->count(); ++index) {
            if (auto *tab = qobject_cast<DocumentTab *>(tabs->widget(index)))
                tab->setCacheLimit(limit);
        }
        rebuildRecentMenu();
    });
    dialog.exec();
}

void MainWindow::showAbout()
{
    WindowDialogs::information(this, AboutInfo::title(), AboutInfo::html());
}

void MainWindow::showThirdParty()
{
    ThirdPartyDialog dialog(this);
    dialog.exec();
}

void MainWindow::zoomIn()
{
    if (DocumentTab *tab = currentTab())
        zoomTo(steppedZoom(tab->zoom(), 1));
}

void MainWindow::zoomOut()
{
    if (DocumentTab *tab = currentTab())
        zoomTo(steppedZoom(tab->zoom(), -1));
}

void MainWindow::zoomTo(double zoom)
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    tab->setZoom(zoom);
    updateForCurrentTab();
}

void MainWindow::fitWidth()
{
    if (DocumentTab *tab = currentTab()) {
        tab->setZoomMode(ZoomController::Mode::FitWidth);
        updateForCurrentTab();
    }
}

void MainWindow::fitWholePage()
{
    if (DocumentTab *tab = currentTab()) {
        tab->setZoomMode(ZoomController::Mode::FitPage);
        updateForCurrentTab();
    }
}

void MainWindow::rotateBy(int degrees)
{
    if (DocumentTab *tab = currentTab()) {
        tab->rotateBy(degrees);
        updateForCurrentTab();
    }
}

void MainWindow::stepPage(int delta)
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    jumpTo(tab->currentPage() + delta);
}

void MainWindow::jumpTo(int zeroBasedPage)
{
    DocumentTab *tab = currentTab();
    if (!tab || tab->pageCount() == 0)
        return;
    const int clamped = qBound(0, zeroBasedPage, tab->pageCount() - 1);
    tab->setCurrentPage(clamped);
    updateForCurrentTab();
}

void MainWindow::goToPageDialog()
{
    DocumentTab *tab = currentTab();
    if (!tab || tab->pageCount() == 0)
        return;
    bool accepted = false;
    const QString text = WindowDialogs::getText(
        this, tr("跳转到页码"),
        tr("页码（1 - %1）：").arg(tab->pageCount()), QLineEdit::Normal,
        QString::number(tab->currentPage() + 1), &accepted);
    if (!accepted)
        return;
    bool understood = false;
    const int number = text.trimmed().toInt(&understood);
    if (!understood || number < 1 || number > tab->pageCount()) {
        WindowDialogs::warning(this, tr("跳转到页码"),
                               tr("页码要在 1 到 %1 之间。").arg(tab->pageCount()));
        return;
    }
    jumpTo(number - 1);
}

void MainWindow::setStatusMessage(const QString &message, int timeoutMilliseconds)
{
    statusBar()->showMessage(message, timeoutMilliseconds);
}

bool MainWindow::applyStartupCommandLine(const CommandLine::Request &request)
{
    if (!request.valid) {
        // Nothing is shown but the complaint. A window with nothing in it,
        // opened so that a message box could sit on top of it, is not what
        // somebody who mistyped a switch wanted.
        WindowDialogs::warning(nullptr, Edition::productName(), request.errorMessage);
        return false;
    }

    if (request.action == CommandLine::Action::Print) {
        // The switch is parsed and the registry entry is written from the first
        // milestone -- see command_line.h -- and the printing itself is M3.
        // Saying so is better than a switch that silently does nothing, which
        // is what a user would report as "the print verb is broken".
        if (!request.paths.isEmpty())
            openDocument(request.paths.first());
        show();
        WindowDialogs::information(this, Edition::productName(),
                                   tr("打印将在后续版本中提供。"));
        return true;
    }

    const int page = request.page;
    for (const QString &path : request.paths)
        openDocument(path, page);

    // Shown even with nothing open: a reader that started from a shortcut is
    // waiting for a file, and the empty state says how to give it one.
    show();
    return true;
}

void MainWindow::applyForwardedCommandLine(const CommandLine::Request &request)
{
    if (!request.valid) {
        WindowDialogs::warning(this, Edition::productName(), request.errorMessage);
        return;
    }
    // The window may be behind others, and a second launch that opened a
    // document should bring it forward rather than leave the user looking at
    // nothing happening.
    show();
    raise();
    activateWindow();

    for (const QString &path : request.paths)
        openDocument(path, request.page);
}

bool MainWindow::hasPendingWork() const
{
    for (int index = 0; index < tabs->count(); ++index) {
        if (auto *tab = qobject_cast<DocumentTab *>(tabs->widget(index))) {
            if (tab->isBusy())
                return true;
        }
    }
    return false;
}

void MainWindow::retranslateUi()
{
    // Only what can be rebuilt without a restart: the language change itself
    // takes effect on the next launch, so the menus stay as they are until
    // then. The title is not a translated string beyond the document name, so
    // this is just the title.
    updateWindowTitle();
}
