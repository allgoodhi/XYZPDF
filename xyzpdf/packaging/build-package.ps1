<#
.SYNOPSIS
    Builds a local MSIX out of what has already been compiled.

.DESCRIPTION
    Stages the deployed program, stamps the version into a copy of the manifest,
    and asks makeappx to pack it. Nothing here installs or signs anything unless
    told to with -Install or -Sign, because both change the machine: a package
    that is signed with a test certificate makes Windows trust an author, and a
    package that is installed changes what double-clicking a PDF does.

    The version comes from one place, as everywhere else in this project: the
    project(... VERSION ...) line in CMakeLists.txt. The committed manifest keeps
    its 0.0.0.0 placeholder -- tests/version_smoke.cpp fails if that is edited,
    precisely so that no one can bump a number in two places and have them
    disagree. Stamping happens on the copy inside the staging directory.

    The manifest names the executable, four tile images and the three shell
    handler DLLs (preview pane, thumbnail, context menu verb). makeappx reports
    a missing file as a pack failure with a message about the layout, which is
    harder to read than the plain "the manifest names X, X is not there" that
    step 3 below prints, so the check is done before packing. The DLLs count as
    much as the pictures do: a handler that is named in the manifest but absent
    from the package is not a pack failure anyone would notice -- the preview
    pane just says "no preview", and that reads like a bug in the handler.

.EXAMPLE
    powershell -File packaging/build-package.ps1
    powershell -File packaging/build-package.ps1 -Sign -Cert 2A1B... -Install
#>
[CmdletBinding()]
param(
    [string]$Build = 'build-local-release',
    [string]$Out = '',
    [string]$Publisher = '',
    [switch]$Sign,
    [string]$Cert = '',
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
Set-Location $Project

# ---------------------------------------------------------------------------
# 1. The version, from the one line that owns it
# ---------------------------------------------------------------------------

$line = Select-String -Path (Join-Path $Project 'CMakeLists.txt') `
        -Pattern '^project\(xyzpdf VERSION ([0-9.]+)' | Select-Object -First 1
if (-not $line) { throw 'the version was not found in CMakeLists.txt' }
$Version = $line.Matches[0].Groups[1].Value
# A package version has four parts. A project version has three, because the
# fourth is not a thing anyone writes down; it is appended rather than asked for.
if (($Version.Split('.')).Count -eq 3) { $Version = "$Version.0" }
Write-Host "packing version $Version"

# ---------------------------------------------------------------------------
# 2. Stage the deployed program
# ---------------------------------------------------------------------------

$StageRoot = if ($Out) { $Out } else { Join-Path $Build 'package' }
$Stage = Join-Path $StageRoot 'app'
# The delete is retried because the previous run's makeappx can still be holding
# a read on the staging tree when this one starts, and the raw error -- another
# process is using the file, on a directory -- says nothing about which one or
# what to do. It clears on its own after a moment, so wait for it rather than
# telling someone to go hunting.
if (Test-Path $StageRoot) {
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        try {
            Remove-Item -Recurse -Force $StageRoot -ErrorAction Stop
            break
        } catch {
            if ($attempt -eq 10) {
                # The usual reason is not another program reading the directory: it
                # is that the package registered from it is still installed, which
                # keeps the layout in use -- DllHost has the handler mapped. So the
                # message says the two words that actually help.
                throw "$StageRoot is still held after 10 tries. If a package registered from it is installed, that is the holder: Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage, then run this again."
            }
            Start-Sleep -Milliseconds 500
        }
    }
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

$cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'E:\qt\Tools\CMake_64\bin\cmake.exe' }
if (-not (Test-Path $cmake)) { throw 'cmake was not found' }
$buildDir = Join-Path $Project $Build
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    throw "$Build has not been configured; run python tools/build_all_tests.py first"
}

Write-Host 'installing the program into the staging directory'
& $cmake --install $buildDir --prefix $Stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed ($LASTEXITCODE)" }

# ---------------------------------------------------------------------------
# 3. The manifest and the pictures it names, then a check that both agree
# ---------------------------------------------------------------------------

Copy-Item (Join-Path $Project 'packaging\AppxManifest.xml') (Join-Path $Stage 'AppxManifest.xml')
Copy-Item -Recurse (Join-Path $Project 'packaging\Assets') (Join-Path $Stage 'Assets')

# Read as UTF-8 explicitly. The manifest carries Chinese in the tile
# description, and Windows PowerShell's default is the ANSI codepage, which
# turns those bytes into something that is no longer well-formed XML -- the
# parse then fails somewhere far from the cause.
[xml]$manifest = Get-Content -LiteralPath (Join-Path $Stage 'AppxManifest.xml') -Raw -Encoding UTF8

$identity = $manifest.Package.Identity
$identity.Version = $Version
if ($Publisher) { $identity.Publisher = $Publisher }

# Every path the manifest declares. Attributes and the text of the Logo
# elements, which is all of them; the element *text* of everything else is not
# usable, because a parent element's text is its children run together --
# "XYZPDFallgoodhiAssets\StoreLogo.png" is what Properties reads as, and it is
# not a path anyone wrote down.
$declared = @()
foreach ($node in $manifest.GetElementsByTagName('*')) {
    $values = @($node.Attributes | ForEach-Object { $_.Value })
    if ($node.LocalName -eq 'Logo') { $values += $node.InnerText }
    foreach ($value in $values) {
        if ($value -match '^[A-Za-z0-9_\-\\.]+\.(exe|png|ico|jpg|dll)$' -and $declared -notcontains $value) {
            $declared += $value
        }
    }
}
$missing = @($declared | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Stage $_)) })
if ($missing) {
    throw ("the manifest names files that are not in the package: " + ($missing -join ', ') `
           + '. Run python tools/makeappicon.py, and check the install step above.')
}
Write-Host ("the manifest names {0} files and all of them are in the package" -f $declared.Count)

