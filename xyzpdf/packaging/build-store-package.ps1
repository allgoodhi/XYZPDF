<#
.SYNOPSIS
    Builds the package that goes to the Microsoft Store -- the same payload as
    the local test package, under the identity Partner Center reserved.

.DESCRIPTION
    The staging, the resource index, the packing and the read-back checks all
    live in build-package.ps1, which this script calls with three things it
    cannot decide for itself: which manifest to use, which identity to stamp,
    and the C runtime to carry app-local. What is *here* is the part that is
    only true of a Store submission -- the identity rules, because this is the
    one package in the project whose mistakes are found hours later by a server
    that says very little about them.

    The identity is read out of the build's own generated header rather than
    passed in. That is what keeps the program, the manifest and the submission
    describing the same package: xyzpdf.exe works out its own package family
    name at runtime to notice whether it is running packaged, so a manifest
    stamped with an identity the binary was not compiled against gives a program
    that believes it is unpackaged and registers fallback shell verbs on top of
    the packaged ones -- two identical entries in Explorer's menu, and nothing
    that tells the user which one to blame.

    Set the reserved values at configure time, before building:

        cmake -B build-store -DXYZPDF_PACKAGE_IDENTITY_NAME=<reserved name> `
                    -DXYZPDF_PACKAGE_PUBLISHER="CN=<assigned publisher id>" `
                    -DXYZPDF_PACKAGE_PUBLISHER_DISPLAY_NAME=<display name>

    The Store signs what it is given, so -Sign is for trying a package out
    locally under a stand-in identity; it needs a certificate whose subject is
    that identity, and build-package.ps1 refuses the pairing otherwise.

.EXAMPLE
    .\build-store-package.ps1
    Builds from build-local-release with whatever identity that tree was
    configured for. Refuses, loudly, while that identity is still CN=allgoodhi.

.EXAMPLE
    .\build-store-package.ps1 -Build build-store
    The usual shape once a name has been reserved.
