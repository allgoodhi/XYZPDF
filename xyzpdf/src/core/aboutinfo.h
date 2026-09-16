// What the About box says.
//
// Kept out of MainWindow so it can be asserted on and looked at without
// standing up the whole window. The one thing about it that can silently go
// wrong is the version: it has to be the version this was built as, not a
// number typed in beside it, and tests/version_smoke.cpp checks that it is.
#pragma once

#include <xyzpdf/version.h>

#include <QCoreApplication>
#include <QString>
#include <QStringLiteral>

#include "edition.h"

namespace AboutInfo {

// qVersion() is the Qt actually loaded rather than the one this was compiled
// against. When a report turns out to be a Qt version difference, the number
// that matters is the one that ran.
//
// The renderer is named with the Qt version it arrives inside, because that is
// the fact: Qt PDF is PDFium, and v1 has no rendering code of its own.
inline QString html()
{
    return QCoreApplication::translate(
               "AboutInfo",
               "<h3>%1 %2</h3>"
               "<p>Windows PDF 阅读器。</p>"
               "<p>Qt %3<br>"
               "渲染引擎：Qt PDF（PDFium）</p>"
               "<p>版权所有 © 2026 allgoodhi</p>")
        .arg(Edition::productName(), QStringLiteral(XYZPDF_VERSION),
             QString::fromLatin1(qVersion()));
}

inline QString title()
{
    return QCoreApplication::translate("AboutInfo", "关于 %1").arg(Edition::productName());
}

} // namespace AboutInfo
