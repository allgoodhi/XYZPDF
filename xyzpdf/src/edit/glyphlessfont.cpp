#include "glyphlessfont.h"

#include <QVector>

namespace {

constexpr unsigned kVersion = 0x00010000u;
constexpr unsigned kUnitsPerEm = 1000;
constexpr int kAscent = 800;
constexpr int kDescent = -200;
constexpr int kAdvance = 1000;
constexpr unsigned kMagic = 0x5F0F3CF5u;

void appendU8(QByteArray &out, unsigned value)
{
    out.append(char(value & 0xFFu));
}

void appendU16(QByteArray &out, unsigned value)
{
    out.append(char((value >> 8) & 0xFFu));
    out.append(char(value & 0xFFu));
}

void appendU32(QByteArray &out, quint32 value)
{
    appendU16(out, value >> 16);
    appendU16(out, value & 0xFFFFu);
}

void appendI16(QByteArray &out, int value)
{
    appendU16(out, unsigned(quint16(value)));
}

void appendU64(QByteArray &out, quint64 value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        appendU8(out, unsigned((value >> shift) & 0xFFu));
}

// The TrueType checksum: the table read as big-endian 32-bit words, summed, with the
// last word padded with zeros. 'head' is checksummed with its checkSumAdjustment field
// zero -- which is how it is built here, before the whole-file adjustment is patched in.
quint32 checksumOf(const QByteArray &data)
{
    quint32 sum = 0;
    for (int i = 0; i < data.size(); i += 4) {
        quint32 word = 0;
        for (int j = 0; j < 4; ++j)
            word = (word << 8) | (i + j < data.size() ? quint8(data.at(i + j)) : 0u);
        sum += word;
    }
    return sum;
}

QByteArray paddedTo4(QByteArray data)
{
    while (data.size() % 4 != 0)
        data.append('\0');
    return data;
}

struct Table
{
    const char *tag;
    QByteArray data;
};

} // namespace

