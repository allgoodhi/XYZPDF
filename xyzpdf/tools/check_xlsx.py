"""Check an exported .xlsx with implementations that are not ours.

    python tools/check_xlsx.py 某文件.xlsx

The program writes this format by hand -- a ZIP and the XML parts inside it --
and tests/excelexport_smoke.cpp unpacks it with code from this repository, which
agrees with the writer by construction: they were written together. This is the
other half of that check, for the times it matters (a change to the ZIP writer, a
release, a report that Excel or WPS will not open the file):

  * zipfile validates every CRC-32 and every local header,
  * ElementTree parses every part, which is what "well-formed" means,
  * every relationship target is a part that is in the archive, resolved the way
    a reader resolves it (a "../drawings/drawing1.xml" from a sheet's _rels is
    xl/drawings/drawing1.xml),
  * every [Content_Types].xml override names a part that is in the archive, and
    every part in the archive is covered by an override or by a default extension,
  * the workbook's sheets are related, in order, to worksheets that exist,
  * every cell that says t="s" points at a shared string that is there, every
    cell's style index is inside cellXfs, and every numFmtId above 163 is defined,
  * every drawing's r:embed names a relationship, and that relationship's media is
    there and decodable as far as its own header,
  * and merge ranges are actually ranges (at least two cells).

Exit code is 0 when everything checks out and 1 when anything does not, so it can
be used in a script as well as by hand.

The control, which has to be red:

    python -c "...see the slice report..."  # one byte changed in xl/workbook.xml
    python tools/check_xlsx.py 那一份.xlsx   # exit 1

Corrupting a part either breaks its CRC (zipfile notices) or breaks the parse or
the cross-checks below; a checker that only printed would notice neither.
"""
import io
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile
import zlib

MAIN = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
PKG = 'http://schemas.openxmlformats.org/package/2006/relationships'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
CT = 'http://schemas.openxmlformats.org/package/2006/content-types'
XDR = 'http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing'
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'

problems = []


def complain(what):
    problems.append(what)
    print('FAIL:', what)


def resolve(rels_part, target):
    """A relationship target, as the part that names it would have to read it."""
    if target.startswith('/'):
        return target[1:]
    base = rels_part.rsplit('/', 2)
    # "xl/worksheets/_rels/sheet1.xml.rels" -> directory "xl/worksheets"
    directory = rels_part[:rels_part.rfind('_rels/')].rstrip('/')
    walked = target
    while walked.startswith('../'):
        walked = walked[3:]
        directory = directory.rsplit('/', 1)[0] if '/' in directory else ''
    return directory + '/' + walked if directory else walked


def column_of(reference):
    return ''.join(ch for ch in reference if ch.isalpha())


