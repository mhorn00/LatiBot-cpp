<#
.SYNOPSIS
    Sets up everything this repo needs to build, skipping whatever is already
    there.

.DESCRIPTION
    Does what the README's setup steps describe, in order, checking each thing
    before doing it:

      1. Git, CMake, PowerShell 7 and Conan, installed with winget if missing.
      2. The Visual Studio 2026 C++ Build Tools with every component this repo
         uses, installed if missing, or modified to add what an existing
         install lacks.
      3. The git submodules (DPP and DECtalk).
      4. A Conan profile, detected if there is none, and pointed at an
         installed compiler if it names one that is not installed.
      5. DPP's recipe, then the dependencies for Debug and Release, into
         build\conan.
      6. `cmake --preset default`, and a `.env` copied from `.env.example`.

    Run it from Windows PowerShell or PowerShell 7. It works in either, since
    a fresh machine only has the first, and the execution policy there blocks
    scripts unless told otherwise:

        powershell -ExecutionPolicy Bypass -File tools\DevEnvSetup.ps1
        powershell -ExecutionPolicy Bypass -File tools\DevEnvSetup.ps1 -CheckOnly
        powershell -ExecutionPolicy Bypass -File tools\DevEnvSetup.ps1 -Build

    Installing asks for administrator rights through the usual prompt, once
    per installer; nothing else needs them. Run it again at any time: a
    machine that is already set up costs a few seconds.

.PARAMETER CheckOnly
    Reports what is missing and changes nothing. Exits 1 when something is.

.PARAMETER Build
    Also builds Debug and runs the tests, which is the proof that it all
    works. The first build takes about ten minutes.

.PARAMETER ResetBuild
    Deletes build\ when it was configured with a different Visual Studio than
    the Conan profile now names, instead of stopping to ask. Only build output
    is lost.
#>
[CmdletBinding()]
param(
    [switch] $CheckOnly,
    [switch] $Build,
    [switch] $ResetBuild
)

# Written for Windows PowerShell 5.1 as well as PowerShell 7, since installing
# 7 is one of the things this does: no ternaries, no ??, no &&.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# What this repo is built with (README "Prerequisites" and step 2). CI uses
# the same compiler, VS 2026's v145 toolset, which Conan calls version 195.
$minimumCMake = [version] '3.21'
$vsVersionRange = '[18.0,19.0)'
$vsComponents = @(
    'Microsoft.VisualStudio.Workload.VCTools',
    # VS 2026's workload no longer pulls the x64 compiler in by itself.
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    'Microsoft.VisualStudio.Component.VC.CMake.Project',
    'Microsoft.VisualStudio.Component.Windows11SDK.26100',
    # AddressSanitizer, for the asan and fuzz presets.
    'Microsoft.VisualStudio.Component.VC.ASAN',
    # clang-tidy and clang-format.
    'Microsoft.VisualStudio.Component.VC.Llvm.Clang',
    'Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset'
)

# Conan's compiler.version for each Visual Studio, and the toolset it builds with.
$conanVersions = @{
    '195' = @{ Range = '[18.0,19.0)'; Folder = '18'; Name = 'Visual Studio 2026' }
    '194' = @{ Range = '[17.10,18.0)'; Folder = '2022'; Name = 'Visual Studio 2022' }
    '193' = @{ Range = '[17.0,17.10)'; Folder = '2022'; Name = 'Visual Studio 2022, before 17.10' }
}

$script:missing = New-Object System.Collections.Generic.List[string]

# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------

function Write-Section([string] $Title) {
    Write-Host ''
    Write-Host "== $Title" -ForegroundColor Cyan
}

function Write-Ok([string] $Text) { Write-Host "  ok    $Text" -ForegroundColor Green }
function Write-Doing([string] $Text) { Write-Host "  ..    $Text" -ForegroundColor Yellow }
function Write-Note([string] $Text) { Write-Host "  note  $Text" -ForegroundColor DarkYellow }

