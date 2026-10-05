#include "thememanager.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

namespace {

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString withAlpha(const QColor &color, int alpha)
{
    QColor adjusted = color;
    adjusted.setAlpha(alpha);
    return cssColor(adjusted);
}

void replaceToken(QString &styleSheet, const QString &token, const QString &value)
{
    // "WINDOW" and "WINDOW_BACKGROUND" both being tokens is why this replaces
    // "$" + token rather than the bare word: the shorter one appears inside the
    // longer one, and a plain replace would corrupt it depending on the order
    // the two substitutions happened in.
    styleSheet.replace(QStringLiteral("$") + token, value);
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

void ThemeManager::initialize(QApplication &application)
{
    instance().application = &application;
}

ThemeColors ThemeManager::colors(AppTheme theme)
{
    ThemeColors colors;
    colors.dark = isDarkTheme(theme);

    switch (theme) {
    case AppTheme::DefaultDark:
        colors.window = QColor(23, 26, 31);
        colors.surface = QColor(32, 36, 43);
        colors.surfaceAlt = QColor(39, 44, 52);
        colors.field = QColor(26, 30, 36);
        colors.text = QColor(238, 242, 247);
        colors.mutedText = QColor(169, 177, 188);
        colors.disabledText = QColor(112, 120, 131);
        colors.border = QColor(62, 69, 79);
        colors.accent = QColor(83, 157, 232);
        colors.accentHover = QColor(107, 174, 240);
        colors.selection = QColor(47, 91, 134);
        colors.hover = QColor(43, 50, 59);
        colors.disabledSurface = QColor(42, 46, 53);
        colors.checkBorder = QColor(132, 143, 157);
        colors.shadow = QColor(0, 0, 0, 115);
        // Darker than the window: a page has to be brighter than what is
        // around it, and in a dark theme that means the surround goes black
        // rather than grey.
        colors.pageBackdrop = QColor(16, 18, 21);
        colors.pagePlaceholder = QColor(38, 42, 49);
        colors.pageBorder = QColor(62, 69, 79);
        break;
    case AppTheme::Default:
        colors.window = QColor(250, 251, 252);
        colors.surface = QColor(255, 255, 255);
        colors.surfaceAlt = QColor(250, 250, 250);
        colors.field = QColor(255, 255, 255);
        colors.text = QColor(31, 34, 38);
        colors.mutedText = QColor(96, 102, 109);
        colors.disabledText = QColor(139, 146, 153);
        colors.border = QColor(201, 206, 212);
        colors.accent = QColor(57, 130, 207);
        colors.accentHover = QColor(45, 113, 184);
        colors.selection = QColor(207, 232, 255);
        colors.hover = QColor(244, 248, 252);
        colors.disabledSurface = QColor(243, 244, 245);
        colors.checkBorder = QColor(137, 147, 158);
        colors.shadow = QColor(51, 65, 80, 40);
        // Light, like every other desktop reader: a surround that is darker
        // than this reads as "the whole program is grey", which is what a user
        // reports, rather than as a page sitting on a desk. Still darker than
        // the page (255, 255, 255) and than the placeholder, so that the page
        // has an edge and the placeholder has a place to be.
        colors.pageBackdrop = QColor(214, 218, 222);
        colors.pagePlaceholder = QColor(246, 247, 249);
        colors.pageBorder = QColor(168, 174, 182);
        break;
    }
    return colors;
}

ThemeColors ThemeManager::currentColors()
{
    return colors(currentTheme());
}

QString ThemeManager::styleSheetFor(AppTheme theme)
{
    const ThemeColors c = colors(theme);
    const QString windowBackground = cssColor(c.window);

    QString styleSheet = QStringLiteral(R"QSS(
        QWidget { color: $TEXT; }
        QMainWindow, QDialog, QMessageBox { background: $WINDOW_BACKGROUND; color: $TEXT; }

        QMenuBar { background: $SURFACE; border-bottom: 1px solid $BORDER; padding: 4px 8px; }
        QMenuBar::item { padding: 6px 11px; background: transparent; border-radius: 5px; }
        QMenuBar::item:selected { background: $HOVER; }
        QMenuBar::item:pressed { background: $SELECTION; }
        QMenu { background: $SURFACE_ALT; color: $TEXT; border: 1px solid $BORDER; padding: 5px; }
        QMenu::item { background: transparent; padding: 6px 28px 6px 12px; border-radius: 4px; }
        QMenu::item:selected { background: $SELECTION; }
        QMenu::item:disabled { color: $DISABLED_TEXT; }
        QMenu::separator { height: 1px; background: $BORDER; margin: 4px 8px; }

        QToolBar { background: $SURFACE; border-bottom: 1px solid $BORDER; padding: 4px 8px; spacing: 4px; }
        QToolBar::separator { width: 1px; background: $BORDER; margin: 4px 6px; }

        /* The strip the toolbars live in. The same surface they are, so that the
           space at its right end -- where the "…" that expands the chrome sits --
           does not look like a hole in it; and the line under the chrome is drawn
           once, by the strip, so that it is straight across the groups and does
           not double up under the last row. */
        QWidget#toolBarStrip { background: $SURFACE; border-bottom: 1px solid $BORDER; }
        QWidget#toolBarStrip > QToolBar { border-bottom: none; }

        QPushButton, QToolButton { background: $SURFACE; color: $TEXT; border: 1px solid $BORDER;
                                   border-radius: 7px; padding: 6px 12px; }
        QPushButton:hover, QToolButton:hover { background: $HOVER; border-color: $ACCENT; }
        QPushButton:pressed, QToolButton:pressed { background: $SELECTION; }
        QPushButton:default { border: 2px solid $ACCENT; padding: 5px 11px; }
        QPushButton:disabled, QToolButton:disabled { color: $DISABLED_TEXT;
                                                    background: $DISABLED_SURFACE; border-color: $BORDER; }
        QToolButton:checked { background: $SELECTION; border-color: $ACCENT; }

        /* The "…" at the end of the toolbar strip: the same button as everything
           else, only with the padding of a three-dot glyph rather than of a word,
           so that it closes the row instead of competing with the controls in it. */
        QToolButton#toolBarMoreButton { padding: 2px 6px; }

        QLineEdit, QComboBox, QAbstractSpinBox { background: $FIELD; color: $TEXT;
                                                border: 1px solid $BORDER; border-radius: 6px;
                                                padding: 5px 8px;
                                                selection-background-color: $ACCENT;
                                                selection-color: $SELECTION_TEXT; }
        QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border: 1px solid $ACCENT; }
        QLineEdit:read-only { background: $DISABLED_SURFACE; }
        QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled {
            color: $DISABLED_TEXT; background: $DISABLED_SURFACE;
        }
        /* The arrow, which Qt stops drawing as soon as the drop-down is styled
           at all. It was missing entirely in XYZip: the box looked like a plain
           field and the only way to discover it opened was to click where the
           arrow would have been. */
        QComboBox::drop-down { border: none; width: 26px; }
        QComboBox::down-arrow { image: url($CHEVRON_DOWN); width: 12px; height: 12px; }
        QComboBox::down-arrow:disabled { image: url($CHEVRON_DOWN); }
        QComboBox QAbstractItemView { background: $SURFACE_ALT; color: $TEXT;
                                     border: 1px solid $BORDER;
                                     selection-background-color: $SELECTION; }

        QGroupBox { background: $SURFACE; border: 1px solid $BORDER; border-radius: 10px;
                    margin-top: 12px; padding: 14px 12px 10px 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: $TEXT; }

        QCheckBox, QRadioButton { spacing: 9px; background: transparent; }
        QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid $CHECK_BORDER;
                               border-radius: 4px; background: $FIELD; }
        QCheckBox::indicator:unchecked:hover { border: 2px solid $ACCENT; background: $HOVER; }
        QCheckBox::indicator:checked { border: 1px solid $ACCENT; background: $ACCENT;
                                      image: url(:/icons/check_white.svg); }
        QCheckBox::indicator:disabled { border-color: $BORDER; background: $DISABLED_SURFACE; }
        QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid $CHECK_BORDER;
                                  border-radius: 10px; background: $FIELD; }
        QRadioButton::indicator:unchecked:hover { border: 2px solid $ACCENT; background: $HOVER; }
        QRadioButton::indicator:checked { border: 1px solid $ACCENT; background: $ACCENT;
                                         image: url(:/icons/radio_dot_white.svg); }

