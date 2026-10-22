"""One-off driver for the PDFium probe tree (this branch only).

build_all_tests.py is the sanctioned way to configure this project, but it turns every
XYZPDF_BUILD_* option on and builds the whole suite; here the point is one target against a
library the default build must not see. So this borrows the gate's own two helpers -- the
compiler environment read out of vcvars64.bat, and the cmake path it would pick -- and runs
configure/build with them, rather than re-implementing either.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import build_all_tests as gate  # noqa: E402

BACKSLASH = chr(92)
PDFIUM = 'H:/test/XYZPDF-wt/third-party/pdfium-8111'


def main():
    environment = gate.compilerEnvironment(gate.findVcvars())
    if not environment:
        print('no compiler environment')
        return 1
    cmake = gate.findCMake(None)
    buildDirectory = os.path.join(ROOT, 'build-pdfium-probe')

    configure = [cmake, '-S', ROOT, '-B', buildDirectory, '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_PREFIX_PATH=' + gate.DEFAULT_QT.replace(BACKSLASH, '/'),
                 '-DXYZPDF_BUILD_PDFIUM_PROBE=ON',
                 '-DXYZPDF_PDFIUM_DIR=' + PDFIUM]
    if len(sys.argv) > 1 and sys.argv[1] == 'configure-only':
        outcome = subprocess.run(configure, cwd=ROOT, env=environment,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(outcome.stdout.decode('utf-8', 'replace')[-4000:])
        return outcome.returncode

    outcome = subprocess.run(configure, cwd=ROOT, env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = outcome.stdout.decode('utf-8', 'replace')
    if outcome.returncode != 0:
        print(text[-4000:])
        return outcome.returncode

    build = [cmake, '--build', buildDirectory, '--target', 'xyzpdf_pdfiumprobe']
    outcome = subprocess.run(build, cwd=ROOT, env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = outcome.stdout.decode('utf-8', 'replace')
    if outcome.returncode != 0:
        for line in text.splitlines():
            if 'error' in line.lower() or 'fatal' in line.lower():
                print('  ' + line.strip()[:400])
        print(text[-6000:])
    else:
        print('  built')
    return outcome.returncode


if __name__ == '__main__':
    sys.exit(main())
