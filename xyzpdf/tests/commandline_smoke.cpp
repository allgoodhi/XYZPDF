// Every way the shell can hand a file to this program.
//
// The registry entries that carry these switches are written once and then live
// in the user's registry for years, through upgrades and restores. A switch
// that is renamed in the parser and not in the registry is a menu entry that
// does nothing, on a machine nobody here can look at -- so the spelling is
// tested here rather than through the window.

#include "../src/cli/commandline.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>

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

const QString kWorkingDirectory = QStringLiteral("C:/work");

// What the shell would hand over for a given item.
CommandLine::Request parse(const QStringList &arguments)
{
    return CommandLine::parse(arguments, kWorkingDirectory);
}

QString absolute(const QString &name)
{
    return QDir::toNativeSeparators(QDir(kWorkingDirectory).filePath(name));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // ---- a bare path: what a double-click arrives as ----
    {
        const CommandLine::Request request = parse({QStringLiteral("xyzpdf.exe"),
                                                    QStringLiteral("report.pdf")});
        check(request.valid, "a bare path is understood");
        check(request.action == CommandLine::Action::Open, "and means open");
        check(request.paths.size() == 1, "one file");
        check(request.paths.first() == absolute(QStringLiteral("report.pdf")),
              "resolved against the working directory and given native separators");
        check(request.page == -1, "with no page named");
    }

    // ---- nothing at all ----
    //
    // A shortcut, or a double-click on the program itself. That is an empty
    // window rather than an error: starting a reader to then open something
    // from it is a thing people do.
    {
        const CommandLine::Request request = parse({QStringLiteral("xyzpdf.exe")});
        check(request.valid, "no arguments is not an error");
        check(request.action == CommandLine::Action::Open, "it opens nothing, which is fine");
        check(request.paths.isEmpty(), "and names no file");
    }

    // ---- several files, which Explorer produces one process at a time ----
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("a.pdf"),
                   QStringLiteral("b.pdf")});
        check(request.valid, "two files are understood");
        check(request.paths.size() == 2, "and both are kept");
    }

    // ---- the page switch ----
    //
    // One-based on the command line, zero-based everywhere inside. The
    // conversion happens once, in the parser, which is why it is checked here.
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--page"),
                   QStringLiteral("3"), QStringLiteral("a.pdf")});
        check(request.valid, "a page number is understood");
        check(request.page == 2, "and is converted to zero-based");
        check(request.action == CommandLine::Action::Open,
              "which is still an open: a page is not an action");

        const CommandLine::Request dash =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("-page"),
                   QStringLiteral("1"), QStringLiteral("a.pdf")});
        check(dash.valid && dash.page == 0, "and -page is the same switch");

        const CommandLine::Request slash =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("/page"),
                   QStringLiteral("10"), QStringLiteral("a.pdf")});
        check(slash.valid && slash.page == 9, "and /page as well");
    }

    // ---- a page switch with nothing after it ----
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--page")});
        check(!request.valid, "a switch left waiting for its value is an error");
        check(!request.errorMessage.isEmpty(), "and says so");
    }

    // ---- a page number that is not a page ----
    {
        const CommandLine::Request zero =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--page"),
                   QStringLiteral("0"), QStringLiteral("a.pdf")});
        check(!zero.valid, "page 0 is refused: pages are counted from one");

        const CommandLine::Request word =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--page"),
                   QStringLiteral("first"), QStringLiteral("a.pdf")});
        check(!word.valid, "and so is a word where a number belongs");
    }

    // ---- print ----
    //
    // Explorer's print verb has been "/p" since before "--" existed, and both
    // spellings have to keep working: the registry entry that writes one may
    // have been written by an older build.
    {
        for (const QString &spelling : {QStringLiteral("--print"), QStringLiteral("/p"),
                                        QStringLiteral("/print")}) {
            const CommandLine::Request request =
                parse({QStringLiteral("xyzpdf.exe"), spelling, QStringLiteral("a.pdf")});
            check(request.valid, QStringLiteral("%1 is understood").arg(spelling));
            check(request.action == CommandLine::Action::Print,
                  QStringLiteral("%1 means print").arg(spelling));
        }

        const CommandLine::Request several =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--print"),
                   QStringLiteral("a.pdf"), QStringLiteral("b.pdf")});
        check(!several.valid, "printing two documents at once is refused");
    }

    // ---- two actions at once ----
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--print"),
                   QStringLiteral("--print"), QStringLiteral("a.pdf")});
        check(!request.valid, "the same action twice is still two actions");
    }

    // ---- an unknown switch ----
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--extra"), QStringLiteral("a.pdf")});
        check(!request.valid, "an unknown switch is an error rather than a file name");
        check(request.errorMessage.contains(QStringLiteral("--extra")),
              "and the message names it");
    }

    // ---- a file whose name begins with two dashes ----
    //
    // "--" is how a shell says "nothing after this is a switch", and a file
    // really can be called this.
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--"),
                   QStringLiteral("--notes.pdf")});
        check(request.valid, "after -- everything is a path");
        check(request.paths.first() == absolute(QStringLiteral("--notes.pdf")),
              "including one that looks like a switch");
    }

    // ---- a path that begins with a slash is not a switch ----
    //
    // On Windows "/foo" means a path from the root of the current drive. Only
    // the exact spellings are switches.
    {
        const CommandLine::Request request =
            parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("/documents/a.pdf")});
        check(request.valid, "a leading slash is a path");
        check(request.paths.size() == 1, "and it is kept as one");
    }

    // ---- coalescing ----
    //
    // Explorer starts one process per selected item, and they all end up in one
    // place. Eight files that mean "open these" should be one request.
    {
        QList<CommandLine::Request> requests;
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("a.pdf")}));
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("b.pdf")}));
        const QList<CommandLine::Request> folded = CommandLine::coalesce(requests);
        check(folded.size() == 1, "two opens with one path each become one");
        check(folded.first().paths.size() == 2, "carrying both paths");
    }

    // A page number belongs to the file it was given for, so a request that
    // names one stands alone rather than being merged into its neighbour.
    {
        QList<CommandLine::Request> requests;
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("a.pdf")}));
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--page"),
                               QStringLiteral("2"), QStringLiteral("b.pdf")}));
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("c.pdf")}));
        const QList<CommandLine::Request> folded = CommandLine::coalesce(requests);
        check(folded.size() == 3, "the page number ends the run of mergeable requests");
        check(folded.at(1).page == 1, "and keeps its page");
    }

    // A request that could not be understood stands alone: merging it would
    // attach its complaint to work that is perfectly fine.
    {
        QList<CommandLine::Request> requests;
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("a.pdf")}));
        requests.append(parse({QStringLiteral("xyzpdf.exe"), QStringLiteral("--oops")}));
        const QList<CommandLine::Request> folded = CommandLine::coalesce(requests);
        check(folded.size() == 2, "an unparseable request is not merged");
        check(!folded.at(1).valid, "and stays invalid");
    }

    // ---- the switches the registry is written from ----
    {
        check(CommandLine::switchFor(CommandLine::Action::Open).isEmpty(),
              "open has no switch: it is a bare path");
        check(CommandLine::switchFor(CommandLine::Action::Print) == QStringLiteral("--print"),
              "print has exactly one spelling, and this is what gets written");
        check(CommandLine::isKnownAction(0) && CommandLine::isKnownAction(1),
              "the actions this build knows");
        check(!CommandLine::isKnownAction(7),
              "and a number from another build is not one of them");
    }

    // ---- the installer's two switches ----
    //
    // packaging/xyzpdf.iss spells them out; these are the spellings it has to use.
    {
        using CommandLine::Setup;
        const QString exe = QStringLiteral("xyzpdf.exe");
        check(CommandLine::switchFor(Setup::RegisterShell) == QStringLiteral("--register-shell"),
              "the installer registers with --register-shell");
        check(CommandLine::switchFor(Setup::UnregisterShell) == QStringLiteral("--unregister-shell"),
              "and unregisters with --unregister-shell");
        check(CommandLine::switchFor(Setup::None).isEmpty(), "and None has no switch");
        check(CommandLine::setupOf({exe, QStringLiteral("--register-shell")}) == Setup::RegisterShell,
              "--register-shell on its own is the installer");
        check(CommandLine::setupOf({exe, QStringLiteral("--unregister-shell")})
                  == Setup::UnregisterShell,
              "and so is --unregister-shell");
        check(CommandLine::setupOf({exe}) == Setup::None, "a bare launch is not");
        check(CommandLine::setupOf({exe, QStringLiteral("a.pdf")}) == Setup::None,
              "nor is a document");
        check(CommandLine::setupOf({exe, QStringLiteral("--"), QStringLiteral("--register-shell")})
                  == Setup::None,
              "nor a document called --register-shell after --");
        check(CommandLine::setupOf({exe, QStringLiteral("--register-shell"), QStringLiteral("a.pdf")})
                  == Setup::None,
              "nor the switch with anything beside it");
        check(CommandLine::setupOf({exe, QStringLiteral("--Register-Shell")}) == Setup::None,
              "and the spelling is exact");
    }

    if (failures == 0) {
        std::cout << "the command line holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