# In -CheckOnly, records something that needs doing; otherwise says it is
# about to be done. Returns whether to go ahead.
function Request-Change([string] $What) {
    if ($CheckOnly) {
        $script:missing.Add($What)
        Write-Host "  todo  $What" -ForegroundColor Magenta
        return $false
    }
    Write-Doing $What
    return $true
}

# --------------------------------------------------------------------------
# Running things
# --------------------------------------------------------------------------

# Runs a native command with its output going straight to the console, and
# throws when it fails. Windows PowerShell 5.1 turns anything a native
# command writes to stderr into an error when $ErrorActionPreference is Stop,
# and Conan writes its progress there, so that is relaxed for the call.
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)] [string] $Command,
        [string[]] $Arguments = @(),
        [int[]] $AllowedExitCodes = @(0)
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # To the console rather than the function's output, so a long install
        # shows its progress.
        & $Command @Arguments | Out-Host
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($AllowedExitCodes -notcontains $code) {
        throw "$Command $($Arguments -join ' ') failed with exit code $code"
    }
}

# A native command's output as text, stderr included, and its exit code.
function Get-NativeOutput {
    param(
        [Parameter(Mandatory = $true)] [string] $Command,
        [string[]] $Arguments = @()
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $text = (& $Command @Arguments 2>&1 | ForEach-Object { "$_" }) -join "`n"
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    [pscustomobject] @{ Text = $text; ExitCode = $code }
}

function Test-Command([string] $Name) {
    [bool] (Get-Command $Name -ErrorAction SilentlyContinue)
}

# An installer adds itself to PATH in the registry, which this session does
# not see until it reads it again.
function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

# winget's "no applicable update": the package is already installed and
# current, which a stale PATH can make look like a missing tool.
$wingetNothingToDo = -1978335189

function Install-WithWinget([string] $Verb, [string] $Id, [string] $Name, [string[]] $Extra = @()) {
    if (-not (Test-Command 'winget')) {
        throw ("winget is not available, so $Name cannot be installed. Install 'App Installer' from the " +
               'Microsoft Store, or install it yourself, then run this again.')
    }
    $arguments = @($Verb, '--id', $Id, '--exact', '--source', 'winget',
                   '--accept-package-agreements', '--accept-source-agreements') + $Extra
    Invoke-Native winget $arguments -AllowedExitCodes @(0, $wingetNothingToDo)
    Update-SessionPath
}

# --------------------------------------------------------------------------
# 1. Command-line tools
# --------------------------------------------------------------------------

function Get-ToolVersion([string] $Command, [string] $Pattern) {
    $output = Get-NativeOutput $Command @('--version')
    $match = [regex]::Match($output.Text, $Pattern)
    if ($match.Success) { return [version] $match.Groups[1].Value }
    $null
}

function Initialize-Tool {
    param(
        [string] $Name,
        [string] $Command,
        [string] $WingetId,
        [string] $VersionPattern,
        [version] $Minimum,
        [string] $Why
    )

    if (Test-Command $Command) {
        $version = Get-ToolVersion $Command $VersionPattern
        if (-not $Minimum -or ($version -and $version -ge $Minimum)) {
            Write-Ok "$Name $version"
            return
        }
        if (-not (Request-Change "upgrade $Name $version to $Minimum or newer ($Why)")) { return }
        Install-WithWinget 'upgrade' $WingetId $Name
    } else {
        if (-not (Request-Change "install $Name ($Why)")) { return }
        Install-WithWinget 'install' $WingetId $Name
    }

    if (-not (Test-Command $Command)) {
        throw "$Name was installed but '$Command' is still not on PATH. Open a new terminal and run this again."
    }
    Write-Ok "$Name $(Get-ToolVersion $Command $VersionPattern)"
}

function Initialize-Tools {
    Write-Section 'Command-line tools'

    if (-not [Environment]::Is64BitOperatingSystem) { throw 'This project builds for 64-bit Windows only.' }
    # 1903 is build 18362, the first with the time zone data C++20's <chrono> uses.
    if ([Environment]::OSVersion.Version.Build -lt 18362) {
        throw 'Windows 10 version 1903 or newer is needed, for the time zones in C++20 <chrono>.'
    }

    Initialize-Tool -Name 'Git' -Command 'git' -WingetId 'Git.Git' `
        -VersionPattern 'git version (\d+\.\d+\.\d+)' -Why 'for the submodules, and DPP''s build'
    Initialize-Tool -Name 'CMake' -Command 'cmake' -WingetId 'Kitware.CMake' `
        -VersionPattern 'cmake version (\d+\.\d+\.\d+)' -Minimum $minimumCMake -Why 'the build'
    Initialize-Tool -Name 'PowerShell 7' -Command 'pwsh' -WingetId 'Microsoft.PowerShell' `
        -VersionPattern '(\d+\.\d+\.\d+)' -Why 'the scripts in tools\ and the VS Code tasks'
    Initialize-Tool -Name 'Conan' -Command 'conan' -WingetId 'Conan.Conan' `
        -VersionPattern 'Conan version (\d+\.\d+\.\d+)' -Minimum ([version] '2.0') -Why 'the dependencies'
}

# --------------------------------------------------------------------------
# 2. Visual Studio Build Tools
# --------------------------------------------------------------------------

function Get-VsWherePath {
    Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
}

# Installation paths of Visual Studio in a version range that have every
# listed component, newest first. Any edition counts, the IDE included.
function Find-VisualStudio([string] $Range, [string[]] $Requires = @()) {
    $vswhere = Get-VsWherePath
    if (-not (Test-Path $vswhere)) { return @() }
    $arguments = @('-products', '*', '-version', $Range, '-sort', '-property', 'installationPath')
    if ($Requires.Count -gt 0) { $arguments += @('-requires') + $Requires }
    $output = Get-NativeOutput $vswhere $arguments
    @($output.Text -split "`n" | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
}

function Initialize-BuildTools {
    Write-Section 'Visual Studio 2026 C++ Build Tools'

    $installs = @(Find-VisualStudio $vsVersionRange)
    if ($installs.Count -eq 0) {
        if (-not (Request-Change 'install the Visual Studio 2026 Build Tools, with the C++ components (several GB)')) { return }

        # The README's command. Left to its defaults the bootstrapper installs
        # no C++ at all, so the components go to it through --override.
        $adds = ($vsComponents | ForEach-Object { "--add $_" }) -join ' '
        Write-Note 'the installer asks for administrator rights; its window shows progress'
        Install-WithWinget 'install' 'Microsoft.VisualStudio.BuildTools' 'the Visual Studio Build Tools' `
            @('--override', "--wait --passive --norestart $adds")
        $installs = @(Find-VisualStudio $vsVersionRange)
        if ($installs.Count -eq 0) {
            throw 'The Build Tools install finished, but vswhere does not list a Visual Studio 2026. Run this again.'
        }
    }

    $install = $installs[0]
    $absent = @($vsComponents | Where-Object { (Find-VisualStudio $vsVersionRange @($_)) -notcontains $install })
    if ($absent.Count -eq 0) {
        Write-Ok "$install, with every component"
        return
    }

    $names = ($absent | ForEach-Object { $_ -replace '^Microsoft\.VisualStudio\.(Component|Workload)\.', '' }) -join ', '
    if (-not (Request-Change "add to $install`: $names")) { return }

    # The installer needs administrator rights, and --passive will not ask
    # for them itself (it exits with 5007), so it is started elevated.
    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    $arguments = @('modify', '--installPath', "`"$install`"", '--passive', '--norestart', '--wait') +
                 ($absent | ForEach-Object { @('--add', $_) })
    Write-Note 'the Visual Studio Installer asks for administrator rights; its window shows progress'
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    # 3010 is success with a restart pending.
    if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
        throw "The Visual Studio Installer failed with exit code $($process.ExitCode)."
    }
    if ($process.ExitCode -eq 3010) { Write-Note 'Windows wants a restart to finish the Build Tools install' }

    $still = @($vsComponents | Where-Object { (Find-VisualStudio $vsVersionRange @($_)) -notcontains $install })
    if ($still.Count -gt 0) { throw "Still missing after the install: $($still -join ', ')" }
    Write-Ok "$install, with every component"
}

# --------------------------------------------------------------------------
# 3. Submodules
# --------------------------------------------------------------------------

function Initialize-Submodules {
    Write-Section 'Submodules'

    if (-not (Test-Path (Join-Path $repo '.git'))) {
        throw "$repo is not a git clone. Clone it with: git clone --recursive https://github.com/mhorn00/LatiBot-cpp.git"
    }
    if (-not (Test-Command 'git')) { Write-Note 'skipped until Git is installed'; return }

    # A leading '-' is a submodule that was never checked out. Only ours are
    # asked about: DPP's own, a stylesheet for its documentation, is not
    # needed to build, although a first checkout brings it along.
    $status = Get-NativeOutput git @('-C', $repo, 'submodule', 'status')
    $absent = @($status.Text -split "`n" | Where-Object { $_ -match '^-' })
    if ($absent.Count -eq 0) {
        Write-Ok 'DPP and DECtalk are checked out'
        return
    }
    if (-not (Request-Change "check out $($absent.Count) submodule(s)")) { return }
    Invoke-Native git @('-C', $repo, 'submodule', 'update', '--init', '--recursive')
    Write-Ok 'DPP and DECtalk are checked out'
}

# --------------------------------------------------------------------------
# 4. Conan profile
# --------------------------------------------------------------------------

# The default profile's path, or nothing when there is none.
function Get-ConanProfilePath {
    $output = Get-NativeOutput conan @('profile', 'path', 'default')
    if ($output.ExitCode -ne 0) { return $null }
    $path = ($output.Text -split "`n" | Select-Object -Last 1).Trim()
    if (Test-Path $path) { return $path }
    $null
}

function Get-ProfileSetting([string] $Path, [string] $Name) {
    $line = Get-Content $Path | Where-Object { $_ -match "^\s*$([regex]::Escape($Name))\s*=" } | Select-Object -First 1
    if ($line) { return ($line -split '=', 2)[1].Trim() }
    $null
}

# The compiler.version the profile builds with, once it is right.
function Initialize-ConanProfile {
    Write-Section 'Conan profile'
    if (-not (Test-Command 'conan')) { Write-Note 'skipped until Conan is installed'; return $null }

    $path = Get-ConanProfilePath
    if (-not $path) {
        if (-not (Request-Change 'create the default Conan profile (conan profile detect)')) { return '195' }
        Invoke-Native conan @('profile', 'detect')
        $path = Get-ConanProfilePath
    }

    $compiler = Get-ProfileSetting $path 'compiler'
    $version = Get-ProfileSetting $path 'compiler.version'
    if ($compiler -ne 'msvc') {
        throw "The Conan profile at $path uses compiler=$compiler. This project needs msvc; see README step 3."
    }

    # A profile that names a compiler which is installed is left alone, even
    # when it is not the one CI uses: it may be somebody's choice, and it
    # works. One that names a compiler which is not installed cannot work.
    $known = $conanVersions[$version]
    if ($known -and @(Find-VisualStudio $known.Range @('Microsoft.VisualStudio.Component.VC.Tools.x86.x64')).Count -gt 0) {
        Write-Ok "$path builds with $($known.Name) (compiler.version=$version)"
        if ($version -ne '195') {
            Write-Note 'CI builds with Visual Studio 2026 (195); README step 3 says how to switch'
        }
        return $version
    }

    $what = if ($known) { "$($known.Name), which is not installed" } else { 'a compiler this script does not know' }
    if (-not (Request-Change "point the Conan profile at Visual Studio 2026: it names compiler.version=$version, $what")) {
        return '195'
    }
    Copy-Item $path "$path.bak" -Force
    (Get-Content $path) -replace '^\s*compiler\.version\s*=.*$', 'compiler.version=195' | Set-Content $path -Encoding ASCII
    Write-Ok "$path now builds with Visual Studio 2026; the old one is at $path.bak"
    '195'
}

# --------------------------------------------------------------------------
# 5. Dependencies
# --------------------------------------------------------------------------

# A build folder remembers the compiler it was first configured with: with
# Ninja, the one the Visual Studio environment pointed at. Found before the
# install that would change it, so the choice is made while nothing has been
# touched.
function Confirm-BuildToolset([string] $ProfileVersion) {
    $cache = Join-Path $repo 'build\build\CMakeCache.txt'
    if (-not $ProfileVersion -or -not (Test-Path $cache)) { return }

    $wanted = $conanVersions[$ProfileVersion]
    $line = Select-String -Path $cache -Pattern '^CMAKE_CXX_COMPILER:[A-Z]+=(.*)$' | Select-Object -First 1
    if (-not $line) { return }
    $had = $line.Matches[0].Groups[1].Value.Trim()
    if ($had -match "Microsoft Visual Studio[\\/]$([regex]::Escape($wanted.Folder))[\\/]") { return }

    $build = Join-Path $repo 'build'
    if (-not $ResetBuild -and -not $CheckOnly) {
        throw ("build\build was configured with the compiler at $had, and the Conan profile now builds with $($wanted.Name). " +
               'Run this again with -ResetBuild to delete build\ (only build output), or delete it yourself.')
    }
    if (-not (Request-Change "delete build\, configured with $had rather than $($wanted.Name)")) { return }
    Remove-Item -Recurse -Force -LiteralPath $build
}

function Invoke-ConanInstall([string] $BuildType, [string[]] $Extra = @()) {
    $arguments = @('install', $repo, '--build=missing', '-s', "build_type=$BuildType", '-s', 'compiler.cppstd=20',
                   '--lockfile-partial') + $Extra
    Invoke-Native conan $arguments
}

function Initialize-Dependencies([string] $ProfileVersion) {
    Write-Section 'Dependencies'
    if (-not (Test-Command 'conan') -or -not $ProfileVersion) { Write-Note 'skipped until Conan is set up'; return }

    Confirm-BuildToolset $ProfileVersion

    # Conan skips whatever is already built and cached, so running these on
    # a machine that has everything takes seconds. The first time, each builds
    # every dependency from source: about ten minutes.
    $toolchain = Join-Path $repo 'build\conan\conan_toolchain.cmake'
    if ($CheckOnly) {
        if (-not (Test-Path $toolchain)) { Request-Change 'install the dependencies for Debug and Release' | Out-Null }
        if (Test-Path $toolchain) { Write-Ok 'installed (a real run also refreshes them)' }
        return
    }

    # DPP is a package built from the submodule (conan/dpp), which Conan
    # must know of before the install can ask for it.
    Write-Doing 'conan export conan/dpp'
    Invoke-Native conan @('export', (Join-Path $repo 'conan\dpp'))

    Write-Doing 'conan install, Release and Debug (seconds when cached, ten minutes each when not)'
    Invoke-ConanInstall 'Release'
    Invoke-ConanInstall 'Debug'
    Write-Ok 'installed into build\conan'
}

# --------------------------------------------------------------------------
# 6. Configure, .env, and optionally build
# --------------------------------------------------------------------------

function Repair-UserPresets {
    # Conan used to write CMakeUserPresets.json, including its own presets from
    # build\generators. It no longer does, and a leftover one breaks every
    # preset once that folder goes.
    $user = Join-Path $repo 'CMakeUserPresets.json'
    if (-not (Test-Path $user)) { return }
    if ((Get-Content -LiteralPath $user -Raw) -notmatch 'generators') { return }
    if (-not (Request-Change "remove the CMakeUserPresets.json Conan wrote, which Conan no longer uses")) { return }
    Remove-Item -LiteralPath $user -Force
}

# The Visual Studio environment Ninja needs, as Conan wrote it for the profile's
# compiler, around one command line.
function Invoke-InBuildEnvironment([string] $CommandLine) {
    $conanbuild = Join-Path $repo 'build\conan\conanbuild.bat'
    return Get-NativeOutput cmd @('/d', '/c', "`"$conanbuild`" && $CommandLine")
}

function Initialize-Configure {
    Write-Section 'Configure'
    if (-not (Test-Command 'cmake')) { Write-Note 'skipped until CMake is installed'; return }
    if (-not (Test-Path (Join-Path $repo 'build\conan\conan_toolchain.cmake'))) {
        Write-Note 'skipped until the dependencies are installed'
        return
    }

    Push-Location $repo
    try {
        Repair-UserPresets
        if ($CheckOnly) {
            if (Test-Path (Join-Path $repo 'build\build\CMakeCache.txt')) { Write-Ok 'build\build is configured' }
            else { Request-Change 'cmake --preset default' | Out-Null }
            return
        }
        # Kept quiet unless it fails: a setup script's job is to say whether it
        # worked, and a configure's pages of output bury that.
        Write-Doing 'cmake --preset default'
        $configure = Invoke-InBuildEnvironment 'cmake --preset default'
        if ($configure.ExitCode -ne 0) {
            Write-Host $configure.Text
            throw "cmake --preset default failed with exit code $($configure.ExitCode)"
        }
        Write-Ok 'build\build is configured'
    } finally {
        Pop-Location
    }
}

function Initialize-DotEnv {
    Write-Section 'Secrets'
    # Only whether it exists: .env holds a live token and is never read here.
    $dotenv = Join-Path $repo '.env'
    if (Test-Path $dotenv) {
        Write-Ok '.env exists'
        return
    }
    if (-not (Request-Change 'create .env from .env.example')) { return }
    Copy-Item (Join-Path $repo '.env.example') $dotenv
    Write-Ok ".env created: put the bot's token after DISCORD_BOT_TOKEN= in it"
}

function Invoke-BuildAndTest {
    if (-not $Build -or $CheckOnly) { return }
    Write-Section 'Build and test (Debug)'
    Push-Location $repo
    try {
        Write-Doing 'cmake --workflow --preset debug: configure, build, test'
        $workflow = Invoke-InBuildEnvironment 'cmake --workflow --preset debug'
        if ($workflow.ExitCode -ne 0) {
            Write-Host $workflow.Text
            throw "cmake --workflow --preset debug failed with exit code $($workflow.ExitCode)"
        }
        Write-Ok 'built, and every test passed'
    } finally {
        Pop-Location
    }
}

# --------------------------------------------------------------------------

Initialize-Tools
Initialize-BuildTools
Initialize-Submodules
$profileVersion = Initialize-ConanProfile
Initialize-Dependencies $profileVersion
Initialize-Configure
Initialize-DotEnv
Invoke-BuildAndTest

Write-Host ''
if ($CheckOnly) {
    if ($script:missing.Count -gt 0) {
        Write-Host "$($script:missing.Count) thing(s) to set up; run this again without -CheckOnly to do them." -ForegroundColor Magenta
        exit 1
    }
    Write-Host 'Everything is set up.' -ForegroundColor Green
    exit 0
}
Write-Host 'Everything is set up.' -ForegroundColor Green
if (-not $Build) { Write-Host 'Build and test with: cmake --workflow --preset debug, in a Developer PowerShell (README.md, step 5)' }
