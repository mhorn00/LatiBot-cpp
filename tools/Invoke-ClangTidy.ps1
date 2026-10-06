<#
.SYNOPSIS
    Runs clang-tidy over the project's own sources.

.DESCRIPTION
        pwsh tools/Invoke-ClangTidy.ps1                     # src/
        pwsh tools/Invoke-ClangTidy.ps1 -IncludeTests       # src/ and tests/
        pwsh tools/Invoke-ClangTidy.ps1 -Path src/core/bot.cpp
        pwsh tools/Invoke-ClangTidy.ps1 -Fix                # apply fixes

    clang-tidy reads compile_commands.json, which the default preset's
    Ninja Multi-Config build writes, with every file once per configuration.
    This script configures that build, then hands clang-tidy the Debug
    entries only, so no file is analysed twice. It deliberately does not run
    the Conan install it depends on: that can build dependencies from source,
    and is not something a task should start behind your back. If the
    toolchain is missing you get the commands to run.

    Which headers are analysed, and which checks run, come from .clang-tidy
    rather than from here, and tests/.clang-tidy for the tests. DPP's headers
    and Conan's are not ours to fix. Only files the compile database knows are
    analysed; the fuzz targets, which only the fuzz preset builds, are skipped
    and named.
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
$build = Join-Path $repo 'build\build'
$toolchain = Join-Path $repo 'build\conan\conan_toolchain.cmake'
$conanbuild = Join-Path $repo 'build\conan\conanbuild.bat'

if (-not (Test-Path $toolchain)) {
    throw @"
The dependencies are not installed. Run these once (the first time builds them from source, so allow several minutes):

  conan export conan/dpp
  conan install . --build=missing -s build_type=Debug -s compiler.cppstd=20
  conan install . --build=missing -s build_type=Release -s compiler.cppstd=20
"@
}

# Configuring needs the Visual Studio environment to find the compiler, which
# conanbuild.bat loads; clang-tidy itself does not, because
# compile_commands.json carries absolute paths.
Push-Location $repo
try {
    Write-Host 'Configuring the default build...'
    cmd /c "`"$conanbuild`" && cmake --preset default" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset default failed with exit code $LASTEXITCODE"
    }

    # The Debug entries, in a folder of their own for clang-tidy's -p. Ninja
    # Multi-Config marks each command with its configuration.
    $tidyDatabase = Join-Path $build 'tidy'
    New-Item -ItemType Directory -Force $tidyDatabase | Out-Null
    $all = Get-Content (Join-Path $build 'compile_commands.json') -Raw | ConvertFrom-Json
    $debugOnly = @($all | Where-Object { $_.command -match 'CMAKE_INTDIR=\\"Debug\\"' })
    if ($debugOnly.Count -eq 0) {
        throw "compile_commands.json in $build has no Debug entries; is it a Ninja Multi-Config build?"
    }
    ConvertTo-Json -InputObject $debugOnly -Depth 4 | Set-Content (Join-Path $tidyDatabase 'compile_commands.json') -Encoding utf8

    if ($Path) {
        $sources = @($Path | ForEach-Object { (Resolve-Path $_).Path })
    } else {
        $roots = if ($IncludeTests) { @('src', 'tests') } else { @('src') }
        $sources = @(Get-OurSources -Roots $roots | Where-Object { $_ -like '*.cpp' })
    }

    # Only what the compile database can build. The fuzz targets, for one,
    # are only configured by the fuzz preset, and without their compile
    # command clang-tidy guesses at the flags and fails to compile them.
    $database = $debugOnly
    $known = @{}
    foreach ($entry in $database) {
        $known[[System.IO.Path]::GetFullPath($entry.file).ToLowerInvariant()] = $true
    }
    $skipped = @($sources | Where-Object { -not $known.ContainsKey($_.ToLowerInvariant()) })
    $sources = @($sources | Where-Object { $known.ContainsKey($_.ToLowerInvariant()) })
    if ($skipped.Count -gt 0) {
        $names = ($skipped | ForEach-Object { [System.IO.Path]::GetRelativePath($repo, $_) }) -join ', '
        Write-Host "Skipping $($skipped.Count) file(s) the default build does not compile: $names"
    }

    if ($sources.Count -eq 0) {
        throw 'No sources to analyse.'
    }

    $clangTidy = Get-LlvmTool -Name 'clang-tidy.exe'

    # No --header-filter here on purpose: .clang-tidy sets HeaderFilterRegex,
    # and passing the flag would override the committed configuration with
    # something only this script knows about.
    $arguments = @('-p', $tidyDatabase)
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
