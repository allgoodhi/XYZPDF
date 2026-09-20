#include "wordexport.h"

#include "docxwriter.h"

// Relative, like every include from inside src/: quoted includes resolve against
// this file's own directory first, and the project root is not on the include path.
#include "../document/document.h"
#include "../document/pagemetrics.h"

#include <QAtomicInt>
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QSharedPointer>

#include <algorithm>

namespace {

// How dark a pixel has to be to be ink rather than paper. Off-white paper, a
// highlight behind the text and the light edge of an anti-aliased glyph are all
// lighter than this.
constexpr int kInkLuminance = 200;

// How large a page is drawn when its ink is being sampled for colour, in pixels
// per point. About 110 dpi: enough that a line of ten-point text is fifteen pixels
// tall and its colour is unambiguous, and little enough that a page of it costs a
// fraction of the render a page image is.
constexpr double kColourSamplingScale = 1.5;

// How far apart the channels of the sampled ink have to be before it counts as a
// colour rather than as grey. Black and grey text is what nearly every document
// is made of, and a Word file that writes black into every run of its text is one
// that has overridden the reader's theme to say nothing.
constexpr int kColourSpread = 40;

// How far outside a line's box a pixel still counts as that line's text, in
// points. The box the backend reports is the ink's box, and an ascender or a
// descender reaches past it: counted as "not text" this would make every page
// look like a picture.
constexpr double kTextBoxSlack = 2.0;

// A pixel that is marking the page, as opposed to paper.
//
// A page is made of three kinds of pixels and they have to be told apart, which
// is what the three luminances here are for:
//
//   kDarkLuminance   something *drawn* that is not the words: a rule, a logo, a
//                    diagram, the letterhead of a certificate. This is what makes
//                    a page a picture, and it is the measure below.
//   kInkLuminance    a glyph. Darker than a shaded panel, lighter than nothing.
//   kMarkLuminance   anything that is not paper at all -- which includes the pale
//                    grey behind a code listing, and *that* is why a page cannot
//                    be judged by this one: pages of shaded code have most of
//                    their area marked and are pages of text, and turning them
//                    into pictures would cost the reader the selectable text to
//                    keep a background they can already see.
constexpr int kDarkLuminance = 140;
constexpr int kMarkLuminance = 246;

// How much of a page has to be *drawn* outside its text before the page is put in
// as a picture rather than as words: ink that is not the words, as a share of the
// page's area.
//
// Three percent, and the number comes from measuring documents rather than from
// taste. A letterhead and a row of logos is about that much; a diagram is more; a
// shaded code listing is nearly none, because the code in it *is* the text. The
// pages this must catch are the ones where what is missing from a conversion of
// the words is most of what the page is. This is deliberately also the boundary
// for a whole-page image, not just a cut-out figure. Word's ordinary paragraphs
// cannot reproduce a table grid, a pin diagram, or their exact positions beside
// prose; retaining the page is more faithful than splitting it into movable bits.
constexpr double kGraphicsShare = 0.015; // of the page's area

// Every other pixel, in both directions, when the page is being weighed. This is
// a measurement of a share, not of a picture: four times less work for an answer
// that is the same to the third decimal.
constexpr int kScanStep = 2;

// What makes a paragraph look centred: its lines sit about the middle of the
// column, and it does not reach the column's edges. Both halves are needed -- a
// line that fills the column has its centre in the middle of it too.
//
// In multiples of an ordinary line's height, because that is the only unit a page
// of boxes has.
constexpr double kCentredWidth = 0.85;
constexpr double kCentredSlack = 0.6;

// The smallest indent or space that means anything, in multiples of an ordinary
// line's height: half a character.
//
// Below that it is not layout, it is measurement -- a glyph's own bearing, a
// fraction of a point of rounding -- and writing it down puts an indent into a
// document that does not have one. Measured on a real Chinese manual: its
// paragraphs came out one point in from the margin, which is nothing at all, and
// a conversion that called that an indent would have indented most of the page.
constexpr double kLayoutLeast = 0.5;

// And never more than a couple of lines of room above: a page with half of it
// blank is a page with a gap in it, not a paragraph that starts halfway down.
constexpr double kSpaceBeforeMost = 2.0;

// The grid the drawn parts of a page are found on, in points, and the smallest
// thing on it that is worth calling a figure. Both bounds are needed and they are
// different bounds: a rule across the page is a point tall, a page number in the
// margin is a point wide, and neither is a diagram.
constexpr double kFigureCell = 6.0;
constexpr double kFigureLeastWidth = 20.0;
constexpr double kFigureLeastHeight = 10.0;

// Room left around a figure that is cut out of a page: a line drawn on the edge of
// a diagram is part of it.
constexpr double kFigureMargin = 2.0;

// How much further down than the page's own line spacing the next line has to
// start before it is a new paragraph rather than the next line of this one.
//
// Against the *spacing*, not the height of a line, and that distinction is the
// whole of a bug: a report whose lines are ten points tall with thirty-one
// between them -- a slide, a tightened layout, anything set with room to breathe
// -- had every one of its lines judged a paragraph of its own, because a gap of
// twenty-one points is a gap of two line heights. Forty-one lines came out as
// forty-one paragraphs.
constexpr double kParagraphGap = 1.6;

// How far short of the right margin a line can end and still be one the next
// line continues. A paragraph's last line is short -- that is what makes it the
// last one -- and a wrapped line reaches the margin.
constexpr double kShortLine = 2.5;

// And the width, as a fraction of the column, under which a line has certainly
// finished: no line of a justified paragraph is this short, because a full line
// fills the column. This is what separates a list of short items -- a list of
// parts, a set of file names, one to a line -- from a paragraph that happens to
// wrap, when the two of them are spaced exactly the same way and neither ends in
// punctuation. The line has to be *much* shorter: half the column, so that the
// raggedness of an ordinary right margin never reaches it.
constexpr double kVeryShortLine = 0.5;

// How much further in a line can start than its paragraph did before it is a new
// paragraph. A first-line indent is one or two characters, and a character is
// about a line's height wide in Chinese and half of one in Latin, so this sits
// between the two: wide enough not to fire on the raggedness of justified text,
// narrow enough to fire on an indent.
constexpr double kIndent = 1.5;

// And how far *left* of its paragraph a line can start before it is a new block
// rather than a continuation. This is what a centred heading over body text looks
// like, and it has to be well clear of kIndent: the first line of an indented
// paragraph starts further *in* than the lines under it, and a rule that broke on
// that would break up every paragraph in every Chinese document.
constexpr double kJumpLeft = 3.0;

// Whether a character is one of the scripts that do not put a space between
// words. Approximate on purpose: it decides whether joining two lines inserts a
// space, and getting it wrong by a character puts a space in the wrong place in
// one line of one paragraph.
bool isCjk(QChar character)
{
    const ushort code = character.unicode();
    return (code >= 0x2E80 && code <= 0x9FFF)    // radicals through unified ideographs
        || (code >= 0x3000 && code <= 0x303F)    // CJK punctuation
        || (code >= 0x3400 && code <= 0x4DBF)    // extension A
        || (code >= 0xF900 && code <= 0xFAFF)    // compatibility ideographs
        || (code >= 0xFF00 && code <= 0xFFEF);   // full-width forms
}

// A line joined onto the paragraph so far: with a space, unless the two sides
// are CJK -- where a line break has no space in it and one that was added shows
// up in the middle of a word.
QString joinLines(const QString &paragraph, const QString &next)
{
    if (paragraph.isEmpty())
        return next;
    if (next.isEmpty())
        return paragraph;
    if (isCjk(paragraph.at(paragraph.length() - 1)) || isCjk(next.at(0)))
        return paragraph + next;
    return paragraph + QLatin1Char(' ') + next;
}

// A line appended to the paragraph being built: joined onto the span it matches,
// or started as a span of its own.
//
// The colour is what decides, because that is the only thing a run is: two lines
// of the same colour are one piece of text, and a line in a different colour is a
// piece of its own -- which is how a phrase picked out in red in the middle of a
// paragraph stays picked out when the paragraph is written into Word.
void appendLine(Paragraph &paragraph, const TextLine &line)
{
    if (!paragraph.spans.isEmpty() && paragraph.spans.last().color == line.color) {
        paragraph.spans.last().text = joinLines(paragraph.spans.last().text, line.text);
        return;
    }
    paragraph.spans.append({line.text, line.color});
}

QString translated(const char *text)
{
    return QCoreApplication::translate("WordExport", text);
}

} // namespace

