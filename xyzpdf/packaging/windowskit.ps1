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
