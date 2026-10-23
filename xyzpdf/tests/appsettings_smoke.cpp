// The per-document session record: save where a reader left off, get it back.
//
// The part that can be wrong quietly is the keying -- a Windows path is full of
// characters QSettings reads as group separators, so the path is hashed rather
// than used raw. This round-trips a session through the settings store (which
// tools/build_all_tests.py redirects to a scratch file) and checks that one file
// remembers its own place and does not answer for another.
//
// The staged-file list (handoff-05 H3) is asked of the same store: it is the only
// thing that lets a later launch delete a file, so it is tested here for exactly
// what it is -- a list of paths that has to come back whole, replace rather than
// duplicate, and refuse to hand back a line it could not read. What the list is
// *for*, with real hidden files to point a sweep at, is tests/documenttab_smoke.cpp.

#include "../src/core/appsettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QVector>

#include <iostream>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

} // namespace

int main(int argc, char **argv)
{
    // An application object so QSettings has somewhere to put a scratch file if
    // the redirect is not set; the tests runner always sets it.
    QCoreApplication app(argc, argv);

    const QString path = QStringLiteral("C:\\Users\\someone\\Documents\\报告 2026.pdf");
    const QString other = QStringLiteral("C:\\Users\\someone\\Documents\\other.pdf");

    AppSettings::DocumentSession out;
    check(!AppSettings::loadDocumentSession(path, out), "a file never opened has no session");

    AppSettings::DocumentSession saved;
    saved.page = 41;
    saved.zoomMode = 2; // Percent
    saved.zoomPercent = 1.75;
    saved.layoutMode = 1;
    AppSettings::saveDocumentSession(path, saved);

    check(AppSettings::loadDocumentSession(path, out), "after saving, the session is found");
    check(out.page == 41 && out.zoomMode == 2 && out.layoutMode == 1
              && qFuzzyCompare(out.zoomPercent, 1.75),
          "and every field comes back unchanged");

    AppSettings::DocumentSession elsewhere;
    check(!AppSettings::loadDocumentSession(other, elsewhere),
          "a different path does not read the first file's session");

    // The same path in a different case is the same file -- the key is lower-cased.
    AppSettings::DocumentSession upper;
    check(AppSettings::loadDocumentSession(path.toUpper(), upper) && upper.page == 41,
          "the key is case-insensitive, like Windows paths");

    // ---- the staged-file list (handoff-05 H3) ----
    //
    // The storage half of "只删自己记过的那几个路径": the list is the only thing that later
    // lets the startup sweep delete a file, so a record that comes back changed -- a path
    // truncated at a comma, a pid read as zero, a second write of the same path becoming two
    // records -- is a delete that either does not happen or happens twice. The sweep itself is
    // in tests/documenttab_smoke.cpp, where there are real files to point it at; this round
    // asks only what the store does with a line.
    {
        const auto clearRecords = [] {
            const QVector<AppSettings::StagedRecord> existing = AppSettings::stagedRecords();
            for (const AppSettings::StagedRecord &entry : existing)
                AppSettings::forgetStagedRecord(entry.path);
        };
        const auto recordsFor = [](const QString &folder) {
            QVector<AppSettings::StagedRecord> found;
            const QVector<AppSettings::StagedRecord> all = AppSettings::stagedRecords();
            for (const AppSettings::StagedRecord &entry : all) {
                // Case-insensitive, the way the store itself matches a path: one record below
                // is re-written in another case on purpose, and a prefix test that cared about
                // letters' case would count one record where the store holds two.
                if (entry.path.startsWith(folder, Qt::CaseInsensitive))
                    found.append(entry);
            }
            return found;
        };

        clearRecords();
        check(AppSettings::stagedRecords().isEmpty(), "a store nobody has written has no records");

        // A path of the shape a save actually stages: a drive letter, a folder full of
        // Chinese, a file name with a comma and brackets in it. The comma is the one worth
        // naming -- this is stored as one string rather than a QStringList precisely so that
        // nothing has to be escaped on the way through IniFormat.
        const QString folder = QStringLiteral("H:\\temp 目录\\xyzpdf-h3");
        const QString staged = folder + QStringLiteral("\\xyzpdf-save-1, 2 (final).pdf");
        const QString other = folder + QStringLiteral("\\xyzpdf-form-3.pdf");

        AppSettings::StagedRecord first;
        first.path = staged;
        first.pid = 424242;
        first.createdMs = 1730000000123;
        first.program = QStringLiteral("xyzpdf.exe");
        AppSettings::putStagedRecord(first);

        AppSettings::StagedRecord second;
        second.path = other;
        second.pid = 99;
        second.createdMs = 0; // the record made before the file existed
        second.program = QStringLiteral("xyzpdf.exe");
        AppSettings::putStagedRecord(second);

        const QVector<AppSettings::StagedRecord> both = recordsFor(folder);
        check(both.size() == 2,
              QStringLiteral("two records in, two records back (%1)").arg(both.size()));
        bool firstWhole = false;
        bool secondKeptZero = false;
        for (const AppSettings::StagedRecord &entry : both) {
            if (entry.path == staged) {
                firstWhole = entry.pid == first.pid && entry.createdMs == first.createdMs
                    && entry.program == first.program;
            }
            if (entry.path == other)
                secondKeptZero = entry.pid == second.pid && entry.createdMs == 0;
        }
        check(firstWhole,
              "and the path, the pid, the time and the program all come back as they went, "
              "comma and brackets and all");
        check(secondKeptZero,
              "and a zero creation time stays zero rather than becoming a number");

        // Writing the same path again replaces it -- this is what lets a staged file be
        // recorded twice around one save without the list growing by one per save.
        AppSettings::StagedRecord stamped = first;
        stamped.createdMs = 1730000009876;
        AppSettings::putStagedRecord(stamped);
        QVector<AppSettings::StagedRecord> after = recordsFor(folder);
        check(after.size() == 2,
              QStringLiteral("recording one path twice is still two records (%1)")
                  .arg(after.size()));
        bool replacementSeen = false;
        for (const AppSettings::StagedRecord &entry : std::as_const(after)) {
            if (entry.path == staged
                && entry.createdMs == 1730000009876)
                replacementSeen = true;
        }
        check(replacementSeen, "and the second writing is the one that is stored");

        // The same path with a different letter's case is the same file on Windows, so it has
        // to be the same record -- otherwise the sweep deletes the file under one spelling and
        // the other stays on the list forever, and "only what we recorded" stops meaning
        // anything once the list is full of paths that no longer exist.
        AppSettings::StagedRecord recased = stamped;
        recased.path = staged.toUpper();
        AppSettings::putStagedRecord(recased);
        after = recordsFor(folder);
        check(after.size() == 2,
              QStringLiteral("a path re-recorded in another case is not a third record (%1)")
                  .arg(after.size()));

        check(AppSettings::forgetStagedRecord(staged), "forgetting a recorded path says true");
        check(recordsFor(folder).size() == 1, "and one record is left");
        check(!AppSettings::forgetStagedRecord(staged),
              "forgetting it again says false -- there is nothing there to drop");
        check(recordsFor(folder).size() == 1, "and the count did not move on the way");

        // A line that cannot be read is dropped by the reader rather than handed back as a
        // record with an empty path: the sweep's whole promise is that it only deletes a path
        // it wrote down, and a half-parsed line is how that becomes a delete of nothing -- or
        // of something the parse did not mean. The two rejects here are a line with no
        // separators at all and a line whose path came out empty.
        QSettings store = AppSettings::settings();
        const QString good = AppSettings::stagedRecordLine(recased);
        store.setValue(AppSettings::stagedRecordsKey(),
                       QStringLiteral("nonsense line with no fields\n%1\n|123|456|xyzpdf.exe")
                           .arg(good));
        const QVector<AppSettings::StagedRecord> parsed = AppSettings::stagedRecords();
        check(parsed.size() == 1,
              QStringLiteral("of four lines, one is a record and three are not (%1)")
                  .arg(parsed.size()));
        check(parsed.isEmpty() || parsed.at(0).path == recased.path,
              "and it is the one that was written whole");

        // The field order the parsing depends on: a '|' inside a path stays part of the path.
        // Such a name is illegal on NTFS, and this is the shape that would have been silently
        // cut in half had the numbers been written first.
        AppSettings::StagedRecord barred;
        barred.path = QStringLiteral("H:\\x|y\\z.pdf");
        barred.pid = 7;
        barred.createdMs = 8;
        barred.program = QStringLiteral("p.exe");
        AppSettings::putStagedRecord(barred);
        bool barredWhole = false;
        const QVector<AppSettings::StagedRecord> withBar = AppSettings::stagedRecords();
        for (const AppSettings::StagedRecord &entry : withBar) {
            if (entry.path == QStringLiteral("H:\\x|y\\z.pdf") && entry.pid == 7
                && entry.createdMs == 8 && entry.program == QStringLiteral("p.exe"))
                barredWhole = true;
        }
        check(barredWhole, "the path is read from the left, so a '|' inside it survives");
        check(AppSettings::forgetStagedRecord(QStringLiteral("H:\\x|y\\z.pdf")),
              "and it is found again when it is forgotten");

        clearRecords();
        check(AppSettings::stagedRecords().isEmpty(),
              "and the store is left empty for whoever runs next");
    }

    if (failures == 0) {
        std::cout << "app settings smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