QByteArray glyphlessTrueType(int glyphs)
{
    if (glyphs < 1 || glyphs > 65535)
        return {};

    // ---- head (54 bytes) ----
    QByteArray head;
    appendU32(head, kVersion);
    appendU32(head, kVersion); // fontRevision
    appendU32(head, 0);        // checkSumAdjustment, patched once the file exists
    appendU32(head, kMagic);
    appendU16(head, 0x0003); // bits 0 and 1: the baseline is y=0 and the left edge x=0
    appendU16(head, kUnitsPerEm);
    appendU64(head, 0); // created
    appendU64(head, 0); // modified
    appendI16(head, 0);             // xMin
    appendI16(head, kDescent);      // yMin
    appendI16(head, kAdvance);      // xMax
    appendI16(head, kAscent);       // yMax
    appendU16(head, 0);             // macStyle
    appendU16(head, 8);             // lowestRecPPEM
    appendI16(head, 2);             // fontDirectionHint
    appendI16(head, 0);             // indexToLocFormat: short offsets
    appendI16(head, 0);             // glyphDataFormat

    // ---- hhea (36 bytes) ----
    QByteArray hhea;
    appendU32(hhea, kVersion);
    appendI16(hhea, kAscent);
    appendI16(hhea, kDescent);
    appendI16(hhea, 0); // lineGap
    appendU16(hhea, kAdvance); // advanceWidthMax
    appendI16(hhea, 0);        // minLeftSideBearing
    appendI16(hhea, 0);        // minRightSideBearing
    appendI16(hhea, kAdvance); // xMaxExtent
    appendI16(hhea, 1);        // caretSlopeRise
    appendI16(hhea, 0);        // caretSlopeRun
    appendI16(hhea, 0);        // caretOffset
    for (int i = 0; i < 4; ++i)
        appendI16(hhea, 0); // reserved
    appendI16(hhea, 0);          // metricDataFormat
    appendU16(hhea, unsigned(glyphs)); // numberOfHMetrics

    // ---- maxp (32 bytes, version 1.0) ----
    QByteArray maxp;
    appendU32(maxp, kVersion);
    appendU16(maxp, unsigned(glyphs));
    appendU16(maxp, 0); // maxPoints
    appendU16(maxp, 0); // maxContours
    appendU16(maxp, 0); // maxCompositePoints
    appendU16(maxp, 0); // maxCompositeContours
    appendU16(maxp, 1); // maxZones
    for (int i = 0; i < 8; ++i)
        appendU16(maxp, 0);

    // ---- hmtx: one metric per glyph, all the same ----
    QByteArray hmtx;
    for (int i = 0; i < glyphs; ++i) {
        appendU16(hmtx, kAdvance);
        appendI16(hmtx, 0); // leftSideBearing
    }

    // ---- glyf: four zero bytes, and no glyph points into them ----
    //
    // FreeType will not open a scalable face whose glyf table is missing, and some versions
    // refuse one of length zero; four zeros is a table it accepts and no outline to draw.
    //
    // What a reader gets from empty glyphs is measured, and it is worth knowing before this
    // is changed. With them, FPDFText hands back each character's box at the place and width
    // the layer asked for -- 247.7 pt against the 247.3 pt the recognizer measured -- and a
    // height of a hundredth of a point, because a character's box comes from the glyph's own
    // bounding box and not from the /Ascent and /Descent the FontDescriptor declares. A
    // selection over a layer like this is therefore a hairline. Giving every glyph a
    // rectangle of the em instead ((0,-200) to (1000,800), one contour, four points) was
    // tried, and the boxes came back 1.8 em tall with their bottoms a whole em above the
    // baseline: the outline is read as ink, and the ink of this font is meant to be nowhere.
    // So the glyphs stay empty, and the hairline is the next person's to fix -- most likely
    // on the reading side, by asking for the loose box where a selection is drawn, which is
    // a question about selection drawing rather than about this file.
    const QByteArray glyf(4, '\0');

    // ---- loca: every glyph empty, i.e. every offset zero ----
    // The short format (offsets in units of two bytes) holds for any number of glyphs the
    // header can name, because they are all at zero.
    QByteArray loca;
    for (int i = 0; i <= glyphs; ++i)
        appendU16(loca, 0);

    // ---- cmap: format 0, the whole byte range mapped to .notdef ----
    // Nothing reads this: /CIDToGIDMap /Identity means a CID is already a glyph index,
    // and the characters a reader copies come from the PDF's /ToUnicode. It is here
    // because a TrueType face without a cmap is a font some parsers will not load.
    QByteArray cmap;
    appendU16(cmap, 0);  // version
    appendU16(cmap, 1);  // one encoding record
    appendU16(cmap, 3);  // Windows
    appendU16(cmap, 0);  // symbol
    appendU32(cmap, 12); // offset of the subtable
    appendU16(cmap, 0);  // format 0
    appendU16(cmap, 262);
    appendU16(cmap, 0);  // language
    cmap.append(QByteArray(256, '\0'));

    // ---- name: none, and that is legal ----
    QByteArray name;
    appendU16(name, 0); // format
    appendU16(name, 0); // count
    appendU16(name, 6); // stringOffset

    // ---- post: version 3.0, "no glyph names" ----
    QByteArray post;
    appendU32(post, 0x00030000u);
    appendU32(post, 0); // italicAngle
    appendI16(post, 0); // underlinePosition
    appendI16(post, 0); // underlineThickness
    for (int i = 0; i < 5; ++i)
        appendU32(post, 0); // isFixedPitch, min/maxMemType42, min/maxMemType1

    // Sorted by tag, which the format requires.
    const QVector<Table> tables{
        {"cmap", cmap}, {"glyf", glyf}, {"head", head}, {"hhea", hhea}, {"hmtx", hmtx},
        {"loca", loca}, {"maxp", maxp}, {"name", name}, {"post", post},
    };

    // The offset table: the binary-search numbers are a property of the count, not a
    // hint parsers are allowed to skip.
    int entrySelector = 0;
    while ((1 << (entrySelector + 1)) <= tables.size())
        ++entrySelector;
    const int searchRange = 16 * (1 << entrySelector);
    const int rangeShift = 16 * tables.size() - searchRange;

    QByteArray font;
    appendU32(font, kVersion);
    appendU16(font, unsigned(tables.size()));
    appendU16(font, unsigned(searchRange));
    appendU16(font, unsigned(entrySelector));
    appendU16(font, unsigned(rangeShift));

    int offset = 12 + 16 * tables.size();
    int headOffset = -1;
    QByteArray body;
    for (const Table &table : tables) {
        // The record is tag, checksum, offset, length -- in that order. Written the other
        // way round, every tag lands in the checksum's slot and the file is a face no
        // parser can load, which is not a failure that stops a PDF from opening: the text
        // layer reads back, the page draws, and only the boxes are wrong.
        font.append(table.tag);
        appendU32(font, checksumOf(table.data));
        appendU32(font, quint32(offset));
        appendU32(font, quint32(table.data.size()));
        if (qstrcmp(table.tag, "head") == 0)
            headOffset = offset;
        const QByteArray data = paddedTo4(table.data);
        body.append(data);
        offset += data.size();
    }
    font.append(body);

    // The whole-file adjustment, per the format: 0xB1B0AFBA minus the file's own
    // checksum, stored in head. The head table's directory checksum stays the one
    // computed with this field zero, which is what the specification asks for.
    const quint32 adjustment = 0xB1B0AFBAu - checksumOf(font);
    for (int i = 0; i < 4; ++i)
        font[headOffset + 8 + i] = char((adjustment >> (24 - 8 * i)) & 0xFFu);
    return font;
}
