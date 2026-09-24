// Taking pages out of a PDF, moving the rest around, putting pages from another file
// in, cutting what is left into several files, and what is promised around all four.
//
// The feature has one interesting failure mode, and it is not the page count: it
// is writing over a file the reader owns. So most of what is pinned down here is
// the promise side of docs/PLAN.md §7.3 and AGENTS.md's 永不写用户的 PDF 文件 --
// the source is never the destination, a refused request changes nothing on disk,
// and a write that cannot land leaves a whole file the reader can find, never a
// truncated one at the path they pointed at.
//
// The round trip is checked through Qt PDF rather than through QPDF alone, for the
// same reason tests/qpdf_smoke.cpp links both: the claim is the handoff between the
// library that writes and the library this program reads with, and one engine
// agreeing with itself proves nothing.
//
// The encrypted cases at the end *measure* rather than assert a belief about what
// an encrypted document does when the structural editor is pointed at it -- that is
// QPDF's decision, not this program's. What is asserted is the invariant that holds
// whatever that turns out to be: nothing damaged, and either a copy that behaves
// like the original or no copy at all. "Behaves like the original" used to mean only
// "opens, and with the password it still needs one"; it now covers the permission
// bits too, because a copy that silently stops saying "not to be edited" is the one
// way this feature could hand someone a document that lies about its protection.

#include "../src/document/qtpdfbackend.h"
#include "../src/edit/documenteditor.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <exception>
#include <iostream>

