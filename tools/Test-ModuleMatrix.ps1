<#
.SYNOPSIS
    Builds and tests the bot with every module, with none, and with each one
    left out in turn.

.DESCRIPTION
        pwsh tools/Test-ModuleMatrix.ps1                # every combination below
        pwsh tools/Test-ModuleMatrix.ps1 -Only llm      # just "everything but llm"

    A module that compiles only because another happens to be built, or a
    core change that breaks a module nobody built, shows up here and nowhere
    else (docs/modules/Module_Plan_Final.md §10, D10). Run it before changing
    src/core/include, a capability, or a module others require.

    Every combination is built in one folder, build/build-matrix, configured
    afresh each time: the core and the modules that do not change are reused,
    so each later step costs a relink and the modules that did change.

    Leaving a module out leaves out the modules that require it too: links
    takes linkstats with it, and voice is built only for dectalk or music.
    Debug only; AddressSanitizer and Release are the presets' jobs.

    It deliberately does not run the Conan install it depends on, as
    tools/Invoke-ClangTidy.ps1 does not.
#>
[CmdletBinding()]
param(
    # Run only "every module but this one".
    [string] $Only
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'Common.ps1')

$repo = Get-RepoRoot
$build = Join-Path $repo 'build\build-matrix'
$conanbuild = Join-Path $repo 'build\conan\conanbuild.bat'
if (-not (Test-Path $conanbuild)) {
    throw 'The dependencies are not installed: run the conan install commands in README.md, step 4, first.'
}

# Each module and the modules that require it, which go when it does
# (src/modules/CMakeLists.txt). Voice has no switch.
$modules = [ordered]@{
    'dectalk'   = @()
    'music'     = @()
    'llm'       = @()
    'triggers'  = @()
    'nicknames' = @()
    'midnight'  = @()
    'links'     = @('linkstats')
    'linkstats' = @()
}

function Get-Switches([string[]] $off) {
    foreach ($name in $modules.Keys) {
        $value = if ($off -contains $name) { 'OFF' } else { 'ON' }
        "-DLATIBOT_WITH_$($name.ToUpperInvariant())=$value"
    }
}

$combinations = [System.Collections.Generic.List[object]]::new()
if (-not $Only) {
    $combinations.Add([pscustomobject]@{ Name = 'every module'; Off = @() })
    $combinations.Add([pscustomobject]@{ Name = 'the core alone'; Off = @($modules.Keys) })
}
foreach ($name in $modules.Keys) {
    if ($Only -and $name -ne $Only) { continue }
    $off = @($name) + $modules[$name]
    $combinations.Add([pscustomobject]@{ Name = "without $($off -join ' and ')"; Off = $off })
}
if ($combinations.Count -eq 0) { throw "No module is called '$Only'. Modules: $($modules.Keys -join ', ')" }

$results = [System.Collections.Generic.List[object]]::new()
Push-Location $repo
try {
    foreach ($combination in $combinations) {
        Write-Host "== $($combination.Name)" -ForegroundColor Cyan
        $switches = (Get-Switches $combination.Off) -join ' '
        $steps = "cmake --preset default -B `"$build`" $switches > nul" +
                 " && cmake --build `"$build`" --config Debug" +
                 " && ctest --test-dir `"$build`" -C Debug --output-on-failure -E `"\[live\]`""
        & cmd.exe /d /c "`"$conanbuild`" > nul 2>&1 && $steps" 2>&1 |
            Select-String -CaseSensitive -Pattern 'error C|error LNK|FAILED:|CMake Error|tests passed|\*\*\*(Failed|Exception|Timeout)|with expansion' |
            ForEach-Object { Write-Host "   $($_.Line.Trim())" }
        $results.Add([pscustomobject]@{ Build = $combination.Name; Passed = $LASTEXITCODE -eq 0 })
    }
} finally {
    Pop-Location
}

Write-Host ''
$results | Format-Table -AutoSize | Out-String | Write-Host
$failed = @($results | Where-Object { -not $_.Passed })
if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) of $($results.Count) builds failed." -ForegroundColor Red
    exit 1
}
Write-Host "All $($results.Count) builds passed." -ForegroundColor Green
