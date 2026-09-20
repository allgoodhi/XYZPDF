// The names a page-image export gives its files.
//
// The render is the same one the viewer already does; the part that can be
// quietly wrong is the file name -- an unpadded number sorts page 10 before page
// 2 in a folder, and a name that does not match the encoder's extension is a file
// that opens as the wrong thing. Both are pinned here without rendering anything.

#include "../src/export/imageexport.h"

#include <QString>

#include <iostream>

namespace {

int failures = 0;

void checkEquals(const QString &actual, const QString &expected, const QString &what)
{
    if (actual == expected)
        return;
    std::cerr << "FAIL: " << what.toStdString() << "\n      expected "
              << expected.toStdString() << "\n      got      " << actual.toStdString() << '\n';
    ++failures;
}

} // namespace

int main(int, char **)
{
    using ImageExport::fileNameFor;
    using ImageExport::Format;

    checkEquals(fileNameFor(QStringLiteral("report"), 1, Format::Png),
                QStringLiteral("report 001.png"), "page 1 is zero-padded and png");
    checkEquals(fileNameFor(QStringLiteral("report"), 7, Format::Jpeg),
                QStringLiteral("report 007.jpg"), "a jpeg page keeps the jpg extension");
    checkEquals(fileNameFor(QStringLiteral("report"), 42, Format::Png),
                QStringLiteral("report 042.png"), "two digits still pad to three");
    checkEquals(fileNameFor(QStringLiteral("report"), 1234, Format::Png),
                QStringLiteral("report 1234.png"), "four digits are not truncated");
    checkEquals(fileNameFor(QStringLiteral("我的手册"), 3, Format::Png),
                QStringLiteral("我的手册 003.png"), "a non-ascii base name survives");

    if (failures == 0) {
        std::cout << "image export smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
