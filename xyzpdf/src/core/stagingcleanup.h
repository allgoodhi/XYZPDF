#ifndef STAGINGCLEANUP_H
#define STAGINGCLEANUP_H

#include "appsettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// The hidden copy a save writes beside the reader's file, and who may delete it afterwards.
//
// AGENTS.md's rule is that nothing but 保存 writes into a reader's PDF. Saving then writes one
// thing more than that rule says: a staged copy in the same folder as the original, because
// the last step has to be a swap inside one volume to be atomic at all (docs/handoff-03.md F8,
// and PDFium's own first stage for filled fields). Normal saves take it away -- replaced into
// the original's name, or removed when the save was refused. A process killed between the
// write and that last step leaves it sitting in the reader's folder, hidden, with nobody left
// who knows it is ours.
//
// This class is the answer, and the rule it works under is narrower than tidying a folder:
//
//   this is the only place in this program that deletes a file inside a folder the reader
//   owns, and it deletes only a path it recorded itself -- recorded before the bytes were
//   written, in AppSettings, with the creation time and the process that wrote it.
//
// So the two things a reader could reasonably expect never to happen are both structural: no
// scan of a folder by name pattern can ever bring a file here (the list is the only input, so
// a file that merely looks like one of ours -- `xyzpdf-save-anything.pdf` written by a build
// that recorded nothing, or by anything else entirely -- is not even looked at), and a record
// whose file has been claimed by the reader stops being a plan to delete something.
// sweepStaleRecords() says which of those two it found, and it never asks the reader about
// either: a leftover that cannot be removed stays on the list for the next launch rather than
// turning a quiet piece of bookkeeping into a dialog.
//
// Where it does not reach, said here because the shape of the list makes it look like it does.
// 另存为 and 拆分 stage their pieces beside the destination too (DocumentEditor::stagedPath()),
// and those are not hidden: one of the two outcomes that leaves one behind is a save that could
// not rename it, and the sentence about that names the file out loud -- "写好的文件留在 %3" --
// so a launch after it deleting that file would take away the one thing the reader was told to
// go and find. They are therefore never recorded, which costs this: a process killed in the
// middle of a split still leaves a visible `xyzpdf-save-<stamp>-<piece>.pdf` in the folder.
// Narrowing that is a question about what the reader was told, not about this sweep.
//
// The recording half is inline because the file that stages a PDFium form save is compiled
// into the preview and thumbnail handlers as well as into the program (CMakeLists.txt's
// xyzpdf_use_document_backend()); the sweep that follows is not, and lives in the .cpp that
// only the program and the tests link.
class StagingCleanup final
{
public:
    // Remember this path as ours. Called before the bytes exist, and called again once the
    // file is hidden -- the second call is what puts its creation time in the record, and
    // because a record is keyed by path, it replaces the first rather than joining it.
    static void record(const QString &stagedPath);

    // Stop recording it, without touching the file. Used when the swap succeeded (the staged
    // path no longer exists -- ReplaceFileW moved it into the original's name) and when the
    // save hands the file over to the reader on purpose, which is a different thing from a
    // leftover even though both end with a file in the folder.
    static void forget(const QString &stagedPath);

    // Delete one of our own staged files and drop its record only if the delete worked: a
    // file that would not go -- held by another program, or in a folder that no longer allows
    // it -- stays on the list so that the next launch can try again.
    static bool removeStaged(const QString &stagedPath);

    struct Sweep
    {
        int removed = 0;       // gone from the folder and from the list
        int dropped = 0;       // off the list, the file left where it is
        int keptForLater = 0;  // still on the list, deliberately
        // One line per record, in the developer log and nothing else. It exists because a
        // sweep that reports three numbers cannot answer "which of these four cases was it",
        // and the four cases are the whole of what this class promises.
        QStringList reasons;
    };

    // The startup sweep: run once, after the single-instance door (main.cpp), so that a
    // second launch forwarding its work to the first does not delete a file the first is
    // still saving.
    static Sweep sweepStaleRecords();

    // The path the record is keyed by. One place, because record() and forget() have to
    // disagree with nobody about whether two spellings are one file.
    static QString normalized(const QString &stagedPath)
    {
        if (stagedPath.isEmpty())
            return {};
        return QDir::cleanPath(QFileInfo(stagedPath).absoluteFilePath());
    }
};

inline void StagingCleanup::record(const QString &stagedPath)
{
    const QString path = normalized(stagedPath);
    if (path.isEmpty())
        return;
    AppSettings::StagedRecord entry;
    entry.path = path;
    entry.pid = qint64(QCoreApplication::applicationPid());
    entry.program = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    // Read from the file itself rather than from a clock: the comparison the sweep makes is
    // between two readings of the same attribute, which is why the resolution of the volume
    // it sits on -- 100 ns on NTFS, 2 s on a FAT stick -- does not matter.
    const QFileInfo info(path);
    const QDateTime birth = info.birthTime();
    if (info.exists() && birth.isValid())
        entry.createdMs = birth.toMSecsSinceEpoch();
    AppSettings::putStagedRecord(entry);
}

inline void StagingCleanup::forget(const QString &stagedPath)
{
    AppSettings::forgetStagedRecord(normalized(stagedPath));
}

inline bool StagingCleanup::removeStaged(const QString &stagedPath)
{
    const QString path = normalized(stagedPath);
    if (path.isEmpty())
        return false;
    if (!QFile::remove(path) && QFileInfo::exists(path))
        return false;
    AppSettings::forgetStagedRecord(path);
    return true;
}

#endif // STAGINGCLEANUP_H
