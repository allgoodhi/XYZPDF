// Which build this is.
//
// v1 ships one edition, so nothing here is gated yet and XYZPDF_PRO is never
// defined. It exists anyway, and the CMake option that sets it exists, because
// the alternative was learned the hard way in XYZip: the paid build was bolted
// on after the fact, and every place that had already assumed one edition --
// the window title, the About box, the package identity, two manifests, the
// test harness -- had to be gone through again.
//
// So the shape is here and the values are not: one source file, one binary, one
// test suite, and a product name that asks which build it is rather than
// spelling it out at each call site. See docs/PLAN.md §7.2.
#pragma once

#include <QCoreApplication>
#include <QString>

#include <xyzpdf/packageconfig.h>

namespace Edition {

#ifdef XYZPDF_PRO
constexpr bool Pro = true;
#else
constexpr bool Pro = false;
#endif

// The name the program goes by: in the window title, in the About box, and in
// the package. Not translated -- it is a product name, the same in every
// language, the way the Language menu entry is.
inline QString productName()
{
    // A product name is not an interface string, so it does not go through
    // tr(); but it does come from the generated identity rather than being
    // written here, so that the title bar, the About box and the manifest's
    // DisplayName (which is what the Start menu and 设置 > 应用 show) cannot end
    // up calling themselves different things. packaging/verify-package.ps1
    // starts the packed program and compares its window title with the manifest
    // it just unpacked; that check is only meaningful while this is one value.
    return QStringLiteral(XYZPDF_PACKAGE_DISPLAY_NAME);
}

} // namespace Edition
