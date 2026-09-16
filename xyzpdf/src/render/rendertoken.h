// What a rendered image is an answer to.
//
// ---- why there is a token at all ----
//
// Rendering happens on other threads and finishes when it finishes. By the time
// one page comes back the user may have scrolled four pages on, zoomed, or
// opened a different document. Putting that image on screen is not a small
// cosmetic problem: it is a page from another document, or the same page at a
// zoom that no longer applies, drawn as though it were current.
//
// XYZip's compressed-file preview took four attempts to get this right, each
// time by tracking "is this still the page I asked for" with something that was
// true most of the time. A generation counter is true all of the time: bumped
// whenever the answer could change -- another document, another zoom, another
// rotation -- and compared on arrival. Anything from an older generation is
// dropped, wherever it comes from and however long it took.
//
// ---- why it carries a region, not just a page ----
//
// A page at 800% is 7140 by 10000 device pixels: 288 MB for one page, more than
// the whole render budget. So a request can ask for one band of a page rather
// than all of it, and the token says which -- including in the cache key, since
// two bands of the same page are two different pictures.
//
// The token also identifies the image in the cache, which is why it carries
// everything that changes what the image looks like and nothing that does not.
#pragma once

#include <QHashFunctions>
#include <QRect>
#include <QSize>
#include <QtGlobal>

struct RenderToken
{
    // Bumped on every change that invalidates work in flight. Never zero for a
    // real request, so that a default-constructed token is recognisably empty.
    quint64 generation = 0;
    // Zero-based page number.
    int page = -1;
    // The size the page would be if it were rendered whole, in device pixels,
    // with the device pixel ratio already folded in. Kept even when only a band
    // is rendered: a band's coordinates only mean something against the whole.
    QSize pixels;
    // Which band of it, in that same pixel grid. Empty means the whole page,
    // which is the ordinary case and the one worth keeping cheap -- a page that
    // fits on screen is rendered in one piece and cached as a whole.
    QRect clip;
    // 0, 90, 180 or 270. Part of the token because a 90-degree rendering of a
    // page is a different image from the unrotated one, at the same size.
    int rotationDegrees = 0;
    // Whether annotations were drawn in. Part of the token for the same reason.
    bool annotations = true;

    // The part of the page this image is of, with "the whole page" written out.
    QRect region() const
    {
        return clip.isNull() ? QRect(QPoint(0, 0), pixels) : clip;
    }

    // The size of the image itself, which is what the renderer will hand back
    // and what the view has to fit back onto the page.
    QSize imageSize() const { return region().size(); }

    bool isValid() const
    {
        return generation != 0 && page >= 0 && !pixels.isEmpty() && !region().isEmpty();
    }

    // Whether this covers everything `wanted` asks for. This is the question
    // the view asks of the image it already has: a band that still contains
    // what is on screen does not have to be rendered again, which is what stops
    // a large page from re-rendering on every pixel of scrolling.
    bool covers(const QRect &wanted) const { return region().contains(wanted); }

    // Whether this result is still wanted. The one question that has to be
    // asked of every image that arrives, and the reason the generation is here
    // at all.
    bool isCurrent(quint64 currentGeneration) const { return generation == currentGeneration; }

    // Equality is about what the image looks like, and deliberately ignores the
    // generation.
    //
    // This is what makes the cache work: zooming out and back in asks for the
    // same page at the same size again, and the image from before is still
    // exactly right -- a generation bump says the work in flight is pointless,
    // not that the picture changed. If equality included the generation, every
    // zoom change would empty the cache in all but name.
    bool operator==(const RenderToken &other) const
    {
        return page == other.page && pixels == other.pixels && clip == other.clip
               && rotationDegrees == other.rotationDegrees && annotations == other.annotations;
    }
};

// The one place a token is built, so that the two spellings of "the whole page"
// -- an empty rectangle and the page's own rectangle -- cannot both exist and
// become two entries for one image.
inline RenderToken makeRenderToken(quint64 generation, int page, QSize pagePixels, QRect clip,
                                   int rotationDegrees, bool annotations)
{
    RenderToken token;
    token.generation = generation;
    token.page = page;
    token.pixels = pagePixels;
    token.clip = (clip.isNull() || clip == QRect(QPoint(0, 0), pagePixels)) ? QRect() : clip;
    token.rotationDegrees = rotationDegrees;
    token.annotations = annotations;
    return token;
}

inline size_t qHash(const RenderToken &token, size_t seed = 0)
{
    // The same fields == compares, and no others: a hash that agrees with fewer
    // fields than == puts unequal keys in one bucket, which is legal but slow,
    // and one that agrees with more would put equal keys in different buckets,
    // which loses them.
    return qHashMulti(seed, token.page, token.pixels.width(), token.pixels.height(),
                      token.clip.x(), token.clip.y(), token.clip.width(), token.clip.height(),
                      token.rotationDegrees, token.annotations);
}
