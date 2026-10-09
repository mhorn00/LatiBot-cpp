#!/usr/bin/env bash
# Fetches and installs what LatiBot needs on a Linux machine it runs on: the
# Linux counterpart of Install-Dependencies.ps1 (Install-Dependencies.md).
#
# Installs ffmpeg, and what this script and the bot need, from the system's
# packages (Debian or Ubuntu, with apt; this part needs root), and puts
# yt-dlp and Deno beside the bot, with bgutil's PO token provider and its
# yt-dlp plugin. Run it again to update everything to the latest release.
# Stop the bot first: it holds yt-dlp, Deno and the provider open.
#
# Every download from GitHub is checked against the SHA-256 GitHub publishes
# for it.
#
#   install-dependencies.sh [--destination DIR] [--skip-packages]
#                           [--skip-pot-provider] [--force]
#
#   --destination DIR    the bot's folder, where LatiBot is; defaults to this
#                        script's own folder, which is where the build puts it
#   --skip-packages      leaves the system's packages as they are, so it runs
#                        without root
#   --skip-pot-provider  leaves out bgutil's PO token provider and its plugin
#   --force              installs into DIR even when LatiBot is not there
set -euo pipefail

destination=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
skip_packages=false
skip_pot_provider=false
force=false
while [ $# -gt 0 ]; do
    case $1 in
    --destination) destination=$(cd "$2" && pwd); shift 2 ;;
    --skip-packages) skip_packages=true; shift ;;
    --skip-pot-provider) skip_pot_provider=true; shift ;;
    --force) force=true; shift ;;
    -h | --help) sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

