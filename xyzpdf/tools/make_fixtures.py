#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Writes the PDFs the tests are run against.

    python tools/make_fixtures.py

Why this exists
---------------

The fixtures have to be PDFs that a real PDF engine accepts, and the ones that
matter most are the awkward ones: two pages of different sizes, a page with a
/Rotate flag, and a file that is truncated. Hand-written PDFs are the only way
to get those exactly as intended -- a file exported from an office program
carries whatever its producer decided, and cannot be made to be damaged on
purpose.

The files are committed, and this script is how to get them again if one is
edited or lost. It is deliberately plain: no library, because the build machine
has none, and the xref table has to be byte-accurate anyway, which is easier to
get right by writing it out than by trusting a generator to.

What is NOT here yet, and why
-----------------------------

A CJK fixture: a page with Chinese text needs a font with Chinese glyphs, and
embedding one in a hand-written PDF means carrying a subset font in the
repository. M2 is where text selection and search arrive, and that is the
milestone that will need it -- see docs/PLAN.md §5.
"""
import os
import sys
import zlib

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, 'tests', 'fixtures')


def makePdf(pages):
    """A PDF with one page per entry in `pages`.

    Each entry is (width, height, rotate, text, extra_keys) with the size in
    points, rotate in degrees or 0, and text drawn at the top left.

    A text containing "\\n" is drawn as that many lines, thirty points apart,
    which is what the multi-line fixture needs: the text of a document with one
    line per page has no line breaks in it, and a line break is exactly the
    thing that used to break selection on every page of every real document.
    """
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)  # 1-based object number

    # The font is a standard one, so nothing has to be embedded: the point of
    # these files is the geometry, not the typography.
    font = add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica '
               b'/Encoding /WinAnsiEncoding >>')

    page_ids = []
    content_ids = []
    for index, (width, height, rotate, text, extra) in enumerate(pages):
        lines = text.split('\n')
        drawn = 'BT /F1 24 Tf 60 %d Td 0 0 Td' % (int(height) - 80)
        for position, line in enumerate(lines):
            if position:
                drawn += ' 0 -30 Td'
            drawn += ' (%s) Tj' % line
        drawn += ' ET'
        stream = drawn.encode('ascii')
        compressed = zlib.compress(stream)
        content_ids.append(add(b'<< /Length %d /Filter /FlateDecode >>\nstream\n' % len(compressed)
                               + compressed + b'\nendstream'))
        rotate_key = b' /Rotate %d' % rotate if rotate else b''
        page_ids.append(add(
            b'<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d]%s'
            b' /Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R%s >>'
            % (0, int(width), int(height), rotate_key, font, content_ids[-1], extra)))

    # The page tree needs the page ids, so it is written after them; the parent
    # reference above is patched in here, which is why the placeholder 0 is
    # there.
    kids = b' '.join(b'%d 0 R' % page for page in page_ids)
    pages_id = add(b'<< /Type /Pages /Kids [%s] /Count %d >>' % (kids, len(page_ids)))
    catalog = add(b'<< /Type /Catalog /Pages %d 0 R >>' % pages_id)

    # The parent in every page object pointed at object 0. Rewritten now that
    # the tree exists.
    for index, page in enumerate(page_ids):
        objects[page - 1] = objects[page - 1].replace(b'/Parent 0 0 R',
                                                      b'/Parent %d 0 R' % pages_id)

    body = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n')
    offsets = [0]
    for number, content in enumerate(objects, start=1):
        offsets.append(len(body))
        body += b'%d 0 obj\n' % number + content + b'\nendobj\n'

    xref_at = len(body)
    body += b'xref\n0 %d\n' % (len(objects) + 1)
    body += b'0000000000 65535 f \n'
    for offset in offsets[1:]:
        body += b'%010d 00000 n \n' % offset
    body += (b'trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n'
             % (len(objects) + 1, catalog, xref_at))
    return bytes(body)


def write(name, data):
    path = os.path.join(FIXTURES, name)
    with open(path, 'wb') as handle:
        handle.write(data)
    print('wrote tests/fixtures/%s (%d bytes)' % (name, len(data)))
    return data


def main():
    os.makedirs(FIXTURES, exist_ok=True)

    # Two pages of different sizes, with text on both. The size difference is
    # the point: a viewer that assumes every page is the first one's size draws
    # the second one wrong, and it is the kind of mistake that only shows up on
    # documents nobody made on purpose.
    text = write('text.pdf', makePdf([
        (595, 842, 0, 'XYZPDF one', b''),
        (400, 500, 0, 'XYZPDF two', b''),
    ]))

    # One page stored portrait and marked 90 degrees. What Qt PDF reports for
    # its size -- the stored one or the rotated one -- is measured rather than
    # assumed, and document_smoke pins the answer.
    write('rotated.pdf', makePdf([
        (300, 400, 90, 'XYZPDF rotated', b''),
    ]))

    # Forty pages, for the memory bound. What a viewer holds has to stay flat as
    # somebody scrolls through a document, and a two-page file cannot show that:
    # it fits in view twice over.
    write('many-pages.pdf', makePdf([
        (595, 842, 0, 'XYZPDF page %d' % (index + 1), b'') for index in range(40)
    ]))

    # One page with no text on it at all -- the empty string draws nothing. This
    # is the page a scan is: no text layer, so every point on it is "not over a
    # line", and a drag anywhere on it is a box rather than a selection. The
    # region-drag test runs against this file.
    write('blank.pdf', makePdf([
        (595, 842, 0, '', b''),
    ]))

    # Three lines on one page. The other fixtures put one line per page, which
    # means their text has no line breaks in it -- and a line break is a
    # character with no geometry, which is what used to make a click on any line
    # but the first land at an arbitrary place in the document. Reported as
    # "Chinese cannot be selected", because the document that showed it up had
    # more than one line and happened to be Chinese; the line break was the
    # cause, not the script.
    write('lines.pdf', makePdf([
        (595, 842, 0, 'XYZPDF first line\nsecond line here\nthird line', b''),
    ]))

    # Truncated: the header, a page object and half the xref table. A PDF
    # reader that trusts its parser crashes on files like this one, and the
    # point of the fixture is that opening it does not.
    write('damaged.pdf', text[:len(text) * 2 // 3])

    return 0


if __name__ == '__main__':
    sys.exit(main())
