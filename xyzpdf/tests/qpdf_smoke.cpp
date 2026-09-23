// QPDF, checked before the program asks anything of it.
//
// The library arrives with docs/PLAN.md §4 M5 (结构编辑先上 QPDF，因为它不碰页面内容流), and
// landing it alone is the point: what gets found out here is that it builds, links, runs, and
// -- the part no build can tell you -- that a file it has rewritten is still a file the reader
// opens. Qt PDF and QPDF are the pair every editing feature will use, one to write and one to
// show, and a writer whose output the reader cannot read is not a writer. So the test below
// links both, and the second opinion is not a copy of the first: it counts the pages through
// QPdfDocument and paints one of them.
//
// Nothing here writes into tests/fixtures. The document being edited is a copy in a temporary
// directory, and the fixture's own bytes are compared by hash at the end rather than trusted:
// "永不写用户的 PDF 文件" is the rule the whole feature has to keep, and a test that broke that
// rule quietly would be the worst kind of passing test.

#include "../src/document/qtpdfbackend.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <exception>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

QString hashOfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

// Whether anything at all was painted. A rendered page is ink over transparency
// (docs/PLAN.md §3.3: the white paper is put down by whoever shows the image), so
// "not null" and "nothing in it" are two different answers, and only one of them
// means the document survived the rewrite.
bool paintsAnything(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const QString fixture = QStringLiteral("tests/fixtures/many-pages.pdf");
    QTemporaryDir scratch;
    check(scratch.isValid(), "the test gets a temporary directory of its own");
    if (!scratch.isValid() || !QFile::exists(fixture)) {
        std::cerr << "FAIL: the inputs this test needs are not there\n";
        return 1;
    }
    const QString originalHash = hashOfFile(fixture);
    check(!originalHash.isEmpty(), "and the fixture can be hashed before anything touches it");

    const QString copy = scratch.filePath(QStringLiteral("copy.pdf"));
    check(QFile::copy(fixture, copy), "the document worked on is a copy, not the fixture");

    const QString written = scratch.filePath(QStringLiteral("one-page-shorter.pdf"));

    // ---- take one page out, and write a file ----
    std::size_t pagesBefore = 0;
    std::size_t pagesAfter = 0;
    try {
        QPDF qpdf;
        qpdf.processFile(copy.toUtf8().constData());
        QPDFPageDocumentHelper helper(qpdf);
        std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
        pagesBefore = pages.size();
        check(pagesBefore > 1, "the fixture has more than one page to take one away from");

        helper.removePage(pages.at(1));
        pagesAfter = helper.getAllPages().size();
        check(pagesAfter == pagesBefore - 1,
              QStringLiteral("one page gone leaves %1 where there were %2")
                  .arg(pagesAfter)
                  .arg(pagesBefore));

        QPDFWriter writer(qpdf, written.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        check(false, QStringLiteral("QPDF did not throw: %1")
                         .arg(QString::fromUtf8(e.what())));
    }

    check(QFile::exists(written), "and the rewritten document is on disk");

    // ---- the same library reading its own output ----
    //
    // Both readings below are against `pagesBefore - 1` rather than against
    // `pagesAfter`, because pagesAfter is the same helper that did the removing: a
    // second opinion that is compared against the first one's answer is not a
    // second opinion.
    if (QFile::exists(written)) {
        try {
            QPDF again;
            again.processFile(written.toUtf8().constData());
            check(again.getAllPages().size() == pagesBefore - 1,
                  "QPDF counts the pages it wrote the way it counted them before writing");
        } catch (const std::exception &e) {
            check(false, QStringLiteral("the written file reads back without throwing: %1")
                             .arg(QString::fromUtf8(e.what())));
        }

        // ---- and the other half: the reader the program actually uses ----
        QtPdfBackend document;
        check(document.load(written, QString()) == Document::Status::Ready,
              "Qt PDF opens what QPDF wrote");
        check(document.pageCount() == int(pagesBefore) - 1,
              "and agrees about how many pages it has");
        const QImage image = document.render(0, QSize(300, 400), RenderOptions{});
        check(!image.isNull(), "the first page renders");
        check(paintsAnything(image), "and there is something in the picture, not an empty canvas");
    }

    // The rule, checked where it could have been broken rather than asserted.
    check(hashOfFile(fixture) == originalHash, "the fixture itself is byte for byte what it was");
    check(hashOfFile(copy) == originalHash, "and so is the copy QPDF was pointed at");

    if (failures == 0) {
        std::cout << "qpdf smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
