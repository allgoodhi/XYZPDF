#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Writes packaging/app.ico, the icon Explorer reads off the executable.

    python tools/makeappicon.py

Why this exists
---------------

Windows takes the icon on the .exe from a resource that has to be a real .ico,
and the .ico has to contain several sizes: 16 for the tree, 32 for the taskbar,
48 for the desktop, 256 for the large views. One bitmap scaled down is visibly
wrong at 16 -- the artwork turns to mush -- so each size is drawn.

There is no image library on the build machine (no Pillow, no ImageMagick), and
adding one to build an icon would be a dependency to install before the project
could ever be built. What is needed is a few hundred lines of geometry, which is
what this is: the same shapes as Resources/icons/app.svg, rasterised by hand and
written out as PNG-compressed ICO frames.

The window icon is the SVG (see main.cpp) and this is the file Explorer reads.
They are kept in step by hand, which is why both files carry the same colours
written down once each.
"""
import os
import struct
import sys
import zlib

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, 'packaging', 'app.ico')

SIZES = (16, 24, 32, 48, 64, 128, 256)

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


def png(size):
    """A PNG of one frame. PNG rather than a raw BMP because Vista and later
    read PNG frames in an ICO, and 256x256 as a BMP would be 256 KB on its own.
    """
    raw = b''.join(b'\x00' + row for row in render(size))

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))

    header = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    frames = [(size, png(size)) for size in SIZES]

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
    return 0


if __name__ == '__main__':
    sys.exit(main())
