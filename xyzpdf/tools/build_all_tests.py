#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Configures, builds and runs every test.

    python tools/build_all_tests.py

Why this exists
---------------

Twice in one afternoon, in XYZip, a test target turned out not to compile, and
both times it had been that way for days without anyone noticing. The reason is
the same both times: a test that has been built once leaves an .exe behind, and
running that .exe afterwards reports whatever it reported the day it was built.
A target dropped out of the build and its stale binary kept saying "passed".

So: configure with every test option on, build, and treat a build failure as a
failure. Then run what was built.

What it does not do
-------------------

* It does not build the paid edition. XYZip configures two directories because
  it has two editions; v1 of this program has one, and configuring a second
  directory would compile the same sources to prove nothing. The option exists
  in CMakeLists.txt for when that changes -- see src/core/edition.h.

* It does not run anything whose name says it takes arguments or reports
  numbers. NOT_A_TEST below is the list.

Every test is run with XYZPDF_SETTINGS_PATH pointed at a scratch file of its
own, so none of them can reach the settings of whoever is running this.
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Targets that must not be run. Matched as substrings of the target name.
#
# 'verify', 'bench' and 'probe' take arguments and print numbers rather than
# passing or failing; 'install' changes the machine, and a test suite must not.
NOT_A_TEST = ('install', 'bench', 'verify', 'probe')

# Tests that need something on their command line, named relative to the build
# directory they were built in. A missing file is reported rather than passed
# over quietly -- passing over quietly is how XYZip's two shell tests came to be
# skipped for months.
ARGUMENTS_FOR = {
    # The preview probe handler is loaded from the DLL rather than linked, so it
    # has to be told where that landed. Relative to the build directory, which is
    # also where the deployment put it.
    'xyzpdf_previewhandler_smoke': ('xyzpdfpreview.dll',),
}

# Where Qt is when the build directory does not know yet. Overridable with
# --qt, and only used for a directory that has never been configured.
DEFAULT_QT = r'E:\qt\6.11.2\msvc2022_64'


def testOptions():
    """Every XYZPDF_BUILD_* option, read from CMakeLists rather than listed.

    A list here would be a list to forget to add to, which is the failure this
    whole script exists to catch.
    """
    text = io.open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    return sorted(set(re.findall(r'option\((XYZPDF_BUILD_[A-Z0-9_]+)', text)))


def findCMake(given):
    if given:
        return given
    found = shutil.which('cmake')
    if found:
        return found
    # Qt ships one, and this project is built with Qt's toolchain anyway.
    for guess in (r'E:\qt\Tools\CMake_64\bin\cmake.exe',
                  r'C:\Qt\Tools\CMake_64\bin\cmake.exe'):
        if os.path.exists(guess):
            return guess
    raise SystemExit('cmake was not found; pass --cmake with its path')