        QListWidget, QTreeWidget, QTreeView, QListView { background: $FIELD;
                       alternate-background-color: $SURFACE_ALT;
                       border: 1px solid $BORDER; border-radius: 7px;
                       selection-background-color: $SELECTION; selection-color: $TEXT;
                       outline: none; }
        QListWidget::item, QTreeWidget::item, QTreeView::item, QListView::item {
                       padding: 5px 4px; border: none; }
        QListWidget::item:hover, QTreeWidget::item:hover, QTreeView::item:hover {
                       background: $HOVER; }
        QListWidget::item:selected, QTreeWidget::item:selected, QTreeView::item:selected {
                       background: $SELECTION; color: $TEXT; border: none; }

        /* The tab bar. Only the selected tab is a surface: a row of equal boxes
           reads as a toolbar rather than as tabs, and the point of the strip is
           that one of them is the document being read. */
        QTabWidget::pane { border: none; border-top: 1px solid $BORDER; background: $WINDOW_BACKGROUND; }
        QTabBar { background: $SURFACE; }
        QTabBar::tab { background: transparent; color: $MUTED_TEXT; border: none;
                       padding: 7px 14px; margin: 3px 1px 0 1px; border-radius: 6px 6px 0 0; }
        QTabBar::tab:hover { background: $HOVER; color: $TEXT; }
        QTabBar::tab:selected { background: $WINDOW_BACKGROUND; color: $TEXT;
                                border-top: 2px solid $ACCENT; }
        /* The tab's close button. It has to be its own image: this rule used to point at the
           combo box's chevron, which is a different shape saying a different thing -- a reader
           who clicks the arrow expecting to close the tab gets nothing. */
        QTabBar::close-button { image: url($TAB_CLOSE); }