namespace {

int failures = 0;
int notes = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

// Printed rather than asserted, and for one reason: the answer is QPDF's, and this
// test is where it gets written down before it becomes a sentence in the docs.
void note(const QString &what)
{
    std::cout << "note: " << what.toStdString() << '\n';
    ++notes;
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

// How many pages Qt PDF counts in a file, or -1 for "it would not open". The two
// failures are kept apart because "no pages" and "unreadable" are different
// sentences, and only one of them means the editor wrote a broken file.
int pagesAsReadByTheReader(const QString &path, const QString &password = QString())
{
    QtPdfBackend document;
    if (document.load(path, password) != Document::Status::Ready)
        return -1;
    return document.pageCount();
}

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

QVector<int> everyPage(int pageCount)
{
    QVector<int> pages;
    pages.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        pages.append(page);
    return pages;
}

using Ref = DocumentEditor::PageRef;

// The same document as the editor keeps it: every page of the one that was opened,
// in the order it is on the screen. A page number a reader types is always a page of
// that file, so every reference below has source 0 -- the tests that need a page from
// a merged-in file say so themselves.
QVector<Ref> everyRef(int pageCount)
{
    QVector<Ref> refs;
    refs.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        refs.append(Ref{0, page});
    return refs;
}

// The same list written out by hand, from the numbers on the screen.
QVector<Ref> onScreen(std::initializer_list<int> numbers)
{
    QVector<Ref> refs;
    for (int number : numbers)
        refs.append(Ref{0, number});
    return refs;
}

// A page order handed back by a plan, read back as numbers so the assertions can say
// "2,3,4" instead of naming pairs. A page that came in from a merged file is written
// as its merge number times a hundred, plus its number in its own file -- the only way
// these tests can point at one, and the reason is the feature's limit rather than a
// shortcut: an imported page has no number on the screen for anyone to type.
QVector<int> asNumbers(const QVector<Ref> &order)
{
    QVector<int> numbers;
    numbers.reserve(order.size());
    for (const Ref &ref : order)
        numbers.append(ref.source == 0 ? ref.page : ref.source * 100 + ref.page);
    return numbers;
}

// A file holding pages 5, 6 and 7 of the fixture and nothing else, written here with
// the editor's own delete: the merge tests need a second document, and a fixture in
// tests/fixtures would be picked up by every other test that globs that folder.
QString writeDonor(const QString &source, const QString &destination, int pagesBefore,
                   const QVector<int> &keep, QString *error)
{
    DocumentEditor prep;
    if (!prep.open(source, QString(), error))
        return {};
    QVector<int> drop;
    for (int page = 0; page < pagesBefore; ++page) {
        if (!keep.contains(page))
            drop.append(page);
    }
    if (!prep.deletePages(drop, error) || !prep.saveAs(destination, error))
        return {};
    return destination;
}

// What the file itself says each page's rotation is, in degrees.
//
// Read out of QPDF rather than off the screen because the renderer's answer to
// "how wide is this page" cannot tell a right turn from a left one, and the thing
// being checked here is that the direction a reader picked in a dialog is the
// direction that got written. The renderer's own reading is checked separately,
// through pageSize(), so the two engines each get asked a question the other
// cannot answer.
QVector<int> rotationsInFile(const QString &path)
{
    QVector<int> angles;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        for (const QPDFPageObjectHelper &page : QPDFPageDocumentHelper(qpdf).getAllPages()) {
            // Asked for rather than read as an integer: most pages have no /Rotate
            // at all, and QPDF answers getIntValue() on a null object with a
            // warning to the console per page.
            const QPDFObjectHandle rotate = page.getObjectHandle().getKey("/Rotate");
            angles.append(rotate.isInteger() ? rotate.getIntValue() : 0);
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the rotations out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return angles;
}

// What order the file says its pages are in, by reading the number each fixture
// page prints on itself.
//
// The only way the reorder can be checked at all: a page count cannot tell a moved
// document from an untouched one, and the whole failure mode of a move is "every
// page is there, in the wrong place". Read through Qt PDF rather than QPDF because
// the claim is about the file a reader would open, not about what the writer thinks
// it wrote. -1 for a page whose number could not be read, so a silent mismatch
// cannot come from the parsing.
QVector<int> orderInFile(const QString &path)
{
    QVector<int> numbers;
    QtPdfBackend document;
    if (document.load(path, QString()) != Document::Status::Ready) {
        std::cerr << "FAIL: the reader would not open " << path.toStdString() << '\n';
        ++failures;
        return numbers;
    }
    static const QRegularExpression printed(QStringLiteral("page (\\d+)"));
    for (int page = 0; page < document.pageCount(); ++page) {
        const QRegularExpressionMatch hit = printed.match(document.text(page).simplified());
        numbers.append(hit.hasMatch() ? hit.captured(1).toInt() : -1);
    }
    return numbers;
}

QString describeOrder(const QVector<int> &numbers)
{
    QStringList parts;
    for (int number : numbers)
        parts.append(QString::number(number));
    return parts.join(QLatin1Char(','));
}

// A document that needs a password to open, written here rather than committed as a
// fixture: the test needs one, it needs to know the password, and a file in
// tests/fixtures would be swept up by every other test that globs the folder.
// `userPassword` empty with modify forbidden is the shape most protected PDFs in the
// wild actually have -- readable by anyone, "not to be edited" written on it.
QString writeEncryptedCopy(const QString &source, const QString &destination,
                           const QString &userPassword, bool allowModify)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.setR6EncryptionParameters(userPassword.toUtf8().constData(),
                                         "owner",
                                         /* allow_accessibility = */ true,
                                         /* allow_extract = */ true,
                                         /* allow_assemble = */ false,
                                         /* allow_annotate_and_form = */ true,
                                         /* allow_form_filling = */ true,
                                         /* allow_modify_other = */ allowModify,
                                         qpdf_r3p_full,
                                         /* encrypt_metadata_aes = */ true);
        writer.write();
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
    return {};
}

// The `/P` integer lifted out of the file's own encryption dictionary.
//
// This is the same number QPDF reports through isEncrypted(R, P), read a second way,
// and the reason for the second way is that the claim under test spans two files that
// one parser reads: "the copy carries the protection the original had". If the writer
// dropped the dictionary and the reader quietly reported a default for both, asking
// QPDF alone would hear the same wrong answer twice. The dictionary is never itself
// encrypted and never lives in an object stream, so these bytes are in the file
// exactly as the writer put them.
qint64 rawPermissionBits(const QString &path, bool *found)
{
    *found = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray bytes = file.readAll();
    file.close();

    // Trailer first: "/Encrypt 12 0 R" is what names the dictionary's object number.
    static const QRegularExpression reference(
        QStringLiteral("/Encrypt\\s+(\\d+)\\s+\\d+\\s+R"));
    const QRegularExpressionMatch ref = reference.match(QString::fromLatin1(bytes));
    if (!ref.hasMatch())
        return 0;
    const QByteArray object = ref.captured(1).toLatin1() + " 0 obj";
    const int start = bytes.indexOf(object);
    const int stop = bytes.indexOf("endobj", start);
    if (start < 0 || stop < 0)
        return 0;

    // The "\s" is what keeps this off /Perms and /PageRange: the bitfield is written
    // as its own key, followed by whitespace and a signed integer.
    const QRegularExpression bits(
        QStringLiteral("/P\\s+(-?\\d+)"), QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch hit = bits.match(QString::fromLatin1(
        bytes.mid(start, stop - start)));
    if (!hit.hasMatch())
        return 0;
    *found = true;
    return hit.captured(1).toLongLong();
}

// What a file says about what may be done with it, from both readers at once.
struct Permissions
{
    bool readable = false;   // QPDF opened it at all with this password
    bool encrypted = false;
    int r = 0;
    qint64 p = 0;
    qint64 rawP = 0;
    bool rawPFound = false;
    bool modifyAssembly = false;
    bool modifyAnnotation = false;
    bool modifyOther = false;
    QString complaint;
};

Permissions readPermissions(const QString &path, const QString &password)
{
    Permissions out;
    out.rawP = rawPermissionBits(path, &out.rawPFound);
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData(), password.toUtf8().constData());
        out.readable = true;
        // isEncrypted takes its R and P as int&, so the struct's qint64 (which is
        // what /P holds in the file) is filled from a local rather than passed in.
        int bits = 0;
        out.encrypted = qpdf.isEncrypted(out.r, bits);
        out.p = bits;
        if (out.encrypted) {
            out.modifyAssembly = qpdf.allowModifyAssembly();
            out.modifyAnnotation = qpdf.allowModifyAnnotation();
            out.modifyOther = qpdf.allowModifyOther();
        }
    } catch (const std::exception &e) {
        out.complaint = QString::fromUtf8(e.what());
    }
    return out;
}

QString describe(const Permissions &p)
{
    return QStringLiteral("R%1 P%2 (raw %3%4) assembly=%5 annotate=%6 other=%7")
        .arg(p.r)
        .arg(p.p)
        .arg(p.rawPFound ? QString::number(p.rawP) : QStringLiteral("absent"))
        .arg(p.encrypted ? QString() : QStringLiteral(", not encrypted"))
        .arg(p.modifyAssembly)
        .arg(p.modifyAnnotation)
        .arg(p.modifyOther);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // ---- what a delete request means, with no document anywhere near it ----
    //
    // The arithmetic the dialog shows while the range is being typed, so the two
    // numbers a reader acts on -- how many go, how many are left -- come from the
    // function that also decides whether OK is pressable. `live` is spelled out
    // here because that is the whole point of it: a request means something against
    // the pages that are still there, and the dialog and the editor have to agree
    // about which those are.
    using Plan = DocumentEditor::DeletePlan;
    {
        const Plan simple = DocumentEditor::planDelete(everyRef(10), {2, 3, 4});
        check(simple.pagesToRemove == 3 && simple.pagesRemaining == 7 && !simple.leavesNothing,
              "three pages out of ten leaves seven");

        const Plan repeated = DocumentEditor::planDelete(everyRef(10), {2, 3, 2, 99, -1});
        check(repeated.pagesToRemove == 2 && repeated.pagesRemaining == 8,
              "a page named twice comes out once, and a number outside the document names nothing");

        const Plan all = DocumentEditor::planDelete(everyRef(3), {0, 1, 2});
        check(all.pagesToRemove == 3 && all.pagesRemaining == 0 && all.leavesNothing,
              "naming every page is the request that cannot be granted");

        const Plan none = DocumentEditor::planDelete(everyRef(3), {});
        check(none.pagesToRemove == 0 && !none.leavesNothing,
              "naming no page is a different problem from naming every one");

        const Plan oneLeft = DocumentEditor::planDelete(everyRef(3), {0, 2});
        check(oneLeft.pagesToRemove == 2 && oneLeft.pagesRemaining == 1 && !oneLeft.leavesNothing,
              "leaving exactly one page is allowed -- it is the smallest document there is");

        // The dialog's "these are already gone" sentence, and the count behind it:
        // pages 0 and 1 are not in the document any more, so a second request that
        // names them removes nothing -- but it is not the same nothing as naming no
        // page at all, which is why alreadyGone is its own number.
        const Plan halfGone = DocumentEditor::planDelete(onScreen({2, 3, 4}), {0, 1, 3});
        check(halfGone.pagesToRemove == 1 && halfGone.alreadyGone == 2
                && halfGone.pagesRemaining == 2,
              "pages an earlier change took out are counted separately from pages never named");

        const Plan allGone = DocumentEditor::planDelete(onScreen({2, 3}), {0, 1});
        check(allGone.pagesToRemove == 0 && allGone.alreadyGone == 2 && !allGone.leavesNothing,
              "naming only gone pages removes nothing, and says why rather than \"you named nothing\"");

        // Naming every page that is left is the ungrantable one, even when the
        // document has already shrunk: what matters is the count going out.
        const Plan restOfThem = DocumentEditor::planDelete(onScreen({2, 3}), {2, 3});
        check(restOfThem.pagesToRemove == 2 && restOfThem.leavesNothing,
              "the second delete that empties the document is refused as surely as the first");

        // A merge is what made these counts worth re-reading: the list on the left
        // now holds pages the screen never showed, and they are counted in what is
        // left but are unreachable from the numbers typed on the right. The dialog
        // has to say so, because "剩下 40 页" beside a 40-page document that is
        // losing three is otherwise a sentence a reader can check and find wrong.
        const Plan pastTheEnd = DocumentEditor::planDelete(
            everyRef(3) << Ref{1, 0} << Ref{1, 1}, {0, 1, 2});
        check(pastTheEnd.pagesToRemove == 3 && pastTheEnd.pagesRemaining == 2
                && !pastTheEnd.leavesNothing,
              "deleting every page of the opened file is not deleting every page of the "
              "document, once a merge has put two more into it");

        const Plan imported = DocumentEditor::planDelete(everyRef(3) << Ref{1, 0}, {0});
        check(imported.pagesToRemove == 1 && imported.pagesRemaining == 3,
              "and the page that came in from another file is in that remaining count, but "
              "is not reachable from the numbers a reader can type");
    }

    // ---- what a move request means, with no document anywhere near it ----
    //
    // The same arithmetic the dialog shows and the save replays, run against a
    // `present` list written out by hand. Every one of these orders is the whole
    // answer rather than a count, because the thing a move can get wrong is which
    // page sits where.
    using Move = DocumentEditor::MoveTarget;
    {
        const DocumentEditor::MovePlan toBack = DocumentEditor::planMove(everyRef(10), {2, 3, 4},
                                                                        Move::Back);
        check(toBack.pagesMoved == 3 && asNumbers(toBack.order).last() == 4 && toBack.landsAt == 7
                && toBack.changesOrder && toBack.usable(),
              QStringLiteral("three pages moved to the back land at 8, 9, 10: %1")
                  .arg(describeOrder(asNumbers(toBack.order))));

        const DocumentEditor::MovePlan toFront = DocumentEditor::planMove(everyRef(10), {7, 8},
                                                                         Move::Front);
        check(asNumbers(toFront.order).front() == 7 && asNumbers(toFront.order).at(1) == 8
                && asNumbers(toFront.order).at(2) == 0 && toFront.landsAt == 0,
              QStringLiteral("the block keeps its own internal order at the front: %1")
                  .arg(describeOrder(asNumbers(toFront.order))));

        // Typed the other way round, and it still comes out 2,3,4: the numbers name
        // which pages travel, not the order they arrive in.
        const DocumentEditor::MovePlan typedBackwards = DocumentEditor::planMove(
            everyRef(10), {4, 2, 3}, Move::Back);
        check(asNumbers(typedBackwards.order).mid(7) == QVector<int>({2, 3, 4}),
              QStringLiteral("naming them out of order does not shuffle them: %1")
                  .arg(describeOrder(asNumbers(typedBackwards.order))));

        const DocumentEditor::MovePlan behind = DocumentEditor::planMove(everyRef(10), {0, 1},
                                                                        Move::AfterPage, 4);
        check(asNumbers(behind.order) == QVector<int>({2, 3, 4, 0, 1, 5, 6, 7, 8, 9})
                && behind.landsAt == 3 && behind.anchorUsed == 4 && !behind.anchorWasGone,
              QStringLiteral("the first two pages move behind the fifth, which is page 5 on the "
                             "screen and index 4 here: %1")
                  .arg(describeOrder(asNumbers(behind.order))));

        // Three ways to ask for the order the document already has, and each one has
        // to come out false so that the button greys and nothing is recorded.
        check(!DocumentEditor::planMove(everyRef(5), {3, 4}, Move::Back).changesOrder,
              "the last two pages are already at the back");
        check(!DocumentEditor::planMove(everyRef(5), {0}, Move::Front).changesOrder,
              "the first page is already at the front");
        check(!DocumentEditor::planMove(everyRef(5), {2}, Move::AfterPage, 1).changesOrder,
              "page 3 is already behind page 2");

        const DocumentEditor::MovePlan ontoItself = DocumentEditor::planMove(everyRef(5), {1, 2},
                                                                            Move::AfterPage, 2);
        check(ontoItself.anchorIsMoved && !ontoItself.usable(),
              "moving a block behind a page inside itself is not a move, and says which page "
              "makes it that way");

        const DocumentEditor::MovePlan namedNothing = DocumentEditor::planMove(everyRef(5), {},
                                                                              Move::Back);
        check(namedNothing.pagesMoved == 0 && !namedNothing.changesOrder && !namedNothing.usable()
                && asNumbers(namedNothing.order) == everyPage(5),
              "naming no page moves nothing, and the order it hands back is the one it was given");

        const DocumentEditor::MovePlan repeats = DocumentEditor::planMove(everyRef(10), {2, 2, 3},
                                                                         Move::Back);
        check(repeats.pagesMoved == 2 && asNumbers(repeats.order).mid(8) == QVector<int>({2, 3}),
              QStringLiteral("a page named twice travels once: %1")
                  .arg(describeOrder(asNumbers(repeats.order))));

        // The two questions a page number raises once unsaved changes exist are
        // separate: is it still there, and where does the anchor land if it is not.
        const DocumentEditor::MovePlan gonePages = DocumentEditor::planMove(onScreen({2, 3, 4}),
                                                                           {0, 1, 3}, Move::Back);
        check(gonePages.pagesMoved == 1 && gonePages.alreadyGone == 2
                && asNumbers(gonePages.order) == QVector<int>({2, 4, 3}),
              "pages an earlier change deleted are counted apart from the one still there");

        const DocumentEditor::MovePlan goneAnchor = DocumentEditor::planMove(
            onScreen({0, 1, 2, 4, 5}), {5}, Move::AfterPage, 3);
        check(goneAnchor.anchorWasGone && goneAnchor.anchorUsed == 4
                && asNumbers(goneAnchor.order) == QVector<int>({0, 1, 2, 5, 4})
                && goneAnchor.usable(),
              QStringLiteral("an anchor a delete took out becomes the gap it left, in front of "
                             "the page that is now there: %1")
                  .arg(describeOrder(asNumbers(goneAnchor.order))));

        const DocumentEditor::MovePlan anchorAtEnd = DocumentEditor::planMove(onScreen({0, 1, 2}),
                                                                             {0}, Move::AfterPage, 5);
        check(anchorAtEnd.anchorWasGone && anchorAtEnd.anchorUsed == -1
                && asNumbers(anchorAtEnd.order) == QVector<int>({1, 2, 0}),
              "and an anchor past the last surviving page is the same answer as moving to the end");

        // A block of imported pages: they travel as a block and keep their own order,
        // but no number a reader types reaches them, so this is the undo-and-move case
        // rather than a dialog. The anchor on the right is still a page of the opened
        // document -- the imported pages in the list do not shift what 第 N 页 means.
        const DocumentEditor::MovePlan importedBlock = DocumentEditor::planMove(
            QVector<Ref>() << Ref{1, 0} << Ref{1, 1} << Ref{0, 0} << Ref{0, 1},
            {}, Move::AfterPage, 0);
        check(importedBlock.pagesMoved == 0 && !importedBlock.changesOrder,
              "naming no page of the opened document moves nothing, even with imported pages "
              "in front");
        const DocumentEditor::MovePlan behindTheImported = DocumentEditor::planMove(
            QVector<Ref>() << Ref{0, 0} << Ref{1, 0} << Ref{1, 1} << Ref{0, 1},
            {0}, Move::AfterPage, 1);
        check(behindTheImported.usable()
                && asNumbers(behindTheImported.order) == QVector<int>({100, 101, 1, 0}),
              QStringLiteral("page 1 moves behind page 2, which is sitting behind two imported "
                             "ones, so it lands after them: %1")
                  .arg(describeOrder(asNumbers(behindTheImported.order))));
    }

    // ---- what a merge request means, with no document anywhere near it ----
    //
    // The same shape as the two blocks above, and the same reason for existing: the
    // dialog's summary and the save's replay both go through planMerge(), so the place
    // a block of pages lands is worked out once. That is the whole point of the
    // landing spot being shared with planMove() -- "第 12 页之后" is one place in this
    // program, not one place per operation.
    {
        const DocumentEditor::MergePlan atBack = DocumentEditor::planMerge(
            everyRef(5), {0, 1, 2}, 1, Move::Back);
        check(atBack.pagesAdded == 3 && atBack.landsAt == 5 && atBack.usable()
                && asNumbers(atBack.order) == QVector<int>({0, 1, 2, 3, 4, 100, 101, 102}),
              QStringLiteral("three pages from the other file join the end, in the order that "
                             "file has them: %1")
                  .arg(describeOrder(asNumbers(atBack.order))));

        const DocumentEditor::MergePlan atFront = DocumentEditor::planMerge(
            everyRef(5), {1}, 2, Move::Front);
        check(atFront.pagesAdded == 1 && atFront.landsAt == 0
                && asNumbers(atFront.order) == QVector<int>({201, 0, 1, 2, 3, 4}),
              QStringLiteral("one page from a second merged file goes to the very front: %1")
                  .arg(describeOrder(asNumbers(atFront.order))));

        // Typed the other way round in the range box, and it still arrives 1 then 3:
        // the numbers say which pages come, not the order they come in.
        check(asNumbers(DocumentEditor::planMerge(everyRef(2), {3, 1}, 1, Move::Back).order)
                  == QVector<int>({0, 1, 101, 103}),
              "naming them out of order does not shuffle the block, either here");

        const DocumentEditor::MergePlan behind = DocumentEditor::planMerge(
            everyRef(5), {0}, 1, Move::AfterPage, 2);
        check(behind.landsAt == 3 && behind.anchorUsed == 2 && !behind.anchorWasGone
                && asNumbers(behind.order) == QVector<int>({0, 1, 2, 100, 3, 4}),
              QStringLiteral("the merge lands behind the third page, which is where a move "
                             "behind it would have gone: %1")
                  .arg(describeOrder(asNumbers(behind.order))));

        // The anchor a delete left behind, asked of a merge rather than of a move. It
        // has to come to the same place, or the two dialogs would be pointing at
        // different gaps with the same sentence.
        const DocumentEditor::MergePlan goneAnchor = DocumentEditor::planMerge(
            onScreen({0, 1, 2, 4, 5}), {0}, 1, Move::AfterPage, 3);
        check(goneAnchor.anchorWasGone && goneAnchor.anchorUsed == 4
                && asNumbers(goneAnchor.order) == QVector<int>({0, 1, 2, 100, 4, 5}),
              "a merged block goes into the gap a deleted anchor left, exactly where a move "
              "would have put it");

        // Repeats and out-of-range numbers cannot reach this function from a dialog --
        // the range box is bounded by the other file's own page count -- but a merge of
        // one page named twice is one page, and that is the only thing that varies.
        check(DocumentEditor::planMerge(everyRef(3), {2, 2}, 1, Move::Back).pagesAdded == 1,
              "a page named twice comes in once");

        // The one case with no counterpart in a move: a merge never takes anything out,
        // so the document cannot be emptied by one, and nothing here refuses a merge of
        // zero pages except the dialog's greyed button. planMerge hands back the list it
        // was given.
        const DocumentEditor::MergePlan namedNothing = DocumentEditor::planMerge(
            everyRef(3), {}, 1, Move::Back);
        check(namedNothing.pagesAdded == 0 && !namedNothing.usable()
                && asNumbers(namedNothing.order) == everyPage(3),
              "naming no page adds nothing, and the order it hands back is the one it was given");

        // Merging into a document that already holds imported pages: the anchor is a
        // page of the opened file, so it resolves against that file's own order rather
        // than against the position the imported pages happen to sit in.
        const DocumentEditor::MergePlan secondMerge = DocumentEditor::planMerge(
            QVector<Ref>() << Ref{0, 0} << Ref{1, 0} << Ref{0, 1}, {0}, 2, Move::AfterPage, 0);
        check(secondMerge.landsAt == 1
                && asNumbers(secondMerge.order) == QVector<int>({0, 200, 100, 1}),
              QStringLiteral("behind page 1 means behind it, with the earlier merge's page "
                             "pushed along: %1")
                  .arg(describeOrder(asNumbers(secondMerge.order))));
    }

    // ---- whether two paths name one file ----
    {
        check(DocumentEditor::sameFile(QStringLiteral("CMakeLists.txt"),
                                       QFileInfo(QStringLiteral("CMakeLists.txt"))
                                           .absoluteFilePath()),
              "a path and its own absolute form are the same file");
        const QString absolute = QFileInfo(QStringLiteral("CMakeLists.txt")).absoluteFilePath();
        check(DocumentEditor::sameFile(absolute, absolute.toUpper()),
              "and the compare does not care about case, because Windows does not either");
        check(DocumentEditor::sameFile(absolute,
                                       QString(absolute).replace(QLatin1Char('/'),
                                                                QLatin1Char('\\'))),
              "nor about which way the separator leans");
        check(!DocumentEditor::sameFile(absolute, QStringLiteral("CMakeCache.txt")),
              "a different name in the same folder is a different file");
        check(!DocumentEditor::sameFile(QString(), absolute), "and an empty path is nobody's file");
    }

    // ---- the round trip ----
    const QString fixture = QStringLiteral("tests/fixtures/many-pages.pdf");
    if (!QFile::exists(fixture)) {
        std::cerr << "FAIL: the fixture this test needs is not there\n";
        return 1;
    }
    QTemporaryDir scratch;
    check(scratch.isValid(), "the test gets a temporary directory of its own");
    if (!scratch.isValid())
        return 1;

    const QString originalHash = hashOfFile(fixture);
    const QString source = scratch.filePath(QStringLiteral("source.pdf"));
    check(QFile::copy(fixture, source), "the document worked on is a copy in the scratch folder");
    const int pagesBefore = pagesAsReadByTheReader(source);
    check(pagesBefore > 3, "and it has enough pages to take two out of");

    {
        DocumentEditor editor;
        check(editor.pageCount() == 0, "an editor that has not opened anything holds no pages");
        QString error;
        // The call is hoisted out of check()'s second argument wherever that
        // argument quotes the error: arguments are built before the call inside
        // them has run, so writing check(f(&error), "...%1".arg(error)) prints an
        // empty explanation for a failure it is reporting.
        const bool opened = editor.open(source, QString(), &error);
        check(opened, QStringLiteral("the editor opens the copy: %1").arg(error));
        check(editor.pageCount() == pagesBefore,
              "and counts its pages the way the reader counts them");

        // Refused before a byte is written, and refused by name rather than by
        // accident: a save dialog is entirely capable of handing back the file it
        // was seeded with.
        error.clear();
        check(!editor.saveAs(source, &error), "saving back over the document it opened is refused");
        check(!error.isEmpty(), "and the refusal says why, rather than returning false silently");
        check(hashOfFile(source) == originalHash, "the file it tried to overwrite is unchanged");

        error.clear();
        check(!editor.deletePages(everyPage(pagesBefore), &error) && !error.isEmpty(),
              "deleting every page is refused, with a sentence");
        check(editor.pageCount() == pagesBefore, "and the refusal changed nothing");

        error.clear();
        const bool removed = editor.deletePages({2, 4}, &error);
        check(removed, QStringLiteral("deleting two named pages is accepted: %1").arg(error));
        check(editor.pageCount() == pagesBefore - 2, "and the document in memory is two pages short");

        // ---- the list, moved back and forth ----
        //
        // Undo and redo are moves between two lists, so what is being checked here
        // is not "does the page come back" -- it is that nothing else moves with it:
        // the page count after a redo has to be the count before the undo, and the
        // save below has to write the same file it would have written had nobody
        // pressed anything.
        check(editor.isDirty() && editor.pendingCount() == 1,
              "one operation is pending, and the tab would say so");
        check(editor.canUndo() && !editor.canRedo(), "there is something to undo and nothing to redo");
        check(!editor.undoDescription().isEmpty() && !editor.pendingSummary().isEmpty(),
              "and the list can name what it would take back");
        check(editor.undo(), "undo gives back the last operation");
        check(editor.pageCount() == pagesBefore, "which puts both pages back where they were");
        check(!editor.isDirty() && !editor.canUndo() && editor.canRedo(),
              "an undone list is an empty list: the star goes and there is something to redo");
        check(editor.redo(), "redo takes it back out again");
        check(editor.pageCount() == pagesBefore - 2, "and the count is the one before the undo");

        // A new operation after an undo throws the undone branch away, which is the
        // convention everywhere else in computing and the one that keeps two
        // histories from disagreeing about what the document is.
        check(editor.undo(), "setup: the delete is taken back once more");
        check(editor.rotatePages({1}, 1, &error), "rotating a page while the delete is undone");
        check(!editor.canRedo(), "and the redo branch is gone, because the list went another way");
        check(editor.undo(), "setup: the rotation is taken back too");
        check(editor.redo(), "setup: and put back");
        check(editor.pageCount() == pagesBefore, "a rotation does not change how many pages there are");
        check(editor.undo(), "setup: back to nothing pending");
        check(editor.deletePages({2, 4}, &error), "setup: the list is the one the save writes");

        const QString result = scratch.filePath(QStringLiteral("two-shorter.pdf"));
        QElapsedTimer clock;
        clock.start();
        error.clear();
        const bool wrote = editor.saveAs(result, &error);
        check(wrote, QStringLiteral("and the new file is written: %1").arg(error));
        note(QStringLiteral("%1 pages, a %2 KB document: open + delete + write took %3 ms")
                 .arg(pagesBefore)
                 .arg(QFileInfo(source).size() / 1024)
                 .arg(clock.elapsed()));

        check(pagesAsReadByTheReader(result) == pagesBefore - 2,
              "the reader counts the same number of pages in the file the editor wrote");
        QtPdfBackend written;
        if (written.load(result, QString()) == Document::Status::Ready) {
            const QImage image = written.render(0, QSize(300, 400), RenderOptions{});
            check(!image.isNull() && paintsAnything(image),
                  "and the first page of it still has something on it");
        } else {
            check(false, "the reader opens the file the editor wrote");
        }

        // The two things this feature is not allowed to do, checked at the end
        // rather than assumed throughout.
        check(hashOfFile(source) == originalHash, "the document that was edited is unchanged");
        check(hashOfFile(fixture) == originalHash, "and so is the fixture it was copied from");
    }

    // ---- a page number means the page on the screen ----
    //
    // The one arithmetic mistake this design can make, and the reason operations
    // store original page numbers rather than positions: a reader who deleted the
    // first two pages and then types "3" is pointing at the third page of what they
    // can still see, not at the third page of the document a save will write. Had
    // the number been taken as a position, the checks below would come out shifted
    // by exactly the pages that had already gone -- a page deleted that nobody
    // named, and a page kept that somebody did.
    //
    // The fixture prints its own page number on each page, which is what makes this
    // answerable at all: the alternative is a page count, and a page count says the
    // right number of pages went out without saying which.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens again");
        check(editor.deletePages({0, 1}, &error), "the first delete takes the two pages at the front");
        check(editor.rotatePages({2, 3}, 1, &error), "then the third and fourth are turned right");
        check(editor.deletePages({3}, &error), "and the fourth is taken out, by the number on the screen");
        check(editor.rotatePages({5}, 3, &error), "and the sixth, which is further down still, is turned left");

        const QString out = scratch.filePath(QStringLiteral("original-numbers.pdf"));
        error.clear();
        check(editor.saveAs(out, &error),
            QStringLiteral("the whole list is written in one go: %1").arg(error));

        QtPdfBackend written;
        check(written.load(out, QString()) == Document::Status::Ready, "and the result opens");
        check(written.pageCount() == pagesBefore - 3,
              "three pages went out of the forty, and no more");
        // What is left, in order, is pages 3, 5, 6 … 40: page 4 was named for
        // deletion after pages 1 and 2 went, and it is the page the reader was
        // looking at when they typed it.
        check(written.text(0).simplified().contains(QStringLiteral("page 3")),
              "the first page of the result is the third page of the original");
        check(written.text(1).simplified().contains(QStringLiteral("page 5")),
              "and the one after it skips the page that was named second, not the one that "
              "happened to sit at that position by then");
        check(written.text(2).simplified().contains(QStringLiteral("page 6")),
              "and the third page of the result is the sixth of the original, which was "
              "numbered against the screen it was named on, not against the list");

        // Qt PDF folds a page's own /Rotate into the size it reports (see
        // pageSize()'s note), so a turned page comes back landscape and this needs
        // no second parser to tell which way the file says the page is.
        check(written.pageSize(0).width() > written.pageSize(0).height(),
              "and the turn landed on the page that was named");
        check(written.pageSize(1).width() < written.pageSize(1).height(),
              "while the page beside it, which was never named, is still upright");

        // The direction, which the size above cannot see: a right turn and a left
        // one both make a portrait page landscape, and a reader who asked for one
        // and got the other has a document whose pages lie the wrong way round
        // until somebody turns them back.
        const QVector<int> angles = rotationsInFile(out);
        check(angles.size() == written.pageCount(), "the file names a rotation for every page");
        if (angles.size() == written.pageCount()) {
            check(angles.at(0) == 90, "the page turned right says 90");
            check(angles.at(1) == 0, "the untouched page beside it says nothing");
            check(angles.at(2) == 270, "the one turned left says 270, which is what a left turn is");
        }
        check(pagesAsReadByTheReader(source) == pagesBefore,
              "and reading the result never changed the document it was made from");
    }

