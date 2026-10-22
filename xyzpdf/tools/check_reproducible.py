#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build the same source twice and ask whether the bytes agree.

    python tools/check_reproducible.py

Why two directories rather than one rebuilt
------------------------------------------

Because "rebuild and compare" cannot be done in the directory you are working
in: the second link overwrites the first file, so there is nothing left to
compare against. Two fresh directories, configured and built the way the gate
builds -- every test option on -- are the smallest thing that keeps both copies.

This is slow by nature: two full builds. It is a check, not a step of the gate,
which is why it is not called from build_all_tests.py and why it writes into
directories of its own rather than into build-local-release.

What a difference means
----------------------

Nothing here guesses at a cause. The two files are compared byte by byte and the
answer is the offsets, the PE section each one lives in, and the two values --
which is how §82 found the four timestamp bytes, and how the resource-tree date
was found to be a fifth difference only in the executable.

Exit code 0 means all four artifacts are identical. Exit code 1 means at least
one differs, or one was not built; the report says which.
"""
import argparse
import hashlib
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_all_tests  # noqa: E402  (the configure/build steps and the compiler environment)

ROOT = build_all_tests.ROOT

# The four binaries a user or the Store receives. The tests are not here: they
# are never shipped, and a difference between two test binaries would not be a
# problem of this kind -- it would be a problem of the assertion inside them.
TARGETS = ('xyzpdf.exe', 'xyzpdfpreview.dll', 'xyzpdfshell.dll', 'xyzpdfthumbnail.dll')

# The debug directory entry types, named from winnt.h. Two matter here: 2 would
# carry an absolute PDB path (a Release image of this project has none, which is
# why no build path reaches these files), and 16 is the record /Brepro writes --
# an algorithm id and a 256-bit digest of the inputs, so it changes whenever
# anything else does and a difference in it is a consequence, not a cause.
DEBUG_NAMES = {0: 'UNKNOWN', 1: 'COFF', 2: 'CODEVIEW', 3: 'FPO', 4: 'MISC',
               5: 'EXCEPTION', 6: 'FIXUP', 7: 'OMAP_TO_SRC', 8: 'OMAP_FROM_SRC',
               9: 'BORLAND', 10: 'RESERVED10', 11: 'CLSID', 12: 'VC_FEATURE',
               13: 'POGO', 14: 'ILTCG', 15: 'MPX', 16: 'REPRO', 18: 'SPGO',
               20: 'EX_DLLCHARACTERISTICS'}


class Image:
    """The handful of PE fields this tool reports on.

    Only enough to name a section and read a timestamp. It is written here
    rather than pulled from a library because a difference has to be explained
    by an offset, and an offset only means something next to the header that
    produced it.
    """

    def __init__(self, data):
        self.data = data
        self.pe = struct.unpack_from('<I', data, 0x3C)[0] if len(data) > 0x40 else 0
        if data[self.pe:self.pe + 4] != b'PE\0\0':
            raise ValueError('not a PE image')
        coff = self.pe + 4
        self.coff_time = struct.unpack_from('<I', data, coff + 4)[0]
        count = struct.unpack_from('<H', data, coff + 2)[0]
        optional_size = struct.unpack_from('<H', data, coff + 16)[0]
        optional = coff + 20
        magic = struct.unpack_from('<H', data, optional)[0]
        self.sections = []
        table = optional + optional_size
        for index in range(count):
            entry = table + index * 40
            name = data[entry:entry + 8].rstrip(b'\0').decode('utf-8', 'replace')
            # name[8], VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData
            virtual_size, virtual, raw_size, raw_pointer = struct.unpack_from('<IIII', data, entry + 8)
            self.sections.append((name, raw_pointer, raw_size, virtual, virtual_size))
        data_directory = optional + (112 if magic == 0x20B else 96)
        self.debug_rva, self.debug_size = struct.unpack_from('<II', data, data_directory + 6 * 8)

    def section_of(self, offset):
        for name, raw_pointer, raw_size, _, _ in self.sections:
            if raw_pointer <= offset < raw_pointer + raw_size:
                return name
        return 'headers'

    def offset_of_rva(self, rva):
        for name, raw_pointer, raw_size, virtual, virtual_size in self.sections:
            if virtual <= rva < virtual + max(virtual_size, raw_size):
                return raw_pointer + (rva - virtual), name
        return None, None

    def debug_entries(self):
        out = []
        if not self.debug_rva:
            return out
        offset, _ = self.offset_of_rva(self.debug_rva)
        if offset is None:
            return out
        for index in range(self.debug_size // 28):
            entry = offset + index * 28
            _, time_stamp, _, _, kind, size, _, raw = struct.unpack_from('<IIHHIIII', self.data, entry)
            out.append((entry, kind, time_stamp, size, raw))
        return out


def sha(data):
    return hashlib.sha256(data).hexdigest()


def report_differences(name, first, second, limit):
    """Print every differing run of bytes, with the section it is in."""
    data_a = open(first, 'rb').read()
    data_b = open(second, 'rb').read()
    if len(data_a) != len(data_b):
        print('  %s: sizes differ -- %d (%s) vs %d (%s)'
              % (name, len(data_a), os.path.basename(os.path.dirname(first)),
                 len(data_b), os.path.basename(os.path.dirname(second))))
        return 1
    differing = [index for index in range(len(data_a)) if data_a[index] != data_b[index]]
    if not differing:
        print('  %s: identical -- %d bytes, sha256 %s' % (name, len(data_a), sha(data_a)))
        return 0
    image = Image(data_a)
    runs = []
    start = previous = differing[0]
    for index in differing[1:]:
        if index != previous + 1:
            runs.append((start, previous))
            start = index
        previous = index
    runs.append((start, previous))
    print('  %s: %d of %d bytes differ' % (name, len(differing), len(data_a)))
    print('    sha256 %s' % sha(data_a))
    print('         %s' % sha(data_b))
    for low, high in runs[:limit]:
        section = image.section_of(low)
        print('    0x%X..0x%X (%d byte%s) in %s: A=%s B=%s'
              % (low, high, high - low + 1, '' if high == low else 's', section,
                 data_a[low:high + 1].hex(), data_b[low:high + 1].hex()))
    if len(runs) > limit:
        print('    (%d more run%s not printed)' % (len(runs) - limit, '' if len(runs) - limit == 1 else 's'))
    # The fields that are a stamp rather than content, so that a diff naming 0x128
    # can be read without unpacking the header by hand -- and so that a digest
    # that moved because something else moved is not mistaken for a second cause.
    print('    COFF TimeDateStamp A=0x%X B=0x%X' % (image.coff_time, Image(data_b).coff_time))
    for offset, kind, time_stamp, size, raw in image.debug_entries():
        print('    debug entry at 0x%X type %d (%s) stamp 0x%X size %d'
              % (offset, kind, DEBUG_NAMES.get(kind, '?'), time_stamp, size))
    return 1


def compare(directory_a, directory_b, limit):
    problems = []
    for name in TARGETS:
        first = os.path.join(directory_a, name)
        second = os.path.join(directory_b, name)
        present_a, present_b = os.path.isfile(first), os.path.isfile(second)
        if not (present_a and present_b):
            print('  %s: NOT BUILT (a=%s b=%s)' % (name, present_a, present_b))
            problems.append(name)
            continue
        if report_differences(name, first, second, limit):
            problems.append(name)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cmake', help='path to cmake.exe')
    parser.add_argument('--qt', default=build_all_tests.DEFAULT_QT,
                        help='the Qt installation to configure against')
    parser.add_argument('--dir-a', default='build-repro-1')
    parser.add_argument('--dir-b', default='build-repro-2')
    parser.add_argument('--keep', action='store_true',
                        help='leave the two build directories behind instead of deleting them')
    parser.add_argument('--compare-only', action='store_true',
                        help='compare what is already in the two directories; do not configure or build')
    parser.add_argument('--limit', type=int, default=12,
                        help='how many differing runs to print per file')
    arguments = parser.parse_args()

    if not arguments.compare_only:
        build_all_tests.ENVIRONMENT = build_all_tests.compilerEnvironment(build_all_tests.findVcvars())
        if not build_all_tests.ENVIRONMENT:
            raise SystemExit('could not read the compiler environment from vcvars64.bat')
        cmake = build_all_tests.findCMake(arguments.cmake)
        options = build_all_tests.testOptions()
        report = []
        for directory in (arguments.dir_a, arguments.dir_b):
            if not build_all_tests.build(cmake, directory, options, arguments.qt, 'Ninja', report):
                print('\nthe build did not produce both directories, so nothing was compared')
                return 1

    directory_a = os.path.join(ROOT, arguments.dir_a)
    directory_b = os.path.join(ROOT, arguments.dir_b)
    print('\n=== compare ===')
    print('  A %s' % directory_a)
    print('  B %s' % directory_b)
    problems = compare(directory_a, directory_b, arguments.limit)

    if not arguments.keep and not arguments.compare_only:
        for directory in (directory_a, directory_b):
            if os.path.isdir(directory):
                shutil.rmtree(directory, ignore_errors=True)
        print('  (the two build directories were removed; pass --keep to inspect them)')

    if problems:
        print('\nNOT reproducible: %s' % ', '.join(problems))
        return 1
    print('\nall four artifacts are byte-identical across the two builds')
    return 0


if __name__ == '__main__':
    sys.exit(main())