        /* The page surface draws itself; this only says what its surround is.
           Selected by name because the class carries no Q_OBJECT of its own. */
        PageView { background: $PAGE_BACKDROP; }

        QStatusBar { background: $SURFACE; color: $TEXT; border-top: 1px solid $BORDER; }
        QStatusBar QLabel { color: $TEXT; padding: 0 6px; }

        QProgressBar { background: $FIELD; color: $TEXT; border: 1px solid $BORDER;
                       border-radius: 6px; text-align: center; }
        QProgressBar::chunk { background: $ACCENT; border-radius: 5px; }

        QScrollBar:vertical { background: transparent; width: 13px; margin: 2px; }
        QScrollBar::handle:vertical { background: $CHECK_BORDER; border-radius: 5px; min-height: 28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar:horizontal { background: transparent; height: 13px; margin: 2px; }
        QScrollBar::handle:horizontal { background: $CHECK_BORDER; border-radius: 5px; min-width: 28px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }

        QSplitter::handle { background: $BORDER; }
        QToolTip { background: $SURFACE_ALT; color: $TEXT; border: 1px solid $BORDER; padding: 5px; }
        QRubberBand { background: $RUBBER_BAND; border: 1px solid $ACCENT; }
    )QSS");

    replaceToken(styleSheet, QStringLiteral("WINDOW_BACKGROUND"), windowBackground);
    replaceToken(styleSheet, QStringLiteral("PAGE_BACKDROP"), cssColor(c.pageBackdrop));
    replaceToken(styleSheet, QStringLiteral("TEXT"), cssColor(c.text));
    replaceToken(styleSheet, QStringLiteral("MUTED_TEXT"), cssColor(c.mutedText));
    replaceToken(styleSheet, QStringLiteral("DISABLED_TEXT"), cssColor(c.disabledText));
    replaceToken(styleSheet, QStringLiteral("SURFACE_ALT"), cssColor(c.surfaceAlt));
    replaceToken(styleSheet, QStringLiteral("SURFACE"), cssColor(c.surface));
    replaceToken(styleSheet, QStringLiteral("FIELD"), cssColor(c.field));
    replaceToken(styleSheet, QStringLiteral("BORDER"), cssColor(c.border));
    replaceToken(styleSheet, QStringLiteral("ACCENT"), cssColor(c.accent));
    // A stylesheet cannot recolour an image, so the arrow comes in two inks and
    // the theme picks one.
    replaceToken(styleSheet, QStringLiteral("CHEVRON_DOWN"),
                 c.dark ? QStringLiteral(":/icons/chevron_down_dark.svg")
                        : QStringLiteral(":/icons/chevron_down_light.svg"));
    replaceToken(styleSheet, QStringLiteral("TAB_CLOSE"),
                 c.dark ? QStringLiteral(":/icons/tab_close_dark.svg")
                        : QStringLiteral(":/icons/tab_close_light.svg"));
    replaceToken(styleSheet, QStringLiteral("SELECTION_TEXT"),
                 cssColor(c.dark ? QColor(250, 252, 255) : QColor(255, 255, 255)));
    replaceToken(styleSheet, QStringLiteral("SELECTION"), cssColor(c.selection));
    replaceToken(styleSheet, QStringLiteral("HOVER"), cssColor(c.hover));
    replaceToken(styleSheet, QStringLiteral("DISABLED_SURFACE"), cssColor(c.disabledSurface));
    replaceToken(styleSheet, QStringLiteral("CHECK_BORDER"), cssColor(c.checkBorder));
    replaceToken(styleSheet, QStringLiteral("RUBBER_BAND"), withAlpha(c.accent, c.dark ? 60 : 48));
    return styleSheet;
}

