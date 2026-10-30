# Finding the Windows SDK tools the packaging scripts need.
#
# Dot-sourced by build-package.ps1 and build-store-package.ps1. It was a
# function inside build-package.ps1 until the second script needed the same
# guessing, which is one copy too many for something this likely to be wrong.
#
# Two lessons are encoded below, both from a machine where the obvious answer
# failed:
#
#   * The SDK is not under Program Files here -- Visual Studio took it onto
#     another drive. Read the install location out of the registry instead of
#     guessing, because guessing finds nothing and then insists the SDK is
#     missing when it is installed and working.
#   * The tools come in two bitness flavours per version directory, and taking
#     the first hit can hand back a 32-bit makeappx. Filter for x64 and take
#     the newest version directory, not the first one the walk turns up.

function Find-WindowsKitTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    # PATH first: if someone is running from a developer prompt, that is the
    # version they mean, and it beats a stale SDK the registry still points at.
    $onPath = (Get-Command $Name -ErrorAction SilentlyContinue).Source
    if ($onPath) { return $onPath }

    $roots = @()
    foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots',
                       'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots')) {
        if (-not (Test-Path $key)) { continue }
        $kitsRoot = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).KitsRoot10
        if ($kitsRoot) { $roots += (Join-Path $kitsRoot 'bin') }
    }
    $roots += @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    )
    # The certification kit ships its own copies, and on this machine they are
    # the only ones not on PATH; makepri is not there, so this is a fallback and
    # not a substitute for the registry lookup above.
    $roots += @(
        'E:\Windows Kits\10\App Certification Kit',
        'C:\Program Files (x86)\Windows Kits\10\App Certification Kit'
    )
    $roots = @($roots | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique)

    foreach ($root in $roots) {
        $found = Get-ChildItem -Path $root -Filter $Name -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\(x86|arm|arm64)\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw ("Could not find $Name. It comes with the Windows SDK: either the standalone " +
           "installer, or the 'Windows 11 SDK' component in the Visual Studio Installer. " +
           'Looked in: ' + ($roots -join '; '))
}

# The folder holding vcruntime140.dll and friends, for copying app-local.
# Moved here out of build-store-package.ps1 when build-installer.ps1 needed the
# same answer: both ship to machines that are not required to have the
# redistributable.
function Find-VcRuntimeDirectory {
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
        if ($candidate) { return $candidate }
    }

    throw ('Cannot find the Visual C++ runtime. Run this from a developer command ' +
           'prompt, or pass -CrtDirectory with the folder holding vcruntime140.dll.')
}
