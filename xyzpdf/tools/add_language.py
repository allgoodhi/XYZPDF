#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wires one more interface language into the build.

    python tools/add_language.py ru

Adding a language touches five places, and the two that are easiest to forget
are the two that fail late:

  * Resources/translations/xyzpdf_<code>.ts  -- the catalogue itself
  * Resources/translations/qtbase_<code>.qm -- Qt's own strings, copied out of
    the Qt installation, so that Qt's buttons ("OK", "Cancel", "Details") are
    not left in English inside an otherwise translated dialog
  * resources.qrc                           -- so that .qm is in the binary
  * CMakeLists.txt, XYZPDF_TS_FILES         -- so the .ts is compiled, and so
    i18n_smoke walks it
  * packaging/AppxManifest.xml and packaging/StoreAppxManifest.xml -- each
    carries a <Resource Language="..."/> list, and the package's language list
    is what Windows shows for it in the Store and in Settings

The last one is the one that bites: nothing about it is visible while you are
translating, and it only shows up at the end. Adding Russian to XYZip by hand
missed exactly that.

What this does NOT do is translate anything. It leaves a catalogue full of
unfinished entries, and the language is not offered until they are filled in --
languages.cpp only offers a language whose catalogue is in the binary, and a
half-translated catalogue that ships looks broken rather than untranslated.

Bringing the translations themselves over is what M4 is for; see
Resources/translations/xyzpdf_en.ts for what a finished one looks like.
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LF = chr(10)
CRLF = chr(13) + LF


def read(path):
    return io.open(path, encoding='utf-8', newline='').read().replace(CRLF, LF)


def write(path, text):
    io.open(path, 'w', encoding='utf-8', newline=CRLF).write(text)


def say(did, what):
    print(('  已改  ' if did else '  跳过  ') + what)


def find_lupdate():
    """lupdate, from PATH or from where this machine keeps Qt.

    The Qt installation is not on PATH in an ordinary shell -- only in a
    developer prompt -- so looking in the two places this project is built out
    of is the difference between this script working and this script telling
    somebody to go and set up a PATH. The same convention as
    tools/build_all_tests.py's findCMake.
    """
    found = shutil.which('lupdate')
    if found:
        return found
    for guess in (os.environ.get('XYZPDF_LUPDATE'),
                  r'E:\qt\6.11.2\msvc2022_64\bin\lupdate.exe',
                  r'C:\Qt\6.11.2\msvc2022_64\bin\lupdate.exe'):
        if guess and os.path.exists(guess):
            return guess
    return None


def find_qt_translations(explicit):
    """Where Qt keeps its own compiled catalogues.

    Taken from the argument, then from an environment variable, then from
    lupdate's own location -- rather than hard-coded to one machine's Qt."""
    if explicit:
        return explicit
    if os.environ.get('XYZPDF_QT_TRANSLATIONS'):
        return os.environ['XYZPDF_QT_TRANSLATIONS']
    lupdate = find_lupdate()
    if lupdate:
        candidate = os.path.join(os.path.dirname(os.path.dirname(lupdate)), 'translations')
        if os.path.isdir(candidate):
            return candidate
    return None


def known_language(code):
    """The program's own list of languages it is willing to show.

    A catalogue for a code that is not in there is invisible at run time: the
    picker never offers it, so the work would be done and never seen."""
    table = read(os.path.join(ROOT, 'src', 'core', 'languages.cpp'))
    return ('QStringLiteral("%s")' % code) in table


# --------------------------------------------------------------- the steps

def make_catalogue(code, dry_run):
    """Creates the .ts with lupdate, if it is not already there."""
    relative = 'Resources/translations/xyzpdf_%s.ts' % code
    path = os.path.join(ROOT, relative.replace('/', os.sep))
    if os.path.exists(path):
        say(False, '%s 已存在（要重新生成请先删除它）' % relative)
        return True
    if dry_run:
        say(True, '会用 lupdate 生成 ' + relative)
        return True
    lupdate = find_lupdate()
    if not lupdate:
        print('  失败  找不到 lupdate。把 Qt 的 bin 目录加进 PATH，')
        print('        或用 XYZPDF_LUPDATE 指定它的完整路径。')
        return False
    # The source directories are named rather than "." on purpose: lupdate would
    # otherwise walk the build tree as well, where the same sources appear a
    # second time with paths of their own -- and every string would be listed
    # twice, once with a location inside build-local-release.
    command = [lupdate, '-no-obsolete', '-source-language', 'zh_CN',
               '-target-language', code, 'main.cpp', 'mainwindow.cpp',
               'documenttab.cpp', 'mainwindow.ui', 'src', '-ts', relative]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            errors='replace')
    if result.returncode != 0 or not os.path.exists(path):
        print('  失败  lupdate 没能生成 %s：%s' % (relative, result.stderr.strip()[:200]))
        return False
    say(True, '生成 ' + relative)
    return True


def copy_qt_catalogue(code, translations, dry_run):
    """Qt's own strings. Missing is a warning, not a failure."""
    name = 'qtbase_%s.qm' % code
    destination = os.path.join(ROOT, 'Resources', 'translations', name)
    if os.path.exists(destination):
        say(False, 'Resources/translations/%s 已存在' % name)
        return True
    if not translations:
        print('  注意  找不到 Qt 的 translations 目录，%s 没有复制；' % name)
        print('        Qt 自己的按钮文字会保持英文。可用 --qt-translations 指定。')
        return True
    source = os.path.join(translations, name)
    if not os.path.exists(source):
        print('  注意  这个 Qt 版本没有 %s，Qt 自己的按钮文字会保持英文。' % name)
        return True
    if not dry_run:
        shutil.copyfile(source, destination)
    say(True, '复制 ' + name)
    return True