void ThemeManager::apply(AppTheme theme)
{
    ThemeManager &manager = instance();
    if (!manager.application)
        manager.application = qobject_cast<QApplication *>(QApplication::instance());
    manager.activeTheme = theme;
    if (!manager.application)
        return;

    const ThemeColors c = colors(theme);
    QPalette palette = manager.application->palette();
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.field);
    palette.setColor(QPalette::AlternateBase, c.surfaceAlt);
    palette.setColor(QPalette::ToolTipBase, c.surfaceAlt);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.surface);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::HighlightedText, c.text);
    palette.setColor(QPalette::PlaceholderText, c.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Button, c.disabledSurface);
    palette.setColor(QPalette::Disabled, QPalette::Base, c.disabledSurface);
    manager.application->setPalette(palette);
    manager.application->setStyleSheet(styleSheetFor(theme));

    // The title bar, which a palette cannot reach.
    //
    // The frame is drawn by Windows, not by us, and since Qt 6.8 Qt decides
    // whether it is dark from the application's colour scheme -- and then sets
    // DWMWA_USE_IMMERSIVE_DARK_MODE itself. XYZip set that attribute by hand
    // too, got S_OK for it, and had it overwritten: the caption stayed light
    // under every dark theme while the whole window below it was dark.
    QGuiApplication::styleHints()->setColorScheme(
        isDarkTheme(theme) ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);

    for (QWidget *window : manager.application->topLevelWidgets())
        window->update();
}

AppTheme ThemeManager::currentTheme()
{
    return instance().activeTheme;
}