QString Paragraph::text() const
{
    QString all;
    for (const TextSpan &span : spans)
        all += span.text;
    return all;
}

QColor inkColor(const QImage &page, const QRectF &box, double pixelsPerPoint)
{
    if (page.isNull() || pixelsPerPoint <= 0 || box.isEmpty())
        return {};

    // The box in the page image's own pixels, rounded outwards rather than to the
    // nearest pixel: a line is two or three of them tall, and losing the last one
    // to rounding is losing a third of the ink.
    const QRect area = QRectF(box.left() * pixelsPerPoint, box.top() * pixelsPerPoint,
                              box.width() * pixelsPerPoint, box.height() * pixelsPerPoint)
                           .toAlignedRect()
                           .intersected(page.rect());
    if (area.isEmpty())
        return {};

    QVector<QPair<int, QRgb>> ink; // luminance and colour, both of them per pixel
    ink.reserve(area.width() * area.height() / 4);
    const QImage rgb = page.format() == QImage::Format_RGB32 ||
                               page.format() == QImage::Format_ARGB32 ||
                               page.format() == QImage::Format_ARGB32_Premultiplied
                           ? page
                           : page.convertToFormat(QImage::Format_RGB32);
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb pixel = line[x];
            const int luminance = qGray(pixel);
            if (luminance < kInkLuminance)
                ink.append({luminance, pixel});
        }
    }
    if (ink.isEmpty())
        return {}; // nothing written here at all

    // Most of the box dark, which is not a line of text: it is a filled shape, or
    // a page whose paper is dark and whose text is light. Either way what is in
    // this box is not ink on paper, and the colour of it is not the colour of the
    // text -- so it is left alone rather than got wrong.
    if (ink.size() * 4 > area.width() * area.height() * 3)
        return {};

    // The darkest quarter of the ink, rather than all of it: the edge of a glyph
    // is anti-aliased towards the paper it sits on, and a line of small red text
    // averaged whole comes back pink.
    std::sort(ink.begin(), ink.end(),
              [](const QPair<int, QRgb> &left, const QPair<int, QRgb> &right) {
                  return left.first < right.first;
              });
    const int take = qMax(1, int(ink.size()) / 4);
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    for (int index = 0; index < take; ++index) {
        const QRgb pixel = ink.at(index).second;
        red += qRed(pixel);
        green += qGreen(pixel);
        blue += qBlue(pixel);
    }
    const QColor sampled(int(red / take), int(green / take), int(blue / take));

    const int highest = qMax(qMax(sampled.red(), sampled.green()), sampled.blue());
    const int lowest = qMin(qMin(sampled.red(), sampled.green()), sampled.blue());
    if (highest - lowest < kColourSpread)
        return {}; // grey, which is what text is when nobody said otherwise
    return sampled;
}

PageInk inkOutsideLines(const QImage &page, const QVector<QRectF> &lines, double pixelsPerPoint)
{
    PageInk ink;
    if (page.isNull() || pixelsPerPoint <= 0)
        return ink;

    // The lines, in the render's own pixels and opened out by a little: what a
    // glyph's ascender and descender do is still the line.
    //
    // Scaled, and that is not a detail: the boxes are in points and everything
    // below is in pixels. Left in points they sit in the top-left corner of the
    // page, and every page is then judged against rectangles that have nothing to
    // do with where its text is -- which reports a share that is neither right nor
    // obviously wrong, on every page of every document.
    const double slack = kTextBoxSlack * pixelsPerPoint;
    QVector<QRectF> boxes;
    boxes.reserve(lines.size());
    for (const QRectF &line : lines) {
        boxes.append(QRectF(line.left() * pixelsPerPoint - slack,
                            line.top() * pixelsPerPoint - slack,
                            line.width() * pixelsPerPoint + 2 * slack,
                            line.height() * pixelsPerPoint + 2 * slack));
    }

    qint64 drawn = 0;
    qint64 marked = 0;
    QVector<QPair<int, int>> spans;

    const QImage rgb = page.format() == QImage::Format_RGB32 ||
                               page.format() == QImage::Format_ARGB32 ||
                               page.format() == QImage::Format_ARGB32_Premultiplied
                           ? page
                           : page.convertToFormat(QImage::Format_RGB32);

    for (int y = 0; y < page.height(); y += kScanStep) {
        // The stretches of this row the text covers. Rebuilt per row rather than
        // tested per pixel: forty lines against a thousand pixels a row is forty
        // thousand comparisons, and the other way round is two million.
        spans.clear();
        for (const QRectF &box : boxes) {
            if (y < box.top() || y > box.bottom())
                continue;
            const int from = qMax(0, int(box.left()));
            const int to = qMin(page.width() - 1, int(box.right()));
            if (to >= from)
                spans.append({from, to});
        }
        std::sort(spans.begin(), spans.end());

        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        int span = 0;
        for (int x = 0; x < page.width(); x += kScanStep) {
            const int luminance = qGray(line[x]);
            if (luminance >= kMarkLuminance)
                continue; // paper, and most of a page is
            while (span < spans.size() && spans.at(span).second < x)
                ++span;
            if (span < spans.size() && spans.at(span).first <= x)
                continue; // the words, which is what a conversion would keep
            ++marked;
            if (luminance < kDarkLuminance)
                ++drawn;
        }
    }

    // Counted over the pixels that were looked at rather than over all of them:
    // the scan takes every other pixel in both directions, and dividing by the
    // whole image would report a share four times smaller than it is.
    const qint64 across = (page.width() + kScanStep - 1) / kScanStep;
    const qint64 down = (page.height() + kScanStep - 1) / kScanStep;
    const qint64 sampled = across * down;
    if (sampled > 0) {
        ink.drawnShare = double(drawn) / double(sampled);
        ink.markedShare = double(marked) / double(sampled);
    }
    return ink;
}

