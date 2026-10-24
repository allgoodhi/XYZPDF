#include "stagingcleanup.h"

#include <QVector>

#include <cstddef> // std::size, for the buffer QueryFullProcessImageNameW is handed
#include <limits>  // std::numeric_limits<DWORD>, the widest number a process id can be

// Last, after the Qt headers. NOMINMAX because `std::numeric_limits<DWORD>::max()` below is a
// syntax error the moment Windows' own `max` macro is in the way, and WIN32_LEAN_AND_MEAN for
// the shorter pull on rpcndr.h -- which is the one that does `#define small char`, the trick
// AGENTS.md tells anyone adding this header to a file to expect (§79).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// Whether the process that wrote a staged file is still the process behind the number.
//
// A PID is a recycled number, so "is this number running" is only half the question and the
// other half is "is it still *this program*". Answering it has to fail towards leaving the
// file alone: the cost of guessing alive is one hidden leftover waiting for the next launch,
// and the cost of guessing dead is this program deleting something inside a folder a reader
// owns.
enum class Owner
{
    Gone,       // nothing runs under that number, or the thing that does is another program
    Running,    // the program that wrote it is still at it -- another window may be mid-save
    CannotTell  // a process exists that this one is not allowed to look at
};

Owner ownerOf(const AppSettings::StagedRecord &record)
{
    // A number that cannot be a process id, or a record written by a build that did not fill
    // the program name in, is not evidence about anything: leave the file for a reader rather
    // than guess who wrote it.
    if (record.pid <= 0 || record.pid > qint64(std::numeric_limits<DWORD>::max())
        || record.program.isEmpty())
        return Owner::CannotTell;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(record.pid));
    if (!process) {
        // The one failure that does not mean "gone": a protected process refuses the open and
        // is very much running. Anything else -- a number nobody holds -- is a dead process.
        return GetLastError() == ERROR_ACCESS_DENIED ? Owner::CannotTell : Owner::Gone;
    }

    Owner state = Owner::Gone;
    DWORD exitCode = 0;
    // STILL_ACTIVE is 259, which is also a real exit code a process can leave behind. It is
    // used here anyway because a number that exits with 259 is a slow leak of one hidden file
    // until the next launch, not a delete nobody asked for.
    if (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE) {
        wchar_t imagePath[4096];
        DWORD size = DWORD(std::size(imagePath));
        if (QueryFullProcessImageNameW(process, 0, imagePath, &size) == 0)
            state = Owner::CannotTell; // running, and who it is cannot be read
        else
            state = QFileInfo(QString::fromWCharArray(imagePath, int(size))).fileName()
                        .compare(record.program, Qt::CaseInsensitive) == 0
                    ? Owner::Running
                    : Owner::Gone; // the number moved on to somebody else's program
    }
    CloseHandle(process);
    return state;
}

// The attribute, asked of Windows rather than of QFileInfo::isHidden(): the rule this sweep
// works under is the one the save sets two lines after writing the file, so both sides of the
// comparison have to mean the same bit.
bool isStillHidden(const QString &path)
{
    const DWORD attributes
        = GetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
}

bool isStillTheStagedFile(const AppSettings::StagedRecord &record, const QFileInfo &info)
{
    // Zero was stored by a record made before the file existed, and there is nothing to
    // compare it with; the hidden bit and the path itself are what is left to go on.
    if (record.createdMs == 0)
        return true;
    const QDateTime birth = info.birthTime();
    return birth.isValid() && birth.toMSecsSinceEpoch() == record.createdMs;
}

} // namespace

StagingCleanup::Sweep StagingCleanup::sweepStaleRecords()
{
    Sweep report;
    // The list is the only input. Nothing here looks at a folder, asks what is in it, or
    // matches a name against a pattern -- which is the difference between this sweep and the
    // one that someday deletes a reader's own file because it started with the right letters.
    const QVector<AppSettings::StagedRecord> records = AppSettings::stagedRecords();
    for (const AppSettings::StagedRecord &entry : records) {
        const QFileInfo info(entry.path);
        if (!info.exists()) {
            // The staged file was taken away by the save itself, or by the reader, or by a
            // disk that is no longer mounted. Either way the bookkeeping is what is left.
            AppSettings::forgetStagedRecord(entry.path);
            ++report.dropped;
            report.reasons << QStringLiteral("gone: %1").arg(entry.path);
            continue;
        }
        if (!isStillHidden(entry.path) || !isStillTheStagedFile(entry, info)) {
            // Not ours any more: somebody unhid it, or the path now holds a different file.
            // It is left exactly where it is and the record goes, because a record that
            // cannot be acted on is not a plan -- it is a line that outlives the thing.
            AppSettings::forgetStagedRecord(entry.path);
            ++report.dropped;
            report.reasons << QStringLiteral("not ours: %1").arg(entry.path);
            continue;
        }
        const Owner owner = ownerOf(entry);
        if (owner != Owner::Gone) {
            // Another window, or another copy of this program, is holding this file open on
            // its way to being swapped in. Waiting costs nothing but the list entry.
            ++report.keptForLater;
            report.reasons << QStringLiteral("owner %1: %2")
                .arg(owner == Owner::Running ? QStringLiteral("running")
                                             : QStringLiteral("unreadable"))
                .arg(entry.path);
            continue;
        }
        // The process that wrote it is gone, so nothing will ever replace this file or take it
        // away. Delete it, quietly. A refusal -- another program has it open, the folder
        // stopped allowing writes -- keeps the record for the next launch and says nothing to
        // the reader, who asked for no dialog about a file they have never seen.
        if (QFile::remove(entry.path)) {
            AppSettings::forgetStagedRecord(entry.path);
            ++report.removed;
            report.reasons << QStringLiteral("removed: %1").arg(entry.path);
        } else {
            ++report.keptForLater;
            report.reasons << QStringLiteral("refused: %1").arg(entry.path);
        }
    }
    return report;
}
