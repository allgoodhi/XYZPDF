#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Writes the pictures the package needs: packaging/app.ico, the icon Explorer
reads off the executable, and the tile images in packaging/Assets/.

    python tools/makeappicon.py

Why this exists
---------------

Windows takes the icon on the .exe from a resource that has to be a real .ico,
and the .ico has to contain several sizes: 16 for the tree, 32 for the taskbar,
48 for the desktop, 256 for the large views. One bitmap scaled down is visibly
wrong at 16 -- the artwork turns to mush -- so each size is drawn.

The package has the same requirement in a different form: AppxManifest.xml names
four tile images and `makeappx` refuses a package whose declared files are
missing, so those four are written here as well. They come from the same
rasteriser as the .ico, which is the only way the icon in the taskbar and the
tile in the Start menu are the same drawing rather than two that drifted.

Each of the four is written at every scale Windows can ask for, and a file per
scale rather than one file resized at runtime: a 44-pixel logo shown on a 200%
display is a 44-pixel bitmap stretched to 88, and the thin red rule in the page
artwork lands between two device pixels and goes grey. The names are how the
scale is chosen (`Square44x44Logo.scale-200.png`), which is also why the packing
scripts have to index them into `resources.pri` -- with no PRI there is nothing
mapping the name in the manifest onto the five files that exist for it, and what
the user then sees is an entry with the right name and no picture at all, on a
package that installs and reports Status Ok.

There is no image library on the build machine (no Pillow, no ImageMagick), and
adding one to build an icon would be a dependency to install before the project
could ever be built. What is needed is a few hundred lines of geometry, which is
what this is: the same shapes as Resources/icons/app.svg, rasterised by hand and
written out as PNG-compressed ICO frames and plain PNGs.

