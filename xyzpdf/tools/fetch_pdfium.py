#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Put the pinned PDFium release where the build expects it, and prove which bytes.

The build does not download anything. This script is the one step that does, and it
is a step somebody runs on purpose:

    python xyzpdf/tools/fetch_pdfium.py --from H:\\somewhere\\pdfium-win-x64.tgz
    python xyzpdf/tools/fetch_pdfium.py --from H:\\somewhere\\pdfium-8111
    python xyzpdf/tools/fetch_pdfium.py --download

It accepts either the release archive or an already extracted directory (such as
the one docs/pdfium-plan.md sec.2 was measured on), verifies the sha256 of the
archive, of bin/pdfium.dll and of lib/pdfium.dll.lib, and refuses to install
anything that does not match the pinned release. The destination is
<p repository>/pdfium_installed, which is gitignored for the same reason
vcpkg_installed is: it is 8 MB of another project's binaries.

--download talks to GitHub only when it is named. Nothing here fetches implicitly,
and an absent --from with no --download is a usage error rather than a download.

The version is pinned in one place: chromium/8111, the release
docs/pdfium-plan.md was measured on. Changing it means changing every hash below
and re-running the measurements that document stands on.
"""
import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEST = os.path.join(ROOT, 'pdfium_installed')

RELEASE_TAG = 'chromium/8111'
RELEASE_VERSION = '158.0.8111.0'
DOWNLOAD_URL = ('https://github.com/bblanchon/pdfium-binaries/releases/download/'
                'chromium%2F8111/pdfium-win-x64.tgz')

TGZ_SHA256 = '3452d11031050ec65e57f180215b57b9e6d465e3fbedafa8a77ad90885c7d23e'
TGZ_SIZE = 3905343
DLL_SHA256 = 'a318c39c137482dc4687c2973b7ec1c27fc1c20253cf2dc31928f922eab89e8f'
DLL_SIZE = 7586816
LIB_SHA256 = '8842b071d21e96861bd5bd2960c143d8079bce1d7215905e7c4506016ab1fcdd'
LIB_SIZE = 115006


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def said(path):
    return '%s (%d bytes, sha256 %s)' % (path, os.path.getsize(path), sha256_of(path))


def verify_release(release):
    """True when `release` is the pinned extraction; says what it found either way."""
    ok = True
    for relative, size, digest in (
            (os.path.join('bin', 'pdfium.dll'), DLL_SIZE, DLL_SHA256),
            (os.path.join('lib', 'pdfium.dll.lib'), LIB_SIZE, LIB_SHA256)):
        path = os.path.join(release, relative)
        if not os.path.isfile(path):
            print('  missing: %s' % path)
            ok = False
            continue
        text = said(path)
        if os.path.getsize(path) != size or sha256_of(path) != digest:
            print('  WRONG FILE: %s' % text)
            print('              expected %d bytes, sha256 %s' % (size, digest))
            ok = False
        else:
            print('  %s' % text)
    for required in (os.path.join('include', 'fpdfview.h'), 'VERSION'):
        if not os.path.isfile(os.path.join(release, required)):
            print('  missing: %s' % os.path.join(release, required))
            ok = False
    return ok


def release_root(base):
    """The directory holding include/ and bin/ -- the archive has them at its root."""
    if os.path.isfile(os.path.join(base, 'include', 'fpdfview.h')):
        return base
    for name in sorted(os.listdir(base)):
        candidate = os.path.join(base, name)
        if os.path.isdir(candidate) and os.path.isfile(
                os.path.join(candidate, 'include', 'fpdfview.h')):
            return candidate
    return None


def stage_archive(archive, staging):
    found = sha256_of(archive)
    print('archive %s' % said(archive))
    if os.path.getsize(archive) != TGZ_SIZE or found != TGZ_SHA256:
        print('REFUSED: this is not the pinned archive')
        print('         expected %d bytes, sha256 %s' % (TGZ_SIZE, TGZ_SHA256))
        return None
    print('  archive matches the pinned release (%s, %s)' % (RELEASE_TAG, RELEASE_VERSION))
    extracted = os.path.join(staging, 'extracted')
    os.mkdir(extracted)
    # filter='data' refuses absolute paths, parent traversal and links that point
    # outside the archive -- the archive is third-party bytes, so it is treated as
    # such rather than as a directory copy that happens to be compressed.
    with tarfile.open(archive, 'r:gz') as tarball:
        tarball.extractall(extracted, filter='data')
    root = release_root(extracted)
    if root is None:
        print('REFUSED: the archive has no include/fpdfview.h')
        return None
    return root


def stage_directory(source, staging):
    root = release_root(source)
    if root is None:
        print('REFUSED: %s is not an extracted PDFium release' % source)
        return None
    copied = os.path.join(staging, 'copied')
    shutil.copytree(root, copied)
    return copied


def install(source, download):
    staging = tempfile.mkdtemp(prefix='.pdfium-install-', dir=ROOT)
    try:
        if download:
            archive = os.path.join(staging, 'pdfium-win-x64.tgz')
            print('downloading %s' % DOWNLOAD_URL)
            with urllib.request.urlopen(DOWNLOAD_URL) as response, open(archive, 'wb') as out:
                shutil.copyfileobj(response, out)
            staged = stage_archive(archive, staging)
        elif os.path.isdir(source):
            print('source directory %s' % source)
            staged = stage_directory(source, staging)
        else:
            staged = stage_archive(source, staging)
        if staged is None:
            return 1

        print('verifying %s' % staged)
        if not verify_release(staged):
            print('REFUSED: the extracted release does not match the pinned hashes')
            return 1

        if os.path.exists(DEST):
            if verify_release(DEST):
                print('%s is already installed and matches; nothing to do' % DEST)
                return 0
            print('REFUSED: %s exists but does not match the pinned release.' % DEST)
            print('         Move it aside by hand, then run this again.')
            return 1

        # Same volume as DEST because staging is inside the repository, so the
        # rename is one operation and a half-installed directory never appears.
        os.replace(staged, DEST)
        print('installed %s' % DEST)
        print('point the build at it with:')
        print('  -DXYZPDF_PDFIUM_DIR=%s' % DEST.replace('\\', '/'))
        return 0
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(
        description='Install the pinned PDFium release into pdfium_installed/.')
    parser.add_argument('--from', dest='source',
                        help='the release archive (.tgz) or an extracted release directory')
    parser.add_argument('--download', action='store_true',
                        help='download the pinned archive from GitHub before installing')
    arguments = parser.parse_args()

    if arguments.download and arguments.source:
        parser.error('--download and --from name two sources; choose one')
    if not arguments.download and not arguments.source:
        # The installed directory may already be right, which is worth answering
        # without asking anybody to find an archive again.
        if os.path.isdir(DEST) and verify_release(DEST):
            print('%s is already installed and matches' % DEST)
            return 0
        parser.error('give --from <archive-or-directory>, or --download')
    if arguments.source and not os.path.exists(arguments.source):
        print('not found: %s' % arguments.source)
        return 2
    return install(arguments.source, arguments.download)


if __name__ == '__main__':
    sys.exit(main())
