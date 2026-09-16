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
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
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
    // A stack of two: the tab strip, and what to show when there is no tab.
    //
    // QTabWidget has no empty state of its own -- with no tabs it draws a bare
    // frame -- and the first thing a new user sees is exactly that, so the
    // empty case gets a page of its own.
    stack = new QStackedWidget(this);

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
    setCentralWidget(stack);

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

    selectAllAction = makeAction(tr("全选(&A)"), QKeySequence::SelectAll);
    selectAllAction->setObjectName(QStringLiteral("selectAllAction"));
    connect(selectAllAction, &QAction::triggered, this, [this] {
        if (DocumentTab *tab = currentTab())
            tab->selectAll();
    });
    editMenu->addAction(selectAllAction);

    // ---- 查看 ----
    QMenu *viewMenu = menuBar()->addMenu(tr("查看(&V)"));

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
    toolBar = addToolBar(tr("主工具栏"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    toolBar->addAction(openAction);
    toolBar->addSeparator();

    toolBar->addAction(firstPageAction);
    toolBar->addAction(previousPageAction);

    pageBox = new QComboBox(toolBar);
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
    toolBar->addWidget(pageBox);

    toolBar->addAction(nextPageAction);
    toolBar->addAction(lastPageAction);
    toolBar->addSeparator();

    toolBar->addAction(zoomOutAction);

    zoomBox = new QComboBox(toolBar);
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
    toolBar->addWidget(zoomBox);

    toolBar->addAction(zoomInAction);
    toolBar->addAction(fitWidthAction);
    toolBar->addSeparator();
    toolBar->addAction(rotateLeftAction);
    toolBar->addAction(rotateRightAction);
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
    const int index = tabs->addTab(tab, QFileInfo(path).fileName());
    tabs->setCurrentIndex(index);

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

void MainWindow::updateForCurrentTab()
{
    DocumentTab *tab = currentTab();
    stack->setCurrentWidget(tab ? static_cast<QWidget *>(tabs) : emptyState);

    const bool hasDocument = tab && tab->isOpen();
    for (QAction *action : {closeTabAction, firstPageAction, previousPageAction, nextPageAction,
                            lastPageAction, goToPageAction, zoomInAction, zoomOutAction,
                            fitWidthAction, fitPageAction, actualSizeAction, rotateLeftAction,
                            rotateRightAction, continuousAction, singlePageAction,
                            selectAllAction}) {
        if (action)
            action->setEnabled(hasDocument);
    }
    // Copy is the one that needs more than an open document: with nothing
    // selected it would clear the clipboard, which is worse than doing nothing.
    if (copyAction)
        copyAction->setEnabled(hasDocument && tab->hasSelection());
    pageBox->setEnabled(hasDocument);
    zoomBox->setEnabled(hasDocument);

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

        statusPage->setText(tr("%1 / %2").arg(tab->currentPage() + 1).arg(count));
        statusZoom->setText(tr("%1%").arg(percent));
    } else {
        pageBox->clear();
        zoomBox->setCurrentText(QString());
        statusPage->setText(QString());
        statusZoom->setText(QString());
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