QVector<QRectF> figureRegions(const QImage &page, const QVector<QRectF> &boxes,
                              double pixelsPerPoint)
{
    QVector<QRectF> regions;
    if (page.isNull() || pixelsPerPoint <= 0)
        return regions;

    const int cell = qMax(1, qRound(kFigureCell * pixelsPerPoint));
    const int cols = (page.width() + cell - 1) / cell;
    const int rows = (page.height() + cell - 1) / cell;
    if (cols <= 0 || rows <= 0)
        return regions;

    // The words, in pixels and opened out a little: a glyph's bearing reaches
    // outside the box the backend reports, and a mark a point wide outside a line
    // is not a figure.
    const double slack = kTextBoxSlack * pixelsPerPoint;
    QVector<QRectF> words;
    words.reserve(boxes.size());
    for (const QRectF &box : boxes) {
        words.append(QRectF(box.left() * pixelsPerPoint - slack,
                            box.top() * pixelsPerPoint - slack,
                            box.width() * pixelsPerPoint + 2 * slack,
                            box.height() * pixelsPerPoint + 2 * slack));
    }

    // Whether a sampled pixel is inside a word, tested against a mask built once
    // from the word rectangles rather than against the list of words for every
    // pixel. A page of a pin-out has hundreds of words and a million sampled
    // pixels, and their product is where this used to spend its time.
    const int step = kScanStep;
    const int sw = (page.width() + step - 1) / step;
    const int sh = (page.height() + step - 1) / step;
    QByteArray inWord(sw * sh, 0);
    for (const QRectF &word : words) {
        const int x0 = qMax(0, int(word.left()) / step);
        const int x1 = qMin(sw - 1, int(word.right()) / step);
        const int y0 = qMax(0, int(word.top()) / step);
        const int y1 = qMin(sh - 1, int(word.bottom()) / step);
        for (int y = y0; y <= y1; ++y) {
            char *row = inWord.data() + y * sw;
            for (int x = x0; x <= x1; ++x)
                row[x] = 1;
        }
    }

    const QImage rgb = page.format() == QImage::Format_RGB32 ||
                               page.format() == QImage::Format_ARGB32 ||
                               page.format() == QImage::Format_ARGB32_Premultiplied
                           ? page
                           : page.convertToFormat(QImage::Format_RGB32);

    const auto hasDrawing = [&](int row, int col) {
        const int x0 = col * cell;
        const int y0 = row * cell;
        const int x1 = qMin(rgb.width() - 1, x0 + cell - 1);
        const int y1 = qMin(rgb.height() - 1, y0 + cell - 1);
        for (int y = y0; y <= y1; y += step) {
            const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            const char *wordRow = inWord.constData() + (y / step) * sw;
            for (int x = x0; x <= x1; x += step) {
                if (qGray(line[x]) >= kMarkLuminance)
                    continue; // paper
                if (!wordRow[x / step])
                    return true;
            }
        }
        return false;
    };

    // The cells that have something drawn in them, and the islands they form with
    // their four neighbours.
    QVector<int> island(cols * rows, -1);
    QVector<int> stack;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int start = row * cols + col;
            if (island.at(start) != -1 || !hasDrawing(row, col))
                continue;

            const int number = regions.size();
            int minRow = row;
            int maxRow = row;
            int minCol = col;
            int maxCol = col;
            island[start] = number;
            stack.clear();
            stack.append(start);
            while (!stack.isEmpty()) {
                const int at = stack.takeLast();
                const int atRow = at / cols;
                const int atCol = at % cols;
                minRow = qMin(minRow, atRow);
                maxRow = qMax(maxRow, atRow);
                minCol = qMin(minCol, atCol);
                maxCol = qMax(maxCol, atCol);
                const int neighbours[4][2] = {{atRow - 1, atCol},
                                              {atRow + 1, atCol},
                                              {atRow, atCol - 1},
                                              {atRow, atCol + 1}};
                for (const auto &neighbour : neighbours) {
                    const int nextRow = neighbour[0];
                    const int nextCol = neighbour[1];
                    if (nextRow < 0 || nextRow >= rows || nextCol < 0 || nextCol >= cols)
                        continue;
                    const int next = nextRow * cols + nextCol;
                    if (island.at(next) != -1 || !hasDrawing(nextRow, nextCol))
                        continue;
                    island[next] = number;
                    stack.append(next);
                }
            }

            // In points, and only when it is both wide and tall enough to be
            // something: a rule across the page is a point tall and a page number
            // in the margin is a point wide, and neither is a figure.
            const QRectF box(QPointF(minCol * kFigureCell, minRow * kFigureCell),
                             QPointF((maxCol + 1) * kFigureCell, (maxRow + 1) * kFigureCell));
            if (box.width() < kFigureLeastWidth || box.height() < kFigureLeastHeight)
                continue;
            regions.append(box);
        }
    }

    // In the order a reader meets them: top to bottom, and left to right where two
    // of them sit side by side, which is what a row of pin-outs is.
    std::sort(regions.begin(), regions.end(), [](const QRectF &left, const QRectF &right) {
        if (qAbs(left.top() - right.top()) > kFigureCell)
            return left.top() < right.top();
        return left.left() < right.left();
    });
    return regions;
}

