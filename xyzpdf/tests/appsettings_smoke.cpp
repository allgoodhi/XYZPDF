// The per-document session record: save where a reader left off, get it back.
//
// The part that can be wrong quietly is the keying -- a Windows path is full of
// characters QSettings reads as group separators, so the path is hashed rather
// than used raw. This round-trips a session through the settings store (which
// tools/build_all_tests.py redirects to a scratch file) and checks that one file
// remembers its own place and does not answer for another.

#include "../src/core/appsettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include <iostream>

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

    if (failures == 0) {
        std::cout << "app settings smoke: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