    // ---- a move writes the order it promised ----
    //
    // The one operation in this program whose failure is silent in every other
    // measurement: a wrongly reordered document has the right number of pages in
    // it and opens fine. So the order is read back off the page labels the fixture
    // prints, and it is read back after each of undo and redo as well -- an undo
    // that moved the wrong block, or a redo that did not move it back, would
    // otherwise leave this file looking exactly like the one before it.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens again");
        check(editor.deletePages({9}, &error),
              "page 10 goes out first, so the numbering the moves use has a hole in it");
        check(editor.pageCount() == pagesBefore - 1, "setup: and the document is one page short");

        error.clear();
        check(!editor.movePages({0}, DocumentEditor::MoveTarget::Front, -1, &error)
                && !error.isEmpty(),
              "moving the first page to the front is refused, with a sentence");
        check(!editor.movePages({2, 3}, DocumentEditor::MoveTarget::AfterPage, 2, &error),
              "moving a block behind a page that is inside the block is refused too");
        error.clear();
        check(!editor.movePages({5}, DocumentEditor::MoveTarget::AfterPage, pagesBefore, &error)
                && !error.isEmpty(),
              "and so is an anchor past the last page of the file -- there is no such page to "
              "move behind, which is a different thing from the page 10 this list has deleted");
        check(!editor.movePages({}, DocumentEditor::MoveTarget::Back, -1, &error),
              "naming no page is refused as surely as naming an impossible one");
        check(editor.pendingCount() == 1, "and not one of the four refusals was recorded");

