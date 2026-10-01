<#
.SYNOPSIS
Fetches and installs what LatiBot needs on the machine it runs on.

.DESCRIPTION
Puts yt-dlp, ffmpeg and Deno beside LatiBot.exe, with bgutil's PO token
provider and its yt-dlp plugin; installs Firefox and the Microsoft Visual C++
Redistributable when they are missing, which needs an administrator; and
makes a Firefox profile for the bot to sign in to YouTube with.

Run it again to update everything to the latest release. Stop the bot first:
it holds yt-dlp, Deno and the provider open while it runs.

Every download is checked against the SHA-256 its publisher gives, or, for
the two installers, against its Authenticode signature. See
Install-Dependencies.md, beside this script, for what each part is for.

.PARAMETER Destination
The bot's folder, where LatiBot.exe is. Defaults to this script's own folder,
which is where the build puts it.

.PARAMETER SkipFirefox
Neither installs Firefox nor makes the bot's Firefox profile.

.PARAMETER SkipPotProvider
Leaves out bgutil's PO token provider and its yt-dlp plugin.

.PARAMETER SkipRedistributable
Leaves the Visual C++ Redistributable as it is.

.PARAMETER SignIn
Afterwards, opens Firefox on the bot's profile at YouTube, to sign in, and
waits for it to be closed.

.PARAMETER Force
Installs into -Destination even when LatiBot.exe is not there.

.EXAMPLE
.\Install-Dependencies.ps1
Installs or updates everything beside the bot.

.EXAMPLE
.\Install-Dependencies.ps1 -SignIn
The same, then opens Firefox to sign the bot's profile in to YouTube.
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Destination = $PSScriptRoot,
    [switch]$SkipFirefox,
    [switch]$SkipPotProvider,
    [switch]$SkipRedistributable,
    [switch]$SignIn,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is many times slower with its progress bar in Windows
# PowerShell 5.1.
$ProgressPreference = 'SilentlyContinue'
# Windows PowerShell 5.1 can default to TLS 1.0, which GitHub refuses.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$UserAgent = 'LatiBot-Install-Dependencies'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Write-Done([string]$Message) {
    Write-Host "   $Message" -ForegroundColor Green
}

function Write-Note([string]$Message) {
    Write-Host "   $Message"
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Runs a program and returns its output, without letting what it writes to
# stderr stop the script: Windows PowerShell 5.1 turns redirected stderr into
# errors, which $ErrorActionPreference = 'Stop' would throw.
function Invoke-Native([string]$Path, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    $output = & $Path @Arguments 2>&1 | ForEach-Object { "$_" }
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = @($output) }
}

# The first line a program prints when asked its version, or $null when it
# is not there or will not say.
function Get-NativeVersion([string]$Path, [string]$Flag) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        $ran = Invoke-Native $Path @($Flag)
        if ($ran.ExitCode -ne 0 -or $ran.Output.Count -eq 0) { return $null }
        return $ran.Output[0].Trim()
    } catch {
        return $null
    }
}

function Get-GitHubRelease([string]$Repository) {
    $uri = "https://api.github.com/repos/$Repository/releases/latest"
    return Invoke-RestMethod -Uri $uri -Headers @{ 'User-Agent' = $UserAgent; 'Accept' = 'application/vnd.github+json' }
}

function Get-ReleaseAsset($Release, [string]$Name) {
    $asset = @($Release.assets | Where-Object { $_.name -eq $Name })
    if ($asset.Count -eq 0) { throw "The $($Release.tag_name) release has no $Name." }
    return $asset[0]
}

function Save-Download([string]$Uri, [string]$Path) {
    Invoke-WebRequest -Uri $Uri -OutFile $Path -UseBasicParsing -Headers @{ 'User-Agent' = $UserAgent }
}

# A small text file's contents. Windows PowerShell 5.1 gives back bytes for
# anything not served as text.
function Get-Text([string]$Uri) {
    $content = (Invoke-WebRequest -Uri $Uri -UseBasicParsing -Headers @{ 'User-Agent' = $UserAgent }).Content
    if ($content -is [byte[]]) { $content = [Text.Encoding]::ASCII.GetString($content) }
    return "$content".Trim()
}