def wire_qrc(code, dry_run):
    """The .qm has to be in the binary, or it is a file nobody loads."""
    name = 'qtbase_%s.qm' % code
    if not os.path.exists(os.path.join(ROOT, 'Resources', 'translations', name)):
        say(False, 'resources.qrc（没有 %s 可加）' % name)
        return True
    path = os.path.join(ROOT, 'resources.qrc')
    body = read(path)
    line = ('        <file alias="translations/%s">Resources/translations/%s</file>'
            % (name, name))
    if line in body:
        say(False, 'resources.qrc 已包含 ' + name)
        return True
    # After the last qtbase entry, wherever that happens to be now.
    entries = list(re.finditer(r'^ *<file alias="translations/qtbase_[^"]+\.qm">.*$',
                               body, re.M))
    if not entries:
        print('  失败  resources.qrc 里找不到任何 qtbase 条目，结构变了。')
        return False
    at = entries[-1].end()
    if not dry_run:
        write(path, body[:at] + LF + line + body[at:])
    say(True, 'resources.qrc')
    return True


def wire_cmake(code, dry_run):
    """One list now, not three: XYZPDF_TS_FILES feeds both the build and the test."""
    path = os.path.join(ROOT, 'CMakeLists.txt')
    body = read(path)
    entry = '    Resources/translations/xyzpdf_%s.ts' % code
    if entry + LF in body or entry + ')' in body:
        say(False, 'CMakeLists.txt 已包含 xyzpdf_%s.ts' % code)
        return True
    match = re.search(r'set\(XYZPDF_TS_FILES\n(.*?)\)\n', body, re.S)
    if not match:
        print('  失败  CMakeLists.txt 里找不到 XYZPDF_TS_FILES。')
        return False
    listing = match.group(1)
    last = list(re.finditer(r'^ *Resources/translations/xyzpdf_[^\n]*$', listing, re.M))
    if not last:
        print('  失败  XYZPDF_TS_FILES 是空的，结构变了。')
        return False
    # The closing paren is NOT in `listing`: the pattern above consumed it. So
    # the new entry is appended plainly, and the paren the pattern ate is put
    # back by the replacement below. Getting this wrong writes two of them,
    # which is what the first run of this script did in XYZip.
    tail = last[-1]
    old_line = tail.group(0)
    new_line = old_line + LF + entry
    if not dry_run:
        write(path, body.replace(match.group(0),
                                 match.group(0).replace(old_line, new_line), 1))
    say(True, 'CMakeLists.txt (XYZPDF_TS_FILES)')
    return True


def wire_manifests(code, dry_run):
    """The step that is invisible until the end.

    Both MSIX manifests carry their own list of the languages the package
    claims, and the list is what Windows shows for the program in the Store and
    in Settings."""
    tag = code.replace('_', '-')
    ok = True
    for name in ('AppxManifest.xml', 'StoreAppxManifest.xml'):
        path = os.path.join(ROOT, 'packaging', name)
        if not os.path.exists(path):
            continue
        body = read(path)
        line = '    <Resource Language="%s" />' % tag
        if line in body:
            say(False, 'packaging/%s 已包含 %s' % (name, tag))
            continue
        entries = list(re.finditer(r'^ *<Resource Language="[^"]+" */>$', body, re.M))
        if not entries:
            print('  失败  packaging/%s 里找不到 <Resource Language>。' % name)
            ok = False
            continue
        at = entries[-1].end()
        if not dry_run:
            write(path, body[:at] + LF + line + body[at:])
        say(True, 'packaging/' + name)
    return ok


def main():
    parser = argparse.ArgumentParser(description='把一种界面语言接进构建')
    parser.add_argument('code', help='语言代码，例如 ru、es、pt_BR')
    parser.add_argument('--qt-translations', help="Qt 的 translations 目录")
    parser.add_argument('--dry-run', action='store_true', help='只说会改什么，不动文件')
    arguments = parser.parse_args()
    code = arguments.code

    if not re.fullmatch(r'[a-z]{2}(_[A-Za-z]{2,4})?', code):
        print('语言代码看起来不对：%s（应形如 ru、zh_TW、pt_BR）' % code)
        return 2

    if not known_language(code):
        print('注意：src/core/languages.cpp 的语言表里没有 "%s"。' % code)
        print('      表里没有的代码，语言选单不会显示它——翻译做了也看不见。')
        print('      先把它加进那张表，再运行本脚本。')
        return 2

    print('把 %s 接进构建：' % code)
    translations = find_qt_translations(arguments.qt_translations)
    steps = [
        make_catalogue(code, arguments.dry_run),
        copy_qt_catalogue(code, translations, arguments.dry_run),
        wire_qrc(code, arguments.dry_run),
        wire_cmake(code, arguments.dry_run),
        wire_manifests(code, arguments.dry_run),
    ]
    if not all(steps):
        print(LF + '有步骤失败了，改动可能只做了一半——用 git status 看一眼。')
        return 1

    print(LF + '接线完成。接下来是翻译本身：')
    print('  1. 填 Resources/translations/xyzpdf_%s.ts 里的每一条' % code)
    print('     （条目现在都是 unfinished，填完之前这个语言不会被提供给用户）')
    print('  2. 重新配置并构建，然后跑：')
    print('       python tools/build_all_tests.py')
    print('     i18n_smoke 会检查这个语言的目录确实在二进制里、能加载、')
    print('     并且真的把字符串翻译成了另一种文字。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
