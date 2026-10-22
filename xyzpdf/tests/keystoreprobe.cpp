// One manual check of the real credential manager: write, read back, delete.
//
// This is not a test. An automated test must not touch the Windows credential
// manager of whoever runs the suite -- it would leave an entry behind, and could
// read back a real key and send a request nobody asked for (see
// src/translate/keystore.h). So the Windows half of the key storage is verified
// by hand, once, with this program:
//
//     cmake --build build-local-release --target xyzpdf_keystore_probe
//     build-local-release\tests\xyzpdf_keystore_probe.exe
//
// It uses its own credential name -- XYZPDF/translate/probe, never the deepl or
// google slot -- so a real key is not overwritten, and it deletes what it wrote
// before exiting. The entry is gone afterwards; `cmdkey /list` shows nothing.
//
// Excluded from the run list by tools/build_all_tests.py, which skips any target
// whose name says "probe".

#include "../src/translate/keystore.h"

#include <QCoreApplication>
#include <QString>

#include <iostream>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    using namespace Translate;

    const QString probeId = QStringLiteral("probe");
    const QString secret = QStringLiteral("XYZPDF-credential-probe-12345");
    const std::unique_ptr<KeyStore> store = makeSystemKeyStore();

    int failures = 0;
    auto report = [&failures](const char *step, bool ok, const QString &detail) {
        std::cout << (ok ? "ok:   " : "FAIL: ") << step << " -- " << detail.toStdString() << '\n';
        if (!ok)
            ++failures;
    };

    QString error;
    // Anything left by an earlier run that died before its delete.
    store->remove(probeId, nullptr);

    const bool written = store->write(probeId, secret, &error);
    report("write", written, written ? credentialTarget(probeId) : error);

    QString readBack;
    error.clear();
    const bool found = store->read(probeId, &readBack, &error);
    report("read", found && readBack == secret,
           QStringLiteral("found=%1 value=%2 %3")
               .arg(found)
               .arg(readBack == secret ? QStringLiteral("matches") : QStringLiteral("differs"),
                    error));

    // A store that answers "no key" for a name nobody wrote is the difference
    // between an unconfigured service and a broken one, and the translate path
    // branches on it.
    QString absent;
    error.clear();
    const bool absentFound = store->read(QStringLiteral("probe-absent"), &absent, &error);
    report("read of an unused name", !absentFound && error.isEmpty(),
           QStringLiteral("found=%1 error=%2").arg(absentFound).arg(error));

    error.clear();
    const bool removed = store->remove(probeId, &error);
    report("delete", removed, removed ? credentialTarget(probeId) : error);

    error.clear();
    const bool goneAfterDelete = store->read(probeId, &readBack, &error);
    report("nothing left after delete", !goneAfterDelete && error.isEmpty(),
           QStringLiteral("found=%1 value=%2 error=%3")
               .arg(goneAfterDelete)
               .arg(readBack, error));

    // Deleting what is not there must be the outcome that was asked for, not a
    // failure: the settings dialog writes an empty key to mean "remove", and a
    // user clearing a field twice would otherwise see an error.
    error.clear();
    report("delete of an absent entry", store->remove(probeId, &error),
           error.isEmpty() ? QStringLiteral("true, no error") : error);

    if (failures == 0) {
        std::cout << "credential manager: write, read and delete all behaved\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