# Moves a folder out of the way and deletes it. node_modules can hold paths
# longer than Windows PowerShell 5.1 can delete; renamed first, a folder that
# cannot be deleted is only left behind, named as old, and said so.
function Remove-Folder([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $old = "$Path.old-" + [guid]::NewGuid().ToString('N').Substring(0, 8)
    Rename-Item -LiteralPath $Path -NewName (Split-Path -Leaf $old)
    try {
        Remove-Item -LiteralPath $old -Recurse -Force
    } catch {
        $warnings.Add("The old copy at $old could not be deleted; delete it yourself.")
    }
}

# Stops with an error, and deletes the file, when its SHA-256 is not the one
# expected.
function Confirm-Sha256([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected.Trim().ToUpperInvariant()) {
        Remove-Item -LiteralPath $Path -Force
        throw "$(Split-Path -Leaf $Path) did not match its published SHA-256, so it was deleted. Try again; if it fails again, do not use it."
    }
}

# Downloads a GitHub release asset and checks it against the SHA-256 GitHub
# publishes for it.
function Save-ReleaseAsset($Release, [string]$Name, [string]$Path) {
    $asset = Get-ReleaseAsset $Release $Name
    Save-Download $asset.browser_download_url $Path
    $digest = $asset.PSObject.Properties['digest']
    if ($null -eq $digest -or -not "$($digest.Value)".StartsWith('sha256:')) {
        throw "GitHub gave no SHA-256 for $Name, so it cannot be checked."
    }
    Confirm-Sha256 $Path "$($digest.Value)".Substring(7)
}

# Stops with an error unless an installer is validly signed by $Publisher.
function Confirm-Signature([string]$Path, [string]$Publisher) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch "O=$([regex]::Escape($Publisher))") {
        Remove-Item -LiteralPath $Path -Force
        throw "$(Split-Path -Leaf $Path) is not signed by $Publisher, so it was deleted."
    }
}

function Expand-Zip([string]$Path, [string]$Into) {
    if (Test-Path -LiteralPath $Into) { Remove-Item -LiteralPath $Into -Recurse -Force }
    Expand-Archive -LiteralPath $Path -DestinationPath $Into -Force
}

function Find-Firefox {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Mozilla Firefox\firefox.exe')
    )
    if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Mozilla Firefox\firefox.exe') }
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\firefox.exe',
                       'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\firefox.exe')) {
        $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($null -ne $entry -and $entry.PSObject.Properties['(default)']) { $candidates += $entry.'(default)'.Trim('"') }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $candidate }
    }
    return $null
}

# ---------------------------------------------------------------------------
# Before anything
# ---------------------------------------------------------------------------

if (-not [Environment]::Is64BitOperatingSystem) { throw 'LatiBot runs on 64-bit Windows only.' }

$Destination = (Resolve-Path -LiteralPath $Destination).Path
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'LatiBot.exe')) -and -not $Force) {
    throw "LatiBot.exe is not in $Destination. Run this from the bot's folder, pass -Destination <the bot's folder>, or -Force to install there anyway."
}

$running = @(Get-Process -Name 'LatiBot' -ErrorAction SilentlyContinue |
             Where-Object { $_.Path -and $_.Path.StartsWith($Destination, [StringComparison]::OrdinalIgnoreCase) })
if ($running.Count -gt 0) {
    throw "LatiBot is running from $Destination. Stop it first: it holds yt-dlp, Deno and the PO token provider open."
}

