<#
.SYNOPSIS
    Helpers shared by the scripts in this folder.

.DESCRIPTION
    Dot-source it:

        . (Join-Path $PSScriptRoot 'Common.ps1')
#>

Set-StrictMode -Version Latest

function Get-RepoRoot {
    (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Get-VsWhere {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere. Install the Visual Studio Build Tools (see README step 2)."
    }
    $vswhere
}

<#
.SYNOPSIS
    Finds an LLVM tool shipped with Visual Studio.

.DESCRIPTION
    clang-tidy and clang-format come from the VC.Llvm.Clang component rather
    than PATH, and the path carries the Visual Studio major version, so it is
    discovered through vswhere rather than written down.

    MSVC 14.51's headers reject Clang older than 20 outright ("error STL1000"),
    so a too-old copy is rejected here with that explanation instead of
    surfacing as thousands of errors in the first header.
#>
function Get-LlvmTool {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Name,
        [int] $MinimumMajor = 20
    )

    # Newest install first, so an older side-by-side Visual Studio is only
    # used when it is the only one that has the component.
    $installs = & (Get-VsWhere) -all -products * -sort -property installationPath
    if (-not $installs) {
        throw 'vswhere reported no Visual Studio installations.'
    }

    $tried = @()
    foreach ($install in $installs) {
        $tool = Join-Path $install "VC\Tools\Llvm\x64\bin\$Name"
        if (-not (Test-Path $tool)) {
            continue
        }

        $version = & $tool --version 2>&1 | Select-String -Pattern 'version (\d+)\.' | Select-Object -First 1
        $major = if ($version) { [int] $version.Matches[0].Groups[1].Value } else { 0 }
        if ($major -ge $MinimumMajor) {
            Write-Verbose "using $Name $major from $install"
            return $tool
        }
        $tried += "$tool (Clang $major)"
    }

    if ($tried) {
        throw ("Found $Name, but only Clang $MinimumMajor or newer works with the MSVC headers this " +
               "project builds against:`n  " + ($tried -join "`n  ") +
               "`nAdd Microsoft.VisualStudio.Component.VC.Llvm.Clang to the newest Visual Studio (README step 2).")
    }
    throw "$Name not found in any Visual Studio installation. Add Microsoft.VisualStudio.Component.VC.Llvm.Clang (README step 2)."
}

<#
.SYNOPSIS
    Every C++ source and header we own.

.DESCRIPTION
    third_party/ and the build directories are excluded: they are not ours to
    format, and formatting a dependency would make it impossible to update.
#>
function Get-OurSources {
    [CmdletBinding()]
    param(
        [string[]] $Roots = @('src', 'tests')
    )

    $repo = Get-RepoRoot
    foreach ($root in $Roots) {
        $full = Join-Path $repo $root
        if (-not (Test-Path $full)) {
            continue
        }
        Get-ChildItem -Path $full -Recurse -File -Include '*.cpp', '*.hpp' |
            Select-Object -ExpandProperty FullName
    }
}
