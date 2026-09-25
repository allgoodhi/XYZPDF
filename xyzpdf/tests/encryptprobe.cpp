// What QPDF actually does with an encrypted file when the only password anybody has
// is the user password -- the measurement that decides whether 解密 can be on this
// program's menu at all, and whether 加密 can change the permissions of a file that
// already has some.
//
// This is a probe, not a test: tools/build_all_tests.py builds it and does not run it
// (its name carries 'probe'), because every line here is QPDF's answer rather than
// this program's promise. It is run by hand, from the project root so the fixtures are
// found by relative path:
//
//   ./build-local-release/tests/xyzpdf_encryptprobe.exe
//
// The questions, in the order the output prints them:
//
//   1. Strip. Open with the user password only, write with setPreserveEncryption(false).
//      Does the library refuse because /P says not to modify, and does the output open
//      with no password at all? This is 解密.
//   2. Strip *while* taking a page out, which is what a save actually replays. A
//      permission-forbidden document that strips clean but cannot be edited would still
//      mean 解密 and 编辑 cannot share one save.
//   3. Re-encrypt over an existing /Encrypt with different permission bits, from a
//      session that only ever had the user password. If that succeeds, "change the
//      restrictions" is a user-password operation, which is a sentence the docs have to
//      carry.
//   4. Owner password vs user password: whether any of the above reads differently.
//   5. Cost, for the two fixtures the other numbers in the docs come from.
//
// Three of the lines are controls on the instrument itself, and the exit code is
// nonzero if any of them is broken. Without them every "it decrypted" above is
// worthless: a probe whose "needs a password" check always answered false would report
// success for all four cases and mean nothing.

#include "../src/document/qtpdfbackend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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

int brokenControls = 0;

void print(const QString &text)
{
    // Deliberately ASCII throughout: this console is GBK, and one non-ASCII byte turns
    // the reading into mojibake -- which is how a measurement gets mis-transcribed.
    std::cout << text.toStdString() << '\n';
    std::cout.flush();
}

void control(const QString &what, bool held)
{
    print(QStringLiteral("  CONTROL %1  %2").arg(held ? "ok   " : "BROKEN").arg(what));
    if (!held)
        ++brokenControls;
}

// ------------------------------------------------------------- reading a file out

struct Opening
{
    bool opened = false;
    QString error;
    bool encrypted = false;
    int r = 0;
    int p = 0;
    int pages = -1;
    bool printHigh = false;
    bool extract = false;
    bool modifyOther = false;
    bool modifyAll = false;
};

Opening tryOpen(const QString &path, const QByteArray &password)
{
    Opening out;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData(),
                         password.isEmpty() ? nullptr : password.constData());
        out.opened = true;
        // isEncrypted(int&, int&) -- a local int on purpose, so the answer does not
        // depend on how qint64 binds to an int reference.
        out.encrypted = qpdf.isEncrypted(out.r, out.p);
        out.printHigh = qpdf.allowPrintHighRes();
        out.extract = qpdf.allowExtractAll();
        out.modifyOther = qpdf.allowModifyOther();
        out.modifyAll = qpdf.allowModifyAll();
        QPDFPageDocumentHelper helper(qpdf);
        out.pages = int(helper.getAllPages().size());
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what()).replace('\n', ' ');
    }
    return out;
}

QString describe(const Opening &o)
{
    if (!o.opened)
        return QStringLiteral("refused: %1").arg(o.error);
    if (!o.encrypted)
        return QStringLiteral("opened, NOT encrypted, pages=%1").arg(o.pages);
    return QStringLiteral("opened, R=%1 P=%2 pages=%3 print=%4 extract=%5 modifyOther=%6 modifyAll=%7")
        .arg(o.r)
        .arg(o.p)
        .arg(o.pages)
        .arg(o.printHigh ? 'Y' : 'N')
        .arg(o.extract ? 'Y' : 'N')
        .arg(o.modifyOther ? 'Y' : 'N')
        .arg(o.modifyAll ? 'Y' : 'N');
}

