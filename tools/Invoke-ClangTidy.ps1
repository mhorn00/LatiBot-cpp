<#
.SYNOPSIS
    Runs clang-tidy over the project's own sources.

.DESCRIPTION
        pwsh tools/Invoke-ClangTidy.ps1                     # src/
        pwsh tools/Invoke-ClangTidy.ps1 -IncludeTests       # src/ and tests/
        pwsh tools/Invoke-ClangTidy.ps1 -Path src/core/bot.cpp
        pwsh tools/Invoke-ClangTidy.ps1 -Fix                # apply fixes

    clang-tidy needs compile_commands.json, which the Visual Studio generator
    cannot produce, so the `ninja-tidy` preset exists to make one. This script
    reconfigures that preset when it is stale, but deliberately does not run
    the Conan install it depends on: that builds dependencies from source and
    is not something a task should start behind your back. If the toolchain is
    missing you get the one command to run.

    Which headers are analysed, and which checks run, come from .clang-tidy
    rather than from here. DPP's headers and Conan's are not ours to fix, and
    one of them (DPP's cache.h) crashes a check outright.
#>
[CmdletBinding()]
param(
    [string[]] $Path,
    [switch] $IncludeTests,
    [switch] $Fix
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'Common.ps1')

$repo = Get-RepoRoot
$tidyBuild = Join-Path $repo 'build-tidy'
$toolchain = Join-Path $repo 'build\Debug\generators\conan_toolchain.cmake'
$conanbuild = Join-Path $repo 'build\Debug\generators\conanbuild.bat'

if (-not (Test-Path $toolchain)) {
    throw @"
The ninja-tidy preset needs a Ninja-flavoured dependency install, which is not present.
Run this once (it builds the dependencies from source, so allow several minutes):

  conan install . --build=missing -s build_type=Debug -c tools.cmake.cmaketoolchain:generator=Ninja
"@
}

# Configuring needs the MSVC environment to find the compiler; clang-tidy
# itself does not, because compile_commands.json carries absolute paths.
Push-Location $repo
try {
    Write-Host 'Configuring the ninja-tidy build...'
    cmd /c "`"$conanbuild`" && cmake --preset ninja-tidy" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset ninja-tidy failed with exit code $LASTEXITCODE"
    }

    if ($Path) {
        $sources = @($Path | ForEach-Object { (Resolve-Path $_).Path })
    } else {
        $roots = if ($IncludeTests) { @('src', 'tests') } else { @('src') }
        $sources = @(Get-OurSources -Roots $roots | Where-Object { $_ -like '*.cpp' })
    }

    if ($sources.Count -eq 0) {
        throw 'No sources to analyse.'
    }

    $clangTidy = Get-LlvmTool -Name 'clang-tidy.exe'

    # No --header-filter here on purpose: .clang-tidy sets HeaderFilterRegex,
    # and passing the flag would override the committed configuration with
    # something only this script knows about.
    $arguments = @('-p', $tidyBuild)
    if ($Fix) {
        $arguments += '--fix'
    }

    Write-Host "Running clang-tidy over $($sources.Count) files..."

    # Teed rather than captured, so findings appear as they are produced and
    # can still be counted afterwards. clang-tidy's exit code only reports
    # compiler errors, not findings, so counting is the only way to know.
    & $clangTidy @arguments @sources 2>&1 | Tee-Object -Variable output
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

$findings = @(
    $output |
        ForEach-Object { [string] $_ } |
        Select-String -Pattern '^.+?:\d+:\d+: (warning|error): '
).Count

if ($code -ne 0) {
    Write-Host "clang-tidy failed to compile something (exit code $code)."
    exit $code
}

if ($findings -gt 0) {
    Write-Host "clang-tidy reported $findings findings."
    exit 1
}

Write-Host 'clang-tidy is clean.'
