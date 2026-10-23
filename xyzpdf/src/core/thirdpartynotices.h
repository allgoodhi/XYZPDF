// What XYZPDF ships that it did not write, and under what terms.
//
// One of the libraries in the package is LGPL -- Qt, which arrives with PDFium
// inside it -- and LGPL asks three things of anyone who passes it on: say that
// it is there, hand over a copy of the licence, and say where the source can be
// had. None of that happens by itself; this is where it happens.
//
// The list is deliberately a hand-written table rather than something derived
// from the build, for the same reason XYZip's is: a generated list would go
// stale silently, whereas a test can check this one against the DLLs actually
// in the package and fail when they disagree. A new dependency that nobody
// declared here does not ship. (That check arrives with the packaging work in
// M4; until then this list is what there is.)
#pragma once

#include <QCoreApplication>
#include <QString>
#include <QVector>

namespace ThirdPartyNotices {

struct Component
{
    QString name;
    // What the package carries for it. A component with no file of its own is
    // covered by the licence named below.
    QString libraryFile;
    QString licence;
    // Where the source is, which is the part LGPL actually requires. Upstream
    // is enough for the ones taken unchanged.
    QString source;
    // The licence text in the resources, shown in the dialog.
    QString licenceResource;
    // Anything a reader would otherwise have to work out for themselves.
    QString note;
};

// Everything in the shipped package that is not XYZPDF's own code.
QVector<Component> components();

// The licence text for a component, read from the resources.
QString licenceText(const Component &component);

inline QString title()
{
    return QCoreApplication::translate("ThirdPartyNotices", "第三方开源许可");
}

// The paragraph above the list. It says the one thing a person opening this
// dialog most likely wants to know.
//
// Qt and PDFium used to be the same sentence, because Qt's PDF module carried
// PDFium inside it. Since F7 the program links PDFium itself, so the two have
// different licences and the paragraph has to say so: LGPL for Qt, BSD-3 for
// PDFium, Apache-2.0 for QPDF, each one a library this program calls through an
// interface rather than links into its own binary.
inline QString summary()
{
    return QCoreApplication::translate(
        "ThirdPartyNotices",
        "XYZPDF 使用了下列开源组件：Qt（LGPL）、PDFium（BSD-3-Clause）与 QPDF（Apache-2.0）。"
        "它们都编译为独立的动态库，XYZPDF 通过接口调用，并未与之静态链接——"
        "你可以用自行修改的版本替换它们。\n\n"
        "下方列出每个组件的许可证与源码地址。许可证全文可在此查看。");
}

} // namespace ThirdPartyNotices
