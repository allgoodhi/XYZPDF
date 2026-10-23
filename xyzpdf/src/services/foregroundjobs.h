// How many jobs are standing on a tab's stack, and the guard that says so.
//
// ---- a job that holds this object on the stack ----
//
// Printing and exporting to images run in the foreground: they render page by
// page on this thread and pump the event loop between pages so the progress box
// stays alive and its Cancel button works. Anything queued runs in that pump --
// and a 关闭标签 arriving there used to take the tab out of the strip and delete
// it, with print() still on its stack and about to render another page through a
// backend that had just been freed. Measured in mainwindow_smoke, which is where
// the shape is visible; docs/review-03.md §2.4 is where it was first suspected.
//
// Today the only thing stopping that is the progress box being window modal,
// which blocks the menu, the shortcut and the strip's ✕ -- an accident of the UI
// rather than a rule, and nothing at all for a route that does not come through
// the keyboard. So the job says so, and MainWindow refuses to take the tab apart
// while one is running.
//
// The counting is here rather than in DocumentTab because the jobs that need it
// are in DocumentOutput, and a rule that has to be remembered by two classes is
// one too many: acquire on the way in, release on the way out, and ask before
// taking anything apart.
#pragma once

class ForegroundJobs
{
public:
    // The guard: one on the stack of a job that must not be interrupted by the
    // tab being taken apart. Null owner is allowed and does nothing, so a caller
    // cannot crash by standing a job on a tab that is not there.
    class Scope
    {
    public:
        explicit Scope(ForegroundJobs *owner)
            : jobs(owner)
        {
            if (jobs)
                jobs->acquire();
        }
        ~Scope()
        {
            if (jobs)
                jobs->release();
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        ForegroundJobs *jobs = nullptr;
    };

    void acquire() { ++count; }
    void release() { --count; }
    bool busy() const { return count > 0; }

private:
    int count = 0;
};
