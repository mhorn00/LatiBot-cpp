<#
.SYNOPSIS
    Regenerates docs/testing/Test_Catalog.md from the test sources.

.DESCRIPTION
    Parses every TEST_CASE in tests/ and groups them by their component tag,
    so the catalog cannot drift from the code. Run it after adding or
    retagging tests:

        pwsh tools/Update-TestCatalog.ps1

    Fails when a test carries no known component tag, which is how a
    mistagged test gets noticed.
#>
[CmdletBinding()]
param(
    [string] $TestRoot = (Join-Path $PSScriptRoot '..' 'tests'),
    [string] $OutputPath = (Join-Path $PSScriptRoot '..' 'docs' 'testing' 'Test_Catalog.md')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Component tags: exactly one per test, and the axis the catalog is grouped by.
$components = [ordered]@{
    'db'       = 'Database (`src/core/db`)'
    'config'   = 'Configuration (`src/core/config`)'
    'commands' = 'Command framework (`src/core/commands`)'
    'discord'  = 'Discord plumbing (`src/core/discord`)'
    'ports'    = 'Ports and mocks (`src/core/ports`, `tests/mocks`)'
    'log'      = 'Logging (`src/core/util/log`)'
    'util'     = 'Utilities (`src/core/util`, `src/core/version`)'
}

# Trait tags: zero or more per test, for filtering rather than grouping.
$traits = @{
    'coro'    = 'drives a coroutine'
    'threads' = 'runs several threads'
    'fs'      = 'touches real files'
    'golden'  = 'compares against a stored reference'
    'live'    = 'needs a real Discord connection'
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$tests = [System.Collections.Generic.List[object]]::new()
$problems = [System.Collections.Generic.List[string]]::new()

foreach ($file in Get-ChildItem -Path $TestRoot -Recurse -Filter '*.cpp' | Sort-Object FullName) {
    $relative = [IO.Path]::GetRelativePath($repoRoot, $file.FullName) -replace '\\', '/'
    $current = $null
    $lineNumber = 0

    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $lineNumber++

        $match = [regex]::Match($line, 'TEST_CASE\("([^"]+)",\s*"([^"]+)"\)')
        if ($match.Success) {
            $tags = [regex]::Matches($match.Groups[2].Value, '\[([a-z0-9_]+)\]') |
                ForEach-Object { $_.Groups[1].Value }

            $component = @($tags | Where-Object { $components.Contains($_) })
            $traitList = @($tags | Where-Object { $traits.ContainsKey($_) })
            $unknown = @($tags | Where-Object { -not $components.Contains($_) -and -not $traits.ContainsKey($_) })

            if ($component.Count -ne 1) {
                $problems.Add("$relative`:$lineNumber expected exactly one component tag, found [$($tags -join '][')]")
            }
            if ($unknown.Count -gt 0) {
                $problems.Add("$relative`:$lineNumber unknown tag(s): [$($unknown -join '][')]")
            }

            $current = [pscustomobject]@{
                Name      = $match.Groups[1].Value
                Component = if ($component.Count -ge 1) { $component[0] } else { 'unknown' }
                Traits    = $traitList
                File      = $relative
                Line      = $lineNumber
                Sections  = 0
            }
            $tests.Add($current)
            continue
        }

        if ($null -ne $current -and $line -match 'SECTION\("') {
            $current.Sections++
        }
    }
}

if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    throw "$($problems.Count) tagging problem(s); fix them and re-run."
}

$builder = [System.Text.StringBuilder]::new()
$null = $builder.AppendLine('# Test catalog')
$null = $builder.AppendLine()
$null = $builder.AppendLine('**Generated** by ``tools/Update-TestCatalog.ps1``. Do not edit by hand;')
$null = $builder.AppendLine('re-run the script after adding or retagging tests.')
$null = $builder.AppendLine()
$null = $builder.AppendLine('See [README.md](README.md) for the strategy, conventions and tag meanings.')
$null = $builder.AppendLine()

$assertionTotal = ($tests | Measure-Object -Property Sections -Sum).Sum
$null = $builder.AppendLine("$($tests.Count) test cases across $($components.Count) components, " +
    "including $assertionTotal sections.")
$null = $builder.AppendLine()

$null = $builder.AppendLine('| Component | Test cases | Sections |')
$null = $builder.AppendLine('|---|---:|---:|')
foreach ($key in $components.Keys) {
    $owned = @($tests | Where-Object { $_.Component -eq $key })
    $sections = ($owned | Measure-Object -Property Sections -Sum).Sum
    $null = $builder.AppendLine("| [$key](#$key) | $($owned.Count) | $sections |")
}
$null = $builder.AppendLine()

foreach ($key in $components.Keys) {
    $owned = @($tests | Where-Object { $_.Component -eq $key } | Sort-Object File, Line)
    $null = $builder.AppendLine("## $key")
    $null = $builder.AppendLine()
    $null = $builder.AppendLine($components[$key])
    $null = $builder.AppendLine()

    if ($owned.Count -eq 0) {
        $null = $builder.AppendLine('_No tests yet._')
        $null = $builder.AppendLine()
        continue
    }

    $null = $builder.AppendLine('| Test | Traits | Sections | Source |')
    $null = $builder.AppendLine('|---|---|---:|---|')
    foreach ($test in $owned) {
        $traitText = if ($test.Traits.Count -gt 0) { '`' + ($test.Traits -join '`, `') + '`' } else { '' }
        $sectionText = if ($test.Sections -gt 0) { $test.Sections } else { '' }
        $null = $builder.AppendLine("| $($test.Name) | $traitText | $sectionText | [$($test.File):$($test.Line)]($(($test.File -replace '^', '../../'))#L$($test.Line)) |")
    }
    $null = $builder.AppendLine()
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path $outputDirectory)) {
    $null = New-Item -ItemType Directory -Path $outputDirectory -Force
}

Set-Content -LiteralPath $OutputPath -Value $builder.ToString().TrimEnd() -Encoding utf8NoBOM
Write-Host "Wrote $($tests.Count) tests to $OutputPath"
