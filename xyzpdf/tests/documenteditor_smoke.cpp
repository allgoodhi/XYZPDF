// Taking pages out of a PDF, moving the rest around, and what is promised around it.
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
        const Plan simple = DocumentEditor::planDelete(everyPage(10), {2, 3, 4});
        check(simple.pagesToRemove == 3 && simple.pagesRemaining == 7 && !simple.leavesNothing,
              "three pages out of ten leaves seven");

        const Plan repeated = DocumentEditor::planDelete(everyPage(10), {2, 3, 2, 99, -1});
        check(repeated.pagesToRemove == 2 && repeated.pagesRemaining == 8,
              "a page named twice comes out once, and a number outside the document names nothing");

        const Plan all = DocumentEditor::planDelete(everyPage(3), {0, 1, 2});
        check(all.pagesToRemove == 3 && all.pagesRemaining == 0 && all.leavesNothing,
              "naming every page is the request that cannot be granted");

        const Plan none = DocumentEditor::planDelete(everyPage(3), {});
        check(none.pagesToRemove == 0 && !none.leavesNothing,
              "naming no page is a different problem from naming every one");

        const Plan oneLeft = DocumentEditor::planDelete(everyPage(3), {0, 2});
        check(oneLeft.pagesToRemove == 2 && oneLeft.pagesRemaining == 1 && !oneLeft.leavesNothing,
              "leaving exactly one page is allowed -- it is the smallest document there is");

        // The dialog's "these are already gone" sentence, and the count behind it:
        // pages 0 and 1 are not in the document any more, so a second request that
        // names them removes nothing -- but it is not the same nothing as naming no
        // page at all, which is why alreadyGone is its own number.
        const Plan halfGone = DocumentEditor::planDelete({2, 3, 4}, {0, 1, 3});
        check(halfGone.pagesToRemove == 1 && halfGone.alreadyGone == 2
                && halfGone.pagesRemaining == 2,
              "pages an earlier change took out are counted separately from pages never named");

        const Plan allGone = DocumentEditor::planDelete({2, 3}, {0, 1});
        check(allGone.pagesToRemove == 0 && allGone.alreadyGone == 2 && !allGone.leavesNothing,
              "naming only gone pages removes nothing, and says why rather than \"you named nothing\"");

        // Naming every page that is left is the ungrantable one, even when the
        // document has already shrunk: what matters is the count going out.
        const Plan restOfThem = DocumentEditor::planDelete({2, 3}, {2, 3});
        check(restOfThem.pagesToRemove == 2 && restOfThem.leavesNothing,
              "the second delete that empties the document is refused as surely as the first");
    }

    // ---- what a move request means, with no document anywhere near it ----
    //
    // The same arithmetic the dialog shows and the save replays, run against a
    // `present` list written out by hand. Every one of these orders is the whole
    // answer rather than a count, because the thing a move can get wrong is which
    // page sits where.
    using Move = DocumentEditor::MoveTarget;
    {
        const DocumentEditor::MovePlan toBack = DocumentEditor::planMove(everyPage(10), {2, 3, 4},
                                                                        Move::Back);
        check(toBack.pagesMoved == 3 && toBack.order.last() == 4 && toBack.landsAt == 7
                && toBack.changesOrder && toBack.usable(),
              QStringLiteral("three pages moved to the back land at 8, 9, 10: %1")
                  .arg(describeOrder(toBack.order)));

        const DocumentEditor::MovePlan toFront = DocumentEditor::planMove(everyPage(10), {7, 8},
                                                                         Move::Front);
        check(toFront.order.front() == 7 && toFront.order.at(1) == 8
                && toFront.order.at(2) == 0 && toFront.landsAt == 0,
              QStringLiteral("the block keeps its own internal order at the front: %1")
                  .arg(describeOrder(toFront.order)));

        // Typed the other way round, and it still comes out 2,3,4: the numbers name
        // which pages travel, not the order they arrive in.
        const DocumentEditor::MovePlan typedBackwards = DocumentEditor::planMove(
            everyPage(10), {4, 2, 3}, Move::Back);
        check(typedBackwards.order.mid(7) == QVector<int>({2, 3, 4}),
              QStringLiteral("naming them out of order does not shuffle them: %1")
                  .arg(describeOrder(typedBackwards.order)));

        const DocumentEditor::MovePlan behind = DocumentEditor::planMove(everyPage(10), {0, 1},
                                                                        Move::AfterPage, 4);
        check(behind.order == QVector<int>({2, 3, 4, 0, 1, 5, 6, 7, 8, 9}) && behind.landsAt == 3
                && behind.anchorUsed == 4 && !behind.anchorWasGone,
              QStringLiteral("the first two pages move behind the fifth, which is page 5 on the "
                             "screen and index 4 here: %1")
                  .arg(describeOrder(behind.order)));

        // Three ways to ask for the order the document already has, and each one has
        // to come out false so that the button greys and nothing is recorded.
        check(!DocumentEditor::planMove(everyPage(5), {3, 4}, Move::Back).changesOrder,
              "the last two pages are already at the back");
        check(!DocumentEditor::planMove(everyPage(5), {0}, Move::Front).changesOrder,
              "the first page is already at the front");
        check(!DocumentEditor::planMove(everyPage(5), {2}, Move::AfterPage, 1).changesOrder,
              "page 3 is already behind page 2");

        const DocumentEditor::MovePlan ontoItself = DocumentEditor::planMove(everyPage(5), {1, 2},
                                                                            Move::AfterPage, 2);
        check(ontoItself.anchorIsMoved && !ontoItself.usable(),
              "moving a block behind a page inside itself is not a move, and says which page "
              "makes it that way");

        const DocumentEditor::MovePlan namedNothing = DocumentEditor::planMove(everyPage(5), {},
                                                                              Move::Back);
        check(namedNothing.pagesMoved == 0 && !namedNothing.changesOrder && !namedNothing.usable()
                && namedNothing.order == everyPage(5),
              "naming no page moves nothing, and the order it hands back is the one it was given");

        const DocumentEditor::MovePlan repeats = DocumentEditor::planMove(everyPage(10), {2, 2, 3},
                                                                         Move::Back);
        check(repeats.pagesMoved == 2 && repeats.order.mid(8) == QVector<int>({2, 3}),
              QStringLiteral("a page named twice travels once: %1")
                  .arg(describeOrder(repeats.order)));

        // The two questions a page number raises once unsaved changes exist are
        // separate: is it still there, and where does the anchor land if it is not.
        const DocumentEditor::MovePlan gonePages = DocumentEditor::planMove({2, 3, 4}, {0, 1, 3},
                                                                           Move::Back);
        check(gonePages.pagesMoved == 1 && gonePages.alreadyGone == 2
                && gonePages.order == QVector<int>({2, 4, 3}),
              "pages an earlier change deleted are counted apart from the one still there");

        const DocumentEditor::MovePlan goneAnchor = DocumentEditor::planMove({0, 1, 2, 4, 5}, {5},
                                                                            Move::AfterPage, 3);
        check(goneAnchor.anchorWasGone && goneAnchor.anchorUsed == 4
                && goneAnchor.order == QVector<int>({0, 1, 2, 5, 4}) && goneAnchor.usable(),
              QStringLiteral("an anchor a delete took out becomes the gap it left, in front of "
                             "the page that is now there: %1")
                  .arg(describeOrder(goneAnchor.order)));

        const DocumentEditor::MovePlan anchorAtEnd = DocumentEditor::planMove({0, 1, 2}, {0},
                                                                             Move::AfterPage, 5);
        check(anchorAtEnd.anchorWasGone && anchorAtEnd.anchorUsed == -1
                && anchorAtEnd.order == QVector<int>({1, 2, 0}),
              "and an anchor past the last surviving page is the same answer as moving to the end");
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
            check(hashOfFile(big) == bigFixtureHash, "and the large fixture is byte for byte what it was");
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
