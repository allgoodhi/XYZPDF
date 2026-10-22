"""Control runs for the PDFium probe: break one thing in the instrument, show that the control that
is supposed to notice actually goes red, and put the file back byte for byte.

    python tools/pdfium_probe_control.py <CTL> [<CTL> ...]

Why this exists: AGENTS.md and handoff-02 both say the same thing in different words -- a control that
cannot change the result proves the opposite of what it looks like (sec.99), and a control that mutates
a tracked file has to restore it by hash, in a finally, because a run that dies mid-print leaves the
repo in the broken state (sec.98). So each named mutation here is one line, the restore is verified by
sha256 against the copy taken before the edit, and the build runs again afterwards so the tree that is
left is the tree that was measured.

The mutations are all on the instrument (the probe), not on the product -- this branch changes no
product code. Each one is the *only* change on its line, which is what makes a red control mean
something.

  no-ffl        stop calling FPDF_FFLDraw at all -> C5 must go red (the claim that PDFium draws a
                widget is then not being made by anything).
  no-annot-flag pass no annotation flag on the route that is supposed to ask for one -> C2 and C5's
                first half go red: the square stops reaching the page where the table says it must.
  no-fill       stop filling the bitmap before drawing into it -> C8 must go red on the upper bound
                (unpainted paper reads black, so the whole page counts as ink).
  no-composite  stop compositing Qt's render onto white -> C8 must go red on the upper bound for the
                qt side (an image with alpha and no paper colour reads as a black page).
  square-draws-without-flag / box-on-blank-paper / pdfium-page-size-lies /
  carried-opens-without-form
                one line each, one control each: C3, C4, C6, C7.
  qt-route-produces-nothing
                the Qt route hands back no image -> C1, and the controls that read the same cells.

The mutation that was tried and dropped: reading PDFium's BGRx buffer as ARGB32 instead of RGB32
reddened nothing at all, which is how that theory was ruled out (the real cause was the unfilled
buffer, see no-fill). AGENTS.md's rule for it: a control that cannot change the result proves the
opposite of what it looks like.
"""
import hashlib
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import build_all_tests as gate  # noqa: E402

PROBE = os.path.join(ROOT, 'tests', 'pdfiumprobe.cpp')
PDFIUM = 'H:/test/XYZPDF-wt/third-party/pdfium-8111'

MUTATIONS = {
    'no-ffl': (
        """    if (drawForm && document.form)
        FPDF_FFLDraw(document.form, bitmap, loaded, 0, 0, pixels.width(), pixels.height(), 0,
                     formFlags);""",
        """    if (false && drawForm && document.form)
        FPDF_FFLDraw(document.form, bitmap, loaded, 0, 0, pixels.width(), pixels.height(), 0,
                     formFlags);""",
    ),
    'no-annot-flag': (
        '''    {"pdfium-page+annot+ffl", false, FPDF_ANNOT, true, FPDF_ANNOT},''',
        '''    {"pdfium-page+annot+ffl", false, 0, true, FPDF_ANNOT},''',
    ),
    'no-fill': (
        '    FPDFBitmap_FillRect(bitmap, 0, 0, pixels.width(), pixels.height(), 0xFFFFFFFF);\n',
        '',
    ),
    # The mutation this branch actually needed: reading PDFium's buffer as ARGB32 instead of RGB32 was
  # tried first and changed nothing at all (0 controls red) -- which is how it was ruled out as the
  # cause of the all-black pages, and why the fill above is the line that matters. So the control here
  # breaks the compositing that Qt's images genuinely need.
  'no-composite': (
        '''    if (!source.hasAlphaChannel())
        return source.convertToFormat(QImage::Format_RGB32);''',
        '''    if (true)
        return source.convertToFormat(QImage::Format_RGB32);''',
    ),
    # Five more, so that every control in the list has been seen red rather than only reasoned
    # about. Each is one line and each is expected to redden exactly the control named after it --
    # except the last, whose cascade is written down where it happens.
    'square-draws-without-flag': (                    # -> C3
        '''    {"pdfium-page", false, 0, false, 0},''',
        '''    {"pdfium-page", false, FPDF_ANNOT, false, 0},''',
    ),
    'box-on-blank-paper': (                           # -> C4
        '''     70, 770, 300, 792},''',
        '''     100, 100, 330, 122},''',
    ),
    'pdfium-page-size-lies': (                        # -> C6
        '''        return QSizeF(size.width, size.height);''',
        '''        return QSizeF(size.width * 1.1, size.height * 1.1);''',
    ),
    'carried-opens-without-form': (                   # -> C7
        '''        PdfDocument *document = holders.pdfium(path, true);
        carried.handleOpened = document && document->handle;''',
        '''        PdfDocument *document = holders.pdfium(path, false);
        carried.handleOpened = document && document->handle;''',
    ),
    # -> C1. Expect this one to redden C2/C4/C5/C8 as well: they all read the same cells, and a route
    # that produced no image must not leave a table that still passes. A log that said "C1 only" here
    # would be a claim the instrument does not support.
    'qt-route-produces-nothing': (
        '''        if (route.useQt) {
            RenderOptions options;
            options.optimizedForLcd = lcd;
            return qt(path)->render(0, pixels, options);
        }''',
        '''        if (route.useQt) {
            return QImage();
        }''',
    ),
}