user_agent=LatiBot-install-dependencies
step() { printf '\n\033[36m== %s\033[0m\n' "$1"; }
done_() { printf '   \033[32m%s\033[0m\n' "$1"; }
note() { printf '   %s\n' "$1"; }
fail() { printf '\033[31merror: %s\033[0m\n' "$1" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Before anything
# ---------------------------------------------------------------------------

[ "$(uname -m)" = x86_64 ] || fail "LatiBot is built for x86-64 Linux; this is $(uname -m)."
if [ ! -x "$destination/LatiBot" ] && [ "$force" = false ]; then
    fail "LatiBot is not in $destination. Run this from the bot's folder, pass --destination <the bot's folder>, or --force to install there anyway."
fi
if pgrep -f "^$destination/LatiBot( |$)" > /dev/null 2>&1; then
    fail "LatiBot is running from $destination. Stop it first: it holds yt-dlp, Deno and the PO token provider open."
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
declare -A summary=()
order=()
record() { summary[$1]=$2; order+=("$1"); }

echo "Installing LatiBot's dependencies into $destination"

# ---------------------------------------------------------------------------
# The system's packages
# ---------------------------------------------------------------------------

if [ "$skip_packages" = true ]; then
    record packages skipped
else
    step "The system's packages"
    [ "$(id -u)" -eq 0 ] || fail "installing packages needs root: run this with sudo, or pass --skip-packages."
    command -v apt-get > /dev/null || fail "this installs packages with apt (Debian or Ubuntu); install curl, jq, unzip, ffmpeg, tzdata and ca-certificates yourself, then pass --skip-packages."
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    # ffmpeg decodes music for Discord; tzdata is what /midnight's time zones
    # are read from; the rest are this script's.
    apt-get install -y -qq --no-install-recommends ca-certificates curl jq unzip ffmpeg tzdata > /dev/null
    done_ "ffmpeg $(ffmpeg -version | head -n 1 | cut -d ' ' -f 3), and what this script needs"
    record packages installed
fi
for tool in curl jq unzip sha256sum; do
    command -v "$tool" > /dev/null || fail "$tool is missing; install it, or run this as root without --skip-packages."
done

# ---------------------------------------------------------------------------
# Helpers for GitHub releases
# ---------------------------------------------------------------------------

release_json() { curl -fsSL -H "User-Agent: $user_agent" -H 'Accept: application/vnd.github+json' "https://api.github.com/repos/$1/releases/latest"; }

# Downloads a release asset to a path, and checks it against the SHA-256
# GitHub publishes for it.
save_asset() {
    local release=$1 name=$2 path=$3 url digest actual
    url=$(jq -r --arg name "$name" '.assets[] | select(.name == $name) | .browser_download_url' <<< "$release")
    digest=$(jq -r --arg name "$name" '.assets[] | select(.name == $name) | .digest // empty' <<< "$release")
    [ -n "$url" ] || fail "the $(jq -r .tag_name <<< "$release") release has no $name."
    [[ $digest == sha256:* ]] || fail "GitHub gave no SHA-256 for $name, so it cannot be checked."
    curl -fsSL -H "User-Agent: $user_agent" -o "$path" "$url"
    actual=$(sha256sum "$path" | cut -d ' ' -f 1)
    if [ "$actual" != "${digest#sha256:}" ]; then
        rm -f "$path"
        fail "$name did not match its published SHA-256, so it was deleted. Try again; if it fails again, do not use it."
    fi
}

# The first line a program prints when asked its version, or nothing.
version_of() { [ -x "$1" ] && "$1" "$2" 2> /dev/null | head -n 1 || true; }

# ---------------------------------------------------------------------------
# yt-dlp, its standalone build, which needs no Python
# ---------------------------------------------------------------------------

step yt-dlp
release=$(release_json yt-dlp/yt-dlp)
tag=$(jq -r .tag_name <<< "$release")
installed=$(version_of "$destination/yt-dlp" --version)
if [ "$installed" = "$tag" ]; then
    done_ "$installed, the latest, is already installed"
else
    save_asset "$release" yt-dlp_linux "$work/yt-dlp"
    install -m 755 "$work/yt-dlp" "$destination/yt-dlp"
    done_ "installed $tag${installed:+, replacing $installed}"
fi
record yt-dlp "$tag"

# ---------------------------------------------------------------------------
# Deno, which yt-dlp solves YouTube's JavaScript challenges with, and the PO
# token provider runs on
# ---------------------------------------------------------------------------

step Deno
release=$(release_json denoland/deno)
tag=$(jq -r .tag_name <<< "$release")
installed=$(version_of "$destination/deno" --version)
if [[ $installed == "deno ${tag#v} "* ]]; then
    done_ "$tag, the latest, is already installed"
else
    save_asset "$release" deno-x86_64-unknown-linux-gnu.zip "$work/deno.zip"
    unzip -q -o "$work/deno.zip" -d "$work/deno"
    install -m 755 "$work/deno/deno" "$destination/deno"
    done_ "installed $tag"
fi
record Deno "$tag"

# ---------------------------------------------------------------------------
# bgutil's PO token provider, and its yt-dlp plugin
# ---------------------------------------------------------------------------

if [ "$skip_pot_provider" = true ]; then
    record "PO token provider" skipped
else
    step "bgutil's PO token provider"
    release=$(release_json Brainicism/bgutil-ytdlp-pot-provider)
    tag=$(jq -r .tag_name <<< "$release")
    provider_home="$destination/bgutil-ytdlp-pot-provider"
    server="$provider_home/server"
    plugin="$destination/yt-dlp-plugins/bgutil-ytdlp-pot-provider.zip"
    installed=$(cat "$provider_home/VERSION.txt" 2> /dev/null || true)
    if [ "$installed" = "$tag" ] && [ -f "$plugin" ] && [ -d "$server/node_modules" ]; then
        done_ "$tag, the latest, is already installed"
    else
        # The plugin, where yt-dlp looks beside its own executable. The
        # provider and the plugin must be the same version.
        mkdir -p "$(dirname "$plugin")"
        save_asset "$release" bgutil-ytdlp-pot-provider.zip "$work/plugin.zip"
        install -m 644 "$work/plugin.zip" "$plugin"

        # The provider's source, at the same tag. GitHub publishes no
        # checksum for a tag's source archive; it comes over HTTPS from the
        # project's own repository.
        curl -fsSL -H "User-Agent: $user_agent" -o "$work/source.zip" \
            "https://github.com/Brainicism/bgutil-ytdlp-pot-provider/archive/refs/tags/$tag.zip"
        unzip -q "$work/source.zip" -d "$work/source"
        unpacked=$(find "$work/source" -mindepth 1 -maxdepth 1 -type d | head -n 1)
        [ -f "$unpacked/server/deno.lock" ] || fail "the provider's $tag source has no server/deno.lock."
        rm -rf "$provider_home"
        mkdir -p "$provider_home"
        cp -r "$unpacked/server" "$server"

        # Its packages, as its README gives for Deno. canvas fetches a
        # prebuilt binary of its own while it installs.
        note "installing its packages with Deno; this takes a minute"
        if ! (cd "$server" && "$destination/deno" install --allow-scripts=npm:canvas --frozen > "$work/deno.log" 2>&1); then
            tail -n 20 "$work/deno.log" | sed 's/^/   /'
            fail "Deno could not install the provider's packages; see above."
        fi
        echo "$tag" > "$provider_home/VERSION.txt"
        done_ "installed $tag"
    fi
    record "PO token provider" "$tag"
fi

# ---------------------------------------------------------------------------
# What was done, and what is left
# ---------------------------------------------------------------------------

step Done
for key in "${order[@]}"; do
    printf '   %-28s %s\n' "$key" "${summary[$key]}"
done

printf '\n\033[36m%s\033[0m\n' "To sign the bot in to YouTube, so age-restricted videos play:"
note "1. Export the bot account's YouTube cookies, as a cookies.txt file, from a"
note "   browser signed in to it (the repository's src/modules/music/docs/Music.md"
note "   §4.9 says how), and copy it to the bot's data/youtube-cookies.txt."
note "2. Add this line to $destination/.env:"
note "     LATIBOT_YTDLP_COOKIES=$destination/data/youtube-cookies.txt"
note "3. Start the bot, and check its log says music signs in with the cookies file."