$isAdministrator = Test-Administrator
$work = Join-Path ([IO.Path]::GetTempPath()) ("latibot-dependencies-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

$summary = [ordered]@{}
$warnings = New-Object System.Collections.Generic.List[string]

Write-Host "Installing LatiBot's dependencies into $Destination"

try {
    # -----------------------------------------------------------------------
    # yt-dlp
    # -----------------------------------------------------------------------
    Write-Step 'yt-dlp'
    $release = Get-GitHubRelease 'yt-dlp/yt-dlp'
    $ytdlp = Join-Path $Destination 'yt-dlp.exe'
    $installed = Get-NativeVersion $ytdlp '--version'
    if ($installed -eq $release.tag_name) {
        Write-Done "$installed, the latest, is already installed"
    } else {
        $download = Join-Path $work 'yt-dlp.exe'
        Save-ReleaseAsset $release 'yt-dlp.exe' $download
        Copy-Item -LiteralPath $download -Destination $ytdlp -Force
        Write-Done "installed $($release.tag_name)$(if ($installed) { ", replacing $installed" })"
    }
    $summary['yt-dlp'] = $release.tag_name

    # -----------------------------------------------------------------------
    # ffmpeg, the release build gyan.dev publishes for Windows
    # -----------------------------------------------------------------------
    Write-Step 'ffmpeg'
    $version = Get-Text 'https://www.gyan.dev/ffmpeg/builds/release-version'
    $ffmpeg = Join-Path $Destination 'ffmpeg.exe'
    $installed = Get-NativeVersion $ffmpeg '-version'
    if ($installed -and $installed.StartsWith("ffmpeg version $version-")) {
        Write-Done "$version, the latest release, is already installed"
    } else {
        $package = "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-$version-essentials_build.zip"
        $zip = Join-Path $work 'ffmpeg.zip'
        Save-Download $package $zip
        Confirm-Sha256 $zip ((Get-Text "$package.sha256") -split '\s+')[0]
        Expand-Zip $zip (Join-Path $work 'ffmpeg')
        foreach ($program in @('ffmpeg.exe', 'ffprobe.exe')) {
            $found = Get-ChildItem -LiteralPath (Join-Path $work 'ffmpeg') -Recurse -Filter $program | Select-Object -First 1
            if ($null -eq $found) { throw "The ffmpeg $version package has no $program." }
            Copy-Item -LiteralPath $found.FullName -Destination (Join-Path $Destination $program) -Force
        }
        Write-Done "installed $version"
    }
    $summary['ffmpeg'] = $version

    # -----------------------------------------------------------------------
    # Deno, which yt-dlp solves YouTube's JavaScript challenges with, and
    # the PO token provider runs on
    # -----------------------------------------------------------------------
    Write-Step 'Deno'
    $release = Get-GitHubRelease 'denoland/deno'
    $deno = Join-Path $Destination 'deno.exe'
    $installed = Get-NativeVersion $deno '--version'
    if ($installed -and $installed.StartsWith("deno $($release.tag_name.TrimStart('v')) ")) {
        Write-Done "$($release.tag_name), the latest, is already installed"
    } else {
        $zip = Join-Path $work 'deno.zip'
        Save-ReleaseAsset $release 'deno-x86_64-pc-windows-msvc.zip' $zip
        Expand-Zip $zip (Join-Path $work 'deno')
        Copy-Item -LiteralPath (Join-Path $work 'deno\deno.exe') -Destination $deno -Force
        Write-Done "installed $($release.tag_name)"
    }
    $summary['Deno'] = $release.tag_name

    # -----------------------------------------------------------------------
    # bgutil's PO token provider, and its yt-dlp plugin
    # -----------------------------------------------------------------------
    if ($SkipPotProvider) {
        $summary['PO token provider'] = 'skipped'
    } else {
        Write-Step "bgutil's PO token provider"
        $release = Get-GitHubRelease 'Brainicism/bgutil-ytdlp-pot-provider'
        $tag = $release.tag_name
        $providerHome = Join-Path $Destination 'bgutil-ytdlp-pot-provider'
        $server = Join-Path $providerHome 'server'
        $versionFile = Join-Path $providerHome 'VERSION.txt'
        $plugins = Join-Path $Destination 'yt-dlp-plugins'
        $plugin = Join-Path $plugins 'bgutil-ytdlp-pot-provider.zip'
        $installed = if (Test-Path -LiteralPath $versionFile) { (Get-Content -LiteralPath $versionFile -Raw).Trim() } else { $null }

        if ($installed -eq $tag -and (Test-Path -LiteralPath $plugin) -and (Test-Path -LiteralPath (Join-Path $server 'node_modules'))) {
            Write-Done "$tag, the latest, is already installed"
        } else {
            # The plugin, where yt-dlp looks beside its own executable. The
            # provider and the plugin must be the same version.
            New-Item -ItemType Directory -Path $plugins -Force | Out-Null
            $download = Join-Path $work 'bgutil-ytdlp-pot-provider.zip'
            Save-ReleaseAsset $release 'bgutil-ytdlp-pot-provider.zip' $download
            Copy-Item -LiteralPath $download -Destination $plugin -Force

            # The provider's source, at the same tag. GitHub publishes no
            # checksum for a tag's source archive; it comes over HTTPS from
            # the project's own repository.
            $source = Join-Path $work 'bgutil-source.zip'
            Save-Download "https://github.com/Brainicism/bgutil-ytdlp-pot-provider/archive/refs/tags/$tag.zip" $source
            Expand-Zip $source (Join-Path $work 'bgutil')
            $unpacked = Get-ChildItem -LiteralPath (Join-Path $work 'bgutil') -Directory | Select-Object -First 1
            if ($null -eq $unpacked -or -not (Test-Path -LiteralPath (Join-Path $unpacked.FullName 'server\deno.lock'))) {
                throw "The provider's $tag source has no server\deno.lock."
            }
            Remove-Folder $providerHome
            New-Item -ItemType Directory -Path $providerHome | Out-Null
            # Copied, not moved: the temporary folder may be on another drive.
            Copy-Item -LiteralPath (Join-Path $unpacked.FullName 'server') -Destination $server -Recurse

            # Its packages, as its README gives for Deno. canvas fetches a
            # prebuilt binary of its own while it installs.
            Write-Note 'installing its packages with Deno; this takes a minute'
            Push-Location -LiteralPath $server
            try {
                $ran = Invoke-Native $deno @('install', '--allow-scripts=npm:canvas', '--frozen')
            } finally {
                Pop-Location
            }
            if ($ran.ExitCode -ne 0) {
                $ran.Output | Select-Object -Last 20 | ForEach-Object { Write-Host "   $_" }
                throw "Deno could not install the provider's packages (exit code $($ran.ExitCode)); see above."
            }
            Set-Content -LiteralPath $versionFile -Value $tag -Encoding ASCII
            Write-Done "installed $tag"
        }
        $summary['PO token provider'] = $tag
    }

    # -----------------------------------------------------------------------
    # The Microsoft Visual C++ Redistributable, which LatiBot.exe needs
    # -----------------------------------------------------------------------
    if ($SkipRedistributable) {
        $summary['Visual C++ Redistributable'] = 'skipped'
    } else {
        Write-Step 'Microsoft Visual C++ Redistributable (x64)'
        $present = $false
        foreach ($key in @('HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
                           'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64')) {
            $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
            if ($null -ne $entry -and $entry.PSObject.Properties['Installed'] -and $entry.Installed -eq 1) { $present = $true }
        }
        if ($isAdministrator) {
            # Always the latest: it has to be at least as new as the compiler
            # that built the bot, and installing over the same version or a
            # newer one changes nothing.
            $installer = Join-Path $work 'vc_redist.x64.exe'
            Save-Download 'https://aka.ms/vc14/vc_redist.x64.exe' $installer
            Confirm-Signature $installer 'Microsoft Corporation'
            $process = Start-Process -FilePath $installer -ArgumentList '/install', '/quiet', '/norestart' -Wait -PassThru
            switch ($process.ExitCode) {
                0 { Write-Done 'installed the latest' }
                1638 { Write-Done 'the latest, or a newer one, is already installed' }
                3010 { Write-Done 'installed the latest; Windows needs a restart to finish' ; $warnings.Add('Restart Windows to finish installing the Visual C++ Redistributable.') }
                default { throw "The Visual C++ Redistributable installer stopped with code $($process.ExitCode)." }
            }
            $summary['Visual C++ Redistributable'] = 'latest'
        } elseif ($present) {
            Write-Note 'installed, but checking it is new enough needs an administrator'
            $summary['Visual C++ Redistributable'] = 'installed, not checked'
            $warnings.Add('If LatiBot.exe will not start, naming MSVCP140.dll or VCRUNTIME140.dll, run this again as an administrator.')
        } else {
            Write-Note 'not installed, and installing it needs an administrator'
            $summary['Visual C++ Redistributable'] = 'missing'
            $warnings.Add('The Visual C++ Redistributable is missing, and LatiBot.exe will not start without it: run this again as an administrator.')
        }
    }

    # -----------------------------------------------------------------------
    # Firefox, and the profile the bot signs in to YouTube with
    # -----------------------------------------------------------------------
    $firefox = $null
    $profileFolder = Join-Path $Destination 'data\firefox-profile'
    if ($SkipFirefox) {
        $summary['Firefox'] = 'skipped'
    } else {
        Write-Step 'Firefox'
        $firefox = Find-Firefox
        if ($firefox) {
            Write-Done "installed at $firefox; it keeps itself up to date"
            $summary['Firefox'] = 'installed'
        } elseif ($isAdministrator) {
            $installer = Join-Path $work 'firefox-setup.exe'
            Save-Download 'https://download.mozilla.org/?product=firefox-latest-ssl&os=win64&lang=en-US' $installer
            Confirm-Signature $installer 'Mozilla Corporation'
            $process = Start-Process -FilePath $installer -ArgumentList '/S' -Wait -PassThru
            if ($process.ExitCode -ne 0) { throw "The Firefox installer stopped with code $($process.ExitCode)." }
            $firefox = Find-Firefox
            if (-not $firefox) { throw 'Firefox was installed, but firefox.exe could not be found.' }
            Write-Done "installed at $firefox"
            $summary['Firefox'] = 'installed'
        } else {
            Write-Note 'not installed, and installing it needs an administrator'
            $summary['Firefox'] = 'missing'
            $warnings.Add('Firefox is missing: run this again as an administrator, or install it yourself, to sign the bot in to YouTube.')
        }

        if (-not (Test-Path -LiteralPath $profileFolder)) {
            New-Item -ItemType Directory -Path $profileFolder -Force | Out-Null
            Write-Done "made the bot's Firefox profile folder, $profileFolder"
        } else {
            Write-Done "the bot's Firefox profile is $profileFolder"
        }
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------------------
# Signing in
# ---------------------------------------------------------------------------

if ($SignIn) {
    if (-not $firefox) { throw 'Firefox is needed to sign in; see above.' }
    Write-Step 'Signing the bot in to YouTube'
    Write-Note 'Firefox opens on the bot''s own profile. In it:'
    Write-Note '  1. Sign in to YouTube with the account the bot should use.'
    Write-Note '  2. Open an age-restricted video, and check that it plays.'
    Write-Note '  3. Close Firefox (Menu > Exit), which saves the cookies where yt-dlp reads them.'
    Write-Note 'Waiting for Firefox to close...'
    Start-Process -FilePath $firefox -ArgumentList '-profile', "`"$profileFolder`"", '-no-remote', 'https://www.youtube.com/' -Wait

    $cookies = Join-Path $profileFolder 'cookies.sqlite'
    $pending = Join-Path $profileFolder 'cookies.sqlite-wal'
    if (-not (Test-Path -LiteralPath $cookies)) {
        $warnings.Add('Firefox saved no cookies to the profile. Run this again with -SignIn, and sign in before closing it.')
    } elseif ((Test-Path -LiteralPath $pending) -and (Get-Item -LiteralPath $pending).Length -gt 0) {
        $warnings.Add('Firefox still has cookies it has not saved. Make sure it is closed (Menu > Exit), not just its window.')
    } else {
        Write-Done 'Firefox closed, and its cookies are saved'
    }
}

# ---------------------------------------------------------------------------
# What was done, and what is left
# ---------------------------------------------------------------------------

Write-Step 'Done'
foreach ($entry in $summary.GetEnumerator()) {
    Write-Note ('{0,-28} {1}' -f $entry.Key, $entry.Value)
}

if (-not $SkipFirefox) {
    Write-Host ''
    Write-Host 'To sign the bot in to YouTube, so age-restricted videos play:' -ForegroundColor Cyan
    if (-not $SignIn) {
        Write-Note "1. Run this again with -SignIn, or open Firefox yourself with:"
        Write-Note "     & `"$(if ($firefox) { $firefox } else { 'firefox.exe' })`" -profile `"$profileFolder`" -no-remote https://www.youtube.com/"
        Write-Note '   sign in to YouTube with the bot''s account, and close Firefox (Menu > Exit).'
    } else {
        Write-Note '1. Signed in, above.'
    }
    Write-Note "2. Add this line to $(Join-Path $Destination '.env'):"
    Write-Note "     LATIBOT_YTDLP_FIREFOX_PROFILE=$profileFolder"
    Write-Note '3. Start the bot, and check its log says music signs in with the Firefox profile.'
}

if ($warnings.Count -gt 0) {
    Write-Host ''
    foreach ($warning in $warnings) { Write-Warning $warning }
}
