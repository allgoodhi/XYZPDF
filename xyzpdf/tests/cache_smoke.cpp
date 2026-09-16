// The render cache: the budget, and which page goes when it is reached.
//
// The budget is in bytes rather than pages, so the arithmetic has to be right:
// a cache that counts pages holds nine scanned pages and calls it full, or
// holds three and calls it empty. And the eviction has to be least-recently
// used rather than least-recently-inserted, or scrolling up and back down
// re-renders everything it passes.

#include "../src/render/rendercache.h"

#include <QString>

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

// ARGB32, so the byte count is exactly width * height * 4 -- the same number
// RenderCache::bytesOf() works out, and small enough to reason about.
QImage imageOf(int bytes)
{
    // 50 x 5 x 4 bytes = 1000.
    const int pixels = bytes / 4;
    QImage image(50, pixels / 50, QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

RenderToken tokenFor(int page, int generation = 1)
{
    RenderToken token;
    token.generation = generation;
    token.page = page;
    token.pixels = QSize(50, 5);
    return token;
}

} // namespace

int main()
{
    const QImage oneThousand = imageOf(1000);
    check(RenderCache::bytesOf(oneThousand) == 1000, "a 50x5 ARGB32 image is 1000 bytes");

    // ---- the budget, and least-recently-used eviction ----
    {
        RenderCache cache(4000);
        const RenderToken a = tokenFor(0);
        const RenderToken b = tokenFor(1);
        const RenderToken c = tokenFor(2);
        const RenderToken d = tokenFor(3);
        const RenderToken e = tokenFor(4);

        cache.insert(a, oneThousand);
        cache.insert(b, oneThousand);
        check(cache.usedBytes() == 2000, "two images is 2000 bytes");
        check(cache.count() == 2, "two images");

        // Reading A is what makes it the most recently used, and B the least.
        check(!cache.image(a).isNull(), "A comes back");
        check(!cache.image(a).isNull(), "A comes back again");

        cache.insert(c, oneThousand);
        cache.insert(d, oneThousand);
        check(cache.usedBytes() == 4000, "four images fill the budget exactly");
        check(cache.count() == 4, "and all four are kept");

        cache.insert(e, oneThousand);
        check(cache.usedBytes() <= 4000, "the fifth image is paid for by eviction");
        check(!cache.contains(b), "B was the least recently used and went first");
        check(cache.contains(a) && cache.contains(c) && cache.contains(d) && cache.contains(e),
              "the rest are still there");
    }

    // ---- a page bigger than the whole budget ----
    //
    // It stays. Without that rule the cache evicts it, the view asks for it
    // again, the render evicts it again, and the page is never shown -- which
    // looks like a page that never finishes loading.
    {
        RenderCache cache(500);
        const RenderToken huge = tokenFor(0);
        cache.insert(huge, oneThousand);
        check(cache.count() == 1, "an oversized image is kept rather than dropped");
        check(cache.usedBytes() == 1000, "even though it is over the budget");

        const RenderToken next = tokenFor(1);
        cache.insert(next, oneThousand);
        check(cache.count() == 1, "the next one replaces it rather than piling up");
        check(cache.contains(next), "and the newest is the one kept");
    }

    // ---- replacing the same image ----
    {
        RenderCache cache(10000);
        const RenderToken a = tokenFor(0);
        cache.insert(a, oneThousand);
        cache.insert(a, oneThousand);
        check(cache.count() == 1, "the same page at the same size is one entry");
        check(cache.usedBytes() == 1000, "and is counted once");
    }

    // ---- what the cache is keyed on ----
    //
    // The picture, not the generation. Zooming out and back in asks for the
    // same page at the same size again, and the image from before is still
    // exactly right: a generation bump says the work in flight is pointless,
    // not that the picture changed. If this were not so, every zoom change
    // would empty the cache in all but name.
    {
        RenderCache cache(10000);
        const RenderToken rendered = tokenFor(0, 1);
        cache.insert(rendered, oneThousand);

        RenderToken askedAgain = tokenFor(0, 9);
        check(cache.contains(askedAgain), "a later generation still finds the image");
        check(!cache.image(askedAgain).isNull(), "and gets it");

        // But a different size is a different picture, whatever the generation.
        RenderToken otherSize = askedAgain;
        otherSize.pixels = QSize(50, 6);
        check(!cache.contains(otherSize), "a different size is a different entry");

        RenderToken turned = askedAgain;
        turned.rotationDegrees = 90;
        check(!cache.contains(turned), "and so is a rotation");
    }

    // ---- what is refused ----
    {
        RenderCache cache(10000);
        RenderToken invalid;
        cache.insert(invalid, oneThousand);
        check(cache.count() == 0, "a token with no generation is not cached");

        cache.insert(tokenFor(0), QImage());
        check(cache.count() == 0, "a null image is not cached");
    }

    // ---- shrinking the budget ----
    {
        RenderCache cache(10000);
        cache.insert(tokenFor(0), oneThousand);
        cache.insert(tokenFor(1), oneThousand);
        cache.insert(tokenFor(2), oneThousand);
        cache.setMaximumBytes(1000);
        check(cache.usedBytes() <= 1000, "lowering the budget frees memory now");
        check(cache.usedBytes() > 0, "and keeps what fits");
    }

    {
        RenderCache cache(10000);
        cache.insert(tokenFor(0), oneThousand);
        cache.clear();
        check(cache.count() == 0 && cache.usedBytes() == 0, "clear empties it");
    }

    if (failures == 0) {
        std::cout << "the render cache holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
