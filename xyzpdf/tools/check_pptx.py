"""Check an exported .pptx with implementations that are not ours.

    python tools/check_pptx.py 某文件.pptx

The writer builds this format by hand -- a ZIP, the parts a presentation is made
of -- and the smoke test unpacks it with code from this repository, which agrees
with the writer by construction. This is the other half of that check, for the
times it matters (a change to the ZIP writer, a release, a report that PowerPoint
will not open the file):

  * zipfile validates every CRC-32 and every local header,
  * ElementTree parses every part, which is what "well-formed" means,
  * every relationship's Target resolves to a part that is in the archive, the
    way PowerPoint resolves it,
  * every picture's r:embed names a relationship whose Target is in the archive,
  * the slide size and slide list are read out of presentation.xml,
  * and each media part is a complete PNG or JPEG.

Exit code is 0 when everything checks out and 1 when anything does not, so it can
be used in a script as well as by hand.
"""
import posixpath
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
import zipfile

P = 'http://schemas.openxmlformats.org/presentationml/2006/main'
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
PKG = 'http://schemas.openxmlformats.org/package/2006/relationships'

problems = []


def complain(what):
    problems.append(what)
    print('FAIL:', what)


def resolve(rels_name, target):
    """The part a relationship points at, from the part that owns the rels."""
    if target.startswith('/'):
        return target.lstrip('/')
    if rels_name.startswith('_rels/'):
        base = ''
    else:
        base = rels_name.rsplit('/_rels/', 1)[0]
    return posixpath.normpath(posixpath.join(base, target))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    path = argv[1]

    try:
        archive = zipfile.ZipFile(path)
    except Exception as error:
        complain('%s is not a ZIP archive: %s' % (path, error))
        return 1

    names = set(archive.namelist())
    print('entries:')
    for name in archive.namelist():
        print('   %-42s %7d bytes' % (name, archive.getinfo(name).file_size))

    bad = archive.testzip()
    print('crc:', 'every entry checks out' if bad is None else bad + ' is corrupt')
    if bad is not None:
        complain('a CRC-32 does not match the data')

    # Parsed once, here, and reused below: a part that does not parse is a problem
    # to report, not a reason for the rest of this script to crash.
    trees = {}
    for name in archive.namelist():
        if not name.endswith(('.xml', '.rels')):
            continue
        try:
            trees[name] = ET.fromstring(archive.read(name))
            print('well-formed xml:', name)
        except Exception as error:
            trees[name] = None
            complain('%s is not well-formed: %s' % (name, error))

    if 'ppt/presentation.xml' not in names:
        complain('there is no ppt/presentation.xml, so this is not a presentation')
        return 1

    # Every relationship must name a part that is there.
    for rels in sorted(n for n in names if n.endswith('.rels')):
        root = trees.get(rels)
        if root is None:
            continue
        for found in root.findall('{%s}Relationship' % PKG):
            if found.get('TargetMode') == 'External':
                continue
            target = resolve(rels, found.get('Target'))
            if target not in names:
                complain('%s points at %s, which is not in the archive' % (rels, target))

    presentation = trees.get('ppt/presentation.xml')
    if presentation is None:
        complain('ppt/presentation.xml cannot be read, so nothing below it can be checked')
        return 1
    section = presentation.find('{%s}sldSz' % P)
    if section is None:
        complain('presentation.xml has no p:sldSz, so the slide has no size')
    else:
        width = int(section.get('cx'))
        height = int(section.get('cy'))
        print('slide: %.0f x %.0f points' % (width / 12700.0, height / 12700.0))

    slides = presentation.findall('{%s}sldIdLst/{%s}sldId' % (P, P))
    print('slides:', len(slides))

    # Each slide's pictures: the blip's embed must be a relationship in that
    # slide's own rels file, and that relationship must be a media part here.
    for number in range(1, len(slides) + 1):
        part = 'ppt/slides/slide%d.xml' % number
        rels_name = 'ppt/slides/_rels/slide%d.xml.rels' % number
        if part not in names:
            complain('%s is missing although presentation.xml lists %d slides'
                     % (part, len(slides)))
            continue
        relationships = {}
        rels_root = trees.get(rels_name)
        if rels_root is not None:
            for found in rels_root.findall('{%s}Relationship' % PKG):
                relationships[found.get('Id')] = found
        document = trees.get(part)
        if document is None:
            complain('%s is in the archive but does not parse' % part)
            continue
        boxes = len(document.findall('.//{%s}sp' % P))
        pictures = len(document.findall('.//{%s}pic' % P))
        print('slide %d: %d text/shape boxes, %d pictures' % (number, boxes, pictures))
        for index, blip in enumerate(document.findall('.//{%s}blip' % A), start=1):
            embed = blip.get('{%s}embed' % R)
            found = relationships.get(embed)
            if found is None:
                complain('slide %d picture %d refers to %s, which its rels do not define'
                         % (number, index, embed))
                continue
            target = resolve(rels_name, found.get('Target'))
            if target not in names:
                complain('slide %d picture %d points at %s, which is not in the archive'
                         % (number, index, target))

    # A picture is a JPEG or a PNG, and has to be whole: a photograph carried over
    # as the PDF stored it, or a drawing rendered.
    for name in sorted(n for n in names if n.startswith('ppt/media/')):
        data = archive.read(name)
        if data[:2] == b'\xff\xd8':
            if data[-2:] != b'\xff\xd9':
                complain('%s is not a complete JPEG' % name)
                continue
            size = frameSize(data)
        elif data[:8] == b'\x89PNG\r\n\x1a\n':
            size, error = pngSize(data)
            if error:
                complain('%s is not a complete PNG: %s' % (name, error))
                continue
        else:
            complain('%s is neither a JPEG nor a PNG' % name)
            continue
        print('image: %s, %d bytes, %s'
              % (name, len(data), '%d x %d pixels' % size if size else 'frame not found'))

    if problems:
        print('%d problem(s)' % len(problems))
        return 1
    print('this is a presentation: a valid archive, and every part of it in place')
    return 0


def pngSize(data):
    """(width, height) out of a PNG's IHDR and None, or None and what is wrong."""
    at = 8
    size = None
    while at + 12 <= len(data):
        length = struct.unpack('>I', data[at:at + 4])[0]
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + length]
        if len(body) != length or at + 12 + length > len(data):
            return None, 'chunk %r runs past the end' % kind
        crc = struct.unpack('>I', data[at + 8 + length:at + 12 + length])[0]
        if zlib.crc32(kind + body) & 0xffffffff != crc:
            return None, 'chunk %r has a bad CRC' % kind
        if at == 8:
            if kind != b'IHDR':
                return None, 'the first chunk is not IHDR'
            size = struct.unpack('>II', body[:8])
        at += 12 + length
        if kind == b'IEND':
            return (size, None) if at == len(data) else (None, 'bytes after IEND')
    return None, 'no IEND'


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
