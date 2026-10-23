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


def assemble(objects, catalog, info=None):
    """The bytes of a PDF from its object list, its catalog and an optional /Info.

    Split out of makePdf because a fixture that is not a page of text -- the link
    annotation one, which has to place its objects in a particular order -- needs
    the same byte-accurate xref, and a second copy of that is a second thing to
    get wrong.
    """
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
    trailer = b'trailer\n<< /Size %d /Root %d 0 R' % (len(objects) + 1, catalog)
    if info is not None:
        trailer += b' /Info %d 0 R' % info
    body += trailer + b' >>\nstartxref\n%d\n%%%%EOF\n' % xref_at
    return bytes(body)


def makePdf(pages, info=None):
    """A PDF with one page per entry in `pages`.

    Each entry is (width, height, rotate, text, extra_keys) with the size in
    points, rotate in degrees or 0, and text drawn at the top left. An optional
    sixth element is raw PDF operators appended to the page's content, which is
    how a fixture draws something that is not text.

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
    for index, page in enumerate(pages):
        width, height, rotate, text, extra = page[:5]
        drawing = page[5] if len(page) > 5 else ''
        lines = text.split('\n')
        drawn = 'BT /F1 24 Tf 60 %d Td 0 0 Td' % (int(height) - 80)
        for position, line in enumerate(lines):
            if position:
                drawn += ' 0 -30 Td'
            drawn += ' (%s) Tj' % line
        drawn += ' ET'
        if drawing:
            drawn += ' ' + drawing
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

    # The document information dictionary, in the trailer where PDF 32000-1
    # §14.3.3 puts it -- not inside the catalog. A reader that looks for /Info
    # under /Root finds nothing and says the file has no title.
    info_number = None
    if info is not None:
        info_number = add(info)

    return assemble(objects, catalog, info_number)


def write(name, data):
    path = os.path.join(FIXTURES, name)
    with open(path, 'wb') as handle:
        handle.write(data)
    print('wrote tests/fixtures/%s (%d bytes)' % (name, len(data)))
    return data


# A4 at 300 dpi, the resolution a scanner defaults to and the one that makes a
# page big enough to matter: 2480 x 3508 pixels of RGB is 26 MB of samples per
# page, which is the "单页 10 MB+" docs/PLAN.md §5 asks for.
SCAN_WIDTH = 2480
SCAN_HEIGHT = 3508

# The same A4 in points, which is what the MediaBox is measured in.
SCAN_PAGE_W = 595
SCAN_PAGE_H = 842


def _scanRows(width, offset):
    """The rows of one page, as a list to be joined.

    Built from two precomputed row images rather than per-pixel loops: a page is
    3508 rows and eight million pixels, and generating it a pixel at a time in
    Python takes long enough that nobody runs this script. The rows are chosen by
    a rule, so the page looks like a scanned text page -- white margins, lines of
    dark marks, a heading -- and nothing here is a glyph, which is the point: a
    scan has no text, and that is the case the fixture exists to exercise.
    """
    white = b'\xff' * (width * 3)

    def line(dark, light, lead):
        row = bytearray(b'\xff' * (width * 3))
        x = lead
        while x < width - lead:
            stop = min(x + dark, width - lead)
            for position in range(x, stop):
                row[position * 3:position * 3 + 3] = b'\x20\x20\x20'
            x = stop + light
        return bytes(row)

    # Two weights, so the "text" is not one uniform bar, and a shorter last line.
    heavy = line(9, 5, 140)
    light = line(7, 6, 140)
    short = line(7, 6, 140)
    short = short[:int(width * 0.62 * 3)] + white[int(width * 0.62 * 3):]

    rows = []
    for y in range(SCAN_HEIGHT):
        band = (y + offset) % 58
        if y < 240 or y > SCAN_HEIGHT - 240:
            rows.append(white)                     # the margins
        elif band < 22 and 300 < y < SCAN_HEIGHT - 300:
            rows.append(heavy if ((y // 58) + offset) % 3 else light)
        elif band == 23:
            rows.append(short)
        else:
            rows.append(white)
    return rows


def makeScanPdf(pages):
    """A document of pure images: one 300 dpi raster per page, and no text."""
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)

    page_ids = []
    for index in range(pages):
        raster = b''.join(_scanRows(SCAN_WIDTH, index * 7))
        compressed = zlib.compress(raster, 6)
        image = add(b'<< /Type /XObject /Subtype /Image /Width %d /Height %d'
                    b' /ColorSpace /DeviceRGB /BitsPerComponent 8'
                    b' /Filter /FlateDecode /Length %d >>\nstream\n'
                    % (SCAN_WIDTH, SCAN_HEIGHT, len(compressed))
                    + compressed + b'\nendstream')
        content = add(b'<< /Length %d >>\nstream\nq %d 0 0 %d 0 0 cm /Im0 Do Q\nendstream'
                      % (len(b'q %d 0 0 %d 0 0 cm /Im0 Do Q' % (SCAN_PAGE_W, SCAN_PAGE_H)),
                         SCAN_PAGE_W, SCAN_PAGE_H))
        page_ids.append(add(
            b'<< /Type /Page /Parent 0 0 R /MediaBox [0 0 %d %d]'
            b' /Resources << /XObject << /Im0 %d 0 R >> >> /Contents %d 0 R >>'
            % (SCAN_PAGE_W, SCAN_PAGE_H, image, content)))

    kids = b' '.join(b'%d 0 R' % page for page in page_ids)
    pages_id = add(b'<< /Type /Pages /Kids [%s] /Count %d >>' % (kids, len(page_ids)))
    catalog = add(b'<< /Type /Catalog /Pages %d 0 R >>' % pages_id)
    for page in page_ids:
        objects[page - 1] = objects[page - 1].replace(b'/Parent 0 0 R',
                                                      b'/Parent %d 0 R' % pages_id)

    return assemble(objects, catalog)


def makeLinksPdf():
    """Two pages, the first with two link annotations on it.

    The shapes are from PDF 32000-1, not from the reader: an annotation
    dictionary (Table 164: /Type /Annot, /Subtype /Link, /Rect, /P), a URI action
    (§12.4.4, /S /URI /URI (http://...)), and an explicit go-to (§12.4.2.1,
    /S /GoTo /D [page /XYZ left top zoom]). The destination is written as an
    array rather than as a named destination because the point of the fixture is
    what a link inside the document carries: the page *and* the place on it,
    including the null zoom, which means "leave the zoom alone".

    The rectangles are in PDF user space with the origin at the page's
    bottom-left, and one of them deliberately overlaps two lines of the page's
    own text: a link box is a rectangle, not a line, and a reader that draws its
    hotspots from the text layer instead of the annotation gets the wrong shape
    here.
    """
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)

    font = add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica '
               b'/Encoding /WinAnsiEncoding >>')

    def content(lines, top):
        drawn = 'BT /F1 18 Tf 60 %d Td' % top
        for position, line in enumerate(lines):
            if position:
                drawn += ' 0 -30 Td'
            drawn += ' (%s) Tj' % line
        drawn += ' ET'
        stream = drawn.encode('ascii')
        return add(b'<< /Length %d >>\nstream\n' % len(stream) + stream + b'\nendstream')

    first = content(['XYZPDF has two links on it', 'the sentence goes under both'], 760)
    second = content(['XYZPDF the link target'], 760)

    # The annotation objects come before the pages that list them, and /P points
    # back at a page whose number is not known yet -- so the parent placeholders
    # are patched below, the same way the page tree's are.
    external = add(b'<< /Type /Annot /Subtype /Link /Rect [55 750 300 775] '
                   b'/Border [0 0 0] /P 0 0 R '
                   b'/A << /Type /Action /S /URI /URI (https://example.com/xyzpdf) >> >>')
    internal = add(b'<< /Type /Annot /Subtype /Link /Rect [55 718 300 743] '
                   b'/Border [0 0 0] /P 0 0 R '
                   b'/A << /Type /Action /S /GoTo /D [0 0 R /XYZ 0 700 null] >> >>')

    page_one = add(b'<< /Type /Page /Parent 0 0 R /MediaBox [0 0 595 842] '
                   b'/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R '
                   b'/Annots [%d 0 R %d 0 R] >>' % (font, first, external, internal))
    page_two = add(b'<< /Type /Page /Parent 0 0 R /MediaBox [0 0 595 842] '
                   b'/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>'
                   % (font, second))

    pages_id = add(b'<< /Type /Pages /Kids [%d 0 R %d 0 R] /Count 2 >>'
                   % (page_one, page_two))
    catalog = add(b'<< /Type /Catalog /Pages %d 0 R >>' % pages_id)

    for page in (page_one, page_two):
        objects[page - 1] = objects[page - 1].replace(b'/Parent 0 0 R',
                                                      b'/Parent %d 0 R' % pages_id)
    objects[external - 1] = objects[external - 1].replace(b'/P 0 0 R',
                                                           b'/P %d 0 R' % page_one)
    objects[internal - 1] = objects[internal - 1].replace(b'/P 0 0 R',
                                                           b'/P %d 0 R' % page_one)
    # The go-to's destination names its page. Patched last, because the page
    # numbers only exist once both pages have been added.
    objects[internal - 1] = objects[internal - 1].replace(b'/D [0 0 R',
                                                           b'/D [%d 0 R' % page_two)

    return assemble(objects, catalog)


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

    # Six hundred pages. docs/PLAN.md §5 asks for a fixture of "500+ pages", and
    # M1's acceptance is "a 500-page document scrolls without lag" -- a claim the
    # forty-page file above cannot support, because forty pages of layout is a
    # list short enough that anything is smooth. Whatever the page tree costs to
    # walk and the cache costs to keep flat only shows up at this size, so this
    # is the file the performance baseline in docs/perf.md is measured against.
    #
    # It stays small on disk: every page is one line of text in a compressed
    # stream, so the size is the page tree, not the content.
    write('huge.pdf', makePdf([
        (595, 842, 0, 'XYZPDF huge page %d' % (index + 1), b'') for index in range(600)
    ]))

    # A scan: three pages of 300 dpi RGB, and no text anywhere in them. The
    # property being stood up is the one every other fixture cannot give -- a page
    # whose every point is "not over a line", so a drag is a box rather than a
    # selection, a search finds nothing, and the pixels that have to be decoded
    # and downsampled before the page can be shown are 26 MB of them.
    #
    # blank.pdf is also textless, but it is one blank page at screen resolution:
    # it exercises "there is no text" and nothing else. The M1 acceptance line
    # "a scan (pure image, 10 MB+ per page) pages normally" needs a page that is
    # actually big, and docs/perf.md measures this one.
    #
    # Small on disk despite the samples: a scanned page is mostly white, and
    # Flate is good at that. It is the decompressed size that costs.
    write('scan.pdf', makeScanPdf(3))

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

    # A page that is a picture rather than text: a filled red rectangle, drawn
    # with an operator and no font at all. This is what a scan is, reduced to its
    # geometry -- something put ink on the page and there is no text layer to
    # read -- and it is the fixture the export's image path needs. blank.pdf
    # cannot show that path working: a white page comes out white whether the
    # picture was copied or not.
    write('picture.pdf', makePdf([
        (595, 842, 0, '', b'', '1 0 0 rg 60 400 475 380 re f'),
    ]))

    # One line of text drawn in pieces, which is how a justified line, a line
    # with a superscript in it, or any line a PDF placed piece by piece comes
    # back from the backend: several boxes side by side, on one line. Taking a box
    # for a line turns one line into a paragraph per piece -- and a real report
    # had a sentence come out as "推荐" / "使用" / "较高" / "配置", one word to a
    # paragraph, for exactly that reason.
    write('runs.pdf', makePdf([
        (595, 842, 0, '', b'',
         'BT /F1 24 Tf 60 700 Td (XYZPDF piece one ) Tj 200 0 Td (and piece two) Tj ET'),
    ]))

    # Small text with wide spacing: ten-point glyphs with thirty-one points
    # between the lines, which is what a slide or a deliberately loose layout
    # looks like. The gap between two of these lines is twice the height of a
    # line, so a rule that measures a paragraph break in line heights calls every
    # line a paragraph of its own -- which is what a real forty-one-line page did.
    write('spaced.pdf', makePdf([
        (595, 842, 0, '', b'',
         'BT /F1 10 Tf 60 700 Td (XYZPDF a paragraph that) Tj 0 -31 Td (wraps onto a second line.) Tj'
         ' 0 -62 Td (No. 2 is a paragraph of its own.) Tj ET'),
    ]))

    # A short black line and a long red one. The text layer carries the characters
    # and nothing about how they were set -- Qt PDF gives out no font and no
    # colour -- so the colour has to be recovered from the page itself, which is
    # what the export does and what this fixture is for. The first line is much
    # shorter than the column on purpose: that is what makes the two lines two
    # paragraphs rather than one, so that the colour of one can be told from the
    # absence of colour in the other.
    write('colored.pdf', makePdf([
        (595, 842, 0, 'XYZPDF', b'',
         'BT /F1 24 Tf 60 700 Td 1 0 0 rg (XYZPDF red line, longer than the black one) Tj ET'),
    ]))

    # Two pages of seventy lines each: a document whose export is *slow*, because
    # the reading of a page is two character queries per line and that is where
    # all of the time goes. Stopping in the middle of a page is what this exists
    # to make testable -- a page of one line has nothing to stop, and a stop that
    # only lands between pages cannot be told from one that does not work.
    def dense(prefix):
        # A str, not bytes: the drawing is appended to the page's own content and
        # the pair of them are encoded together.
        block = ' '.join('(%s %02d) Tj T*' % (prefix, index + 1) for index in range(70))
        return 'BT /F1 7 Tf 11 TL 60 810 Td ' + block + ' ET'

    write('dense.pdf', makePdf([
        (595, 842, 0, '', b'', dense('XYZPDF line')),
        (595, 842, 0, '', b'', dense('XYZPDF more')),
    ]))

    # A certificate, reduced to its parts: a pale panel the words sit on, and a
    # dark letterhead above them and a row of dark marks below -- the logos and
    # the rules, which is what a conversion of the words alone throws away.
    #
    # The page HAS a text layer, so nothing about it says "scan"; it is the pale
    # panel that is deliberately *not* what makes it a picture (a grey background
    # behind a code listing is the same shape and must stay text), and the dark
    # ink outside the words that is.
    # The line under the panel is the one that makes this a mixed page rather than
    # a drawn one: it is prose that belongs to the page and not to the drawing, and
    # a conversion of it has to come out as words *and* as a picture of the panel.
    write('graphic.pdf', makePdf([
        (595, 842, 0, 'XYZPDF words on a panel', b'',
         'BT /F1 12 Tf 60 120 Td (XYZPDF a line of its own, under the panel) Tj ET '
         '0.85 0.85 0.95 rg 60 400 475 380 re f '
         '0 0 0 rg 60 800 380 26 re f '
         '0.2 0.2 0.6 rg 150 420 120 60 re f 250 420 120 60 re f'),
    ]))

    # A ruled two-by-two table with a text layer in every cell. This is separate
    # from graphic.pdf because a panel is correctly a picture while a grid is
    # editable structure: the exporter must tell those two kinds of drawing apart.
    write('table.pdf', makePdf([
        (595, 842, 0, '', b'',
         '0 0 0 RG 1 w '
         '60 500 m 360 500 l S 60 600 m 360 600 l S 60 700 m 360 700 l S '
         '60 500 m 60 700 l S 210 500 m 210 700 l S 360 500 m 360 700 l S '
         'BT /F1 14 Tf 90 650 Td (top left) Tj 160 0 Td (top right) Tj ET '
         'BT /F1 14 Tf 90 550 Td (bottom left) Tj 135 0 Td (bottom right) Tj ET'),
    ]))

    # One page of text and one page that is a picture: the shape of a scanned
    # report with a typed cover, which is what most documents that need this are.
    # The two have to come out in the same file, in order, one as words and one as
    # a page image, with a page break between them.
    write('mixed.pdf', makePdf([
        (595, 842, 0, 'XYZPDF typed cover', b''),
        (595, 842, 0, '', b'', '0 0 1 rg 60 400 475 380 re f'),
    ]))

    # A file that says what it is. Every other fixture here has an empty
    # document information dictionary, so the Properties dialog has always had
    # nothing to show and a reader cannot tell "the file has no title" from "we
    # did not look". The keys and the date format are PDF 32000-1 §14.3.3,
    # Table 231, copied from the specification rather than from the parser: the
    # creation date carries a zone (and is therefore an instant), the
    # modification date does not (and is therefore the clock of wherever the file
    # was made), and the apostrophes in -05'00' are part of the format.
    #
    # The zone is deliberately not this machine's. It was +08'00' first, on a
    # +08:00 machine, and a control that threw the zone away entirely still
    # passed -- the two readings are the same instant there, so the assertion
    # could not have told the two apart anywhere it mattered.
    write('info.pdf', makePdf(
        [(595, 842, 0, 'XYZPDF a document that describes itself', b'')],
        info=b'<< /Title (XYZPDF Fixture Title) /Author (XYZPDF Fixture Author) '
             b'/Subject (The subject of a fixture) /Keywords (fixture, metadata, one, two) '
             b'/Creator (make_fixtures.py) /Producer (XYZPDF test harness) '
             b"/CreationDate (D:20260102030405-05'00') /ModDate (D:20260103120000) >>"))

    # Two links on one page: one to an address outside the document and one to a
    # place inside it. Every other fixture has none, so links() has been
    # answering "empty" on files that genuinely had nothing and that is not the
    # same as having been checked.
    write('links.pdf', makeLinksPdf())

    # Truncated: the header, a page object and half the xref table. A PDF
    # reader that trusts its parser crashes on files like this one, and the
    # point of the fixture is that opening it does not.
    write('damaged.pdf', text[:len(text) * 2 // 3])

    return 0


if __name__ == '__main__':
    sys.exit(main())
