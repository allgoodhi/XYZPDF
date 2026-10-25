// The font a searchable PDF embeds: valid TrueType, every glyph empty, every advance
// one em.
//
// ---- why generate one rather than ship one ----
//
// The invisible text layer names every character it writes with a CID, and the CIDs
// have to be glyph indices (/CIDToGIDMap /Identity), so the font must have at least as
// many glyphs as the document has distinct characters. A Chinese book can reach a few
// thousand; a fixed font large enough for every case would put tens of thousands of
// empty glyph records into every file, and a fixed font that is smaller would drop the
// characters it ran out of. Building it per document makes it exactly as large as the
// document's own text -- the table with one row per glyph is the only part that grows.
//
// Tesseract's PDF writer does the same thing for the same reason (its glyphless font is
// likewise all empty glyphs), and it is why a 500-page scan does not carry a typeface it
// never draws.
//
// ---- the numbers ----
//
// 1000 units per em, ascent 800, descent -200, every glyph 1000 wide and 0 tall. The
// PDF FontDescriptor repeats those numbers (textlayerwriter.cpp), and the widths it
// declares in /W are 1000 too; if the two ever disagree the invisible text is placed
// with one idea of its advance and measured with another, which shows up as a selection
// highlight that ends in the middle of the last word.
//
// The tables are the minimum FreeType will open a TrueType face from: head, hhea, maxp,
// hmtx, loca, glyf (empty), cmap, name and post. There is no OS/2: nothing on this path
// asks for one, and a font program is only ever read by the PDF engine here.
#pragma once

#include <QByteArray>

// A TrueType font with `glyphs` glyphs, glyph 0 (.notdef) included. Empty when `glyphs`
// is not 1..65535, which is the whole range a TrueType numGlyphs can name.
QByteArray glyphlessTrueType(int glyphs);
