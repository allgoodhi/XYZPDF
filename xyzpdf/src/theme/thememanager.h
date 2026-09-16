#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include "theme.h"

#include <QColor>
#include <QObject>
#include <QString>

class QApplication;

// The palette and stylesheet for each theme, in one place.
//
// The window that owns the view needs the theme's colours as numbers, not just
// as a stylesheet: the page surface, the placeholder behind a page that is
// still rendering and the rule around a page are painted by PageView, and a
// stylesheet cannot style a QPainter. Hence colors() being public.
struct ThemeColors {
    QColor window;
    QColor surface;
    QColor surfaceAlt;
    QColor field;
    QColor text;
    QColor mutedText;
    QColor disabledText;
    QColor border;
    QColor accent;
    QColor accentHover;
    QColor selection;
    QColor hover;
    QColor disabledSurface;
    QColor checkBorder;
    QColor shadow;
    // The backdrop the pages sit on, the box drawn while one is on its way, and
    // the rule around a page. Three greys, and they have to be told apart at a
    // glance: the backdrop is what the user calls "the background colour", the
    // placeholder has to be distinguishable from a page that has finished
    // rendering, and the border is what gives a white page its edge when the
    // backdrop is light.
    QColor pageBackdrop;
    QColor pagePlaceholder;
    QColor pageBorder;
    bool dark = false;
};

class ThemeManager final : public QObject
{
    Q_OBJECT

public:
    static void initialize(QApplication &application);
    static void apply(AppTheme theme);
    static AppTheme currentTheme();
    static ThemeColors colors(AppTheme theme);
    static ThemeColors currentColors();
    static QString styleSheetFor(AppTheme theme);

private:
    explicit ThemeManager(QObject *parent = nullptr);
    static ThemeManager &instance();

    QApplication *application = nullptr;
    AppTheme activeTheme = AppTheme::Default;
};

#endif // THEMEMANAGER_H
