#include "thirdpartynotices.h"

#include <QFile>

namespace ThirdPartyNotices {

QVector<Component> components()
{
    const auto tr = [](const char *text) {
        return QCoreApplication::translate("ThirdPartyNotices", text);
    };
    // Every cell of this table is read by a person, so every prose cell goes through
    // that lambda. A QStringLiteral here is invisible to lupdate rather than merely
    // untranslated, which is how §20 found the English interface answering two of the
    // notes below in Chinese: the notes had been translated all along, and the columns
    // above them were not in the catalogue at all. Only the DLL names and the URLs stay
    // as literals -- a file name is not prose, and 「编入 X.dll」 was a phrasing, which
    // is why the PDFium row now names pdfium.dll directly instead of saying it.

    return {
        {QStringLiteral("Qt"),
         QStringLiteral("Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Network.dll, "
                        "Qt6Svg.dll"),
         QStringLiteral("LGPL-3.0"),
         QStringLiteral("https://download.qt.io/official_releases/qt/"),
         QStringLiteral(":/licenses/LGPL-3.0.txt"),
         tr("界面框架。以动态库形式随包分发，未做修改。")},

        {QStringLiteral("PDFium"), QStringLiteral("pdfium.dll"),
         QStringLiteral("BSD-3-Clause"),
         QStringLiteral("https://pdfium.googlesource.com/pdfium/"),
         QStringLiteral(":/licenses/pdfium.txt"),
         tr("渲染引擎：页面光栅化、文本提取、书签与链接都由它完成。"
            "以动态库形式随包分发，未做修改；它自己内含的第三方组件的许可证都在同一份文本里。")},

        {QStringLiteral("QPDF"),
         QStringLiteral("qpdf30.dll, z.dll, jpeg62.dll"),
         QStringLiteral("Apache-2.0"),
         QStringLiteral("https://github.com/qpdf/qpdf"),
         QStringLiteral(":/licenses/qpdf.txt"),
         tr("结构编辑器：删除页面后的文档由它写出。以动态库形式随包分发，未做修改。"
            "z.dll 与 jpeg62.dll 是它自带的依赖（zlib、libjpeg），一并动态链接。")},

        {tr("Qt 内置的第三方库"),
         tr("（编入 Qt6Gui.dll、Qt6Core.dll）"),
         tr("各自的原许可证，见许可证全文"),
         QStringLiteral("https://download.qt.io/official_releases/qt/"),
         QStringLiteral(":/licenses/qt.txt"),
         tr("Qt 自身内含 zlib、libpng、freetype、harfbuzz、libjpeg-turbo 等库，"
            "随 Qt 的二进制库一并分发。它们的许可证不影响 XYZPDF 自身代码的授权。")},
    };
}

QString licenceText(const Component &component)
{
    QFile file(component.licenceResource);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QCoreApplication::translate("ThirdPartyNotices", "许可证文本缺失：%1")
            .arg(component.licenceResource);
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace ThirdPartyNotices
