<#
.SYNOPSIS
    Checks that a built .msix carries everything it needs, nothing it must not,
    and that the program inside it starts with no help from this machine.

.DESCRIPTION
    A package that is missing a DLL builds, packs and uploads without complaint.
    It fails on the machine of whoever installs it, which is the worst place to
    find out. So this unpacks the package and answers three questions.

    Is everything there that should be? Two things in particular are invisible to
    any dependency walker: resources.pri, without which Windows cannot resolve
    the tile images the manifest names (the package installs, reports Status Ok,
    and shows an entry with the right name and no picture); and the three handler
    DLLs, whose absence makes the preview pane say "no preview" -- which reads
    like a bug in the handler rather than a missing file.

    Is everything there that should *not* be? A test executable or a .pdb means
    the payload was globbed rather than listed, and the first sign of that is a
    submission twice the size it should be.

    And does it run standalone? The program is started from the unpacked folder
    with a PATH holding nothing but Windows, and every module it loads is traced
    back to where it came from. Running it out of the build directory proves
    nothing, because Qt is found there through PATH -- and on this machine so is
    the Visual C++ runtime, which is why the CRT DLLs are traced by name rather
    than excused as "part of Windows": a package that does not carry
    vcruntime140.dll passes every other check and fails at startup on a customer's
    machine that has no redistributable installed.

    Two traps XYZip's version of this script recorded, both of which had made an
    earlier check report success on a broken package:

    A missing DLL and a missing Qt platform plugin both put up an error box, and
    an error box is a window. "It started and showed a window" was true in every
    failing case, so the check is the window's *title*, compared with -cne
    because PowerShell's default string compare ignores case and the two titles
    differ only by it.

    The title it expects is read out of the unpacked manifest rather than written
    here, which makes the same check answer a second question: whether the program
    and its package call themselves the same thing. That is not hypothetical --
    the packaging script stamps Properties/DisplayName from the build, and the
    window title is the same value compiled into the binary, so anything that
    breaks the pairing (a title hard-coded on one side, a stale tree) shows up
    here. Measured both ways on 2026-09-22: with the stand-in name compiled in it
    passes, and with the name put back to a literal in the source it fails saying
    so. The error text names the error box as the likely cause because that is
    what it catches most of the time; a disagreement between the two names is the
    other thing it means.

    And the single-instance gateway makes a second copy of the program a
    messenger: if an XYZPDF is already running -- and the installed package puts
    one a click away -- the unpacked exe hands over its arguments and exits, which
    this script would otherwise read as "the package's program exits immediately".
    So it looks for a running copy first and refuses to guess.

.EXAMPLE
    .\verify-package.ps1 -Package build-local-release\package-store\XYZPDF_0.1.0.0_x64.msix
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Package,

    # Kept afterwards for looking at. Deleted first if it already exists.
    [string]$UnpackTo,

    # The package must carry the C runtime app-local. A Store package does; the
    # local test package is built for this machine, which has the redistributable,
    # and asking it for the DLLs would report a problem that is not one.
    [switch]$CarriesCrt
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Project = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($Package)) { $Package = Join-Path $Project $Package }
$Package = (Resolve-Path -LiteralPath $Package).Path
if (-not $UnpackTo) {
    $UnpackTo = Join-Path $env:TEMP ('xyzpdf-verify-' + [Guid]::NewGuid().ToString('N'))
}
if (Test-Path $UnpackTo) { Remove-Item $UnpackTo -Recurse -Force }
[System.IO.Compression.ZipFile]::ExtractToDirectory($Package, $UnpackTo)

$problems = @()
$notes = @()

$manifestPath = Join-Path $UnpackTo 'AppxManifest.xml'
if (-not (Test-Path $manifestPath)) { throw 'the package has no AppxManifest.xml in it' }
[xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8

# What this package calls the application. The window it puts up has to match:
# the name comes out of the manifest rather than being written here, so a package
# whose program and manifest disagree about the name is caught by this rather than
# by someone noticing the wrong text in the Start menu.
$expectedTitle = $manifest.Package.Properties.DisplayName
if (-not $expectedTitle) { throw 'the manifest has no Properties/DisplayName' }
$executable = $manifest.Package.Applications.Application.Executable
if (-not $executable) { throw 'the manifest has no Application/@Executable' }

# ---------------------------------------------------------------------------
# 1. The file list
# ---------------------------------------------------------------------------

$files = @(Get-ChildItem $UnpackTo -Recurse -File)
$total = ($files | Measure-Object Length -Sum).Sum
Write-Host ('{0} unpacks to {1:N1} MB in {2} files' -f
            (Split-Path -Leaf $Package), ($total / 1MB), $files.Count)

$required = @(
    # The index. Listed first because its absence is the one failure here with no
    # symptom to trace back from.
    'resources.pri',
    # The program and the three handlers the manifest names, one per DllHost.
    'bin\xyzpdf.exe', 'bin\xyzpdfpreview.dll', 'bin\xyzpdfthumbnail.dll',
    'bin\xyzpdfshell.dll',
    # The Qt runtime. Qt6Pdf is the renderer and Qt6Svg is what lets QIcon read
    # :/icons/app.svg -- neither is imported by name in a way a walker checks.
    'bin\Qt6Core.dll', 'bin\Qt6Gui.dll', 'bin\Qt6Widgets.dll', 'bin\Qt6Pdf.dll',
    'bin\Qt6Network.dll', 'bin\Qt6PrintSupport.dll', 'bin\Qt6Svg.dll',
    # The plugins Qt loads by name, so nothing in an import table asks for them.
    'plugins\platforms\qwindows.dll', 'plugins\imageformats\qsvg.dll',
    'plugins\styles\qmodernwindowsstyle.dll', 'plugins\tls\qschannelbackend.dll',
    # The tiles, one name per size class plus a scaled variant: the variant is
    # what the PRI exists for, so checking it is present checks the index has
    # something to index.
    'Assets\StoreLogo.png', 'Assets\Square44x44Logo.png', 'Assets\Square150x150Logo.png',
    'Assets\Wide310x150Logo.png', 'Assets\Square44x44Logo.scale-150.png',
    'Assets\Square150x150Logo.scale-150.png'
)
if ($CarriesCrt) {
    $required += @('bin\vcruntime140.dll', 'bin\vcruntime140_1.dll', 'bin\msvcp140.dll')
}
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $UnpackTo $name))) { $problems += "missing: $name" }
}

