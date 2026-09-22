<#
.SYNOPSIS
    Builds a local MSIX out of what has already been compiled.

.DESCRIPTION
    Stages the deployed program, stamps the identity and the version into a copy
    of the manifest, indexes the pictures into resources.pri, and asks makeappx
    to pack it. Nothing here installs or signs anything unless told to with
    -Install or -Sign, because both change the machine: a package that is signed
    with a test certificate makes Windows trust an author, and a package that is
    installed changes what double-clicking a PDF does.

    This is the one packer for both packages in the project. The -Manifest,
    -IdentityName, -DisplayName, -PublisherDisplayName and -CrtDirectory
    parameters exist so that build-store-package.ps1 can make the Store package
    out of this same staging rather than carrying a second copy of it, and the
    defaults are the local test package: same manifest, committed identity, no
    C runtime (this machine has the redistributable; a customer's may not).

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

    -Sign wants a certificate whose subject is the manifest's Publisher, which for
    a local test package means making it as -Subject 'CN=allgoodhi'; step 5 says
    why that is not something signtool will tell you.
#>
[CmdletBinding()]
param(
    [string]$Build = 'build-local-release',
    [string]$Out = '',
    [string]$Publisher = '',
    # The identity of the package, and which manifest describes it. Both exist
    # for build-store-package.ps1 to pass: the Store package is this same payload
    # with a different identity and a different manifest, and two copies of the
    # staging, indexing and packing steps is two chances to fix one of them.
    [string]$Manifest = 'AppxManifest.xml',
    [string]$IdentityName = '',
    # The two names a package shows: Properties/DisplayName is the installed-apps
    # list, uap:VisualElements/@DisplayName is the Start menu. Both come from
    # XYZPDF_PACKAGE_DISPLAY_NAME in the build, because the program's own strings
    # come from that same variable and a package that calls itself something the
    # binary does not is one of the two wearing the other's name.
    [string]$DisplayName = '',
    [string]$PublisherDisplayName = '',
    # Folder holding vcruntime140.dll and friends, copied in app-local. Empty
    # means the package does not carry the C runtime, which is fine on the
    # machine that built it and not fine on a customer's.
    [string]$CrtDirectory = '',
    [switch]$Sign,
    [string]$Cert = '',
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
Set-Location $Project
. (Join-Path $PSScriptRoot 'windowskit.ps1')

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
# Make it absolute before anything joins onto it. Two of the consumers below are
# .NET calls (manifest.Save, ZipFile::OpenRead) rather than cmdlets, and a .NET
# call resolves a relative path against the *process* working directory, which is
# not PowerShell's location: run this from an elevated Start-Process and the
# staged manifest is saved to C:\Windows\System32\build-local-release\package\app,
# failing as a "path not found" that names a directory nobody typed. Steps 1 to 3
# all succeed first, so it looks like the manifest broke rather than the path.
if (-not [IO.Path]::IsPathRooted($StageRoot)) { $StageRoot = Join-Path $Project $StageRoot }
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
# Same rooted-or-relative question as $StageRoot above, and joining blindly is
# what a caller passing an absolute path used to get: Join-Path glues the two
# together into H:\test\XYZPDF\xyzpdf\H:\test\..., which then fails as "this
# build directory has not been configured" about a directory that plainly has.
$buildDir = if ([IO.Path]::IsPathRooted($Build)) { $Build } else { Join-Path $Project $Build }
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    throw "$Build has not been configured; run python tools/build_all_tests.py first"
}

Write-Host 'installing the program into the staging directory'
& $cmake --install $buildDir --prefix $Stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed ($LASTEXITCODE)" }

# The C runtime, app-local, into bin\ beside the executable that imports it.
# Checked for by name rather than copied and hoped over: a machine installing
# from the Store is not required to have a redistributable, and a missing
# vcruntime140.dll is a dialog box at startup rather than anything the program
# can report -- which the user then cannot connect to this package.
if ($CrtDirectory) {
    Write-Host "copying the Visual C++ runtime from $CrtDirectory"
    Copy-Item -Path (Join-Path $CrtDirectory '*.dll') -Destination (Join-Path $Stage 'bin') -Force
    foreach ($needed in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
        if (-not (Test-Path (Join-Path $Stage "bin\$needed"))) {
            throw "$needed is not in $CrtDirectory, and xyzpdf.exe imports it"
        }
    }
}

# ---------------------------------------------------------------------------
# 3. The manifest and the pictures it names, then a check that both agree
# ---------------------------------------------------------------------------

$manifestSource = Join-Path $Project (Join-Path 'packaging' $Manifest)
if (-not (Test-Path -LiteralPath $manifestSource)) {
    throw "there is no manifest at $manifestSource"
}
Copy-Item $manifestSource (Join-Path $Stage 'AppxManifest.xml')
Copy-Item -Recurse (Join-Path $Project 'packaging\Assets') (Join-Path $Stage 'Assets')

# Read as UTF-8 explicitly. The manifest carries Chinese in the tile
# description, and Windows PowerShell's default is the ANSI codepage, which
# turns those bytes into something that is no longer well-formed XML -- the
# parse then fails somewhere far from the cause.
[xml]$manifest = Get-Content -LiteralPath (Join-Path $Stage 'AppxManifest.xml') -Raw -Encoding UTF8

$identity = $manifest.Package.Identity
$identity.Version = $Version
if ($Publisher) { $identity.Publisher = $Publisher }
if ($IdentityName) { $identity.Name = $IdentityName }
if ($PublisherDisplayName) { $manifest.Package.Properties.PublisherDisplayName = $PublisherDisplayName }
if ($DisplayName) {
    $manifest.Package.Properties.DisplayName = $DisplayName
    # The name the Start menu shows lives on the application element, not in
    # Properties, and the two are easy to set one at a time.
    $visual = $manifest.Package.Applications.Application.VisualElements
    $visual.DisplayName = $DisplayName
    if ($visual.DisplayName -cne $manifest.Package.Properties.DisplayName) {
        # Both were just assigned from one variable, so this cannot fail on the
        # names disagreeing. What it can fail on is a path into the manifest that
        # no longer lands on an element: that reads back as $null, and a silent
        # $null here is a package whose Start menu entry keeps the old name while
        # its installed-apps entry has the new one.
        throw ("the application's display name is '$($visual.DisplayName)' but the package's is " `
               + "'$($manifest.Package.Properties.DisplayName)'. One of the two paths into the " `
               + 'manifest is wrong, or the manifest has lost an element it should have.')
    }
}

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
# 3b. The resource index
#
# Windows does not open Assets\Square44x44Logo.png by that path. It asks the
# resource system for the logo, and the resource system answers out of
# resources.pri -- the file that maps one name onto the several that exist for
# it: tools/makeappicon.py writes four tiles at five scales each (and five exact
# pixel sizes for the 44 one), while the manifest names only the four plain
# files.
#
# With no PRI there is no answer, and what the user sees is an entry in the Start
# menu with the right name and no picture at all -- while the package installs,
# reports Status Ok, and contains every tile it should. XYZip shipped exactly
# that before this step was added, and the symptom is the reason it is generated
# here and then checked for in the packed file below rather than trusted.
#
# The manifest is indexed too (/mn), because its <Resource Language> entries are
# resolved the same way the pictures are. The qualifier list is read out of the
# manifest rather than written here: tools/add_language.py is the thing that
# learns a new language, and a list in this script would be a second place to
# remember it.
# ---------------------------------------------------------------------------

# By the element name, which is Resource: Language is its attribute, and asking
# for an element called Language finds nothing and reads as "the manifest has no
# languages" on a manifest that has two.
$languages = @($manifest.GetElementsByTagName('Resource') |
               ForEach-Object { $_.GetAttribute('Language') })
if (-not $languages) {
    throw 'the manifest declares no <Resource Language>, so there is nothing to index'
}
$makepri = Find-WindowsKitTool 'makepri.exe'
# The config is an input, so it goes beside the staging directory rather than
# inside it: a file in $Stage would be packed into the package.
$priConfig = Join-Path $StageRoot 'priconfig.xml'
& $makepri createconfig /cf $priConfig /dq ('lang-' + ($languages -join '_lang-')) /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makepri createconfig failed ($LASTEXITCODE)" }
& $makepri new /pr $Stage /cf $priConfig /mn (Join-Path $Stage 'AppxManifest.xml') `
              /of (Join-Path $Stage 'resources.pri') /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makepri new failed ($LASTEXITCODE)" }
Remove-Item -Path $priConfig -Force -ErrorAction SilentlyContinue
if (-not (Test-Path (Join-Path $Stage 'resources.pri'))) {
    throw 'makepri reported success but wrote no resources.pri'
}
Write-Host ("wrote resources.pri over {0} pictures in Assets" -f
            (Get-ChildItem (Join-Path $Stage 'Assets') -Filter *.png).Count)

# ---------------------------------------------------------------------------
# 4. Pack
# ---------------------------------------------------------------------------

$makeappx = Find-WindowsKitTool 'makeappx.exe'

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
    # Asked of the package rather than of the staging directory, because the
    # pack is the step that decides what a user's machine ever sees.
    $hasPri = [bool]$zip.GetEntry('resources.pri')
} finally {
    $zip.Dispose() }
$packedVersion = $packed.Package.Identity.Version
if ($packedVersion -ne $Version) {
    throw "the package says version $packedVersion, which is not $Version"
}
Write-Host "the package's own manifest says version $packedVersion"
if (-not $hasPri) {
    throw 'the package has no resources.pri, so Windows cannot resolve its tile images'
}

# ---------------------------------------------------------------------------
# 5. Sign and install, both opt-in because both change the machine
# ---------------------------------------------------------------------------

if ($Sign) {
    if (-not $Cert) { throw '-Sign needs -Cert with the thumbprint of a code-signing certificate' }
    $signtool = Find-WindowsKitTool 'signtool.exe'

    # The signing certificate has to be one whose subject is the manifest's
    # Publisher, and that is worth checking here because signtool does not say
    # so. What it says instead is "SignerSign() failed" with -2147024885 /
    # 0x8007000b -- ERROR_BAD_FORMAT, "an attempt was made to load a program with
    # an incorrect format" -- which reads as a broken appx SIP, a 32/64-bit mixup
    # or a corrupt package. All three were chased on this machine, and the real
    # cause only showed up when the same package was signed with a certificate
    # minted as -Subject 'CN=allgoodhi': rc=0, first try. So a test certificate
    # must carry the name the manifest declares, not a friendlier one.
    #
    # Compared as a prefix rather than for equality because that is the rule the
    # packaging API uses: a certificate subject may carry more distinguished-name
    # parts than the manifest names, and a Store-issued cert does exactly that.
    $certObj = Get-ChildItem "Cert:\CurrentUser\My\$Cert" -ErrorAction SilentlyContinue
    if (-not $certObj) { $certObj = Get-ChildItem "Cert:\LocalMachine\My\$Cert" -ErrorAction SilentlyContinue }
    if (-not $certObj) { throw "there is no certificate $Cert in My, current user or machine" }
    $publisher = $packed.Package.Identity.Publisher
    if (-not $certObj.Subject.StartsWith($publisher, [StringComparison]::Ordinal)) {
        throw ("the certificate subject is '$($certObj.Subject)' but the package Publisher is '$publisher'. " `
               + 'signtool would fail with a meaningless 0x8007000b. Either make the certificate with ' `
               + "-Subject '$publisher' or pass -Publisher '$($certObj.Subject)'.")
    }

    & $signtool sign /fd SHA256 /sha1 $Cert /tr http://timestamp.digicert.com /td SHA256 $package
    if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
    Write-Host 'signed'
}

if ($Install) {
    if ($Sign) {
        # A signed package goes in through its own path, because that is the only
        # way to get a deployed package, which is what the certification tests
        # need; the loose registration below cannot give it.
        Add-AppxPackage -Path $package
        Write-Host "installed from $package"
    } else {
        # Loose registration of the staged layout, not the .msix. Three ways this
        # was found by failing rather than by reading:
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
        # The .msix route does open up once there is a certificate whose subject is
        # the manifest's Publisher and that certificate sits in
        # LocalMachine\TrustedPeople -- which is what the -Sign branch above makes
        # possible, and why it is the branch the certification tests need: a loosely
        # registered layout is not a deployed package, and the tests can only see a
        # deployed one.
        #
        # So this installs what is on disk, which is also what makes the source of a
        # bug the file that was just built rather than a copy nobody can read.
        Add-AppxPackage -Register (Join-Path $Stage 'AppxManifest.xml')
        Write-Host "installed; launch it with: explorer.exe shell:AppsFolder\$($identity.Name)_<publisherhash>!XYZPDF"
        Write-Host '          Get-StartApps shows the exact id once it is registered.'
    }
}

Write-Host ''
Write-Host "next: the package is $package"
if ($Install) {
    Write-Host '      it is installed, so Settings and double-clicking a PDF now see it.'
} else {
    Write-Host '      it is not installed, and nothing about the system has changed.'
}