struct ExtractedTable
{
    QRectF box;
    QVector<double> columns;
    QVector<double> rows;
};

bool mayContainRuledTable(const QImage &page)
{
    if (page.isNull())
        return false;
    // Read the pixels as ARGB words straight off each scanline. This gate runs on
    // every page that has text, over the whole page, twice -- through the column
    // pass -- and QImage::pixel() re-dispatches on the format for every one of
    // those reads, which on an A4 is the difference between milliseconds and a
    // tenth of a second a page.
    const QImage rgb = page.format() == QImage::Format_RGB32 ||
                               page.format() == QImage::Format_ARGB32 ||
                               page.format() == QImage::Format_ARGB32_Premultiplied
                           ? page
                           : page.convertToFormat(QImage::Format_RGB32);
    const int w = rgb.width();
    const int h = rgb.height();
    if (w <= 0 || h <= 0)
        return false;

    // One row-major pass does both directions: a running horizontal length per
    // row, and a per-column run carried down the rows. The column pass is the one
    // that used to walk the image in the cache-hostile order; here the columns are
    // remembered in a row of counters instead.
    const auto strokeGroups = [](const QVector<int> &positions) {
        int groups = 0;
        for (int index = 0; index < positions.size();) {
            ++groups;
            int end = index;
            while (end + 1 < positions.size() && positions.at(end + 1) <= positions.at(end) + 2)
                ++end;
            index = end + 1;
        }
        return groups;
    };

    QVector<int> colRun(w, 0);
    QVector<int> colLongest(w, 0);
    QVector<int> horizontal;
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        int run = 0;
        int longest = 0;
        for (int x = 0; x < w; ++x) {
            const bool dark = qGray(line[x]) < kDarkLuminance;
            run = dark ? run + 1 : 0;
            longest = qMax(longest, run);
            colRun[x] = dark ? colRun[x] + 1 : 0;
            colLongest[x] = qMax(colLongest[x], colRun[x]);
        }
        if (longest >= 100)
            horizontal.append(y);
    }
    QVector<int> vertical;
    for (int x = 0; x < w; ++x) {
        if (colLongest[x] >= 60)
            vertical.append(x);
    }
    return strokeGroups(horizontal) >= 3 && strokeGroups(vertical) >= 3;
}

// Finds the continuous strokes of a ruled table. Text makes short dark runs;
// a cell border crosses nearly the whole candidate region, which remains true
// for a PDF rendered with anti-aliasing and for lightly grey table rules.
bool tableGrid(const QImage &page, const QRectF &region, double scale, ExtractedTable *result)
{
    if (!result || page.isNull() || scale <= 0)
        return false;
    // The candidate region is where the text and figure ink was found, which a
    // hairline border can sit a few points outside of: the outer rules of the grid
    // are drawn beyond the last glyph and beyond the cell that produced the region.
    // Search a little wider than asked so those border strokes are counted; the
    // "a rule crosses most of the region" test below still holds, because a grid's
    // border spans the whole grid however far the region is padded out.
    const double padX = region.width() * 0.1 + 12.0;
    const double padY = region.height() * 0.1 + 12.0;
    const QRectF pagePoints(0, 0, page.width() / scale, page.height() / scale);
    const QRectF search = region.adjusted(-padX, -padY, padX, padY).intersected(pagePoints);
    const QRect bounds = QRectF(search.left() * scale, search.top() * scale,
                                search.width() * scale, search.height() * scale)
                            .toAlignedRect().intersected(page.rect());
    if (bounds.width() < 40 || bounds.height() < 40)
        return false;

    const auto grouped = [](const QVector<int> &positions) {
        QVector<double> lines;
        for (int at = 0; at < positions.size();) {
            int end = at;
            while (end + 1 < positions.size() && positions.at(end + 1) <= positions.at(end) + 2)
                ++end;
            lines.append((positions.at(at) + positions.at(end)) / 2.0);
            at = end + 1;
        }
        return lines;
    };

    const QImage rgb = page.format() == QImage::Format_RGB32 ||
                               page.format() == QImage::Format_ARGB32 ||
                               page.format() == QImage::Format_ARGB32_Premultiplied
                           ? page
                           : page.convertToFormat(QImage::Format_RGB32);
    // One row-major pass counts the dark pixels per row and, in a row of
    // counters, per column -- rather than walking the region twice, the second
    // time down the columns. The region is scanned at 200 dpi, so this is a few
    // hundred thousand pixels per table on the page.
    const int bw = bounds.width();
    QVector<int> colDark(bw, 0);
    QVector<int> horizontal;
    QVector<int> vertical;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        int dark = 0;
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (qGray(line[x]) < kDarkLuminance) {
                ++dark;
                ++colDark[x - bounds.left()];
            }
        }
        if (dark >= bounds.width() * 0.65)
            horizontal.append(y);
    }
    for (int x = 0; x < bw; ++x) {
        if (colDark[x] >= bounds.height() * 0.65)
            vertical.append(bounds.left() + x);
    }
    const QVector<double> ys = grouped(horizontal);
    const QVector<double> xs = grouped(vertical);
    if (xs.size() < 3 || ys.size() < 3)
        return false;

    result->box = QRectF(xs.first() / scale, ys.first() / scale,
                         (xs.last() - xs.first()) / scale, (ys.last() - ys.first()) / scale);
    result->columns.clear();
    result->rows.clear();
    for (double x : xs)
        result->columns.append(x / scale);
    for (double y : ys)
        result->rows.append(y / scale);
    return result->box.width() >= kFigureLeastWidth && result->box.height() >= kFigureLeastHeight;
}

int tableSlot(const QVector<double> &edges, double point)
{
    for (int index = 0; index + 1 < edges.size(); ++index) {
        if (point >= edges.at(index) - 1.0 && point <= edges.at(index + 1) + 1.0)
            return index;
    }
    return -1;
}

// Whether two boxes are two pieces of one line rather than two lines.
//
// The runs of a page are not its lines: a justified line, a line with a
// superscript in it, a line a PDF placed piece by piece comes back as several
// boxes side by side. Their middles have to be at about the same height -- not
// merely overlapping, because the descenders of one line reach down into the
// next -- and the second has to begin about where the first ends, because a piece
// of text on the far side of the page is a column and not the same line.
bool sameLine(const QRectF &previous, const QRectF &next)
{
    const double body = qMax(previous.height(), next.height());
    if (body <= 0)
        return false;
    if (std::fabs(previous.center().y() - next.center().y()) > body * 0.6)
        return false;
    const double gap = next.left() - previous.right();
    return gap <= body * 3.0 && gap >= -body * 0.5;
}