foreach ($name in @('AppxManifest.xml', 'AppxBlockMap.xml')) {
    if (-not (Test-Path -LiteralPath (Join-Path $UnpackTo $name))) { $problems += "missing: $name" }
}

# Things that mean the payload was globbed rather than listed.
$unwanted = @('vc_redist.x64.exe')
foreach ($name in $unwanted) {
    if (Test-Path -LiteralPath (Join-Path $UnpackTo $name)) { $problems += "should not be here: $name" }
}
foreach ($stray in @($files | Where-Object { $_.Extension -in @('.lib', '.pdb', '.ilk', '.obj') })) {
    $problems += "build leftover: $($stray.Name)"
}
foreach ($test in @($files | Where-Object { $_.Name -like '*_smoke.exe' -or $_.Name -like '*_bench.exe' })) {
    $problems += "test executable in the package: $($test.Name)"
}

# Carried, and probably not needed -- reported rather than failed, because proving
# it takes running the program on a machine where the answer is visible, and a
# script that guesses here would be worse than one that says the number out loud.
$doubtful = @($files | Where-Object {
    $_.Name -in @('opengl32sw.dll', 'D3Dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll') })
$doubtful += @($files | Where-Object { $_.Directory.Name -eq 'translations' -and $_.Extension -eq '.qm' })
if ($doubtful) {
    $waste = [math]::Round((($doubtful | Measure-Object Length -Sum).Sum) / 1MB, 1)
    $notes += ("{0} file(s) totalling {1} MB are carried and may not be needed: " `
               -f $doubtful.Count, $waste) +
              (($doubtful | Select-Object -First 4 | ForEach-Object { $_.Name }) -join ', ') +
              $(if ($doubtful.Count -gt 4) { " and $($doubtful.Count - 4) more" } else { '' }) +
              '. This program paints with the raster engine and keeps its own catalogues.'
}

# ---------------------------------------------------------------------------
# 2. Does it run with nothing but Windows on PATH?
# ---------------------------------------------------------------------------

$running = @(Get-Process xyzpdf -ErrorAction SilentlyContinue)
if ($running) {
    $problems += ("an xyzpdf.exe is already running (PID {0}), so the single-instance " `
                  -f (($running | ForEach-Object { $_.Id }) -join ', ')) +
                 'gateway would take this launch and the unpacked copy would exit at ' +
                 'once. Quit the running one and run this again.'
}

if ($problems.Count -eq 0) {
    $clean = "$env:SystemRoot\system32;$env:SystemRoot;$env:SystemRoot\system32\Wbem"
    $saved = $env:PATH
    $env:PATH = $clean
    try {
        $process = Start-Process -FilePath (Join-Path $UnpackTo $executable) -PassThru
    } finally {
        $env:PATH = $saved
    }
    try {
        Start-Sleep -Seconds 8
        $process.Refresh()
        if ($process.HasExited) {
            $problems += ("{0} exited with 0x{1:X8} rather than opening a window" -f
                          (Split-Path -Leaf $executable), $process.ExitCode)
        } elseif ($process.MainWindowTitle -cne $expectedTitle) {
            $title = $process.MainWindowTitle
            $problems += ("{0} put up a window titled '{1}' rather than '{2}', which is " `
                          -f (Split-Path -Leaf $executable), $title, $expectedTitle) +
                         'what its manifest says it is called. Either the program ' +
                         'failed to start (its fatal-error box is titled with the ' +
                         'lowercase application name, and the compare is -cne for that ' +
                         'reason), or it and its package were built from different ' +
                         'names, which is what the store identity is stamped from.'
        } else {
            Write-Host 'starts from the unpacked package with no Qt on PATH'

            $traced = @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Pdf.dll',
                        'Qt6Network.dll', 'Qt6PrintSupport.dll', 'Qt6Svg.dll',
                        'qwindows.dll', 'qsvg.dll')
            if ($CarriesCrt) {
                $traced += @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')
            }
            foreach ($name in $traced) {
                $module = @($process.Modules | Where-Object { $_.ModuleName -eq $name })
                if (-not $module) {
                    $problems += "not loaded at startup: $name"
                } elseif ($module[0].FileName -notlike "$UnpackTo*") {
                    $problems += "loaded from outside the package: $name <- $($module[0].FileName)"
                }
            }
        }
    } finally {
        if ($process -and -not $process.HasExited) { $process.Kill() }
    }
}

# ---------------------------------------------------------------------------
# 3. The verdict
# ---------------------------------------------------------------------------

Write-Host ''
foreach ($note in $notes) { Write-Host "note: $note" }
if ($problems.Count -eq 0) {
    Write-Host 'the package is complete and runs standalone'
    Write-Host "unpacked copy left in $UnpackTo"
    exit 0
}
Write-Host "$($problems.Count) problem(s):"
$problems | ForEach-Object { Write-Host "    $_" }
Write-Host "unpacked copy left in $UnpackTo"
exit 1
