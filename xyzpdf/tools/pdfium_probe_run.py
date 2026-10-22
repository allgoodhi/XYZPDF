"""Run the PDFium probe the way the gate runs a test: this project's root as the working
directory, Qt's bin on PATH, the sample directory in the environment.

    python tools/pdfium_probe_run.py [path-to-a-real-book.pdf] [log-name]

The samples are written by the documenteditor smoke test (sec.54's generator), so this runs that
first -- it is the only way to get the same files the window and the smoke suite see, instead of a
hand-made one nobody else can reproduce. The probe prints readings; its exit code is the control
count, not a pass/fail on the product.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import build_all_tests as gate  # noqa: E402

SAMPLES = os.environ.get('XYZPDF_FORM_SAMPLE_DIR', 'H:/tmp/pdfium-probe/samples')


def main():
    book = sys.argv[1] if len(sys.argv) > 1 else 'H:/tmp/outline/real-book.pdf'
    log = sys.argv[2] if len(sys.argv) > 2 else 'H:/tmp/pdfium-probe/run.txt'
    environment = gate.compilerEnvironment(gate.findVcvars())
    if not environment:
        print('no compiler environment')
        return 1
    qtBin = gate.qtBinDirectory('build-local-release')
    environment['PATH'] = qtBin + os.pathsep + environment.get('PATH', '')
    environment['XYZPDF_FORM_SAMPLE_DIR'] = SAMPLES
    # The same guard the gate sets, so a probe that saves a preference writes into a scratch file
    # rather than into whoever ran this.
    import tempfile
    scratch = tempfile.mkdtemp(prefix='xyzpdf-pdfium-probe-')
    environment['XYZPDF_SETTINGS_PATH'] = os.path.join(scratch, 'pdfiumprobe.ini')
    os.makedirs(SAMPLES, exist_ok=True)

    smoke = os.path.join(ROOT, 'build-local-release', 'tests',
                         'xyzpdf_documenteditor_smoke.exe')
    outcome = subprocess.run([smoke], cwd=ROOT, env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
    print('sample writer exit %d' % outcome.returncode)
    if outcome.returncode != 0:
        return 1

    probe = os.path.join(ROOT, 'build-pdfium-probe', 'tests', 'xyzpdf_pdfiumprobe.exe')
    outcome = subprocess.run([probe, book], cwd=ROOT, env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    text = outcome.stdout.decode('utf-8', 'replace')
    with open(log, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(text)
    print('probe exit %d, %d lines, log %s' % (outcome.returncode, len(text.splitlines()), log))
    for line in text.splitlines():
        if line.strip().startswith('C') or 'CONTROL' in line or 'control(s)' in line:
            print(line)
    return outcome.returncode


if __name__ == '__main__':
    sys.exit(main())