def findVcvars():
    """The MSVC environment script, or None when the shell already has it.

    ninja records the compiler's path at configure time but not the INCLUDE and
    LIB it needs, so a build started from an ordinary shell fails on the first
    #include. Running it through vcvars64.bat is what a developer prompt does,
    and doing it here means this can be run from anywhere.
    """
    if shutil.which('cl'):
        return None
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    roots = []
    if os.path.exists(vswhere):
        found = subprocess.run([vswhere, '-latest', '-products', '*', '-property',
                                'installationPath'],
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        path = found.stdout.decode('utf-8', 'replace').strip()
        if path:
            roots.append(path)
    roots += [r'E:\Microsoft Visual Studio\Community',
              r'C:\Program Files\Microsoft Visual Studio\2022\Community',
              r'C:\Program Files\Microsoft Visual Studio\2022\Professional']
    for root in roots:
        candidate = os.path.join(root, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
        if os.path.exists(candidate):
            return candidate
    return None


def compilerEnvironment(vcvars):
    """The environment vcvars64.bat produces, read once and reused.

    Rather than wrapping every command in `cmd /c call vcvars && ...`, which
    needs quotes inside quotes and loses them: subprocess escapes the inner ones
    and cmd then looks for a program whose name begins with a backslash. Asking
    the batch file for its variables once and passing them along is both simpler
    and faster -- vcvars takes a second, and a build runs many commands.
    """
    marker = '---XYZPDF-COMPILER-ENVIRONMENT---'
    outcome = subprocess.run('call "%s" >nul && echo %s && set' % (vcvars, marker),
                             shell=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    text = outcome.stdout.decode('utf-8', 'replace')
    if marker not in text:
        return None
    environment = dict(os.environ)
    for line in text.split(marker, 1)[1].splitlines():
        if '=' in line:
            name, value = line.split('=', 1)
            environment[name.strip()] = value.strip()
    return environment


ENVIRONMENT = None


def run(command):
    return subprocess.run(command, cwd=ROOT, env=ENVIRONMENT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def build(cmake, directory, options, qt, generator, report):
    """Configures and builds. Returns True when it built."""
    print('\n=== %s ===' % directory)

    configure = [cmake, '-S', ROOT, '-B', os.path.join(ROOT, directory)]
    configure += ['-D%s=ON' % option for option in options]
    # Only when the directory has not been configured. Two things follow from
    # that: the Qt location lives in an existing CMakeCache.txt and passing a
    # path that disagrees with it would reconfigure against another Qt without
    # anyone asking, and the generator cannot be changed in an existing tree at
    # all -- CMake refuses and says to use a fresh directory.
    #
    # Ninja is named rather than left to CMake's default, which on this machine
    # is NMake Makefiles: the build script and the CMakeLists both depend on
    # which generator it is -- the MSVC /showIncludes fix and the compiler
    # launcher only apply to Ninja -- and a default that differs between
    # machines is how a build ends up configured differently from the one that
    # was tested.
    if not os.path.exists(os.path.join(ROOT, directory, 'CMakeCache.txt')):
        configure += ['-G', generator, '-DCMAKE_BUILD_TYPE=Release']
        if qt:
            configure += ['-DCMAKE_PREFIX_PATH=%s' % qt.replace('\\', '/')]
    outcome = run(configure)
    if outcome.returncode != 0:
        print(outcome.stdout.decode('utf-8', 'replace')[-6000:])
        report.append('%s: configuring failed' % directory)
        return False

    outcome = run([cmake, '--build', os.path.join(ROOT, directory)])
    text = outcome.stdout.decode('utf-8', 'replace')
    if outcome.returncode != 0:
        # The errors, not the whole log: a failed link prints the same twenty
        # symbols and the useful line is the first one.
        for line in text.splitlines():
            if 'error' in line.lower():
                print('  ' + line.strip()[:220])
        report.append('%s: the build failed' % directory)
        return False
    print('  built')
    return True


def qtBinDirectory(directory):
    """Where Qt's DLLs are, read out of the build directory that used them.

    The tests link Qt and are not deployed next to a copy of it, so they need it
    on PATH -- without it every one of them fails to start, which looks exactly
    like every one of them failing. Taken from CMakeCache rather than guessed,
    so it is the Qt this build was actually configured against.
    """
    cache = os.path.join(ROOT, directory, 'CMakeCache.txt')
    if not os.path.exists(cache):
        return None
    for line in io.open(cache, encoding='utf-8', errors='replace'):
        if line.startswith('Qt6_DIR:'):
            qt6 = line.split('=', 1)[1].strip()
            # <qt>/lib/cmake/Qt6 -> <qt>/bin
            root = os.path.dirname(os.path.dirname(os.path.dirname(qt6)))
            candidate = os.path.join(root, 'bin')
            return candidate if os.path.isdir(candidate) else None
    return None


def runTests(directory, scratch, report):
    tests = os.path.join(ROOT, directory, 'tests')
    if not os.path.isdir(tests):
        report.append('%s: no tests were built' % directory)
        return 0, 0

    qtBin = qtBinDirectory(directory)
    if not qtBin:
        report.append('%s: Qt was not found, so the tests cannot start' % directory)
        return 0, 0

    passed = 0
    failed = 0
    for name in sorted(os.listdir(tests)):
        if not name.endswith('.exe'):
            continue
        stem = name[:-4]
        if any(word in stem for word in NOT_A_TEST):
            continue

        arguments = []
        missing = None
        for relative in ARGUMENTS_FOR.get(stem, ()):
            path = os.path.join(ROOT, directory, relative)
            if not os.path.exists(path):
                missing = relative
                break
            arguments.append(path)
        if missing:
            failed += 1
            print('  FAILED %s (needs %s, which was not built)' % (stem, missing))
            report.append('%s: %s could not run' % (directory, stem))
            continue

        environment = dict(ENVIRONMENT or os.environ)
        environment['PATH'] = qtBin + os.pathsep + environment.get('PATH', '')
        # A scratch settings store per test, so that a test which saves a
        # preference does not write into the registry of whoever ran it.
        environment['XYZPDF_SETTINGS_PATH'] = os.path.join(scratch, stem + '.ini')
        # And a small render cache, so that the eviction path -- which is what
        # the budget is for -- is exercised by the ordinary documents rather
        # than only by a 500-page one.
        environment['XYZPDF_RENDER_CACHE_BYTES'] = str(32 * 1024 * 1024)
        outcome = subprocess.run([os.path.join(tests, name)] + arguments, cwd=ROOT,
                                 env=environment, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=600)
        if outcome.returncode == 0:
            passed += 1
            continue
        failed += 1
        text = outcome.stdout.decode('utf-8', 'replace')
        first = next((line for line in text.splitlines()
                      if 'FAIL' in line or 'failure' in line or '失败' in line), '')
        print('  FAILED %s (%d) %s' % (stem, outcome.returncode, first.strip()[:140]))
        report.append('%s: %s failed' % (directory, stem))
    print('  %d passed, %d failed' % (passed, failed))
    return passed, failed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cmake', help='path to cmake.exe')
    parser.add_argument('--qt', default=DEFAULT_QT, help='the Qt installation to configure against')
    parser.add_argument('--build-dir', default='build-local-release')
    parser.add_argument('--generator', default='Ninja',
                        help='the CMake generator to configure a fresh directory with')
    parser.add_argument('--build-only', action='store_true',
                        help='stop after building; do not run anything')
    parser.add_argument('--run-only', action='store_true',
                        help='run what is already built, without configuring or building')
    arguments = parser.parse_args()

    global ENVIRONMENT
    vcvars = findVcvars()
    if vcvars:
        ENVIRONMENT = compilerEnvironment(vcvars)
        if not ENVIRONMENT:
            raise SystemExit('could not read the compiler environment from ' + vcvars)
        print('using the compiler environment from %s' % vcvars)

    cmake = findCMake(arguments.cmake)
    options = testOptions()
    print('%d test targets found in CMakeLists.txt' % len(options))

    report = []
    scratch = tempfile.mkdtemp(prefix='xyzpdf-tests-')
    try:
        if not arguments.run_only:
            if not build(cmake, arguments.build_dir, options, arguments.qt,
                         arguments.generator, report):
                raise SystemExit(1)
        if not arguments.build_only:
            runTests(arguments.build_dir, scratch, report)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    print()
    if report:
        print('%d problem(s):' % len(report))
        for line in report:
            print('  - ' + line)
        return 1
    print('everything built and everything passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