def row_of(reference):
    return int(''.join(ch for ch in reference if ch.isdigit()))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    path = argv[1]

    try:
        archive = zipfile.ZipFile(path)
    except Exception as error:  # not a ZIP at all
        complain('%s is not a ZIP archive: %s' % (path, error))
        return 1

    names = archive.namelist()
    print('entries:')
    for name in names:
        print('   %-40s %7d bytes' % (name, archive.getinfo(name).file_size))

    bad = archive.testzip()
    print('crc:', 'every entry checks out' if bad is None else bad + ' is corrupt')
    if bad is not None:
        complain('a CRC-32 does not match the data')

    trees = {}
    for name in names:
        if not name.endswith(('.xml', '.rels')):
            continue
        try:
            trees[name] = ET.fromstring(archive.read(name))
            print('well-formed xml:', name)
        except Exception as error:
            complain('%s is not well-formed: %s' % (name, error))

    for required in ('[Content_Types].xml', '_rels/.rels', 'xl/workbook.xml',
                     'xl/_rels/workbook.xml.rels', 'xl/styles.xml', 'xl/sharedStrings.xml'):
        if required not in names:
            complain('%s is missing, so this is not a workbook' % required)
    if problems:
        return 1

    # Every relationship, in every part: the target has to be a part that is here.
    for name in [n for n in names if n.endswith('.rels')]:
        for rel in trees[name].findall('{%s}Relationship' % PKG):
            if rel.get('TargetMode') == 'External':
                continue
            part = resolve(name, rel.get('Target'))
            if part not in names:
                complain('%s points at %s, which is not in the archive' % (name, part))
            if not rel.get('Id'):
                complain('%s has a relationship with no Id' % name)

    # Content types both ways: each override names a part, and each part is covered.
    defaults = {}
    overridden = set()
    for entry in trees['[Content_Types].xml'].findall('{%s}Default' % CT):
        defaults[entry.get('Extension').lower()] = entry.get('ContentType')
    for entry in trees['[Content_Types].xml'].findall('{%s}Override' % CT):
        part = entry.get('PartName').lstrip('/')
        overridden.add(part)
        if part not in names:
            complain('[Content_Types].xml declares %s, which is not in the archive' % part)
    for name in names:
        if name in overridden:
            continue
        extension = name.rsplit('.', 1)[-1].lower()
        if extension not in defaults:
            complain('%s has neither an override nor a default content type' % name)

    # The workbook's sheets, in order, each one related to a part that is here.
    book = trees['xl/workbook.xml']
    sheets = book.findall('.//{%s}sheet' % MAIN)
    print('sheets:')
    for sheet in sheets:
        id_ = sheet.get('{%s}id' % R)
        found = trees['xl/_rels/workbook.xml.rels'].find(
            ".//{%s}Relationship[@Id='%s']" % (PKG, id_))
        if found is None:
            complain('sheet %s refers to %s, which no relationship defines' % (sheet.get('name'), id_))
            continue
        part = resolve('xl/_rels/workbook.xml.rels', found.get('Target'))
        if part not in names:
            complain('sheet %s points at %s, which is not in the archive' % (sheet.get('name'), part))
            continue
        tree = trees.get(part)
        if tree is None:
            complain('sheet %s is not well-formed XML' % part)
            continue
        rows = tree.findall('.//{%s}row' % MAIN)
        cells = tree.findall('.//{%s}c' % MAIN)
        merges = tree.findall('.//{%s}mergeCell' % MAIN)
        dimension = tree.find('{%s}dimension' % MAIN)
        print('   %-20s %-12s %2d rows %3d cells %d merged'
              % (sheet.get('name'), dimension.get('ref') if dimension is not None else '?',
                 len(rows), len(cells), len(merges)))
        for merge in merges:
            ref = merge.get('ref')
            first, _, last = ref.partition(':')
            if not last or (column_of(first) == column_of(last) and row_of(first) == row_of(last)):
                complain('the merged range %s on sheet %s is one cell' % (ref, sheet.get('name')))

    # The shared strings: what the cells count on.
    strings = []
    for si in trees['xl/sharedStrings.xml'].findall('{%s}si' % MAIN):
        node = si.find('{%s}t' % MAIN)
        strings.append(node.text if node is not None and node.text else '')
    declared = trees['xl/sharedStrings.xml'].get('uniqueCount')
    if declared is not None and int(declared) != len(strings):
        complain('sharedStrings says uniqueCount=%s and holds %d' % (declared, len(strings)))
    print('shared strings:', len(strings), 'of', trees['xl/sharedStrings.xml'].get('count'), 'uses')

    # The styles: the count of each table, and every custom format defined.
    styles = trees['xl/styles.xml']
    cell_xfs = styles.find('{%s}cellXfs' % MAIN)
    xf_count = len(cell_xfs.findall('{%s}xf' % MAIN)) if cell_xfs is not None else 0
    if cell_xfs is not None and int(cell_xfs.get('count', xf_count)) != xf_count:
        complain('cellXfs says count=%s and holds %d' % (cell_xfs.get('count'), xf_count))
    formats = {}
    for fmt in styles.findall('.//{%s}numFmt' % MAIN):
        formats[fmt.get('numFmtId')] = fmt.get('formatCode')
    print('styles:', xf_count, 'cell formats,', len(formats), 'custom number formats')
    for code in formats.values():
        print('   formatCode:', code)

    # Every cell: a string index in range, a style index that exists, a format that
    # is either a built-in one or defined here.
    for name in [n for n in names if n.startswith('xl/worksheets/sheet') and n.endswith('.xml')]:
        for cell in trees[name].findall('.//{%s}c' % MAIN):
            reference = cell.get('r')
            style = int(cell.get('s', '0'))
            if style >= xf_count:
                complain('%s %s uses style %d, and there are only %d'
                         % (name, reference, style, xf_count))
            xf = cell_xfs[style] if cell_xfs is not None and style < xf_count else None
            if xf is not None:
                num_fmt = xf.get('numFmtId')
                if num_fmt is not None and int(num_fmt) >= 164 and num_fmt not in formats:
                    complain('%s %s asks for number format %s, which styles.xml does not define'
                             % (name, reference, num_fmt))
            if cell.get('t') == 's':
                value = cell.find('{%s}v' % MAIN)
                if value is None or not value.text:
                    complain('%s %s says it is a shared string and carries no index' % (name, reference))
                    continue
                index = int(value.text)
                if index < 0 or index >= len(strings):
                    complain('%s %s points at string %d of %d' % (name, reference, index, len(strings)))
            elif cell.get('t') in ('str', 'inlineStr', 'e'):
                complain('%s %s uses the cell type %s, which this writer does not write'
                         % (name, reference, cell.get('t')))

    # Pictures: each blip names a relationship of its drawing, and that media part
    # is here and whole.
    for name in [n for n in names if n.startswith('xl/drawings/') and n.endswith('.xml')]:
        rels_name = name[:name.rfind('/')] + '/_rels/' + name.rsplit('/', 1)[-1] + '.rels'
        if rels_name not in names:
            complain('%s has pictures and no relationship part' % name)
            continue
        for blip in trees[name].findall('.//{%s}blip' % A):
            embed = blip.get('{%s}embed' % R)
            found = trees[rels_name].find(".//{%s}Relationship[@Id='%s']" % (PKG, embed))
            if found is None:
                complain('%s embeds %s, which %s does not define' % (name, embed, rels_name))
                continue
            part = resolve(rels_name, found.get('Target'))
            if part not in names:
                complain('%s embeds %s, which is not in the archive' % (name, part))
                continue
            data = archive.read(part)
            print('image:', part, len(data), 'bytes,', describe_image(data))
        for anchor in trees[name]:
            tag = anchor.tag.split('}')[-1]
            if tag not in ('oneCellAnchor', 'twoCellAnchor'):
                complain('%s holds an unexpected %s' % (name, tag))

    if problems:
        print('%d problem(s)' % len(problems))
        return 1
    print('this is a workbook: a valid archive, and every part of it in place')
    return 0