The window icon is the SVG (see main.cpp) and these are the files Explorer and
the shell read. They are kept in step by hand, which is why all three carry the
same colours written down once each.
"""
import os
import struct
import sys
import zlib

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, 'packaging', 'app.ico')
ASSETS = os.path.join(ROOT, 'packaging', 'Assets')

SIZES = (16, 24, 32, 48, 64, 128, 256)

# The names AppxManifest.xml uses, and the size each one has to be. The wide
# tile is the only non-square one: the artwork is drawn square and centred on
# it, which is what the Start menu expects of a logo rather than a banner.
TILES = (
    ('StoreLogo.png', 50, 50),
    ('Square44x44Logo.png', 44, 44),
    ('Square150x150Logo.png', 150, 150),
    ('Wide310x150Logo.png', 310, 150),
)

# The display scales Windows may pick a tile at. 100 is the plain name; a
# package that offers nothing else is what every scaled display then stretches.
SCALES = (100, 125, 150, 200, 400)

# Exact pixel sizes some surfaces ask for instead of a scale, and only for the
# 44 tile: the Start menu's small icons and the app list want one of these.
# There is no `_altform-unplated` set beside them, unlike XYZip's assets,
# because that form asks for the logo without its background plate and this
# artwork's red rounded square *is* the drawing -- an unplated copy would be the
# same pixels, so the plated file is what every surface is offered.
TARGETS = (16, 24, 32, 48, 256)

# Supersampling. Edges drawn at 16 pixels are mostly edge, and without this the
# tree icon is a red blob with a white smear in it.
SUPERSAMPLE = 4

BACKDROP = (196, 48, 43, 255)
PAGE = (255, 255, 255, 255)
FOLD = (217, 221, 227, 255)
LINE = (196, 48, 43, 255)
LINE_FAINT = (227, 166, 162, 255)


def in_rounded_rect(u, v, left, top, right, bottom, radius):
    """Whether a point is inside a rounded rectangle, in 0..1 coordinates."""
    if u < left or u > right or v < top or v > bottom:
        return False
    # Pull the point into the corner squares and test the circle there; the
    # straight parts are already answered above.
    cx = min(max(u, left + radius), right - radius)
    cy = min(max(v, top + radius), bottom - radius)
    return (u - cx) ** 2 + (v - cy) ** 2 <= radius * radius


def in_fold(u, v):
    """The turned-down corner: the top right of the page, cut diagonally."""
    if not (0.61 <= u <= 0.78 and 0.14 <= v <= 0.31):
        return False
    return (u - 0.61) + (v - 0.14) >= 0.17


def in_line(u, v, top, height, width):
    return 0.297 <= u <= 0.297 + width and top <= v <= top + height


def render(size):
    """One RGBA frame, as a list of rows of bytes."""
    rows = []
    step = 1.0 / (size * SUPERSAMPLE)
    samples = SUPERSAMPLE * SUPERSAMPLE

    for j in range(size):
        row = bytearray()
        for i in range(size):
            backdrop = page = fold = 0
            ink = faint = 0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    u = (i * SUPERSAMPLE + sx + 0.5) * step
                    v = (j * SUPERSAMPLE + sy + 0.5) * step
                    inside_page = in_rounded_rect(u, v, 0.172, 0.141, 0.828, 0.859, 0.047)
                    if inside_page:
                        if in_fold(u, v):
                            fold += 1
                        else:
                            page += 1
                            if in_line(u, v, 0.492, 0.039, 0.406):
                                ink += 1
                            elif in_line(u, v, 0.602, 0.039, 0.406):
                                faint += 1
                            elif in_line(u, v, 0.711, 0.039, 0.266):
                                faint += 1
                    if in_rounded_rect(u, v, 0.0, 0.0, 1.0, 1.0, 0.219):
                        backdrop += 1

            # Composited back to front, each shape covering the one below it.
            colour = [0.0, 0.0, 0.0]
            alpha = backdrop / samples
            if alpha <= 0:
                row += b'\x00\x00\x00\x00'
                continue

            for index in range(3):
                colour[index] = BACKDROP[index]

            def blend(rgb, coverage, into):
                if coverage <= 0:
                    return
                weight = coverage / samples
                for index in range(3):
                    into[index] = into[index] * (1 - weight) + rgb[index] * weight

            blend(FOLD, fold, colour)
            # The page is drawn before the fold so that the fold wins where the
            # two overlap -- the turned corner is on top of the page.
            blend(PAGE, page + fold, colour)
            blend(LINE, ink, colour)
            blend(LINE_FAINT, faint, colour)

            row += bytes((int(round(colour[0])), int(round(colour[1])), int(round(colour[2])),
                          int(round(alpha * 255))))
        rows.append(bytes(row))
    return rows


def png(rows, width, height):
    """A PNG of one picture. PNG rather than a raw BMP because Vista and later
    read PNG frames in an ICO, and 256x256 as a BMP would be 256 KB on its own.
    """
    raw = b''.join(b'\x00' + row for row in rows)

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))

    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def centred(width, height, art):
    """The square artwork, drawn at `art` pixels and pasted into the middle of a
    transparent canvas. The wide tile wants a logo in the middle of it, not a
    stretched one: a page that is twice as wide as it is tall is not this page.
    """
    rows = render(art)
    left = (width - art) // 2
    top = (height - art) // 2
    blank = bytes(width * 4)
    canvas = []
    for y in range(height):
        source = y - top
        if 0 <= source < art:
            canvas.append(b'\x00' * (left * 4) + rows[source]
                          + b'\x00' * ((width - left - art) * 4))
        else:
            canvas.append(blank)
    return canvas


def write_asset(name, width, height):
    """One tile, drawn at the size `name` asks for rather than resized into it."""
    art = min(width, height)
    data = png(centred(width, height, art) if width != height else render(art),
               width, height)
    path = os.path.join(ASSETS, name)
    with open(path, 'wb') as handle:
        handle.write(data)
    print('wrote %s (%d bytes, %dx%d)'
          % (os.path.relpath(path, ROOT), len(data), width, height))


def main():
    frames = [(size, png(render(size), size, size)) for size in SIZES]

    directory = struct.pack('<HHH', 0, 1, len(frames))
    offset = len(directory) + 16 * len(frames)
    entries = b''
    for size, data in frames:
        # 256 is written as 0: the field is one byte, and 256 does not fit in
        # one. This is the ICO format's own quirk, not a mistake.
        dimension = 0 if size >= 256 else size
        entries += struct.pack('<BBBBHHII', dimension, dimension, 0, 0, 1, 32, len(data), offset)
        offset += len(data)

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, 'wb') as handle:
        handle.write(directory + entries + b''.join(data for _, data in frames))

    print('wrote %s (%d bytes, %s)'
          % (os.path.relpath(OUTPUT, ROOT), os.path.getsize(OUTPUT),
             ', '.join(str(size) for size in SIZES)))

    os.makedirs(ASSETS, exist_ok=True)
    written = set()
    for name, width, height in TILES:
        stem = name[:-len('.png')]
        for scale in SCALES:
            # Round half up, because Python's round() is banker's rounding:
            # round(62.5) answers 62, and 62.5 is what three of the four tiles
            # come to at 125% (50, 150 and 310 all end in a half). One pixel over
            # is only ever a pixel of transparent margin; one pixel under is a
            # logo with its edge cut off on every 125% display.
            w = int(width * scale / 100 + 0.5)
            h = int(height * scale / 100 + 0.5)
            out = name if scale == 100 else '%s.scale-%d.png' % (stem, scale)
            written.add(out)
            write_asset(out, w, h)

        if name == 'Square44x44Logo.png':
            for target in TARGETS:
                out = 'Square44x44Logo.targetsize-%d.png' % target
                written.add(out)
                write_asset(out, target, target)

    # A picture left over from an older scheme would be packed anyway --
    # makeappx takes the whole directory -- so it goes.
    for stale in sorted(os.listdir(ASSETS)):
        if stale.endswith('.png') and stale not in written:
            os.remove(os.path.join(ASSETS, stale))
            print('removed stale %s' % os.path.relpath(os.path.join(ASSETS, stale), ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
