<#
.SYNOPSIS
    Formats every source file we own with .clang-format.

.DESCRIPTION
    VS Code formats on save, so this is for the cases it misses: a change to
    .clang-format itself, files edited outside the editor, and checking a
    whole branch.

        pwsh tools/Invoke-ClangFormat.ps1           # rewrite files in place
        pwsh tools/Invoke-ClangFormat.ps1 -Check    # report, change nothing

    -Check exits non-zero and names each file that is not formatted, which is
    what a CI step would use.
#>
[CmdletBinding()]
param(
    [switch] $Check,
    [string[]] $Roots = @('src', 'tests')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'Common.ps1')

$clangFormat = Get-LlvmTool -Name 'clang-format.exe' -MinimumMajor 0
$sources = @(Get-OurSources -Roots $Roots)

if ($sources.Count -eq 0) {
    throw "No sources found under: $($Roots -join ', ')"
}

if (-not $Check) {
    & $clangFormat -i @sources
    if ($LASTEXITCODE -ne 0) {
        throw "clang-format failed with exit code $LASTEXITCODE"
    }
    Write-Host "Formatted $($sources.Count) files."
    return
}

# --dry-run reports one diagnostic per difference on stderr and says nothing
# about which files they add up to, so the list comes from parsing. -Werror
# raises those diagnostics from warning to error, hence both severities.
$output = & $clangFormat --dry-run -Werror @sources 2>&1
$unformatted = @(
    $output |
        ForEach-Object { [string] $_ } |
        Select-String -Pattern '^(.+?):\d+:\d+: (?:warning|error): code should be clang-formatted' |
        ForEach-Object { $_.Matches[0].Groups[1].Value } |
        Sort-Object -Unique
)

if ($unformatted.Count -eq 0) {
    Write-Host "All $($sources.Count) files are formatted."
    return
}

$repo = Get-RepoRoot
Write-Host "Not formatted ($($unformatted.Count) of $($sources.Count)):"
foreach ($file in $unformatted) {
    Write-Host "  $($file.Replace("$repo\", ''))"
}
Write-Host 'Run: pwsh tools/Invoke-ClangFormat.ps1'
exit 1