// Whether a line ends a sentence, and so -- with the line being short -- ends the
// paragraph it is in.
//
// This is the signal that survives a page whose lines are evenly spaced, where
// nothing about the geometry says where one item stops and the next starts: a
// line that ends in a full stop and does not reach the margin has finished what
// it was saying. Closing quotes and brackets are looked past, because a sentence
// in Chinese usually ends with the punctuation inside them.
bool endsSentence(const QString &text)
{
    static const QString enders = QStringLiteral("。．.！!？?；;：:");
    static const QString closers = QStringLiteral("”’」』》）)]】〕〉…");
    for (int index = text.length() - 1; index >= 0; --index) {
        const QChar character = text.at(index);
        if (closers.contains(character))
            continue;
        return enders.contains(character);
    }
    return false;
}

// The page's own line spacing: how far apart consecutive lines usually are.
//
// The median of the steps between them, so that a heading with a space under it
// does not decide what normal is. With two lines there is nothing to compare
// against and this is that one step, which is why the gap rule cannot see a
// paragraph break on a page that has only two.
double linePitch(const QVector<TextLine> &lines)
{
    QVector<double> steps;
    steps.reserve(lines.size());
    for (int index = 1; index < lines.size(); ++index)
        steps.append(lines.at(index).box.top() - lines.at(index - 1).box.top());
    if (steps.isEmpty())
        return 0.0;
    std::sort(steps.begin(), steps.end());
    return steps.at(steps.size() / 2);
}

// How much of a page goes into a picture of it, in dots per inch.
//
// 200 puts an A4 page at 1653x2339 pixels: sharp enough for the fine grid lines,
// pin labels and Chinese glyphs in a technical manual, while still comfortably
// under the per-page cap. A page image is not the page's own data --
// that is the scan's, and it is inside the PDF where nothing here can get at it
// except by rendering it -- so this is the resolution of a *copy* of the page,
// and the copy has to be worth emailing.
constexpr double kImageDpi = 200.0;

// The most pixels one page may take. A poster-sized page at 200 dpi is sixty-two
// megapixels, which is a hundred megabytes of image in the working set and a
// picture nobody wants in a document; past this the scale comes down rather than
// the page going in at all.
constexpr qint64 kMaximumImagePixels = 8LL * 1024 * 1024;

// What the JPEG is written at. High enough that the glyphs of ordinary text do
// not smear, low enough that a page stays small.
constexpr int kImageQuality = 85;

// One page as a picture: encoded, with what it is and how big it should be.
//
// Rendered at a resolution of its own rather than at whatever the reader happens
// to be zoomed to -- a scan exported while the window was showing it at 40% would
// be unreadable, and one exported at 800% would be gigantic, and neither is what
// the document says the page is.
// The page on white paper.
//
// A rendered page has no background of its own: the white is supplied by whoever
// shows it, and every pixel the page did not paint is transparent. Anything that
// reads the render has to put it on paper first -- to a JPEG, which has no
// transparency, transparent comes out black; to code looking at colour alone,
// transparent reads as black ink, and a page of red text on "black paper" is a
// page with nothing to say about the colour of its text.
QImage onPaper(const QImage &page)
{
    QImage paper(page.size(), QImage::Format_RGB32);
    paper.fill(Qt::white);
    QPainter painter(&paper);
    painter.drawImage(0, 0, page);
    return paper;
}

// An image as the bytes of a JPEG, or of a PNG when this build cannot write one.
//
// JPEG first and deliberately: what is put in a Word file from a PDF is a scan or
// a diagram, and a JPEG of either is a fraction of the size of a PNG for no
// difference a reader can see.
QByteArray encodedImage(const QImage &image, QString *extension)
{
    if (image.isNull())
        return {};

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (image.save(&buffer, "JPEG", kImageQuality)) {
        *extension = QStringLiteral("jpeg");
        return bytes;
    }

    bytes.clear();
    QBuffer second(&bytes);
    second.open(QIODevice::WriteOnly);
    if (!image.save(&second, "PNG"))
        return {};
    *extension = QStringLiteral("png");
    return bytes;
}

QByteArray pageImage(Document &document, int page, QString *extension, QSizeF *points)
{
    const QSizeF size = document.pageSize(page);
    if (size.isEmpty())
        return {};

    QSize pixels = PageMetrics::pixelsFor(size, kImageDpi / 72.0, 1.0);
    if (qint64(pixels.width()) * qint64(pixels.height()) > kMaximumImagePixels) {
        const double scale =
            std::sqrt(double(kMaximumImagePixels) / (double(pixels.width()) * double(pixels.height())));
        pixels = QSize(qMax(1, int(pixels.width() * scale)), qMax(1, int(pixels.height() * scale)));
    }

    RenderOptions options;
    // Annotations drawn in, like the reader shows them: a conversion that dropped
    // the highlights somebody made is a conversion that lost work.
    options.annotations = true;
    const QImage rendered = document.render(page, pixels, options);
    if (rendered.isNull())
        return {};

    const QImage flat = onPaper(rendered);

    // The extension has to match what was actually written, because it is the name
    // the image has inside the file -- and that is what encodedImage() promises.
    const QByteArray encoded = encodedImage(flat, extension);
    if (encoded.isEmpty())
        return {};

    *points = size;
    return encoded;
}

QVector<QRectF> lineBoxesOfPage(const Document &document, int page)
{
    // A run is not a line, and taking one for one turns a justified line -- or a
    // line with a superscript in it, or any line a PDF placed piece by piece --
    // into as many lines as it has pieces.
    QVector<QRectF> boxes;
    for (const TextRun &run : document.textRuns(page)) {
        if (run.box.width() <= 0 || run.box.height() <= 0)
            continue;
        if (!boxes.isEmpty() && sameLine(boxes.last(), run.box))
            boxes.last() = boxes.last().united(run.box);
        else
            boxes.append(run.box);
    }
    return boxes;
}