def digest(path):
    with open(path, 'rb') as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def build(environment, cmake):
    outcome = subprocess.run([cmake, '--build', 'build-pdfium-probe', '--target',
                              'xyzpdf_pdfiumprobe'], cwd=ROOT, env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return outcome.returncode == 0, outcome.stdout.decode('utf-8', 'replace')


def runProbe(environment, name):
    # One log per mutation, kept: the point of a control run is the pair -- what was broken and what
    # the instrument said -- and a log overwritten by the next run cannot be quoted afterwards.
    log = 'H:/tmp/pdfium-probe/control-%s.txt' % name
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pdfium_probe_run  # noqa: E402
    sys.argv = ['pdfium_probe_run.py', 'H:/tmp/outline/real-book.pdf', log]
    outcome = pdfium_probe_run.main()
    text = io.open(log, encoding='utf-8', errors='replace').read()
    return outcome, text


def main():
    names = sys.argv[1:] or ['no-ffl', 'no-annot-flag', 'no-fill', 'no-composite']
    for name in names:
        if name not in MUTATIONS:
            print('unknown control %s' % name)
            return 2

    original = digest(PROBE)
    with io.open(PROBE, encoding='utf-8') as handle:
        source = handle.read()
    environment = gate.compilerEnvironment(gate.findVcvars())
    cmake = gate.findCMake(None)
    failures = []

    try:
        for name in names:
            old, new = MUTATIONS[name]
            if source.count(old) != 1:
                print('%s: the line to break was found %d times -- refusing'
                      % (name, source.count(old)))
                failures.append(name)
                continue
            with io.open(PROBE, 'w', encoding='utf-8', newline='\n') as handle:
                handle.write(source.replace(old, new, 1))
            print('\n=== %s ===' % name)
            built, text = build(environment, cmake)
            if not built:
                print('  the mutated probe did not build: %s'
                      % [line for line in text.splitlines() if 'error' in line][:3])
                failures.append(name)
                continue
            outcome, output = runProbe(environment, name)
            reds = [line.strip() for line in output.splitlines()
                    if line.strip().startswith('C') and 'BROKEN' in line]
            print('  exit %d, %d control(s) red' % (outcome, len(reds)))
            for line in reds:
                print('   ' + line[:180])
            if outcome == 0 or not reds:
                print('  the control did not go red -- this mutation proves the assertion '
                      'is not watching anything')
                failures.append(name)
    finally:
        with io.open(PROBE, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(source)
        restored = digest(PROBE)
        print('\nrestored %s: %s (%s)' % (os.path.basename(PROBE),
                                          'byte-identical' if restored == original else 'DIFFERENT',
                                          restored[:12]))
        built, text = build(environment, cmake)
        print('rebuild after restore: %s' % ('built' if built else 'FAILED'))
        if not built:
            print('\n'.join([line for line in text.splitlines() if 'error' in line][:5]))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
