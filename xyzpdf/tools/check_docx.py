"""Check an exported .docx with implementations that are not ours.

    python tools/check_docx.py 某文件.docx

The program writes this format by hand -- a ZIP and four XML parts -- and the
tests unpack it with code from this repository, which agrees with the writer by
construction: they were written together. This is the other half of that check,
for the times it matters (a change to the ZIP writer, a release, a report that
Word will not open the file):

  * zipfile validates every CRC-32 and every local header,
  * ElementTree parses every part, which is what "well-formed" means,
  * the parts are checked against each other the way Word checks them -- the
    drawing's r:embed must name a relationship, and that relationship's Target
    must be a part that is in the archive,
  * and the JPEG is decoded as far as its frame header, because a picture nobody
    can decode is a page that is not there.

Exit code is 0 when everything checks out and 1 when anything does not, so it can
be used in a script as well as by hand.
"""
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
PKG = 'http://schemas.openxmlformats.org/package/2006/relationships'

problems = []


def complain(what):
    problems.append(what)
    print('FAIL:', what)


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

    print('entries:')
    for name in archive.namelist():
        print('   %-34s %7d bytes' % (name, archive.getinfo(name).file_size))

    bad = archive.testzip()
    print('crc:', 'every entry checks out' if bad is None else bad + ' is corrupt')
    if bad is not None:
        complain('a CRC-32 does not match the data')

    for name in archive.namelist():
        if not name.endswith(('.xml', '.rels')):
            continue
        try:
            ET.fromstring(archive.read(name))
            print('well-formed xml:', name)
        except Exception as error:
            complain('%s is not well-formed: %s' % (name, error))

    if 'word/document.xml' not in archive.namelist():
        complain('there is no word/document.xml, so this is not a Word document')
        return 1

    document = ET.fromstring(archive.read('word/document.xml'))
    relationships = ET.fromstring(archive.read('word/_rels/document.xml.rels'))
    for picture, blip in enumerate(document.findall('.//{%s}blip' % A), start=1):
        embed = blip.get('{%s}embed' % R)
        found = relationships.find(".//{%s}Relationship[@Id='%s']" % (PKG, embed))
        if found is None:
            complain('picture %d refers to %s, which no relationship defines' % (picture, embed))
            continue
        target = found.get('Target')
        inside = ('word/' + target) in archive.namelist()
        print('picture %d: %s -> %s, and that part is in the file: %s'
              % (picture, embed, target, inside))
        if not inside:
            complain('picture %d points at %s, which is not in the archive' % (picture, target))

    for name in [n for n in archive.namelist() if n.startswith('word/media/')]:
        data = archive.read(name)
        if data[:2] != b'\xff\xd8' or data[-2:] != b'\xff\xd9':
            complain('%s is not a complete JPEG' % name)
            continue
        size = frameSize(data)
        print('image: %s, %d bytes, %s'
              % (name, len(data), '%d x %d pixels' % size if size else 'frame not found'))

    print('paragraphs:')
    for one in document.findall('.//{%s}p' % W):
        text = ''.join(t.text or '' for t in one.findall('.//{%s}t' % W))
        kinds = []
        if one.findall('.//{%s}br' % W):
            kinds.append('page break')
        if one.findall('.//{%s}drawing' % W):
            kinds.append('picture')
        print('   [%s] %s' % (', '.join(kinds) or 'text', text[:60]))

    section = document.find('.//{%s}sectPr' % W)
    size = section.find('{%s}pgSz' % W) if section is not None else None
    if size is not None:
        width = int(size.get('{%s}w' % W))
        height = int(size.get('{%s}h' % W))
        print('page: %.0f x %.0f points' % (width / 20.0, height / 20.0))

    if problems:
        print('%d problem(s)' % len(problems))
        return 1
    print('this is a Word document: a valid archive, and every part of it in place')
    return 0


def frameSize(data):
    """The width and height out of a JPEG's frame header, or None."""
    at = 2
    while at < len(data) - 9:
        if data[at] != 0xFF:
            at += 1
            continue
        marker = data[at + 1]
        if marker in (0xC0, 0xC1, 0xC2):
            height, width = struct.unpack('>HH', data[at + 5:at + 9])
            return width, height
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            at += 2
            continue
        at += 2 + struct.unpack('>H', data[at + 2:at + 4])[0]
    return None


if __name__ == '__main__':
    sys.exit(main(sys.argv))
