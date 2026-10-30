<#
.SYNOPSIS
    Builds the GitHub installer, XYZPDF-<version>-setup.exe, out of what has
    already been compiled.

.DESCRIPTION
    1. Stages the program with build-package.ps1 -StageOnly, C runtime included:
       the same payload the MSIX carries, from the same lines.
    2. Reads the version from CMakeLists.txt and the preview handler's CLSID from
       AppxManifest.xml, and checks that CLSID against the two sources that
       compile it in -- the installer registers the class, the program points the
       ProgID at it, the DLL answers for it, and the three must be one number.
    3. Compiles packaging/xyzpdf.iss with Inno Setup's ISCC, passing those as
       defines, and writes the installer's SHA-256 beside it.

    Nothing here installs anything or touches the registry. Run the setup.exe to
    do that.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf/packaging/build-installer.ps1
#>
[CmdletBinding()]
param(
    [string]$Build = 'build-local-release',
    # ISCC.exe. Empty means: PATH, then the per-user and per-machine places the
    # Inno Setup 6 installer puts it.
    [string]$Iscc = '',
    [string]$CrtDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
Set-Location $Project
. (Join-Path $PSScriptRoot 'windowskit.ps1')

$buildDir = if ([IO.Path]::IsPathRooted($Build)) { $Build } else { Join-Path $Project $Build }
$outRoot = Join-Path $buildDir 'installer'

# ---------------------------------------------------------------------------
# 1. The numbers: version, and the preview handler's class
# ---------------------------------------------------------------------------

$line = Select-String -Path (Join-Path $Project 'CMakeLists.txt') `
        -Pattern '^project\(xyzpdf VERSION ([0-9.]+)' | Select-Object -First 1
if (-not $line) { throw 'the version was not found in CMakeLists.txt' }
$Version = $line.Matches[0].Groups[1].Value
Write-Host "installer version $Version"

[xml]$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml') -Raw -Encoding UTF8
$previewNode = $manifest.GetElementsByTagName('desktop2:DesktopPreviewHandler') | Select-Object -First 1
if (-not $previewNode) { throw 'AppxManifest.xml declares no DesktopPreviewHandler' }
$PreviewClsid = $previewNode.GetAttribute('Clsid').ToUpperInvariant()
if ($PreviewClsid -notmatch '^[0-9A-F]{8}(-[0-9A-F]{4}){3}-[0-9A-F]{12}$') {
    throw "the manifest's preview CLSID '$PreviewClsid' is not a bare GUID"
}
# The program writes the ProgID pointer from its own copy of the number, and the
# DLL is built from a third. The struct form in previewhandler.cpp starts with
# the first group as a hex literal; the pointer is the string form.
$first = $PreviewClsid.Substring(0, 8).ToLowerInvariant()
$checks = @(
    @{ File = 'src\windows\shellintegration.cpp'; Pattern = [regex]::Escape("{$PreviewClsid}") },
    @{ File = 'src\windows\previewhandler.cpp';   Pattern = "0x$first," }
)
foreach ($c in $checks) {
    $path = Join-Path $Project $c.File
    if (-not (Select-String -LiteralPath $path -Pattern $c.Pattern -Quiet)) {
        throw "$($c.File) does not carry the preview CLSID $PreviewClsid that AppxManifest.xml declares"
    }
}
Write-Host "preview handler class {$PreviewClsid}, the same in the manifest, shellintegration.cpp and previewhandler.cpp"

# ---------------------------------------------------------------------------
# 2. ISCC
# ---------------------------------------------------------------------------

if (-not $Iscc) {
    $candidates = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    $Iscc = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if (-not $Iscc -or -not (Test-Path -LiteralPath $Iscc)) {
    throw 'ISCC.exe was not found. Install Inno Setup 6 (jrsoftware.org), or pass -Iscc.'
}
Write-Host "Inno Setup compiler $Iscc"

# ---------------------------------------------------------------------------
# 3. Stage
# ---------------------------------------------------------------------------

if (-not $CrtDirectory) { $CrtDirectory = Find-VcRuntimeDirectory }
Write-Host "C runtime $CrtDirectory"

& (Join-Path $PSScriptRoot 'build-package.ps1') -Build $buildDir -Out $outRoot -CrtDirectory $CrtDirectory -StageOnly
$stage = Join-Path $outRoot 'app'

# What the installer's own entries name, checked before compiling: ISCC packs a
# folder, and a missing file there is an installer that runs, registers a class
# for a DLL that is not on disk, and says nothing.
$needed = @('bin\xyzpdf.exe', 'bin\xyzpdfpreview.dll', 'bin\pdfium.dll', 'bin\qpdf30.dll',
            'bin\vcruntime140.dll', 'bin\vcruntime140_1.dll', 'bin\msvcp140.dll',
            'bin\Qt6Core.dll', 'bin\Qt6Gui.dll', 'bin\Qt6Widgets.dll')
$missing = @($needed | Where-Object { -not (Test-Path -LiteralPath (Join-Path $stage $_)) })
if ($missing) { throw ('the staged program lacks ' + ($missing -join ', ')) }
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
$bytes = ($files | Measure-Object Length -Sum).Sum
Write-Host ("staged {0} files, {1} MB" -f $files.Count, [math]::Round($bytes / 1MB, 1))

# ---------------------------------------------------------------------------
# 4. Compile
# ---------------------------------------------------------------------------

$setup = Join-Path $outRoot ("XYZPDF-{0}-setup.exe" -f $Version)
if (Test-Path -LiteralPath $setup) { Remove-Item -LiteralPath $setup -Force }
$log = Join-Path $outRoot 'iscc.log'
& $Iscc /Q "/DAppVersion=$Version" "/DStageDir=$stage" "/DOutputDir=$outRoot" `
        "/DPreviewClsid=$PreviewClsid" (Join-Path $PSScriptRoot 'xyzpdf.iss') 2>&1 |
    Out-File -FilePath $log -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Get-Content $log -Tail 40
    throw "ISCC failed ($LASTEXITCODE); the whole log is $log"
}
if (-not (Test-Path -LiteralPath $setup)) { throw "ISCC reported success but $setup is not there" }

$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$hashFile = "$setup.sha256"
# The format sha256sum -c reads: hash, two spaces, file name.
[IO.File]::WriteAllText($hashFile, ("{0}  {1}`n" -f $hash, (Split-Path -Leaf $setup)))
$size = (Get-Item -LiteralPath $setup).Length
Write-Host ("wrote {0} ({1} bytes, {2} MB)" -f $setup, $size, [math]::Round($size / 1MB, 1))
Write-Host "sha256 $hash"
Write-Host 'not signed: Windows SmartScreen will say the publisher is unknown.'
