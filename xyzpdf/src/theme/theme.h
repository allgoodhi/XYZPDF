#ifndef THEME_H
#define THEME_H

#include <QString>

enum class AppTheme {
    Default = 0,
    DefaultDark
};

inline QString appThemeId(AppTheme theme)
{
    switch (theme) {
    case AppTheme::DefaultDark:
        return QStringLiteral("default-dark");
    case AppTheme::Default:
        return QStringLiteral("default");
    }
    return QStringLiteral("default");
}

inline AppTheme appThemeFromId(const QString &id)
{
    if (id == QStringLiteral("default-dark"))
        return AppTheme::DefaultDark;

    // Two themes were removed in XYZip: they set a DWM acrylic backdrop that
    // the window then painted over, so the effect was never visible. Anyone
    // whose settings still name one is mapped to the surviving theme that
    // matches the colours they had chosen, rather than dropped into a light
    // window without asking. Kept here because the ids are in the same
    // settings store this program reads; there is no upgrade step that could
    // have removed them.
    if (id == QStringLiteral("glass-dark"))
        return AppTheme::DefaultDark;
    if (id == QStringLiteral("glass"))
        return AppTheme::Default;

    return AppTheme::Default;
}

inline bool isDarkTheme(AppTheme theme)
{
    return theme == AppTheme::DefaultDark;
}

#endif // THEME_H