def describe_image(data):
    """As much of a picture as its own header says, or what is wrong with it."""
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        at = 8
        size = None
        while at + 12 <= len(data):
            length = struct.unpack('>I', data[at:at + 4])[0]
            kind = data[at + 4:at + 8]
            body = data[at + 8:at + 8 + length]
            if len(body) != length:
                return 'chunk %r runs past the end' % kind
            crc = struct.unpack('>I', data[at + 8 + length:at + 12 + length])[0]
            if zlib.crc32(kind + body) & 0xffffffff != crc:
                return 'chunk %r has a bad CRC' % kind
            if kind == b'IHDR':
                size = struct.unpack('>II', body[:8])
            at += 12 + length
            if kind == b'IEND':
                return '%d x %d pixels' % size if size else 'no IHDR'
        return 'no IEND'
    if data[:2] == b'\xff\xd8':
        if data[-2:] != b'\xff\xd9':
            return 'a JPEG that does not end'
        at = 2
        while at < len(data) - 9:
            if data[at] != 0xFF:
                at += 1
                continue
            marker = data[at + 1]
            if marker in (0xC0, 0xC1, 0xC2):
                height, width = struct.unpack('>HH', data[at + 5:at + 9])
                return '%d x %d pixels' % (width, height)
            if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
                at += 2
                continue
            at += 2 + struct.unpack('>H', data[at + 2:at + 4])[0]
        return 'a JPEG with no frame header'
    return 'neither a PNG nor a JPEG'


if __name__ == '__main__':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.exit(main(sys.argv))
