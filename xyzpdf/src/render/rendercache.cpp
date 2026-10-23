#include "rendercache.h"

#include <QtGlobal>

RenderCache::RenderCache(qint64 maximumBytes)
    : limit(qMax<qint64>(0, maximumBytes))
{
}

void RenderCache::setMaximumBytes(qint64 maximumBytes)
{
    limit = qMax<qint64>(0, maximumBytes);
    // Shrinking the budget takes effect now rather than at the next insert: a
    // user who lowers the limit to free memory should see the memory freed,
    // not see it freed the next time they scroll.
    //
    // Nothing is named as protected here, so the eviction is free to drop
    // anything -- including whatever was drawn last. That is correct: the view
    // holds a reference to the image it is drawing, and would re-request it if
    // it were asked to paint again.
    if (!entries.isEmpty())
        evictToFit(0, RenderToken{});
}

qint64 RenderCache::bytesOf(const QImage &image)
{
    // sizeInBytes() counts the allocation the image actually holds, including
    // the alignment and the format's depth -- which is the number that matters
    // here, since the budget is about memory rather than about pixels.
    return image.isNull() ? 0 : static_cast<qint64>(image.sizeInBytes());
}

bool RenderCache::contains(const RenderToken &token) const
{
    return entries.contains(token);
}

QImage RenderCache::image(const RenderToken &token)
{
    const auto found = entries.find(token);
    if (found == entries.end())
        return {};
    found->lastUsed = ++clock;
    return found->image;
}

void RenderCache::insert(const RenderToken &token, const QImage &image)
{
    if (!token.isValid() || image.isNull())
        return;

    // A re-render of something already cached replaces it rather than sitting
    // beside it, and is taken out before the eviction count is worked out so
    // that the bytes it frees can pay for the bytes going in.
    const auto existing = entries.find(token);
    if (existing != entries.end()) {
        used -= bytesOf(existing->image);
        entries.erase(existing);
    }

    evictToFit(bytesOf(image), token);

    Entry entry;
    entry.image = image;
    entry.lastUsed = ++clock;
    used += bytesOf(image);
    entries.insert(token, entry);
}

void RenderCache::evictToFit(qint64 incoming, const RenderToken &protectedToken)
{
    // The entry being inserted is the only one that is never evicted, and it is
    // not in the map yet -- so it cannot be found by the scan and needs no
    // special case beyond this comparison. Without the rule, one page larger
    // than the whole budget evicts everything, is evicted itself, is asked for
    // again, and the view renders the same page forever without ever showing
    // it. With it, that page stays and the budget is exceeded by exactly one
    // page, which is the least bad outcome available.
    while (used + incoming > limit) {
        auto oldest = entries.end();
        for (auto candidate = entries.begin(); candidate != entries.end(); ++candidate) {
            if (candidate.key() == protectedToken)
                continue;
            if (oldest == entries.end() || candidate->lastUsed < oldest->lastUsed)
                oldest = candidate;
        }
        // Only the protected entry is left, and it is worth more than the whole
        // budget. Keeping it is deliberate; see above.
        if (oldest == entries.end())
            return;
        used -= bytesOf(oldest->image);
        entries.erase(oldest);
    }
}

void RenderCache::clear()
{
    entries.clear();
    used = 0;
    // The clock is not reset: a stamp only ever has to be comparable with the
    // stamps around it, and restarting it would make the first entry after a
    // clear look as old as the entries before it.
}

void RenderCache::dropPage(int page)
{
    for (auto entry = entries.begin(); entry != entries.end();) {
        if (entry.key().page != page) {
            ++entry;
            continue;
        }
        used -= bytesOf(entry->image);
        entry = entries.erase(entry);
    }
}
