#include "mainwindow.h"
#include "src/cli/commandline.h"
#include "src/cli/instancegateway.h"
#include "src/core/appsettings.h"
#include "src/core/languages.h"
#include "src/core/tempmanager.h"
#include "src/theme/thememanager.h"
#include "src/translate/keystore.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>
#include <QTranslator>
#include <QWidget>

#include <memory>

namespace {

// Nothing visible, nothing running, nothing queued -- and a moment's grace for
// launches still on their way. A process that was asked to do something and
// then finds itself with nothing to show has to decide when to stop, and this
// is the only place that decides it.
constexpr int kGraceMilliseconds = 3000;

bool anythingVisible()
{
    const QWidgetList windows = QApplication::topLevelWidgets();
    for (const QWidget *window : windows) {
        if (window->isVisible())
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // Both names, and the same value: QSettings addresses its store by them,
    // and this program has never been called anything else, so there is nothing
    // to migrate and no older store to look for.
    QCoreApplication::setOrganizationName(QStringLiteral("xyzpdf"));
    QCoreApplication::setApplicationName(QStringLiteral("xyzpdf"));

    // Versions before the credential manager kept the translation key in plain
    // text in QSettings. Move it once, before any window can read it; a failure
    // leaves the old value in place so the next launch can try again instead of
    // losing the key. With nothing stored -- every install that never had a key,
    // and every launch after the first -- this is one settings read.
    {
        const std::unique_ptr<Translate::KeyStore> keyStore = Translate::makeSystemKeyStore();
        QString migrationError;
        if (!Translate::migrateLegacyApiKey(*keyStore, &migrationError))
            qWarning() << "[translate]" << migrationError;
    }

    // Every window takes this unless it says otherwise. Rendered from the SVG
    // rather than loaded from the .ico so each size is drawn rather than scaled
    // -- the same artwork the .ico is generated from, so the two cannot drift.
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    // Which language, before anything builds a string.
    //
    // The source strings in this program are Chinese, so Chinese needs no
    // catalogue of its own -- it is what tr() returns untranslated. English
    // needs both halves: ours for the program's own text, and the absence of
    // Qt's for the built-in widgets, whose default is already English.
    const QString wanted = Languages::resolve(AppSettings::languageCode());

    // Qt's own catalogue first, for the words this program never writes: the
    // buttons on a message box, the parts of the file dialog, "Hide Details".
    static QTranslator qtTranslator;
    if (wanted != QStringLiteral("en")) {
        const QString catalogue = QStringLiteral(":/translations/qtbase_%1.qm").arg(wanted);
        if (qtTranslator.load(catalogue))
            a.installTranslator(&qtTranslator);
        else
            qWarning() << "[i18n] Qt's catalogue for" << wanted
                       << "is missing; built-in widgets stay English";
    }

    // Then this program's own. Chinese is the source language, so there is
    // nothing to load for it.
    static QTranslator appTranslator;
    if (wanted != QStringLiteral("zh_CN")) {
        // Missing is worth a warning rather than a silent Chinese interface:
        // someone seeing Chinese has no way to tell a broken build from a
        // program that was never translated.
        if (appTranslator.load(Languages::catalogueResource(wanted)))
            a.installTranslator(&appTranslator);
        else
            qWarning() << "[i18n] the catalogue for" << wanted
                       << "is missing; the interface stays Chinese";
    }

    // No font is chosen here on purpose. XYZip compiled three variable fonts
    // into its executable -- 55 MB of a 57 MB binary -- and reading their cmap
    // tables against what Windows ships settled it: the system fonts cover
    // more, and Qt's own fallback finds the rest.
    ThemeManager::initialize(a);
    ThemeManager::apply(AppSettings::theme());

    // Leftovers from a run that ended badly -- a crashed export, a print spool
    // file -- before anything has a chance to create this run's. The age check
    // is what keeps this from stepping on the files of another copy running
    // right now.
    TempManager::cleanupStaleJobs();

    // Everything past here can be reached from Explorer: a double-clicked PDF,
    // or one of the right-click verbs.
    const CommandLine::Request request =
        CommandLine::parse(QCoreApplication::arguments(), QDir::currentPath());

    // A second launch hands its work to the first and goes away.
    //
    // Explorer starts one process per selected item, so opening ten documents
    // from a multi-selection used to start ten copies of xyzpdf, each with its
    // own window, all reading the same disk.
    const QString instanceName = InstanceGateway::serverName();

    // Short, because this is the "is anybody there" question and the usual
    // answer is no. Connecting to a pipe that does not exist fails at once.
    if (InstanceGateway::forward(request, instanceName, 1000))
        return 0;

    // Claimed before the window is built, not after.
    //
    // Building a MainWindow takes long enough that several launches started
    // together would all get past the question above, all spend that time
    // constructing, and only then start arguing about who is in charge -- by
    // which point they have each done the work.
    InstanceGateway::Server gateway;
    const bool isInstance = gateway.listen(instanceName);
    if (!isInstance) {
        // Somebody else won it in the moment in between. They are certainly
        // there, so this waits properly rather than giving up after a second:
        // they may still be starting, and the alternative to waiting is doing
        // the work twice.
        if (InstanceGateway::forward(request, instanceName, 60000))
            return 0;
        // Still no answer. Rather than drop what the user asked for, carry on
        // and do it here.
    }

    int result = 0;
    {
        MainWindow window;

        QObject::connect(&gateway, &InstanceGateway::Server::requestReceived, &window,
                         [&window](const CommandLine::Request &forwarded) {
                             window.applyForwardedCommandLine(forwarded);
                         });

        const bool windowShown = window.applyStartupCommandLine(request);

        // A request that showed no window still has work: a document being
        // read, a page being rendered. Qt ends the program when the last window
        // closes, which for a process that never shows one is a rule that never
        // fires.
        QTimer idleCheck;
        QElapsedTimer sinceBusy;
        if (!windowShown) {
            QApplication::setQuitOnLastWindowClosed(false);
            sinceBusy.start();
            idleCheck.setInterval(250);
            QObject::connect(&idleCheck, &QTimer::timeout, &window,
                             [&window, &sinceBusy] {
                                 if (anythingVisible() || window.hasPendingWork()) {
                                     sinceBusy.restart();
                                     return;
                                 }
                                 if (sinceBusy.elapsed() >= kGraceMilliseconds)
                                     QCoreApplication::quit();
                             });
            idleCheck.start();
        }

        result = QApplication::exec();
    }

    // Whatever this run created and still owns. A run that ended by being
    // killed leaves its directories behind for the next launch's stale sweep.
    TempManager::cleanupOwnedJobs();
    return result;
}