QVector<TextLine> linesOfPage(const Document &document, int page, const QAtomicInt *cancel)
{
    QVector<TextLine> lines;
    const QVector<QRectF> boxes = lineBoxesOfPage(document, page);

    lines.reserve(boxes.size());
    for (const QRectF &box : boxes) {
        // Between lines, which is where the time goes and so where a stop has to
        // be able to land: a page of a hundred and thirty lines is nine seconds
        // of these two queries per line, and waiting for the page rather than for
        // the line means a reader who asked to stop watches it carry on for nine
        // more seconds -- which reads as a button that does nothing.
        if (cancel && cancel->loadRelaxed())
            return lines; // a prefix, and the caller knows to throw it away

        // The line's first character and one past its last, asked as a range.
        // characterIndexAt answers with the character that starts after a point,
        // so the box's left edge is the line's first character and its right edge
        // is where the next line begins -- which is exactly what pageSelection
        // wants, and exactly the text of this line. This is the same pairing the
        // search highlights and the selection are built from; there is no other
        // one available, and the backend's own comment says why guessing is worse
        // than asking.
        //
        // Asked once for the whole line rather than once per piece: the range
        // covers the gaps between the pieces, so the text comes back with the
        // spaces the page has in it -- which is the spaces a reader gets by
        // selecting that line and copying it.
        const double middle = box.center().y();
        const int from = document.characterIndexAt(page, QPointF(box.left(), middle));
        const int to = document.characterIndexAt(page, QPointF(box.right(), middle));
        if (from < 0 || to <= from)
            continue;

        const QString text = document.pageSelection(page, from, to).text.trimmed();
        if (text.isEmpty())
            continue;
        lines.append({box, text});
    }
    return lines;
}

QVector<Paragraph> paragraphsFromLines(const QVector<TextLine> &lines)
{
    QVector<Paragraph> paragraphs;
    if (lines.isEmpty())
        return paragraphs;

    // The height of an ordinary line, taken as the median rather than the mean so
    // that the heading at the top of a page does not decide what ordinary is.
    QVector<double> heights;
    heights.reserve(lines.size());
    for (const TextLine &line : lines)
        heights.append(line.box.height());
    std::sort(heights.begin(), heights.end());
    const double body = qMax(1.0, heights.at(heights.size() / 2));

    // The right margin, as the furthest right any line on this page reaches: a
    // page has no other way to say where the text stops, and a paragraph that is
    // still going reaches it.
    double rightMargin = 0;
    for (const TextLine &line : lines)
        rightMargin = qMax(rightMargin, line.box.right());

    // And how far apart this page's lines usually are, which is what a "gap"
    // between two of them has to be measured against.
    const double pitch = linePitch(lines);

    // The width of the column the text is set in, from the leftmost line to the
    // furthest right one: what "short" is short of.
    double leftMargin = lines.first().box.left();
    for (const TextLine &line : lines)
        leftMargin = qMin(leftMargin, line.box.left());
    const double textWidth = qMax(0.0, rightMargin - leftMargin);

    // Where the paragraphs sit, filled in as each one is finished: where its lines
    // are is what says whether it is centred, how far it is set in, and how much
    // room was left above it. Points to twips, because that is what Word counts
    // in, and read out of the same boxes the paragraph was built from.
    const auto settle = [&](Paragraph &target, double minLeft, double maxRight,
                            double firstLeft, double roomAbove) {
        const double width = qMax(0.0, maxRight - minLeft);
        const double centre = (minLeft + maxRight) / 2;
        const bool centred = textWidth > 0 && width < textWidth * kCentredWidth
                          && qAbs(centre - (leftMargin + rightMargin) / 2) < body * kCentredSlack;
        if (centred) {
            // And no indents either: an indent on a centred paragraph would push
            // it off centre by exactly what it indents by.
            target.align = ParagraphAlign::Center;
            return;
        }
        const double inset = qMax(0.0, minLeft - leftMargin);
        if (inset > body * kLayoutLeast)
            target.indentTwips = qRound(inset * 20);
        const double firstLine = qMax(0.0, firstLeft - minLeft);
        if (firstLine > body * kLayoutLeast)
            target.firstLineTwips = qRound(firstLine * 20);
        if (roomAbove > body * kLayoutLeast)
            target.spaceBeforeTwips = qRound(roomAbove * 20);
    };

    Paragraph paragraph;
    double paragraphLeft = 0;
    double minLeft = 0;
    double maxRight = 0;
    double roomAbove = 0;
    QRectF previous;
    QString previousText;
    bool started = false;

    for (const TextLine &line : lines) {
        bool fresh = !started;
        // The room above this line beyond the page's own rhythm, and zero unless
        // this line turns out to begin a paragraph.
        double above = 0;
        if (started) {
            if (pitch > 0 && line.box.top() - previous.top() > pitch * kParagraphGap) {
                // Further down than this page's own line spacing: whatever was
                // between the two is a blank line, or the end of a block.
                fresh = true;
            } else if (textWidth > 0 && previous.width() < textWidth * kVeryShortLine) {
                // Far too short to be a line that ran out of room: whatever it is
                // -- a name, an item of a list, the last line of a paragraph --
                // the next line begins something else.
                fresh = true;
            } else if (previous.right() < rightMargin - body * kShortLine
                       && endsSentence(previousText)) {
                // The line before ended its sentence and did not reach the
                // margin: it had finished, and this is the next paragraph. Both
                // halves are needed -- a line that ends in a full stop at the
                // margin is one the next line continues, and a short line that
                // stops mid-sentence is one that ran out of room for its last
                // word.
                fresh = true;
            } else if (line.box.left() > paragraphLeft + body * kIndent) {
                fresh = true; // indented, or centred: a new paragraph either way
            } else if (line.box.left() < paragraphLeft - body * kJumpLeft) {
                // Back out to the margin from a block that started further in:
                // the centred heading is over and the text under it has begun.
                fresh = true;
            }
        }

        if (fresh) {
            if (started)
                above = qMin(qMax(0.0, line.box.top() - previous.top() - pitch),
                             pitch * kSpaceBeforeMost);
            if (!paragraph.spans.isEmpty()) {
                settle(paragraph, minLeft, maxRight, paragraphLeft, roomAbove);
                paragraphs.append(paragraph);
            }
            paragraph = Paragraph();
            appendLine(paragraph, line);
            paragraph.top = line.box.top();
            paragraphLeft = line.box.left();
            minLeft = line.box.left();
            maxRight = line.box.right();
            roomAbove = above;
        } else {
            appendLine(paragraph, line);
            minLeft = qMin(minLeft, line.box.left());
            maxRight = qMax(maxRight, line.box.right());
        }
        previous = line.box;
        previousText = line.text;
        started = true;
    }

    if (!paragraph.spans.isEmpty()) {
        settle(paragraph, minLeft, maxRight, paragraphLeft, roomAbove);
        paragraphs.append(paragraph);
    }
    return paragraphs;
}