#>
[CmdletBinding()]
param(
    [string]$Build = 'build-local-release',
    [string]$Out = '',
    [switch]$Sign,
    [string]$Cert = '',
    # Where vcruntime140.dll and friends live. Found through vswhere when empty,
    # so this does not need a developer command prompt.
    [string]$CrtDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windowskit.ps1')

# Absolute before anything else: build-package.ps1 changes the current directory
# when it runs, and a relative path handed to it then means something different
# than it did here.
if (-not [IO.Path]::IsPathRooted($Build)) { $Build = Join-Path $Project $Build }

# ---------------------------------------------------------------------------
# 1. The identity, from the header the build was compiled against
# ---------------------------------------------------------------------------

$generated = Join-Path $Build 'generated/xyzpdf'
foreach ($header in @('version.h', 'packageconfig.h')) {
    if (-not (Test-Path (Join-Path $generated $header))) {
        throw ("$header is not in $generated. That directory is written by CMake at " +
               'configure time, so -Build is either not a CMake build directory or it ' +
               'predates the generated headers -- reconfigure it.')
    }
}

function Read-Define {
    param([string]$File, [string]$Name)
    $text = Get-Content -Path $File -Raw -Encoding UTF8
    $match = [regex]::Match($text, '(?m)^#define\s+' + [regex]::Escape($Name) + '\s+"([^"]*)"')
    if (-not $match.Success) { throw "could not read $Name out of $File" }
    return $match.Groups[1].Value
}

$versionHeader = Join-Path $generated 'version.h'
$configHeader = Join-Path $generated 'packageconfig.h'
$version = Read-Define -File $versionHeader -Name 'XYZPDF_VERSION_QUAD'
$identityName = Read-Define -File $configHeader -Name 'XYZPDF_PACKAGE_IDENTITY_NAME'
$publisher = Read-Define -File $configHeader -Name 'XYZPDF_PACKAGE_PUBLISHER'
$displayName = Read-Define -File $configHeader -Name 'XYZPDF_PACKAGE_DISPLAY_NAME'
$publisherDisplayName = Read-Define -File $configHeader -Name 'XYZPDF_PACKAGE_PUBLISHER_DISPLAY_NAME'

Write-Host "version              $version"
Write-Host "Identity/@Name       $identityName"
Write-Host "Identity/@Publisher  $publisher"
Write-Host "DisplayName          $displayName"
Write-Host "PublisherDisplayName $publisherDisplayName"

# The header's version and the line that owns it have to agree, or the build tree
# is stale: CMakeLists.txt was bumped and nothing was reconfigured, so the binary
# reports one number and its package another. build-package.ps1 stamps from
# CMakeLists.txt -- which is the single source everywhere else in this project --
# so a disagreement here is a stale tree rather than a second place to edit.
$cmakeLine = Select-String -Path (Join-Path $Project 'CMakeLists.txt') `
        -Pattern '^project\(xyzpdf VERSION ([0-9.]+)' | Select-Object -First 1
if (-not $cmakeLine) { throw 'the version was not found in CMakeLists.txt' }
$sourceVersion = $cmakeLine.Matches[0].Groups[1].Value
if ($sourceVersion.Split('.').Count -eq 3) { $sourceVersion = "$sourceVersion.0" }
if ($version -ne $sourceVersion) {
    throw ("the build's generated header says $version but CMakeLists.txt says " `
           + "$sourceVersion. The tree is stale: reconfigure and build it before packing, " `
           + 'or the package carries a version its own executable disagrees with.')
}

# ---------------------------------------------------------------------------
# 2. The package family name, and what it refuses
# ---------------------------------------------------------------------------

function Get-PublisherId {
    param([Parameter(Mandatory = $true)][string] $Publisher)

    # The second half of a package family name: Crockford base32 (no i, l, o or
    # u) over the first 8 bytes of the SHA-256 of the publisher encoded as
    # UTF-16LE, most significant bits first, one bit of padding at the low end.
    #
    # Worked out against two known pairs rather than from a description, and kept
    # honest by the first of them: Microsoft's own publisher must come out as
    # 8wekyb3d8bbwe, which is checked below before this is trusted with anything.
    $alphabet = '0123456789abcdefghjkmnpqrstvwxyz'
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha.ComputeHash([System.Text.Encoding]::Unicode.GetBytes($Publisher))
    } finally {
        $sha.Dispose()
    }

    $value = [System.Numerics.BigInteger]::Zero
    foreach ($byte in $digest[0..7]) { $value = $value * 256 + $byte }
    $value = $value * 2

    $out = ''
    for ($index = 12; $index -ge 0; $index--) {
        $shifted = $value / [System.Numerics.BigInteger]::Pow(32, $index)
        $out += $alphabet[[int]($shifted % 32)]
    }
    return $out
}

$knownMicrosoft = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'
if ((Get-PublisherId -Publisher $knownMicrosoft) -ne '8wekyb3d8bbwe') {
    throw ('The package-family-name calculation is wrong: it does not reproduce ' +
           "Microsoft's own well-known publisher ID. Refusing to check anything with it.")
}

# A refusal rather than a warning, and on the publisher on its own rather than on
# both values together. Identity/@Publisher is half of what the family name is
# derived from, and the derivation is a one-way hash: a package built under the
# local development publisher cannot produce the family name Partner Center
# reserved, so the submission comes back without much to say about why. That is a
# slow way to learn something knowable here. (Carried over from XYZip, where the
# first version of this check required *both* values to still be the development
# ones -- so setting the reserved name and leaving the publisher behind, exactly
# the order a person does it in, slipped through silently.)
if ($publisher -eq 'CN=allgoodhi') {
    throw ('Identity/@Publisher is still the local development value CN=allgoodhi, so ' `
           + 'nothing has been reserved in Partner Center for this program yet. A Store ' `
           + 'package must carry the publisher Partner Center assigned: it is hashed into ' `
           + 'the package family name, and the submission is rejected without it. Copy ' `
           + 'Package/Identity/Publisher from Partner Center (Product identity) and ' `
           + "reconfigure the build with -DXYZPDF_PACKAGE_PUBLISHER=`"<that value>`". " `
           + 'To exercise this script before a name exists, configure a separate build ' `
           + 'directory with a stand-in identity and pass it as -Build.')
}

$reservedFamilyName = '{0}_{1}' -f $identityName, (Get-PublisherId -Publisher $publisher)
Write-Host "Package family name  $reservedFamilyName"

# ---------------------------------------------------------------------------
# 3. The C runtime, app-local
# ---------------------------------------------------------------------------

if (-not $CrtDirectory) {
    # VCToolsRedistDir is only set inside a developer command prompt, and
    # requiring one just to build a package is a trap: the error arrives after
    # the staging has run, so it reads like something went wrong rather than like
    # the shell was the wrong shell. Ask vswhere instead -- it sits at a fixed
    # path precisely so it can be found without an initialised environment.
    $redistRoots = @()
    if ($env:VCToolsRedistDir) {
        $redistRoots += $env:VCToolsRedistDir
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $vswhere) {
            $installs = & $vswhere -products * -latest -property installationPath
            foreach ($install in @($installs)) {
                $msvc = Join-Path $install 'VC\Redist\MSVC'
                if (Test-Path $msvc) {
                    # Newest toolset, compared as versions: sorted as text, 14.9
                    # beats 14.51.
                    $redistRoots += Get-ChildItem -Path $msvc -Directory |
                        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } `
                                    -Descending |
                        Select-Object -ExpandProperty FullName
                }
            }
        }
    }

    foreach ($root in $redistRoots) {
        $x64 = Join-Path $root 'x64'
        if (-not (Test-Path $x64)) { continue }
        $candidate = Get-ChildItem -Path $x64 -Filter 'Microsoft.VC*.CRT' -Directory `
                        -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
        if ($candidate) { $CrtDirectory = $candidate; break }
    }

    if (-not $CrtDirectory) {
        throw ('Cannot find the Visual C++ runtime. Run this from a developer command ' +
               'prompt, or pass -CrtDirectory with the folder holding vcruntime140.dll.')
    }
}
Write-Host "C runtime            $CrtDirectory"

# ---------------------------------------------------------------------------
# 4. The package itself, which is build-package.ps1's job
# ---------------------------------------------------------------------------

$packer = Join-Path $PSScriptRoot 'build-package.ps1'
$packerArgs = @{
    Build = $Build
    Manifest = 'StoreAppxManifest.xml'
    Publisher = $publisher
    IdentityName = $identityName
    DisplayName = $displayName
    PublisherDisplayName = $publisherDisplayName
    CrtDirectory = $CrtDirectory
}
if ($Out) {
    $packerArgs.Out = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $Project $Out }
} else {
    $packerArgs.Out = Join-Path $Build 'package-store'
}
if ($Sign) {
    $packerArgs.Sign = $true
    $packerArgs.Cert = $Cert
}

& $packer @packerArgs

Write-Host ''
Write-Host "next: the Store takes the unsigned package -- it signs what it is given, so"
Write-Host "      uploading a -Sign output would only make it re-sign a signature of ours."
Write-Host "      Upload the .msix that build-package.ps1 printed above, from"
Write-Host "      Partner Center > Overview > Packages > Add a new Windows or Mac package."
