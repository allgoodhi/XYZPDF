#include "textlayer.h"

#include <QtGlobal>

QVector<PageTextRange> selectionRanges(int anchorPage, int anchorIndex, int focusPage,
                                       int focusIndex,
                                       const std::function<int(int)> &charactersOn)
{
    QVector<PageTextRange> ranges;
    if (anchorPage < 0 || focusPage < 0)
        return ranges;

    // Dragging backwards is the same selection as dragging forwards, so the two
    // ends are sorted once here and everything below works on first/last.
    // Getting this wrong is not a crash: it is a selection that vanishes when
    // the pointer moves up, which reads as "selection does not work sometimes".
    int firstPage = anchorPage;
    int firstIndex = anchorIndex;
    int lastPage = focusPage;
    int lastIndex = focusIndex;
    if (firstPage > lastPage || (firstPage == lastPage && firstIndex > lastIndex)) {
        firstPage = focusPage;
        firstIndex = focusIndex;
        lastPage = anchorPage;
        lastIndex = anchorIndex;
    }

    if (firstPage == lastPage) {
        const int length = charactersOn(firstPage);
        const int from = qBound(0, firstIndex, length);
        const int to = qBound(0, lastIndex, length);
        if (to > from)
            ranges.append({firstPage, from, to});
        return ranges;
    }

    // From the anchor to the end of its page, then every page in between in
    // full, then from the start of the last page to the focus.
    const int firstLength = charactersOn(firstPage);
    const int from = qBound(0, firstIndex, firstLength);
    if (firstLength > from)
        ranges.append({firstPage, from, firstLength});

    for (int page = firstPage + 1; page < lastPage; ++page) {
        const int length = charactersOn(page);
        if (length > 0)
            ranges.append({page, 0, length});
    }

    const int lastLength = charactersOn(lastPage);
    const int to = qBound(0, lastIndex, lastLength);
    if (to > 0)
        ranges.append({lastPage, 0, to});
    return ranges;
}

bool isEmptySelection(int anchorPage, int anchorIndex, int focusPage, int focusIndex)
{
    if (anchorPage < 0 || focusPage < 0)
        return true;
    return anchorPage == focusPage && anchorIndex == focusIndex;
}

QString selectionText(const QVector<PageTextRange> &ranges,
                      const std::function<QString(int)> &textOf)
{
    QString result;
    for (int index = 0; index < ranges.size(); ++index) {
        const PageTextRange &range = ranges.at(index);
        if (range.isEmpty())
            continue;
        const QString page = textOf(range.page);
        if (page.isEmpty())
            continue;
        if (!result.isEmpty())
            result += QLatin1Char('\n');
        // mid() rather than left/right arithmetic, and clamped rather than
        // trusted: the text is the backend's, and a range that outlived a
        // re-read of the document must not take a substring past its end.
        result += page.mid(range.from, range.to - range.from);
    }
    return result;
}