// Whether the file names an /Encrypt dictionary, read out of its own bytes rather than
// through QPDF. Same reason rawPermissionBits() exists in documenteditor_smoke.cpp: a
// claim that spans two files and one parser hears its own answer twice, and "the writer
// dropped the dictionary" is exactly the thing being asked here.
bool hasEncryptDictionary(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    return bytes.contains("/Encrypt");
}

// How many pages the reader's own engine counts, -1 for "it would not open". This is the
// answer that matters to a user of this program; QPDF agreeing with itself is not it.
int pagesWithReader(const QString &path, const QString &password = QString())
{
    QtPdfBackend document;
    if (document.load(path, password) != Document::Status::Ready)
        return -1;
    return document.pageCount();
}

// ---------------------------------------------------------------- writing a file out

enum class Mode { Keep, Strip, Reencrypt };

// The one write this program will really perform on an encrypted source: a structural
// change (here: one page taken out) plus whatever encryption is asked for. keepDeleted
// is not a knob the product has -- it is here so the "strip and edit" line can be told
// apart from a plain copy.
struct Written
{
    bool ok = false;
    QString error;
    qint64 milliseconds = -1;
    QString path;
};

Written writeOut(const QString &source, const QByteArray &password, const QString &destination,
                 Mode mode, const QByteArray &newUser = {}, const QByteArray &newOwner = {},
                 bool newAllowModify = true, bool dropOnePage = false)
{
    Written out;
    QElapsedTimer clock;
    clock.start();
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData(),
                         password.isEmpty() ? nullptr : password.constData());
        if (dropOnePage) {
            QPDFPageDocumentHelper helper(qpdf);
            const std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
            if (pages.size() > 2)
                helper.removePage(pages.at(2));
        }
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        switch (mode) {
        case Mode::Keep:
            // The writer's default: carry the original's /Encrypt, which is what every
            // existing 保存修改 path does and what §11.2 pinned the bits of.
            break;
        case Mode::Strip:
            writer.setPreserveEncryption(false);
            break;
        case Mode::Reencrypt:
            writer.setR6EncryptionParameters(newUser.constData(), newOwner.constData(),
                                             /* allow_accessibility = */ true,
                                             /* allow_extract = */ true,
                                             /* allow_assemble = */ false,
                                             /* allow_annotate_and_form = */ true,
                                             /* allow_form_filling = */ true,
                                             /* allow_modify_other = */ newAllowModify,
                                             qpdf_r3p_full,
                                             /* encrypt_metadata_aes = */ true);
            break;
        }
        writer.write();
        out.ok = true;
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what()).replace('\n', ' ');
        QFile::remove(destination);
    }
    out.milliseconds = clock.elapsed();
    out.path = destination;
    return out;
}

// A protected file, written here rather than committed as a fixture: the probe needs to
// know the passwords, and a file in tests/fixtures would be swept up by every other test
// that globs the folder.
QString makeEncrypted(const QString &source, const QString &destination, const QByteArray &user,
                      const QByteArray &owner, bool allowModify)
{
    const Written w = writeOut(source, {}, destination, Mode::Reencrypt, user, owner, allowModify);
    return w.ok ? QString() : w.error;
}

