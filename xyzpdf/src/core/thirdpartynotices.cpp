#include "thirdpartynotices.h"

#include <QFile>

namespace ThirdPartyNotices {

QVector<Component> components()
{
    const auto tr = [](const char *text) {
        return QCoreApplication::translate("ThirdPartyNotices", text);
    };

    return {
        {QStringLiteral("Qt"),
         QStringLiteral("Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Network.dll, "
                        "Qt6Svg.dll, Qt6Pdf.dll"),
         QStringLiteral("LGPL-3.0"),
         QStringLiteral("https://download.qt.io/official_releases/qt/"),
         QStringLiteral(":/licenses/LGPL-3.0.txt"),
         tr("界面框架与 PDF 渲染接口。以动态库形式随包分发，未做修改。")},

        {QStringLiteral("PDFium"), QStringLiteral("（编入 Qt6Pdf.dll）"),
         QStringLiteral("BSD-3-Clause"),
         QStringLiteral("https://pdfium.googlesource.com/pdfium/"),
         QStringLiteral(":/licenses/pdfium.txt"),
         tr("v1 的渲染引擎：页面光栅化、文本提取、书签与链接都由它完成。"
            "由 Qt PDF 编入 Qt6Pdf.dll，未做修改。")},

        {QStringLiteral("Qt 内置的第三方库"),
         QStringLiteral("（编入 Qt6Gui.dll、Qt6Core.dll）"),
         QStringLiteral("各自的原许可证，见许可证全文"),
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
