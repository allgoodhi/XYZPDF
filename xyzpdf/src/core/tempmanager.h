#ifndef TEMPMANAGER_H
#define TEMPMANAGER_H

#include <QString>

// Where this program may write files that are not the user's.
//
// v1 is read-only and writes nothing here -- rendering happens in memory and
// the user's PDF is never modified. It exists from the first milestone because
// the alternative in XYZip was retrofitting it: once three unrelated places had
// each invented their own temporary directory, the question "may I delete this"
// had three answers.
//
// Job directories are owned while the program runs (a lock file each) and swept
// when it starts again, so a crash leaves at most one run's leftovers behind
// rather than an ever-growing pile.
class TempManager final
{
public:
    enum class Category {
        // Staged files handed to Explorer for a drag-out operation.
        Drag,
        // Page images written out by "export as PNG/JPEG".
        Export,
        // A spool file for the print path, which needs a file rather than a
        // stream because QPrinter is given one.
        Print,
        Other
    };

    static QString rootPath();
    static QString categoryPath(Category category);
    static QString createJobDirectory(Category category);
    static bool removeJobDirectory(const QString &jobPath);
    static void cleanupStaleJobs(qint64 minimumAgeSeconds = 24 * 60 * 60);
    static void cleanupOwnedJobs();

private:
    static bool isOwnedJobPath(const QString &path);
    static void removeEmptyParents(const QString &jobPath);
};

#endif // TEMPMANAGER_H