WordExportReport exportToWord(Document &document, const QString &path,
                              const WordExportOptions &options)
{
    const QSharedPointer<QAtomicInt> &cancel = options.cancel;
    WordExportReport report;
    report.path = path;

    if (path.isEmpty()) {
        report.error = translated("没有指定要写入的文件。");
        return report;
    }

    DocxWriter writer;
    bool wroteAPage = false;

    const int pages = document.pageCount();
    // The paper the document is written on is the paper of the PDF: its first
    // page's size, with a small margin. Told rather than left to a reader's
    // default, because a page image on paper of the wrong size is either shrunk
    // or cut in half, and both look like the export went wrong.
    if (pages > 0)
        writer.setPageSize(document.pageSize(0));

    // A stop is asked for from another thread and looked at here, at every place
    // where there is work to stop: between pages, between the lines of a page
    // (see linesOfPage), around the renders, and once more before the file is
    // written -- which is the last moment at which stopping can still mean a
    // document that was not touched at all.
    const auto abandoned = [&cancel] { return cancel && cancel->loadRelaxed(); };
    const auto stop = [&report] {
        report.cancelled = true;
        report.error = translated("导出已取消。");
        return report;
    };

    // Said before the first page rather than after it. A page of a real manual is
    // seconds of character queries, and a reader who is looking for the way out
    // has to be offered one while there is still time to use it -- a box that
    // appears once the first page is done is a box that appears when a third of
    // the work is already behind it.
    if (options.progress && pages > 0)
        options.progress(0, pages);

    for (int page = 0; page < pages; ++page) {
        // Between pages, which is where a document being replaced can be noticed.
        // Half a file from one document and half from another is worse than none:
        // the reader would have no way to tell.
        if (abandoned())
            return stop();

        // Only the boxes, to begin with. Everything asked of a page before its
        // words are wanted -- is it mostly words, and what colour is its text --
        // is a question about where the lines are, and the boxes cost two merged
        // runs per line against two character queries. A page that turns out to be
        // a picture therefore never has its text read at all, which is the whole
        // of the difference between exporting this manual in two seconds and in
        // twenty-three.
        const QVector<QRectF> boxes = lineBoxesOfPage(document, page);
        if (abandoned())
            return stop();

        // The page as it was drawn. Two things come out of this one render: the
        // colour of the text, which the backend does not have (Qt PDF gives out
        // the characters and the boxes they sit in and nothing about how any of
        // it was set), and the answer to whether this page is mostly words or
        // mostly something else. It is cheap -- three milliseconds measured on a
        // page of a manual, against the seconds the text calls take.
        QImage paper;
        if (!boxes.isEmpty()) {
            RenderOptions sampling;
            sampling.annotations = true;
            const QImage drawn = document.render(
                page,
                PageMetrics::pixelsFor(document.pageSize(page), kColourSamplingScale, 1.0),
                sampling);
            if (!drawn.isNull()) {
                // On paper: the render's own background is transparent, and
                // transparent is black to anything that looks at colour alone.
                paper = onPaper(drawn);
            }
        }

        // Mostly words, or mostly not? A certificate with a coloured panel, a
        // letterhead and a row of logos at the bottom has a text layer, so it is
        // not a scan -- and exporting its words alone is what "the conversion
        // looks poor" means for a page like that. The page goes in as the page.
        const PageInk ink =
            paper.isNull() ? PageInk() : inkOutsideLines(paper, boxes, kColourSamplingScale);
        // Thin ruled tables have much less ink than a diagram and can stay below
        // the page-area threshold. Their connected grid is still a drawn region,
        // so let that geometry start table recognition without treating ordinary
        // text glyphs as a figure.
        const bool hasFigures = !boxes.isEmpty() && ink.drawnShare > kGraphicsShare;

        // A page with no text layer is a scan, drawing or photograph. There is
        // nothing to write as editable text, so it is retained as its page image.
        if (boxes.isEmpty()) {
            if (abandoned())
                return stop();
            QString extension;
            QSizeF sizePoints;
            const QByteArray image = pageImage(document, page, &extension, &sizePoints);
            if (image.isEmpty()) {
                ++report.emptyPages;
            } else {
                writer.addImage(image, extension, sizePoints, wroteAPage);
                wroteAPage = true;
                ++report.pages;
                ++report.imagePages;
            }
        } else {
            // Now the words, since this page is a page of them.
            QVector<TextLine> lines = linesOfPage(document, page, cancel.data());
            if (abandoned())
                return stop(); // what came back is the beginning of a page
            if (!paper.isNull()) {
                for (TextLine &line : lines)
                    line.color = inkColor(paper, line.box, kColourSamplingScale);
            }

            // Keep a real mixed page mixed: its prose remains editable and each
            // diagram/table is inserted as the part of the rendered page it is.
            // Only a failed segmentation falls back to a whole-page image below;
            // that is preferable to silently dropping drawing content.
            struct Figure
            {
                QRectF box;
                QByteArray bytes;
                QString extension;
            };
            QVector<Figure> figures;
            struct Table
            {
                double top = 0;
                DocxTable contents;
            };
            QVector<Table> tables;
            // A ruled table can be made of hairline strokes and therefore occupy
            // less than the graphics-area threshold. Pages with several text
            // lines get a cheap table probe as well; an ordinary prose page that
            // finds neither a table nor a figure remains ordinary editable text.
            const bool probeTables = mayContainRuledTable(paper);
            if ((hasFigures || probeTables) && !abandoned()) {
                RenderOptions detail;
                detail.annotations = true;
                const double scale = kImageDpi / 72.0;
                const QImage zoomed = document.render(
                    page, PageMetrics::pixelsFor(document.pageSize(page), scale, 1.0), detail);
                if (!zoomed.isNull()) {
                    const QImage flat = onPaper(zoomed);
                    QVector<QRectF> regions = figureRegions(flat, boxes, scale);
                    // Only the regions figureRegions found are candidates to be cut
                    // out as pictures. The text bounds below is added solely to seed
                    // table detection -- it is the whole text area, so cropping it
                    // as a figure would turn a page of prose and tables into one
                    // big image and lose the editable text.
                    const int figureRegionCount = regions.size();
                    if (probeTables) {
                        QRectF textBounds;
                        for (const TextLine &line : std::as_const(lines))
                            textBounds = textBounds.isNull() ? line.box : textBounds.united(line.box);
                        if (!textBounds.isNull())
                            regions.append(textBounds.adjusted(-45, -45, 45, 45));
                    }
                    QVector<QRectF> gridBoxes;
                    for (int regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
                        const QRectF &region = regions.at(regionIndex);
                        const bool mayCropAsFigure = regionIndex < figureRegionCount;
                        // The figure geometry and the text bounds often straddle the
                        // same grid; a table once taken must not be taken twice.
                        bool alreadyFound = false;
                        for (const QRectF &found : std::as_const(gridBoxes)) {
                            if (found.intersected(region).width()
                                    >= region.width() * 0.5)
                                alreadyFound = true;
                        }
                        if (alreadyFound)
                            continue;
                        ExtractedTable grid;
                        if (tableGrid(flat, region, scale, &grid)) {
                            gridBoxes.append(grid.box);
                            Table table;
                            table.top = grid.box.top();
                            for (int column = 0; column + 1 < grid.columns.size(); ++column)
                                table.contents.columnWidthsPoints.append(
                                    grid.columns.at(column + 1) - grid.columns.at(column));
                            const int rowCount = grid.rows.size() - 1;
                            const int columnCount = table.contents.columnWidthsPoints.size();
                            table.contents.cells.resize(rowCount);
                            for (int row = 0; row < rowCount; ++row)
                                table.contents.cells[row].resize(columnCount);

                            // A text line belongs to the cell containing its centre.
                            // PDF boxes include a glyph's bearings, so intersections
                            // would misfile text sitting against a grid line.
                            for (int index = lines.size() - 1; index >= 0; --index) {
                                const QPointF centre = lines.at(index).box.center();
                                const int row = tableSlot(grid.rows, centre.y());
                                const int column = tableSlot(grid.columns, centre.x());
                                if (row < 0 || column < 0)
                                    continue;
                                QVector<DocxRun> run;
                                run.append({lines.at(index).text, lines.at(index).color});
                                table.contents.cells[row][column].append(run);
                                lines.removeAt(index);
                            }
                            tables.append(table);
                            continue;
                        }
                        if (!mayCropAsFigure)
                            continue; // a table-probe region that found no grid
                        const QRect crop =
                            QRectF((region.left() - kFigureMargin) * scale,
                                   (region.top() - kFigureMargin) * scale,
                                   (region.width() + 2 * kFigureMargin) * scale,
                                   (region.height() + 2 * kFigureMargin) * scale)
                                .toAlignedRect()
                                .intersected(flat.rect());
                        if (crop.isEmpty())
                            continue;
                        Figure figure;
                        figure.box = region;
                        figure.bytes = encodedImage(flat.copy(crop), &figure.extension);
                        if (!figure.bytes.isEmpty())
                            figures.append(figure);
                    }
                }


                if (hasFigures && figures.isEmpty() && tables.isEmpty()) {
                    // The page definitely has non-text ink, but it did not make a
                    // usable isolated region. Writing its words alone would lose
                    // something the reader can see, so preserve this one page.
                    QString extension;
                    QSizeF sizePoints;
                    const QByteArray image = pageImage(document, page, &extension, &sizePoints);
                    if (image.isEmpty()) {
                        ++report.emptyPages;
                    } else {
                        writer.addImage(image, extension, sizePoints, wroteAPage);
                        wroteAPage = true;
                        ++report.pages;
                        ++report.imagePages;
                    }
                    if (options.progress)
                        options.progress(page + 1, pages);
                    continue;
                }

                // Labels inside a diagram are already in its image. Do not write
                // them again as a second, displaced copy. Table text was removed
                // above only after it was placed in its editable cell.
                for (int index = lines.size() - 1; index >= 0; --index) {
                    for (const Figure &figure : std::as_const(figures)) {
                        if (figure.box.contains(lines.at(index).box)) {
                            lines.removeAt(index);
                            break;
                        }
                    }
                }
                report.figures += figures.size();
                report.tables += tables.size();
            }

            const QVector<Paragraph> paragraphs = paragraphsFromLines(lines);
            bool firstOfPage = true;
            int nextFigure = 0;
            int nextTable = 0;
            const auto writeNextDrawing = [&] {
                const bool tableFirst = nextTable < tables.size()
                    && (nextFigure >= figures.size()
                        || tables.at(nextTable).top <= figures.at(nextFigure).box.top());
                if (tableFirst) {
                    writer.addTable(tables.at(nextTable).contents, firstOfPage && wroteAPage);
                    ++nextTable;
                } else {
                    const Figure &figure = figures.at(nextFigure);
                    writer.addImage(figure.bytes, figure.extension, figure.box.size(),
                                    firstOfPage && wroteAPage);
                    ++nextFigure;
                }
                firstOfPage = false;
            };
            for (const Paragraph &paragraph : paragraphs) {
                while ((nextFigure < figures.size() && figures.at(nextFigure).box.top() <= paragraph.top)
                       || (nextTable < tables.size() && tables.at(nextTable).top <= paragraph.top))
                    writeNextDrawing();
                QVector<DocxRun> runs;
                runs.reserve(paragraph.spans.size());
                for (const TextSpan &span : paragraph.spans)
                    runs.append({span.text, span.color});
                DocxLayout layout;
                layout.align = paragraph.align == ParagraphAlign::Center
                                   ? DocxLayout::Align::Center
                                   : DocxLayout::Align::Left;
                layout.indentTwips = paragraph.indentTwips;
                layout.firstLineTwips = paragraph.firstLineTwips;
                layout.spaceBeforeTwips = paragraph.spaceBeforeTwips;
                writer.addParagraph(runs, firstOfPage && wroteAPage, layout);
                firstOfPage = false;
            }
            while (nextFigure < figures.size() || nextTable < tables.size())
                writeNextDrawing();
            wroteAPage = true;
            ++report.pages;
            report.paragraphs += int(paragraphs.size());
        }

        // After every page, including the ones that went in as pictures: those
        // are the slow ones, and they are the reason this is here at all.
        if (options.progress)
            options.progress(page + 1, pages);
    }

    if (report.pages == 0) {
        report.error = translated(
            "这份 PDF 没有可导出的内容：既没有文字层，也没有渲染得出来的页面。");
        return report;
    }

    // The last moment at which stopping still means "nothing was written": past
    // here the file exists, and a report saying it was cancelled while the file
    // sits there finished would be worse than either answer on its own.
    if (abandoned())
        return stop();

    QString error;
    if (!writer.save(path, &error)) {
        report.error = translated("写入文件失败：%1").arg(error);
        return report;
    }
    report.ok = true;
    return report;
}