        error.clear();
        check(editor.movePages({2, 3}, DocumentEditor::MoveTarget::AfterPage, 0, &error),
            QStringLiteral("pages 3 and 4 move behind page 1: %1").arg(error));
        check(editor.pageCount() == pagesBefore - 1, "a move changes no page count at all");
        check(editor.pendingCount() == 2 && editor.isDirty(), "and it is the second thing on the list");
        check(editor.undoDescription().contains(QStringLiteral("第 1 页")),
              QStringLiteral("the undo item names the anchor the reader used: %1")
                  .arg(editor.undoDescription()));
        check(editor.movePages({8}, DocumentEditor::MoveTarget::Front, -1, &error),
              "then page 9 goes to the very front");

        const QString out = scratch.filePath(QStringLiteral("moved.pdf"));
        error.clear();
        check(editor.saveAs(out, &error),
            QStringLiteral("the whole list is written in one go: %1").arg(error));
        QVector<int> wanted;
        wanted << 9 << 1 << 3 << 4 << 2 << 5 << 6 << 7 << 8;
        for (int label = 11; label <= pagesBefore; ++label)
            wanted.append(label);
        const QVector<int> written = orderInFile(out);
        check(written == wanted,
              QStringLiteral("the file reads back as the two moves and one delete asked for: got "
                             "%1, wanted %2")
                  .arg(describeOrder(wanted), describeOrder(written)));

        // Undo is not "the count goes back" here; it is the block going home.
        check(editor.undo(), "the move to the front is taken back");
        const QString undoneFile = scratch.filePath(QStringLiteral("moved-one-back.pdf"));
        check(editor.saveAs(undoneFile, &error), "and the shorter list writes again");
        QVector<int> wantedUndone;
        wantedUndone << 1 << 3 << 4 << 2 << 5 << 6 << 7 << 8 << 9;
        for (int label = 11; label <= pagesBefore; ++label)
            wantedUndone.append(label);
        const QVector<int> undone = orderInFile(undoneFile);
        check(undone == wantedUndone,
              QStringLiteral("with only the second move gone, page 9 is back in the middle and "
                             "nothing else shifted: got %1, wanted %2")
                  .arg(describeOrder(wantedUndone), describeOrder(undone)));

        check(editor.redo(), "redo puts the move back on the list");
        const QString redoneFile = scratch.filePath(QStringLiteral("moved-redone.pdf"));
        check(editor.saveAs(redoneFile, &error), "and it writes");
        check(orderInFile(redoneFile) == written,
              "the redone document is the same order as the one before the undo");

        // Two moves whose blocks overlap, which is the case a position-based design
        // gets wrong: the second one names a page the first one relocated, and the
        // number the reader typed is still the number on the screen.
        DocumentEditor twice;
        check(twice.open(source, QString(), &error), "setup: a fresh editor, nothing deleted");
        check(twice.movePages({0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "the first two pages go to the end");
        check(twice.movePages({0}, DocumentEditor::MoveTarget::AfterPage, 3, &error),
              "then page 1, which is now near the end, is asked for again -- behind page 4");
        const QString twiceOut = scratch.filePath(QStringLiteral("moved-twice.pdf"));
        check(twice.saveAs(twiceOut, &error), "and the two are written together");
        // Page 1 came back from the end to sit behind page 4, page 2 -- which was
        // moved to the end by the first request and never named by the second --
        // stays there, and everything between them is what it always was.
        QVector<int> wantedTwice;
        wantedTwice << 3 << 4 << 1;
        for (int label = 5; label <= pagesBefore; ++label)
            wantedTwice.append(label);
        wantedTwice << 2;
        const QVector<int> twiceWritten = orderInFile(twiceOut);
        check(twiceWritten == wantedTwice,
              QStringLiteral("the second move found page 1 where the first one left it, and the "
                             "two pages that went to the end stay in their own order: got %1, "
                             "wanted %2")
                  .arg(describeOrder(wantedTwice), describeOrder(twiceWritten)));

        check(hashOfFile(source) == originalHash, "and none of this touched the document");
    }

    // ---- a merge writes in the pages it promised, out of a file it never opened ----
    //
    // This is the operation whose failure cannot be caught by a page count, because a
    // merge is the one thing here that changes the count: a wrongly copied page looks
    // like a right one from across the room. So every case below reads the written file
    // back through the reader and asks each page what its own label says -- which also
    // proves the handoff between the library that copies a page out of one file and the
    // one that displays it, since these fixtures print their number inside the page.
    //
    // The second document is written here by deleting pages out of the first, so the
    // labels the two files share are known without adding a fixture to the folder every
    // other test globs.
    {
        const QString donor = scratch.filePath(QStringLiteral("donor.pdf"));
        QString error;
        check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
              QStringLiteral("setup: a second document, holding pages 5, 6 and 7 of the "
                             "first: %1").arg(error));
        const QString donorHash = hashOfFile(donor);

        int importPages = 0;
        error.clear();
        check(DocumentEditor::inspectImport(donor, &importPages, &error) && importPages == 3,
              QStringLiteral("and looking one over says how many pages it has: %1").arg(error));

        // Refused at the moment the file is chosen, which is the decision made in this
        // slice: no password box here, and the sentence that says which file and why is
        // the whole of what a reader gets to act on.
        const QString locked = scratch.filePath(QStringLiteral("locked-donor.pdf"));
        check(writeEncryptedCopy(source, locked, QStringLiteral("hunter2"), true).isEmpty(),
              "setup: a donor that needs a password to open");
        QString complaint;
        int lockedPages = -1;
        check(!DocumentEditor::inspectImport(locked, &lockedPages, &complaint)
                && lockedPages == 0
                && complaint.contains(QStringLiteral("口令"))
                && complaint.contains(QStringLiteral("locked-donor.pdf")),
              QStringLiteral("a document that needs a password is refused, by name, before "
                             "anything is recorded: %1").arg(complaint));

        // A file merely marked not-to-modify is a different thing: it opens for anyone,
        // so it merges. The round trip below is what says whether the copy survives.
        const QString restricted = scratch.filePath(QStringLiteral("restricted-donor.pdf"));
        check(writeEncryptedCopy(source, restricted, QString(), false).isEmpty(),
              "setup: a donor marked not to be modified");
        complaint.clear();
        int restrictedPages = 0;
        check(DocumentEditor::inspectImport(restricted, &restrictedPages, &complaint)
                && restrictedPages == pagesBefore,
              QStringLiteral("and a file that only forbids editing is looked over fine: %1")
                  .arg(complaint));

        // ---- the whole donor at the back ----
        DocumentEditor editor;
        check(editor.open(source, QString(), &error),
              QStringLiteral("setup: the edited document opens: %1").arg(error));
        check(editor.importedPageCount() == 0, "and nothing has come in yet");
        error.clear();
        check(!editor.mergeFile(donor, {}, DocumentEditor::MoveTarget::Back, -1, &error)
                && !error.isEmpty(),
              "naming no page of the other file is refused, with a sentence");
        check(!editor.isDirty(), "and the refusal recorded nothing");
        error.clear();
        check(!editor.mergeFile(donor, {0, 1, 9}, DocumentEditor::MoveTarget::Back, -1, &error)
                && error.contains(QStringLiteral("donor.pdf")),
              QStringLiteral("a page the other file does not have is refused by its own number, "
                             "against that file's own length: %1").arg(error));
        check(!editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::AfterPage,
                                pagesBefore, &error),
              "and so is an anchor past the last page of the document on the screen");
        check(editor.pendingCount() == 0, "and none of the three refusals was recorded");