$manifest.Save((Join-Path $Stage 'AppxManifest.xml'))

# ---------------------------------------------------------------------------
# 4. Pack
# ---------------------------------------------------------------------------

function Find-KitTool([string]$name) {
    $found = (Get-Command $name -ErrorAction SilentlyContinue).Source
    if ($found) { return $found }
    # Two places the Windows SDK has installed this to over the years, and on
    # this machine it is neither: the App Certification Kit directory, which is
    # not on PATH.
    $guesses = @(
        'E:\Windows Kits\10\App Certification Kit',
        'C:\Program Files (x86)\Windows Kits\10\App Certification Kit'
    )
    foreach ($base in $guesses) {
        if (Test-Path (Join-Path $base $name)) { return (Join-Path $base $name) }
    }
    foreach ($base in @('C:\Program Files (x86)\Windows Kits\10\bin',
                        'E:\Windows Kits\10\bin')) {
        if (-not (Test-Path $base)) { continue }
        $hit = Get-ChildItem -Path $base -Recurse -Filter $name -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$makeappx = Find-KitTool 'makeappx.exe'
if (-not $makeappx) { throw 'makeappx.exe was not found; the Windows SDK is not installed' }

$package = Join-Path $StageRoot ("XYZPDF_{0}_x64.msix" -f $Version)
Write-Host 'packing'
# makeappx names every file it takes in, which is a hundred lines of noise on a
# success and the only useful part on a failure. So it goes to a log, and the log
# is printed when the pack did not work.
$log = Join-Path $StageRoot 'makeappx.log'
& $makeappx pack /d $Stage /p $package /o 2>&1 | Out-File -FilePath $log -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Get-Content $log -Tail 40
    throw "makeappx pack failed ($LASTEXITCODE); the whole log is $log"
}
$size = [math]::Round((Get-Item $package).Length / 1MB, 1)
Write-Host ("wrote {0} ({1} MB)" -f (Split-Path -Leaf $package), $size)

# ---------------------------------------------------------------------------
# 4b. Read the manifest back out of the package
#
# An MSIX is a zip, so this asks the packed file what it says rather than
# trusting that the stamping survived the pack. The version is the number that
# shows up in Settings and in the Store listing, and a package that quietly kept
# 0.0.0.0 would install over itself forever without anyone noticing.
# ---------------------------------------------------------------------------

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($package)
try {
    $entry = $zip.GetEntry('AppxManifest.xml')
    if (-not $entry) { throw 'the package has no AppxManifest.xml in it' }
    $reader = New-Object System.IO.StreamReader($entry.Open())
    [xml]$packed = $reader.ReadToEnd()
    $reader.Close()
} finally {
    $zip.Dispose() }
$packedVersion = $packed.Package.Identity.Version
if ($packedVersion -ne $Version) {
    throw "the package says version $packedVersion, which is not $Version"
}
Write-Host "the package's own manifest says version $packedVersion"

# ---------------------------------------------------------------------------
# 5. Sign and install, both opt-in because both change the machine
# ---------------------------------------------------------------------------

if ($Sign) {
    if (-not $Cert) { throw '-Sign needs -Cert with the thumbprint of a code-signing certificate' }
    $signtool = Find-KitTool 'signtool.exe'
    if (-not $signtool) { throw 'signtool.exe was not found' }
    & $signtool sign /fd SHA256 /sha1 $Cert /tr http://timestamp.digicert.com /td SHA256 $package
    if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
    Write-Host 'signed'
}

if ($Install) {
    # Loose registration of the staged layout, not the .msix. Three ways this was
    # found by failing rather than by reading:
    #
    #   * the .msix route refuses an unsigned package (0x800B0100) even with
    #     developer mode on, and with -AllowUnsigned it goes one step further and
    #     then refuses the publisher certificate (0x80073D2C), because
    #     CN=allgoodhi is a self-signed name the machine does not trust;
    #   * -Register takes the manifest of the unpacked directory and works;
    #   * and it works only WITHOUT -DisableDevelopmentMode. Passing that flag
    #     makes the registration fail as "the manifest is not located in a package
    #     directory", which reads like a path problem and is not one.
    #
    # So this installs what is on disk, which is also what makes the source of a
    # bug the file that was just built rather than a copy nobody can read.
    Add-AppxPackage -Register (Join-Path $Stage 'AppxManifest.xml')
    Write-Host "installed; launch it with: explorer.exe shell:AppsFolder\$($identity.Name)_<publisherhash>!XYZPDF"
    Write-Host '          Get-StartApps shows the exact id once it is registered.'
}

Write-Host ''
Write-Host "next: the package is $package"
Write-Host '      it is not installed, and nothing about the system has changed.'