void reportWrite(const QString &label, const Written &written, const QByteArray &expectUserPassword)
{
    if (!written.ok) {
        print(QStringLiteral("  %1 -> refused: %2").arg(label, written.error));
        return;
    }
    const Opening readBack = tryOpen(written.path, expectUserPassword);
    print(QStringLiteral("  %1 -> wrote in %2 ms; /Encrypt in bytes=%3; open with \"%4\": %5; "
                         "reader without password sees %6 pages")
              .arg(label)
              .arg(written.milliseconds)
              .arg(hasEncryptDictionary(written.path) ? "yes" : "no")
              .arg(QString::fromUtf8(expectUserPassword))
              .arg(describe(readBack))
              .arg(pagesWithReader(written.path)));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString fixture = QStringLiteral("tests/fixtures/many-pages.pdf");
    const QString big = QStringLiteral("tests/fixtures/huge.pdf");
    if (!QFileInfo::exists(fixture)) {
        print("FAIL: run this from the project root (needs tests/fixtures/many-pages.pdf)");
        return 2;
    }

    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        print("FAIL: no scratch folder");
        return 2;
    }
    const QString dir = scratch.path();
    const QByteArray user = "hunter2";
    const QByteArray owner = "ownersecret";

    // ---------------------------------------------------------------- the instrument
    print("== controls on the instrument itself ==");
    {
        const QString plainCopy = dir + "/control-plain.pdf";
        const QString locked = dir + "/control-locked.pdf";
        control("the plain fixture reads as not encrypted",
                !tryOpen(fixture, {}).encrypted && !hasEncryptDictionary(fixture));
        control("a wrong password is refused, so \"opened\" means something",
                makeEncrypted(fixture, locked, user, owner, true).isEmpty()
                    && !tryOpen(locked, "wrong").opened);
        const Written kept = writeOut(locked, user, plainCopy, Mode::Keep);
        control("copying an encrypted file without touching encryption still needs the password",
                kept.ok && hasEncryptDictionary(plainCopy) && !tryOpen(plainCopy, {}).opened);
    }

    // ------------------------------------------------------------------- the subjects
    struct Subject
    {
        QString name;
        QString path;
        QByteArray userPassword;
        QString label;
    };
    QVector<Subject> subjects;
    {
        const QString allowModify = dir + "/locked-allowmodify.pdf";
        const QString forbidModify = dir + "/locked-forbidmodify.pdf";
        const QString openUserLockedOwner = dir + "/restricted.pdf";
        const QString samePassword = dir + "/samepw.pdf";
        QStringList problems;
        if (!makeEncrypted(fixture, allowModify, user, owner, true).isEmpty())
            problems << "allowmodify";
        if (!makeEncrypted(fixture, forbidModify, user, owner, false).isEmpty())
            problems << "forbidmodify";
        // The shape most protected PDFs in the wild have: anybody may read, editing is
        // written down as forbidden, and only the owner password can lift that.
        if (!makeEncrypted(fixture, openUserLockedOwner, {}, owner, false).isEmpty())
            problems << "restricted";
        // A file whose single password serves as both, which is what most tools that ask
        // for "a password" produce.
        if (!makeEncrypted(fixture, samePassword, user, user, false).isEmpty())
            problems << "samepw";
        if (!problems.isEmpty()) {
            print("FAIL: could not set up: " + problems.join(", "));
            return 2;
        }
        subjects = {
            { "locked-allowmodify", allowModify, user, "user password given, /P allows modifying" },
            { "locked-forbidmodify", forbidModify, user, "user password given, /P forbids modifying" },
            { "restricted-empty-user", openUserLockedOwner, {}, "empty user password, /P forbids modifying" },
            { "same-user-and-owner", samePassword, user, "user password == owner password, /P forbids" },
        };
    }

    print("");
    print("== what each one says when opened (question 4: does owner differ from user) ==");
    for (const Subject &s : subjects) {
        print(QStringLiteral("%1  (%2)").arg(s.name, s.label));
        print("    open with user    : " + describe(tryOpen(s.path, s.userPassword)));
        print("    open with owner   : " + describe(tryOpen(s.path, owner)));
        reportWrite("strip, user password ",
                    writeOut(s.path, s.userPassword, s.path + "-strip-user.pdf", Mode::Strip), {});
        reportWrite("strip, owner password",
                    writeOut(s.path, owner, s.path + "-strip-owner.pdf", Mode::Strip), {});
        print("    strip+drop-a-page : "
              + [&] {
                    const Written w = writeOut(s.path, s.userPassword, s.path + "-strip-edit.pdf",
                                               Mode::Strip, {}, {}, true, /* dropOnePage = */ true);
                    if (!w.ok)
                        return QStringLiteral("refused: %1").arg(w.error);
                    return QStringLiteral("wrote in %1 ms; /Encrypt=%2; reader sees %3 pages, "
                                          "page count after dropping one of %4")
                        .arg(w.milliseconds)
                        .arg(hasEncryptDictionary(w.path) ? "yes" : "no")
                        .arg(pagesWithReader(w.path))
                        .arg(tryOpen(s.path, s.userPassword).pages);
                }());
        print("    re-encrypt, new bits (question 3): "
              + [&] {
                    const Written w = writeOut(s.path, s.userPassword, s.path + "-reencrypt.pdf",
                                               Mode::Reencrypt, "second", owner,
                                               /* newAllowModify = */ true);
                    if (!w.ok)
                        return QStringLiteral("refused: %1").arg(w.error);
                    const Opening asNew = tryOpen(w.path, "second");
                    const Opening asOld = tryOpen(w.path, s.userPassword);
                    // The hand-off, which is the one line here that could ruin the
                    // feature: a file QPDF is happy with and Qt PDF cannot open would be
                    // readable by everything except this program.
                    return QStringLiteral("wrote; open with \"second\": %1; reader with the new "
                                          "password sees %2 pages, reader without it sees %3; "
                                          "open with the old user password: %4")
                        .arg(describe(asNew))
                        .arg(pagesWithReader(w.path, QStringLiteral("second")))
                        .arg(pagesWithReader(w.path))
                        .arg(asOld.opened ? QStringLiteral("still works")
                                          : QStringLiteral("refused: %1").arg(asOld.error));
                }());
    }

    // ------------------------------------------------------------------------ cost
    // Three writes of the same file, because a bare "10 ms" has no reading: the number a
    // save owes is the *difference* between what it does today (carry the encryption
    // through) and what 解密 would do (take it out), and both of those pay for opening a
    // file that has a crypto dictionary.
    print("");
    print("== cost, the same file written three ways ==");
    if (QFileInfo::exists(big) && !makeEncrypted(big, dir + "/big-locked.pdf", user, owner, false).isEmpty())
        print("  (skipped: could not encrypt the 600-page fixture)");
    {
        const struct { const char *label; QString source; } sizes[] = {
            { "40 pages", dir + "/locked-forbidmodify.pdf" },
            { "600 pages", dir + "/big-locked.pdf" },
        };
        for (const auto &row : sizes) {
            if (!QFileInfo::exists(row.source)) {
                print(QStringLiteral("%1  (skipped: not there)").arg(row.label));
                continue;
            }
            const Written keep = writeOut(row.source, user, row.source + "-c-keep.pdf", Mode::Keep);
            const Written strip = writeOut(row.source, user, row.source + "-c-strip.pdf", Mode::Strip);
            const Written edit = writeOut(row.source, user, row.source + "-c-edit.pdf", Mode::Strip,
                                          {}, {}, true, /* dropOnePage = */ true);
            const qint64 bytes = QFileInfo(row.source).size() / 1024;
            print(QStringLiteral("%1 / %2 KB   keep=%3 ms   strip=%4 ms   strip+drop-one-page=%5 ms")
                      .arg(row.label)
                      .arg(bytes)
                      .arg(keep.ok ? QString::number(keep.milliseconds) : QStringLiteral("refused"))
                      .arg(strip.ok ? QString::number(strip.milliseconds) : QStringLiteral("refused"))
                      .arg(edit.ok ? QString::number(edit.milliseconds) : QStringLiteral("refused")));
        }
    }

    print("");
    if (brokenControls > 0) {
        print(QStringLiteral("BROKEN CONTROLS: %1 -- the lines above measure nothing.").arg(brokenControls));
        return 1;
    }
    print("controls held; read the lines above rather than this one.");
    return 0;
}
