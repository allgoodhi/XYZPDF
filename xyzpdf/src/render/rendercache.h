// Rendered pages, kept until the memory budget says otherwise.
//
// ---- what the budget counts ----
//
// Bytes of image, not pages. A page at 100% on a 1080p screen is about 8 MB; a
// scanned A4 at 400% is 200 MB. Counting pages would make the first of those
// waste most of the budget and the second blow through it, which is exactly
// backwards: it is the big pages that need to be evicted.
//
// Eviction is least-recently-used, and a page that was just inserted is never
// the one evicted -- otherwise a single page bigger than the whole budget would
// evict everything, be evicted itself, be asked for again, and the view would
// render the same page forever without ever showing it.
#pragma once

#include "rendertoken.h"

#include <QHash>
#include <QImage>

class RenderCache
{
public:
    // 256 MB, which is between ten and thirty ordinary pages. Configurable
    // through AppSettings, and the number a user with a small laptop will want
    // to lower.
    static constexpr qint64 DefaultMaximumBytes = 256LL * 1024 * 1024;

    explicit RenderCache(qint64 maximumBytes = DefaultMaximumBytes);

    void setMaximumBytes(qint64 maximumBytes);
    qint64 maximumBytes() const { return limit; }

    qint64 usedBytes() const { return used; }
    int count() const { return entries.size(); }

    bool contains(const RenderToken &token) const;

    // The image, or a null one. Touches the entry: whatever comes back from
    // here is what the view is about to draw, so it must not be the next thing
    // evicted.
    QImage image(const RenderToken &token);

    void insert(const RenderToken &token, const QImage &image);

    // Everything, now.
    //
    // What DocumentTab calls when a tab is given another document: the pages of
    // the previous one are keyed by page number and size, so a 10-page file
    // opened in a tab that had a 500-page one would find and draw that other
    // file's page 3. A generation-scoped version of this was written and
    // removed -- nothing called it, and a function kept alive only by its own
    // test is a function nobody is checking.
    void clear();

    // One page's answers, now.
    //
    // A form edit changes what a page looks like without changing what an image is an
    // answer to: the token is the same page, the same band, the same size, the same
    // rotation, and token equality ignores the generation (see rendertoken.h), so the
    // entry the cache holds for it is exactly the page from before the keystroke and
    // would be handed straight back. This is the one place that knows to throw it --
    // the view cannot, because the cache is the tab's.
    void dropPage(int page);

    // What an image costs. A static so the tests and the cache cannot disagree
    // about the arithmetic.
    static qint64 bytesOf(const QImage &image);

private:
    struct Entry
    {
        QImage image;
        // A monotonic stamp rather than a list of keys: eviction is a scan for
        // the smallest stamp, and with a budget of a few hundred megabytes that
        // is a few hundred entries. A linked list would be the right structure
        // for a million, and the wrong amount of code for this.
        quint64 lastUsed = 0;
    };

    void evictToFit(qint64 incoming, const RenderToken &protectedToken);

    QHash<RenderToken, Entry> entries;
    qint64 limit;
    qint64 used = 0;
    quint64 clock = 0;
};