        error.clear();
        // Timed, because a merge is the first edit whose save opens a second file:
        // the "重放 + 整份写 is milliseconds" claim in docs/PLAN.md was measured with
        // deletions only, and this is the case that could have quietly doubled it.
        QElapsedTimer mergeClock;
        mergeClock.start();
        check(editor.mergeFile(donor, {0, 1, 2}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("the three pages of the donor come in at the end: %1").arg(error));
        check(editor.pageCount() == pagesBefore + 3,
              "and the document a save would write is three pages longer");
        check(editor.importedPageCount() == 3,
              "and the editor can say how many of those the screen has never shown");
        check(editor.undoDescription().contains(QStringLiteral("donor.pdf")),
              QStringLiteral("the undo item names which file: %1").arg(editor.undoDescription()));

        const QString merged = scratch.filePath(QStringLiteral("merged.pdf"));
        error.clear();
        check(editor.saveAs(merged, &error),
              QStringLiteral("and the merge is written into a new file: %1").arg(error));
        note(QStringLiteral("%1 pages plus %2 merged in, a %3 KB document: open + merge + write "
                            "took %4 ms")
                 .arg(pagesBefore)
                 .arg(3)
                 .arg(QFileInfo(source).size() / 1024)
                 .arg(mergeClock.elapsed()));
        QVector<int> wanted;
        for (int label = 1; label <= pagesBefore; ++label)
            wanted.append(label);
        wanted << 5 << 6 << 7;
        check(orderInFile(merged) == wanted,
              QStringLiteral("the file reads back as the forty pages then the donor's three, in "
                             "that order: got %1")
                  .arg(describeOrder(orderInFile(merged))));
        check(pagesAsReadByTheReader(merged) == pagesBefore + 3,
              "and the reader counts every one of them");

        // The donor was read from, and nothing more: the same promise the source has had
        // since the first test in this file, now extended to a second file on the same
        // save -- and the destination is refused for exactly this reason.
        check(hashOfFile(donor) == donorHash, "the file that was merged in is unchanged");
        check(hashOfFile(source) == originalHash, "and so is the one that was opened");
        error.clear();
        check(!editor.saveAs(donor, &error) && error.contains(QStringLiteral("donor.pdf")),
              QStringLiteral("saving over the file that was merged in is refused: %1").arg(error));
        check(hashOfFile(donor) == donorHash, "and the refusal left it alone");

        // Undo is the only way an imported page can be taken away, so it is checked the
        // same way the merge was: by what the file comes back holding.
        check(editor.undo(), "the merge is taken back");
        check(editor.pageCount() == pagesBefore && editor.importedPageCount() == 0,
              "and the document is the opened file again, page for page");
        const QString undoneFile = scratch.filePath(QStringLiteral("merged-undone.pdf"));
        check(editor.saveAs(undoneFile, &error), "and the shorter list writes");
        QVector<int> wantedUndone;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedUndone.append(label);
        check(orderInFile(undoneFile) == wantedUndone,
              QStringLiteral("nothing is left of the merge in the file: got %1")
                  .arg(describeOrder(orderInFile(undoneFile))));

        check(editor.redo(), "redo puts the merge back on the list");
        check(editor.importedPageCount() == 3, "and the three pages with it");
        const QString redoneFile = scratch.filePath(QStringLiteral("merged-redone.pdf"));
        check(editor.saveAs(redoneFile, &error), "and it writes");
        check(orderInFile(redoneFile) == wanted,
              "the redone document is the one the merge wrote the first time");

        // ---- a merged page in front, and a number that cannot reach it ----
        //
        // The consequence of numbering by the screen, checked where it would do the most
        // damage: a reader who deletes "page 1" after a merge means the first page they
        // can see, which is the first page of the file they opened -- not the imported
        // page that a save now happens to write in front of it. Had the number been taken
        // as a position in the saved document, the page that went out here would be one
        // nobody named, and the file would still have the right count.
        DocumentEditor front;
        check(front.open(source, QString(), &error), "setup: the copy opens again");
        check(front.mergeFile(donor, {0, 1, 2}, DocumentEditor::MoveTarget::Front, -1, &error),
              "the donor's three pages land at the very front");
        check(front.deletePages({0}, &error),
              "then page 1 is deleted -- which is the opened file's first page, not the "
              "imported one now sitting in front of it");
        check(front.pageCount() == pagesBefore + 2, "and the count says which of the two went");
        const QString frontOut = scratch.filePath(QStringLiteral("merged-front.pdf"));
        check(front.saveAs(frontOut, &error),
            QStringLiteral("and it is written: %1").arg(error));
        QVector<int> wantedFront;
        wantedFront << 5 << 6 << 7;
        for (int label = 2; label <= pagesBefore; ++label)
            wantedFront.append(label);
        check(orderInFile(frontOut) == wantedFront,
              QStringLiteral("the file holds the donor's three pages, then the opened document "
                             "from its second page: got %1")
                  .arg(describeOrder(orderInFile(frontOut))));
        check(front.deletePages({0}, &error) == false,
              "and asking again for the page that has gone says so rather than taking the "
              "imported one out instead");

        // ---- one page, twice ----
        //
        // The case a merge is free to make and no other operation is: the same page of
        // the same file twice in one document. It is not a bug to allow -- "把第 3 页再来
        // 一遍" is a thing a reader can mean -- so what is pinned is that the two copies
        // are two pages, and that undoing one leaves the other.
        DocumentEditor twice;
        check(twice.open(source, QString(), &error), "setup: the copy opens again");
        check(twice.mergeFile(donor, {1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "one page of the donor goes in");
        check(twice.mergeFile(donor, {1}, DocumentEditor::MoveTarget::AfterPage, 0, &error),
              "and the same page again, behind the first page of the opened document");
        check(twice.pendingCount() == 2, "two operations, one per merge");
        check(twice.importedPageCount() == 2, "two pages, the same page twice");
        const QString twiceOut = scratch.filePath(QStringLiteral("merged-twice.pdf"));
        check(twice.saveAs(twiceOut, &error),
            QStringLiteral("and the two are written together: %1").arg(error));
        QVector<int> wantedTwice;
        wantedTwice << 1 << 6;
        for (int label = 2; label <= pagesBefore; ++label)
            wantedTwice.append(label);
        wantedTwice << 6;
        check(orderInFile(twiceOut) == wantedTwice,
              QStringLiteral("page 6 of the donor appears twice, once behind page 1 and once at "
                             "the end, and they are separate pages: got %1")
                  .arg(describeOrder(orderInFile(twiceOut))));
        check(twice.undo(), "taking back the second merge");
        check(twice.importedPageCount() == 1, "leaves the first one where it was");
        const QString oneOut = scratch.filePath(QStringLiteral("merged-twice-one.pdf"));
        check(twice.saveAs(oneOut, &error), "and the file says so too");
        QVector<int> wantedOne;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedOne.append(label);
        wantedOne << 6;
        check(orderInFile(oneOut) == wantedOne,
              QStringLiteral("the surviving copy is the one at the end: got %1")
                  .arg(describeOrder(orderInFile(oneOut))));

        // ---- and a move that leans against one of the two copies ----
        //
        // Where an identity that only named the file would go wrong. Two pages from one
        // file with one number are the same reference, so looking one of them up answers
        // for whichever comes first -- and a move that inserts behind the second copy
        // lands it behind the first. Nothing downstream notices: the document has every
        // page in it and the right count, which is the failure class this file keeps
        // reading labels for.
        DocumentEditor lean;
        check(lean.open(source, QString(), &error), "setup: the copy opens again");
        check(lean.mergeFile(donor, {1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "the donor's second page goes in at the end");
        check(lean.mergeFile(donor, {1}, DocumentEditor::MoveTarget::AfterPage, 0, &error),
              "and that same page again, behind page 1");
        check(lean.movePages({1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "then page 2 of the opened document moves to the very end, behind both copies");
        const QString leanOut = scratch.filePath(QStringLiteral("merged-lean.pdf"));
        error.clear();
        check(lean.saveAs(leanOut, &error),
            QStringLiteral("and the three operations are written together: %1").arg(error));
        QVector<int> wantedLean;
        wantedLean << 1 << 6;
        for (int label = 3; label <= pagesBefore; ++label)
            wantedLean.append(label);
        wantedLean << 6 << 2;
        const QVector<int> leanWritten = orderInFile(leanOut);
        check(leanWritten == wantedLean,
              QStringLiteral("page 2 came to the end, behind the second copy of page 6 rather "
                             "than the first: got %1, wanted %2")
                  .arg(describeOrder(leanWritten), describeOrder(wantedLean)));

        // ---- the opened document merged into itself ----
        //
        // Nothing special about it to the copy path -- the pages are foreign objects in a
        // second handle on the same file -- and it is the merge a reader can most plausibly
        // ask for ("把这份的前两页也放到末尾"), so it is checked end to end, including the
        // refusal that has to come with it: the destination cannot be that same file.
        DocumentEditor itself;
        check(itself.open(source, QString(), &error), "setup: the copy opens again");
        check(itself.mergeFile(source, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "the document can be merged into itself");
        error.clear();
        check(!itself.saveAs(source, &error),
              "and saving it back over the file it opened is still refused");
        const QString itselfOut = scratch.filePath(QStringLiteral("merged-itself.pdf"));
        error.clear();
        check(itself.saveAs(itselfOut, &error),
            QStringLiteral("the doubled document writes elsewhere: %1").arg(error));
        QVector<int> wantedItself;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedItself.append(label);
        wantedItself << 1 << 2;
        check(orderInFile(itselfOut) == wantedItself,
              QStringLiteral("with the two pages at the end as well as where they were: got %1")
                  .arg(describeOrder(orderInFile(itselfOut))));
        check(hashOfFile(source) == originalHash, "and the file it read twice is unchanged");

        // ---- a file that changed after the merge was recorded ----
        //
        // The merge was promised against a document the reader could look at, and the save
        // reads it again. If it is no longer the file that was promised -- trimmed,
        // replaced, saved over by another program -- then the summary the reader acted on
        // names pages that may not exist, and the only honest answers are to refuse or to
        // say nothing at all. This is the case the up-front re-check exists for: the same
        // arithmetic planMerge() ran is run again over the real page tree at the end, and
        // the two have to agree before any bytes are written.
        DocumentEditor stale;
        check(stale.open(source, QString(), &error), "setup: the copy opens again");
        check(stale.mergeFile(donor, {0, 1, 2}, DocumentEditor::MoveTarget::Back, -1, &error),
              "setup: three pages of the donor are recorded");
        check(writeDonor(source, donor, pagesBefore, {4}, &error) == donor,
              "setup: and then the donor is replaced with a one-page version of itself");
        const QString staleOut = scratch.filePath(QStringLiteral("merged-stale.pdf"));
        error.clear();
        check(!stale.saveAs(staleOut, &error) && !error.isEmpty(),
              QStringLiteral("the save notices, and refuses rather than writing what it can: %1")
                  .arg(error));
        check(error.contains(QStringLiteral("donor.pdf")),
              QStringLiteral("and the refusal names which file stopped matching: %1").arg(error));
        check(!QFile::exists(staleOut), "and no file was written on the way to refusing");
        check(hashOfFile(source) == originalHash, "and the opened document is untouched");

        // Restoring the donor is enough: the list is replayed from the file every save,
        // so what a save writes is what the files hold now, not what they held when the
        // dialog closed.
        check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
              "setup: the donor is put back the way it was");
        error.clear();
        check(stale.saveAs(staleOut, &error),
            QStringLiteral("and the merge that was refused now writes: %1").arg(error));
        QVector<int> wantedStale;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedStale.append(label);
        wantedStale << 5 << 6 << 7;
        const QVector<int> staleWritten = orderInFile(staleOut);
        check(staleWritten == wantedStale,
              QStringLiteral("with the three pages it was promised holding: got %1")
                  .arg(describeOrder(staleWritten)));
        check(pagesAsReadByTheReader(donor) == 3,
              "and the donor is the three-page file it was again");

        // ---- merging out of a file that forbids being modified ----
        {
            DocumentEditor fromRestricted;
            check(fromRestricted.open(source, QString(), &error), "setup: the copy opens again");
            const bool recorded = fromRestricted.mergeFile(
                restricted, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error);
            note(QStringLiteral("merging two pages out of a document marked not-to-modify: %1")
                     .arg(recorded ? QStringLiteral("recorded") : error));
            if (recorded) {
                const QString out = scratch.filePath(QStringLiteral("merged-restricted.pdf"));
                error.clear();
                const bool wrote = fromRestricted.saveAs(out, &error);
                note(QStringLiteral("writing it: %1")
                         .arg(wrote ? QStringLiteral("succeeded") : error));
                if (wrote) {
                    QVector<int> labels = orderInFile(out);
                    check(labels.size() == pagesBefore + 2,
                          "and the copy the reader gets is the two documents' pages");
                    if (labels.size() == pagesBefore + 2) {
                        check(labels.at(pagesBefore) == 1 && labels.at(pagesBefore + 1) == 2,
                              "and the pages that came from the protected file are the pages "
                              "that were named, not a blank stand-in for them");
                    }
                }
            }
        }

        check(hashOfFile(source) == originalHash, "and none of the merge cases touched the "
                                                 "document that was opened");
    }

    // ---- a refused write leaves nothing behind ----
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens again");
        check(editor.deletePages({0}, &error), "setup: one page is taken out");
        const QString missing = scratch.filePath(QStringLiteral("no-such-folder/out.pdf"));
        error.clear();
        check(!editor.saveAs(missing, &error) && !error.isEmpty(),
              "a destination whose folder does not exist is refused, with a sentence");
        check(!QFile::exists(missing), "and no file was created on the way to refusing");
    }

    // ---- a destination that cannot be replaced keeps its own bytes ----
    //
    // The hazard is QPDFWriter having opened, and half-filled, a file the reader
    // chose to replace. The staged write is what makes that survivable, so it is
    // tested by occupying the destination: an open handle without FILE_SHARE_DELETE,
    // which is how Qt opens files.
    {
        const QString occupied = scratch.filePath(QStringLiteral("occupied.pdf"));
        {
            QFile seed(occupied);
            check(seed.open(QIODevice::ReadWrite), "a file is sitting at the destination");
            seed.write("this is not a pdf and must still be here afterwards");
            seed.flush();
        }
        const QString heldHash = hashOfFile(occupied);
        check(!heldHash.isEmpty(), "and it can be hashed before anything touches it");
        QFile holder(occupied);
        check(holder.open(QIODevice::ReadWrite), "which is then held open by this process");

        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens");
        check(editor.deletePages({0}, &error), "setup: one page is taken out");
        const bool saved = editor.saveAs(occupied, &error);
        holder.close();

        if (saved) {
            // Not a failure, and not something to assert either way on a guess:
            // whether Windows lets a file be replaced under an open handle depends
            // on sharing modes this process does not control. Recorded so that the
            // other branch is known to be the one being tested.
            note(QStringLiteral("renaming over an open handle succeeded, so the recovery "
                                "branch below did not run (a %1-byte file took its place)")
                     .arg(QFileInfo(occupied).size()));
            check(hashOfFile(occupied) != heldHash, "and the destination really was replaced");
        } else {
            check(!error.isEmpty(), "the failure says what happened");
            check(hashOfFile(occupied) == heldHash,
                  "the file that was there is byte for byte still there -- no half-written "
                  "replacement, which is what the staged write exists to prevent");
            const QStringList left =
                QDir(scratch.path()).entryList(QStringList() << QStringLiteral("xyzpdf-save-*.pdf"),
                                               QDir::Files);
            check(left.size() == 1, "the finished copy is kept, where the message can name it");
            if (left.size() == 1) {
                const QString kept = scratch.filePath(left.first());
                check(pagesAsReadByTheReader(kept) == pagesBefore - 1,
                      "and the copy that was kept is a whole document with the page out of it");
                check(error.contains(left.first()),
                      QStringLiteral("the message names the file it left behind: %1").arg(error));
                QFile::remove(kept);
            }
        }
    }

    // ---- encrypted documents, measured ----
    {
        // Each round trip below asserts that the copy's permission bits equal the
        // original's. Two numbers carried out here are what stop that from passing
        // vacuously: see the check at the end of the block.
        qint64 permissiveP = 0;
        qint64 restrictedP = 0;

        const QString locked = scratch.filePath(QStringLiteral("encrypted.pdf"));
        const QString complaint = writeEncryptedCopy(source, locked, QStringLiteral("hunter2"), true);
        check(complaint.isEmpty(),
              QStringLiteral("the password-protected test file could be written: %1").arg(complaint));
        check(pagesAsReadByTheReader(locked) == -1,
              "and the reader cannot open it without the password");

        DocumentEditor editor;
        QString error;
        const bool openedWithout = editor.open(locked, QString(), &error);
        note(QStringLiteral("opening a user-password file with no password: %1")
                 .arg(openedWithout ? QStringLiteral("succeeded") : error));
        check(!openedWithout, "the editor does not quietly read a file it was not given the key to");

        error.clear();
        const bool openedWith = editor.open(locked, QStringLiteral("hunter2"), &error);
        note(QStringLiteral("opening it with the password: %1")
                 .arg(openedWith ? QStringLiteral("succeeded") : error));
        if (openedWith) {
            check(editor.pageCount() == pagesBefore, "and the page count survives the key");
            check(editor.deletePages({1}, &error), "setup: a page comes out of the decrypted copy");
            const QString out = scratch.filePath(QStringLiteral("encrypted-result.pdf"));
            error.clear();
            const bool wrote = editor.saveAs(out, &error);
            note(QStringLiteral("writing an edited copy of an encrypted file: %1")
                     .arg(wrote ? QStringLiteral("succeeded") : error));
            if (wrote) {
                check(pagesAsReadByTheReader(out) == -1,
                      "the copy is still encrypted -- losing the protection on the way "
                      "through an edit would be worse than refusing the edit");
                check(pagesAsReadByTheReader(out, QStringLiteral("hunter2")) == pagesBefore - 1,
                      "and with the password it is the document that was asked for");

                // "Still encrypted" above only says Qt PDF could not open it without a
                // key, which a copy re-encrypted under some *other* key would satisfy
                // too. So the protection is compared as numbers. The raw read is there
                // because both files are parsed by the same library: a writer that
                // dropped the dictionary and a reader that defaulted to a value would
                // agree with each other and with itself.
                const Permissions before = readPermissions(locked, QStringLiteral("hunter2"));
                const Permissions after = readPermissions(out, QStringLiteral("hunter2"));
                note(QStringLiteral("permissions across the key-and-edit round trip: %1 -> %2")
                         .arg(describe(before), describe(after)));
                check(before.rawPFound && after.rawPFound && before.rawP == after.rawP,
                      "the copy's /P is the original's, out of the bytes and not out of "
                      "the same parser twice");
                check(after.encrypted && after.r == before.r && after.p == before.p,
                      "and QPDF agrees on R and P for both files");
                check(after.modifyAssembly == before.modifyAssembly
                        && after.modifyAnnotation == before.modifyAnnotation
                        && after.modifyOther == before.modifyOther,
                      "the three modify flags mean the same thing on both files");
                check(after.modifyOther, "this fixture does allow modification, and says so");
                permissiveP = after.rawP;
            } else {
                check(!QFile::exists(out), "a refused write left no file behind");
            }
        }

        const QString restricted = scratch.filePath(QStringLiteral("restricted.pdf"));
        const QString secondComplaint = writeEncryptedCopy(source, restricted, QString(), false);
        check(secondComplaint.isEmpty(),
              QStringLiteral("the restricted test file could be written: %1").arg(secondComplaint));
        check(pagesAsReadByTheReader(restricted) == pagesBefore,
              "the reader opens a restricted file as if nothing were wrong");

        DocumentEditor editor2;
        error.clear();
        const bool openedRestricted = editor2.open(restricted, QString(), &error);
        note(QStringLiteral("opening a file marked not-to-modify: %1")
                 .arg(openedRestricted ? QStringLiteral("succeeded") : error));
        if (openedRestricted) {
            check(editor2.deletePages({1}, &error), "setup: a page comes out");
            const QString out2 = scratch.filePath(QStringLiteral("restricted-result.pdf"));
            error.clear();
            const bool wrote = editor2.saveAs(out2, &error);
            note(QStringLiteral("writing an edited copy of a file marked not-to-modify: %1")
                     .arg(wrote ? QStringLiteral("succeeded") : error));
            if (wrote) {
                check(pagesAsReadByTheReader(out2) == pagesBefore - 1,
                      "the copy opens, with one page fewer than the original");
                check(pagesAsReadByTheReader(restricted) == pagesBefore,
                      "and the original is still the document it was");

                // The shape this case exists for: readable by anyone, "not to be
                // edited" written on it, and the reader who asks for that is the one
                // whose copy must not come back without the words.
                const Permissions before = readPermissions(restricted, QString());
                const Permissions after = readPermissions(out2, QString());
                note(QStringLiteral("not-to-modify across the round trip: %1 -> %2")
                         .arg(describe(before), describe(after)));
                check(before.rawPFound && after.rawPFound && before.rawP == after.rawP,
                      "the restricted copy carries the same /P as the restricted original");
                check(after.encrypted && after.r == before.r && after.p == before.p,
                      "same R, same P, still an encrypted document");
                check(after.modifyAssembly == before.modifyAssembly
                        && after.modifyAnnotation == before.modifyAnnotation
                        && after.modifyOther == before.modifyOther,
                      "and every modify flag is the one it was");
                check(!after.modifyOther,
                      "the copy still says the document may not be modified -- a round trip "
                      "that quietly drops that word would hand someone a file lying about "
                      "its own protection");
                restrictedP = after.rawP;
            } else {
                check(!QFile::exists(out2), "a refused write left no file behind");
            }
        }
        // The two fixtures differ by exactly one permission bit, and the numbers the
        // equality checks above compared differ by that same bit. A raw reader stuck at
        // zero, or one that always found the same integer, would satisfy every
        // before==after assertion in this block and fail here.
        check(permissiveP != restrictedP,
              QStringLiteral("the two fixtures disagree about modification (%1 vs %2), "
                             "so agreeing with itself is not what the checks above did")
                  .arg(permissiveP)
                  .arg(restrictedP));
        check(hashOfFile(source) == originalHash, "none of the encrypted cases touched the source");
    }

    // ---- the same round trip at a size a real document has ----
    //
    // The measurement above is on a 12 KB fixture, and a number from it that is
    // printed without the second one next to it says "this is instant", which is a
    // claim about a 40-page file rather than about the 500-page ones the delete
    // dialog will actually be pointed at. Same three calls, timed the same way.
    {
        const QString big = QStringLiteral("tests/fixtures/huge.pdf");
        if (!QFile::exists(big)) {
            note("tests/fixtures/huge.pdf is not there, so the scale measurement was skipped");
        } else {
            const QString bigFixtureHash = hashOfFile(big);
            const QString bigSource = scratch.filePath(QStringLiteral("big-source.pdf"));
            check(QFile::copy(big, bigSource), "setup: the large fixture was copied into the scratch folder");

            const QString bigResult = scratch.filePath(QStringLiteral("big-result.pdf"));
            const int bigPagesBefore = pagesAsReadByTheReader(bigSource);
            check(bigPagesBefore > 100, "and it is a document with enough pages to mean something");

            DocumentEditor editor;
            QString error;
            QElapsedTimer clock;
            clock.start();
            const bool done = editor.open(bigSource, QString(), &error)
                && editor.deletePages({0, bigPagesBefore / 2, bigPagesBefore - 1}, &error)
                && editor.saveAs(bigResult, &error);
            const qint64 taken = clock.elapsed();
            check(done, QStringLiteral("opening, taking three pages out of the big one and writing it: %1")
                            .arg(error));
            const int writtenPages = pagesAsReadByTheReader(bigResult);
            check(writtenPages == bigPagesBefore - 3,
                  "and the reader counts three pages fewer in the copy it wrote");
            note(QStringLiteral("%1 pages, %2 KB: open + delete + write took %3 ms")
                     .arg(bigPagesBefore)
                     .arg(QFileInfo(bigSource).size() / 1024)
                     .arg(taken));

            // The same at scale with a merge in it. This is the one worth the second
            // number: a deletion's save holds one document open, a merge's holds two,
            // and every page copied over brings its objects into the first one's tree.
            const QString bigDonor = scratch.filePath(QStringLiteral("big-donor.pdf"));
            check(writeDonor(bigSource, bigDonor, bigPagesBefore, {4, 5, 6}, &error) == bigDonor,
                  QStringLiteral("setup: a three-page donor cut out of the large fixture: %1")
                      .arg(error));

            const QString bigMerged = scratch.filePath(QStringLiteral("big-merged.pdf"));
            DocumentEditor merging;
            QElapsedTimer bigMergeClock;
            bigMergeClock.start();
            const bool mergedIn = merging.open(bigSource, QString(), &error)
                && merging.mergeFile(bigDonor, {0, 1, 2}, DocumentEditor::MoveTarget::Back, -1,
                                     &error)
                && merging.saveAs(bigMerged, &error);
            check(mergedIn,
                  QStringLiteral("opening, merging three pages into the big one and writing it: %1")
                      .arg(error));
            check(pagesAsReadByTheReader(bigMerged) == bigPagesBefore + 3,
                  "and the file it wrote is three pages longer, which is the merged three");
            note(QStringLiteral("%1 pages plus 3 merged in, %2 KB and a %3 KB donor: "
                                "open + merge + write took %4 ms")
                     .arg(bigPagesBefore)
                     .arg(QFileInfo(bigSource).size() / 1024)
                     .arg(QFileInfo(bigDonor).size() / 1024)
                     .arg(bigMergeClock.elapsed()));

            // And the same at scale for a split, which is the one save whose cost is a
            // multiple of everything above: a split replays and writes once per piece, so
            // this is the number that says whether "每 50 页一份" is a 12× save or a 3× one.
            const QString bigCut = scratch.filePath(QStringLiteral("big-cut.pdf"));
            DocumentEditor cutting;
            QElapsedTimer bigSplitClock;
            bigSplitClock.start();
            const bool cutUp = cutting.open(bigSource, QString(), &error)
                && cutting.splitInto(bigPagesBefore / 4, &error)
                && cutting.saveAs(bigCut, &error);
            check(cutUp,
                  QStringLiteral("opening, splitting the big one into four and writing it: %1")
                      .arg(error));
            const QVector<DocumentEditor::PieceRange> bigPieces = cutting.pieces();
            int acrossPieces = 0;
            for (int index = 0; index < bigPieces.size(); ++index)
                acrossPieces += pagesAsReadByTheReader(
                    DocumentEditor::piecePath(bigCut, index, bigPieces.size(), bigPieces.at(index)));
            check(acrossPieces == bigPagesBefore,
                  "and the four pieces together hold every page the document had, with none doubled");
            note(QStringLiteral("%1 pages, %2 KB: split four ways, four replays and four writes took "
                                "%3 ms")
                     .arg(bigPagesBefore)
                     .arg(QFileInfo(bigSource).size() / 1024)
                     .arg(bigSplitClock.elapsed()));

            check(hashOfFile(big) == bigFixtureHash, "and the large fixture is byte for byte what it was");
        }
    }

    // ---- 拆分: which pages go together, and what each file is called ----
    //
    // A split is the one operation in this class that changes no page, so none of the
    // checks above say anything about it. What is pinned down here is the set of files:
    // which pages land in one piece, what each piece is named, that the naming the dialog
    // showed is the naming the folder ends up with, and that a name already taken stops the
    // whole save rather than writing the pieces that happened to be free.
    {
        using Piece = DocumentEditor::PieceRange;
        const auto piece = [](int first, int last) {
            Piece range;
            range.first = first;
            range.last = last;
            return range;
        };
        const auto widths = [](const QVector<Piece> &pieces) {
            QStringList out;
            for (const Piece &one : pieces)
                out << QString::number(one.count());
            return out.join(QLatin1Char(','));
        };
        const auto bounds = [](const QVector<Piece> &pieces) {
            QStringList out;
            for (const Piece &one : pieces)
                out << QStringLiteral("%1-%2").arg(one.first).arg(one.last);
            return out.join(QLatin1Char(','));
        };
        const auto namesOf = [](const QString &base, const QVector<Piece> &pieces) {
            QStringList out;
            for (int index = 0; index < pieces.size(); ++index)
                out << QFileInfo(DocumentEditor::piecePath(base, index, pieces.size(),
                                                           pieces.at(index)))
                           .fileName();
            return out;
        };

        check(DocumentEditor::pieceRanges(10, 0).size() == 1,
              "no split pending still names one piece: the whole document");
        check(bounds(DocumentEditor::pieceRanges(10, 3)) == QStringLiteral("0-2,3-5,6-8,9-9"),
              "every third page: the last piece is what is left over, not another full one");
        check(widths(DocumentEditor::pieceRanges(10, 5)) == QStringLiteral("5,5"),
              "an exact multiple does not add an empty piece at the end");
        check(DocumentEditor::pieceRanges(10, 1).size() == 10,
              "one page per piece is ten pieces, the smallest split there is");
        check(DocumentEditor::pieceRanges(10, 10).size() == 1
                  && DocumentEditor::pieceRanges(10, 11).size() == 1,
              "a piece as big as the document, or bigger, comes out as one piece");
        check(DocumentEditor::pieceRanges(0, 3).isEmpty(),
              "and a document with no pages has no pieces either");

        // Contiguity over the shapes a remainder can take. Nothing here may drop a page or
        // put one page in two files, and "every piece starts where the last one ended, and
        // the two ends are the first and last page" is the whole claim.
        for (const int size : {3, 7, 39, 40}) {
            const QVector<Piece> pieces = DocumentEditor::pieceRanges(40, size);
            bool inOneRun = !pieces.isEmpty() && pieces.first().first == 0
                            && pieces.last().last == 39;
            int covered = 0;
            for (int index = 0; index < pieces.size(); ++index) {
                covered += pieces.at(index).count();
                if (index > 0 && pieces.at(index).first != pieces.at(index - 1).last + 1)
                    inOneRun = false;
            }
            check(inOneRun && covered == 40,
                  QStringLiteral("每 %1 页一份 covers pages 1 to 40 once each").arg(size));
        }

        // ---- the names ----
        check(DocumentEditor::piecePath(QStringLiteral("C:/docs/report.pdf"), 0, 3, piece(0, 14))
                  == QStringLiteral("C:/docs/report 01 (1-15).pdf"),
              "the first piece of three is 01 and says which pages it holds");
        check(DocumentEditor::piecePath(QStringLiteral("C:/docs/report.pdf"), 10, 12,
                                        piece(20, 20))
                  == QStringLiteral("C:/docs/report 11 (21).pdf"),
              "a one-page piece says 21, not 21-21");
        check(DocumentEditor::piecePath(QStringLiteral("D:/a/my.report.v2.pdf"), 2, 3,
                                        piece(54, 71))
                  == QStringLiteral("D:/a/my.report.v2 03 (55-72).pdf"),
              "dots inside a name are not the extension -- only the last one is");
        check(DocumentEditor::piecePath(QStringLiteral("D:/a/no-extension"), 0, 2, piece(0, 3))
                  == QStringLiteral("D:/a/no-extension 01 (1-4)"),
              "a destination typed with no extension gives pieces with no extension: the reader "
              "chose that for the name they typed, and this is not the place to second-guess it");

        // The padding is not decoration: a folder sorts by name, and pieces that read 1,
        // 10, 11, 2 are a page order nobody can check by eye.
        {
            const QVector<Piece> pieces = DocumentEditor::pieceRanges(60, 5);
            const QStringList ordered = namesOf(QStringLiteral("book.pdf"), pieces);
            QStringList sorted = ordered;
            std::sort(sorted.begin(), sorted.end());
            check(ordered == sorted,
                  QStringLiteral("twelve pieces of a 60-page book sort by name into page order: %1")
                      .arg(ordered.join(QLatin1Char(' '))));
            check(ordered.first() == QStringLiteral("book 01 (1-5).pdf")
                      && ordered.last() == QStringLiteral("book 12 (56-60).pdf"),
              QStringLiteral("wide enough for the last piece, not just the first: %1 ... %2")
                  .arg(ordered.first(), ordered.last()));
        }

        // ---- recording it, and taking it back ----
        {
            DocumentEditor editor;
            QString error;
            check(editor.open(source, QString(), &error), "setup: the document to split opens");
            const int total = editor.pageCount();
            check(total == 40, QStringLiteral("setup: %1 pages").arg(total));

            check(editor.splitPieceSize() == 0 && editor.pieceCount() == 1,
                  "nothing is split yet, so a save would write one file");
            error.clear();
            check(!editor.splitInto(0, &error) && !error.isEmpty(),
                  "a piece of no pages is refused, and says so");
            check(!editor.splitInto(total, &error) && !error.isEmpty(),
                  "a piece as big as the document is refused too -- it writes one file, which "
                  "is what the reader already has");
            check(!editor.isDirty() && editor.pieceCount() == 1,
                  "and a refused split leaves nothing pending: no dirty tab, no undo row");

            check(editor.splitInto(15, &error), QStringLiteral("每 15 页一份 is recorded: %1").arg(error));
            check(editor.isDirty(), "a split on its own dirties the tab");
            check(editor.pageCount() == total, "and no page moved, turned or went away");
            check(editor.splitPieceSize() == 15 && editor.pieceCount() == 3,
                  "three pieces, counted by the same function the save loops over");
            check(editor.undoDescription().contains(QStringLiteral("15")),
                  QStringLiteral("the undo row says the size: %1").arg(editor.undoDescription()));
            check(editor.pendingSummary() == QStringLiteral("拆成每 15 页一份"),
                  QStringLiteral("and so is the whole status line: %1")
                      .arg(editor.pendingSummary()));

            check(editor.splitInto(8, &error), "a second split is the reader changing their mind");
            check(editor.splitPieceSize() == 8,
                  "the last one is what counts, not the first still sitting in the list");
            check(editor.undo() && editor.splitPieceSize() == 15,
                  "undo lands back on the first, which was still there");
            check(editor.undo() && editor.splitPieceSize() == 0 && !editor.isDirty(),
                  "a split alone undoes to a clean document");
            check(editor.redo() && editor.splitPieceSize() == 15,
                  "and redo brings it back with the size it was given");

            // The pieces a save lays down are cut from the document the *list* describes,
            // which is the same page order every other dialog here promises.
            check(editor.undo() && editor.deletePages({0, 1}, &error) && editor.splitInto(19, &error),
                  "setup: two pages out, then nineteen to a piece");
            check(editor.pageCount() == 38 && editor.pieceCount() == 2
                      && editor.pieces().last().last == 37,
                  "the split is numbered against the 38 pages a save writes, not the 40 on screen");
        }

        // ---- 拆分 and 合并 are the two that cannot share a document, either way round ----
        //
        // An imported page carries a number from a file the reader is no longer looking at
        // (docs/store-submission.md §17), so "第 6-10 页" would name pages of two different
        // documents and no piece boundary would mean anything. The rule has to be enforced
        // from both ends because the dialogs open in no fixed order.
        {
            const QString donor = scratch.filePath(QStringLiteral("split-donor.pdf"));
            QString error;
            check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
                  QStringLiteral("setup: a three-page donor: %1").arg(error));

            DocumentEditor mergedFirst;
            check(mergedFirst.open(source, QString(), &error), "setup: the document opens");
            check(mergedFirst.mergeFile(donor, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: two pages in: %1").arg(error));
            check(mergedFirst.importedPageCount() == 2, "setup: and they count as imported");
            error.clear();
            check(!mergedFirst.splitInto(10, &error),
                  "a split is refused while pages came in from another file");
            check(error.contains(QStringLiteral("2")),
                  QStringLiteral("and the refusal says how many pages make it impossible: %1")
                      .arg(error));
            check(mergedFirst.undo(), "setup: the merge is undone");
            check(mergedFirst.importedPageCount() == 0 && mergedFirst.splitInto(10, &error),
                  "and the very same split is accepted once they are gone");

            DocumentEditor splitFirst;
            check(splitFirst.open(source, QString(), &error), "setup: the document opens again");
            check(splitFirst.splitInto(10, &error), "setup: split first this time");
            error.clear();
            check(!splitFirst.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  "and a merge is refused while a split is pending -- the same rule, other end");
            check(splitFirst.undo()
                      && splitFirst.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1,
                                              &error),
                  QStringLiteral("undo the split and the merge goes through: %1").arg(error));
        }

        // ---- a save with a split pending writes a set, not a file ----
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            check(editor.splitInto(15, &error), "setup: 15, 15 and the 10 left over");

            const QString base = scratch.filePath(QStringLiteral("cut.pdf"));
            const QVector<Piece> pieces = editor.pieces();
            QElapsedTimer clock;
            clock.start();
            const bool wrote = editor.saveAs(base, &error);
            const qint64 taken = clock.elapsed();
            note(QStringLiteral("40 pages split 15/15/10: three replays and three writes took %1 ms "
                                "for a %2 KB source")
                     .arg(taken)
                     .arg(QFileInfo(source).size() / 1024));
            check(wrote, QStringLiteral("the split save goes through: %1").arg(error));
            check(!QFile::exists(base),
                  "and the name the reader typed is not itself written -- there is no document "
                  "with all forty pages in it any more");

            QStringList made;
            for (int index = 0; index < pieces.size(); ++index)
                made << DocumentEditor::piecePath(base, index, pieces.size(), pieces.at(index));
            check(made.size() == 3, "three pieces were promised, and");
            bool everyOne = true;
            for (const QString &one : std::as_const(made))
                everyOne = everyOne && QFile::exists(one);
            check(everyOne,
                  QStringLiteral("they are exactly the three files named: %1")
                      .arg(namesOf(base, pieces).join(QStringLiteral(", "))));

            // Contents page by page. A piece of the right length made of the wrong pages is
            // the failure a page count cannot see, and it is the one a hand-cut page tree
            // would make.
            for (int index = 0; index < made.size(); ++index) {
                const QVector<int> order = orderInFile(made.at(index));
                const Piece &one = pieces.at(index);
                bool rightRun = order.size() == one.count();
                for (int at = 0; at < order.size(); ++at)
                    rightRun = rightRun && order.at(at) == one.first + at + 1;
                check(rightRun,
                      QStringLiteral("piece %1 holds pages %2-%3, in that order: got %4")
                          .arg(index + 1)
                          .arg(one.first + 1)
                          .arg(one.last + 1)
                          .arg(describeOrder(order)));
            }
            check(hashOfFile(source) == originalHash,
                  "and the document that was split is byte for byte what it was");

            // The undo case, seen from the file side rather than the list side: taking the
            // split back has to go back to writing the one name the reader chose.
            check(editor.undo() && editor.splitPieceSize() == 0, "setup: the split is undone");
            const QString plain = scratch.filePath(QStringLiteral("whole.pdf"));
            check(editor.saveAs(plain, &error),
                  QStringLiteral("and a plain save still works with a split in the undo list: %1")
                      .arg(error));
            check(QFile::exists(plain) && !QFile::exists(scratch.filePath(QStringLiteral("whole 01 (1-15).pdf"))),
                  "one file, named exactly what was typed, with no piece names beside it");
        }

        // ---- a lot of pieces at once ----
        //
        // 每 1 页一份 of a forty-page document is forty finished files sitting in the
        // scratch folder waiting to be renamed. That shape is the one worth testing even
        // though nobody will thank us for it: it is where a staged name that two pieces
        // could use turns a second writer into a truncation of the first one's file, and
        // where "all of them are built before any is moved" stops being a figure of speech.
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            check(editor.splitInto(1, &error), "setup: one page to a file");
            check(editor.pieceCount() == 40, "and the document has forty pieces");
            const QString base = scratch.filePath(QStringLiteral("single.pdf"));
            const QVector<Piece> pieces = editor.pieces();
            QElapsedTimer clock;
            clock.start();
            const bool forty = editor.saveAs(base, &error);
            note(QStringLiteral("40 pages 每 1 页一份: forty replays and forty writes took %1 ms")
                     .arg(clock.elapsed()));
            check(forty, QStringLiteral("forty pieces are written: %1").arg(error));
            bool everyOne = !QFile::exists(base);
            for (int index = 0; index < pieces.size(); ++index) {
                const QString one = DocumentEditor::piecePath(base, index, pieces.size(),
                                                              pieces.at(index));
                const QVector<int> order = QFile::exists(one) ? orderInFile(one) : QVector<int>();
                everyOne = everyOne && order.size() == 1 && order.value(0) == index + 1;
            }
            check(everyOne,
                  "and there are forty files, each holding one page, each holding the page its "
                  "own name says it holds");
            check(QDir(scratch.path())
                      .entryList({QStringLiteral("xyzpdf-save-*.pdf")}, QDir::Files)
                      .isEmpty(),
                  "and nothing of ours is left in the folder under a temporary name");
        }

        // ---- a name already taken refuses the whole save ----
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            editor.splitInto(15, &error);
            const QString base = scratch.filePath(QStringLiteral("blocked.pdf"));
            const QVector<Piece> pieces = editor.pieces();
            const QString taken = DocumentEditor::piecePath(base, 1, pieces.size(), pieces.at(1));
            const QString first = DocumentEditor::piecePath(base, 0, pieces.size(), pieces.at(0));
            {
                QFile squatter(taken);
                check(squatter.open(QIODevice::WriteOnly),
                      "setup: one of the piece names is already somebody's file");
                squatter.write("not a pdf, and not ours");
            }
            error.clear();
            check(!editor.saveAs(base, &error), "the save is refused");
            check(error.contains(QFileInfo(taken).fileName()),
                  QStringLiteral("and the sentence names which one: %1").arg(error));
            check(!QFile::exists(first) && !QFile::exists(base),
                  "nothing was written: a folder with two of three pieces in it looks like a "
                  "split that worked");
            check(QFile::remove(taken), "setup: the name is freed again");
            error.clear();
            check(editor.saveAs(base, &error),
                  QStringLiteral("and the same save now goes through, so it was the name that "
                                 "was refused and nothing else: %1").arg(error));
            check(QFile::exists(first), "including the piece that had nowhere to go before");
            check(QFileInfo(taken).size() > 200,
                  "and the file that had been in the way is now a piece of the document rather "
                  "than the twenty-odd bytes that were there");
        }

        // ---- a piece that would land on the document being read ----
        //
        // Reachable without anybody meaning to: split a document, keep one piece, open that
        // piece later and split it again choosing the old base name. Same rule as 另存为不能
        // 覆盖正在打开的文件, reached one piece at a time.
        {
            const QString awkward = scratch.filePath(QStringLiteral("again 02 (21-40).pdf"));
            QString error;
            check(QFile::copy(source, awkward),
                  "setup: a forty-page copy named exactly the way a piece of 每 20 页一份 is named");
            DocumentEditor editor;
            check(editor.open(awkward, QString(), &error), "setup: it opens as a document");
            check(editor.splitInto(20, &error), "setup: two pieces, 1-20 and 21-40");
            check(!editor.saveAs(scratch.filePath(QStringLiteral("again.pdf")), &error),
                  "refused: the second piece would overwrite the document being read");
            check(error.contains(QFileInfo(awkward).fileName()),
                  QStringLiteral("and the sentence names it: %1").arg(error));
            check(!QFile::exists(scratch.filePath(QStringLiteral("again 01 (1-20).pdf"))),
                  "the piece that did not collide was not written either");
            check(hashOfFile(awkward) == originalHash, "and the source is untouched");
        }

        // ---- a split of a moved document follows the move, not the file ----
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            check(editor.movePages({8}, DocumentEditor::MoveTarget::Front, -1, &error),
                  "setup: page 9 goes to the very front");
            check(editor.splitInto(20, &error), "setup: and the document is cut in half");
            const QString base = scratch.filePath(QStringLiteral("moved-cut.pdf"));
            check(editor.saveAs(base, &error),
                  QStringLiteral("the split save of a moved document goes through: %1").arg(error));
            const QVector<Piece> pieces = editor.pieces();
            const QVector<int> head = orderInFile(
                DocumentEditor::piecePath(base, 0, pieces.size(), pieces.at(0)));
            check(head.size() == 20 && head.first() == 9 && head.at(1) == 1 && head.last() == 20,
                  QStringLiteral("the first piece is 9 then 1-8 then 10-20, so the move reached "
                                 "the file: %1").arg(describeOrder(head)));
        }

        // ---- every piece carries the protection the original said it had ----
        //
        // The single-save version of this is checked above; what a split adds is that the
        // writer is pointed at N destinations, and one piece arriving unprotected is as
        // much a lie as the whole copy arriving unprotected.
        {
            const QString locked = scratch.filePath(QStringLiteral("locked-for-split.pdf"));
            QString error;
            check(writeEncryptedCopy(source, locked, QString(), false).isEmpty(),
                  "setup: a copy readable by anyone and marked not to be edited");
            DocumentEditor editor;
            if (!editor.open(locked, QString(), &error)) {
                note(QStringLiteral("a file marked not-to-modify would not open for editing: %1")
                         .arg(error));
                check(false, "the encrypted split could not be set up -- see §11's measurement");
            } else {
                check(editor.splitInto(20, &error), "setup: two pieces");
                const QString base = scratch.filePath(QStringLiteral("locked-cut.pdf"));
                const QVector<Piece> pieces = editor.pieces();
                if (editor.saveAs(base, &error)) {
                    const Permissions before = readPermissions(locked, QString());
                    bool everyPiece = pieces.size() == 2;
                    for (int index = 0; index < pieces.size(); ++index) {
                        const Permissions after = readPermissions(
                            DocumentEditor::piecePath(base, index, pieces.size(),
                                                      pieces.at(index)),
                            QString());
                        everyPiece = everyPiece && after.rawPFound && after.encrypted
                                     && after.rawP == before.rawP && after.r == before.r;
                    }
                    check(everyPiece,
                          QStringLiteral("both pieces carry the same /P as the original: %1")
                              .arg(describe(before)));
                } else {
                    note(QStringLiteral("a split save of a file marked not-to-modify was "
                                        "refused: %1").arg(error));
                    check(false, "the pieces were written at all -- see §11's measurement");
                }
            }
        }
    }

    // ---- and, when handed one, a document from outside the repository ----
    //
    // docs/perf.md keeps its real-book numbers the same way: a path given on the
    // command line, so the gate itself never depends on a file that is not in the
    // tree. Run as
    //   build-local-release\tests\xyzpdf_documenteditor_smoke.exe ^
    //       H:\pdf_test\VerilogHDL硬件描述语言.pdf
    // This is a measurement, not a check that may go red on somebody else's
    // machine: only the source-not-touched promise is asserted, because that is
    // the one this feature is not allowed to break on a file that cannot be
    // restored from the repository.
    if (argc > 1) {
        const QString real = QString::fromLocal8Bit(argv[1]);
        if (!QFile::exists(real)) {
            std::cerr << "FAIL: the file named on the command line is not there\n";
            ++failures;
        } else {
            const QString before = hashOfFile(real);
            DocumentEditor editor;
            QString error;
            QElapsedTimer clock;
            clock.start();
            bool done = editor.open(real, QString(), &error);
            const int realPages = done ? editor.pageCount() : 0;
            const QString realResult = scratch.filePath(QStringLiteral("real-result.pdf"));
            done = done && editor.deletePages({0, realPages / 2, realPages - 1}, &error)
                && editor.saveAs(realResult, &error);
            const qint64 taken = clock.elapsed();
            note(QStringLiteral("%1: %2 pages, %3 KB, open + delete + write took %4 ms")
                     .arg(QFileInfo(real).fileName())
                     .arg(realPages)
                     .arg(QFileInfo(real).size() / 1024)
                     .arg(taken));
            if (!done)
                std::cerr << "FAIL: the real document could not be edited: "
                          << error.toStdString() << '\n';
            else
                check(pagesAsReadByTheReader(realResult) == realPages - 3,
                      "and the copy of the real document reads back three pages shorter");
            check(hashOfFile(real) == before,
                  "and the document on disk outside the repository is byte for byte what it was");
        }
    }

    if (failures == 0) {
        std::cout << "document editor smoke: all checks passed (" << notes
                  << " measurement(s) printed above)\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
