# A thin wrapper, so that nobody writes a "run the tests" script that only runs
# them.
#
# tools/build_all_tests.py is the implementation, and it configures and builds
# as well as running -- which is the whole point: a test that stopped compiling
# leaves an .exe behind, and running that .exe reports whatever it reported the
# day it was built. XYZip lost two days to exactly that.
#
# This exists for the shell on this machine being PowerShell: it finds python
# and forwards every argument.
#
#     .\tools\run_all_tests.ps1
#     .\tools\run_all_tests.ps1 --build-only

$ErrorActionPreference = 'Stop'

$script = Join-Path $PSScriptRoot 'build_all_tests.py'

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) {
    $python = (Get-Command py -ErrorAction SilentlyContinue).Source
}
if (-not $python) {
    Write-Error 'python was not found on PATH; install it or run build_all_tests.py with an interpreter you have.'
    exit 2
}

& $python $script @args
exit $LASTEXITCODE
