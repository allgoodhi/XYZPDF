#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#include <QList>
#include <QString>
#include <QStringList>

// What the shell asked xyzpdf to do.
//
// Every right-click verb, the file association, and a launch from a terminal
// arrive here as argv. The parsing is kept apart from the window on purpose:
// the registry entries that carry these switches are written once and then live
// in the user's registry for a long time -- through upgrades, restores from
// backup and one settings import -- so the exact spelling of them is a
// compatibility surface worth testing on its own rather than through a GUI.
//
// The switches are named in this header and nowhere else. The M3 shell
// integration writes them into the registry and has to write exactly these; a
// second list is a menu entry that does nothing, with no clue why.
namespace CommandLine {

enum class Action {
    // Open the document and show it. What a double-click arrives as.
    Open,
    // Print and carry on. The switch exists from M0 because a file association
    // that never mentions printing is a file association somebody has to redo,
    // and the registry would have to be rewritten on their machine to add it.
    Print
};

struct Request
{
    Action action = Action::Open;
    // Absolute, native-separator paths, in the order given.
    QStringList paths;
    // The page to show first, zero-based, or -1 for "wherever the document
    // opens". Parsed from the one-based switch and converted once, here: the
    // command line is the only place in the program that counts pages from
    // one, and the conversion belongs with the parsing.
    int page = -1;
    // Set when the arguments could not be understood. The caller shows it and
    // exits; it is never empty when `valid` is false.
    bool valid = true;
    QString errorMessage;

    // Printing is about one document; opening is happy with several -- Explorer
    // launches one process per selected file, and they all arrive here.
    bool takesOneDocument() const { return action == Action::Print; }
};

// `arguments` is QCoreApplication::arguments(), program name included.
//
// `workingDirectory` resolves relative paths. It is a parameter rather than a
// call to QDir::current() so the tests do not depend on where they are run
// from -- the shell always passes absolute paths anyway.
Request parse(const QStringList &arguments, const QString &workingDirectory);

// Folds a queue of requests into the fewest that mean the same thing.
//
// The shell delivers a multi-selection as one launch per item, so eight files
// arrive as eight requests carrying one path each. Run separately that is eight
// documents to open, which for a reader is the same as one request with eight
// paths in it. Neighbours with the same action are merged; a different action
// ends the run, so the order of what was asked for is kept.
QList<Request> coalesce(const QList<Request> &requests);

// The switch a registry verb should carry for `action`. Empty for Open, which
// is a bare path and has no switch.
QString switchFor(Action action);

// What an installer asks of the program, as opposed to what a reader does.
//
// packaging/xyzpdf.iss runs `xyzpdf.exe --register-shell` after copying the files and
// `--unregister-shell` before removing them, so the 打开方式 entry and the menu are written by
// the same code the settings box uses -- one writer for those keys, not a second copy of them
// in an installer script that drifts. Not an Action: these never cross the instance pipe (an
// uninstall running while a window is open must not hand its request to that window), and
// they are recognised only as the one argument on the line, so a document that happens to be
// called "--register-shell" is still a document when it comes after "--".
enum class Setup { None, RegisterShell, UnregisterShell };

Setup setupOf(const QStringList &arguments);

QString switchFor(Setup setup);

// Whether an integer that came off the wire is an action this build knows.
//
// Asked rather than cast: what crosses the instance pipe comes from another
// build of this program, which may be older or newer, and an action number from
// a different build is not something to reinterpret and hope about.
bool isKnownAction(int value);

} // namespace CommandLine

#endif // COMMANDLINE_H
