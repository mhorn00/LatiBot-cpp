# Install-Dependencies.ps1

Sets up the machine LatiBot runs on: it fetches and installs what music
needs, and what `LatiBot.exe` itself needs, beside the bot. It also makes a
Firefox profile for the bot to sign in to YouTube with, so age-restricted
videos play. Run it again at any time to update everything to the latest
release.

The build copies this file and the script beside `LatiBot.exe`, so a build
output folder copied to a server has them already. How music uses all of
this is in `docs/features/Music.md` in the repository: §4.9 for signing in,
§4.10 for PO tokens, §5 for the programs.

## What it installs

| What | Why | Where | Checked against |
|---|---|---|---|
| **yt-dlp** | Reads music links, and fetches their audio | `yt-dlp.exe`, beside the bot | GitHub's SHA-256 for the release asset |
| **ffmpeg** | Decodes the audio for Discord | `ffmpeg.exe` and `ffprobe.exe`, beside the bot | gyan.dev's published SHA-256 |
| **Deno** | Solves YouTube's JavaScript challenges for yt-dlp, and runs the PO token provider | `deno.exe`, beside the bot | GitHub's SHA-256 |
| **bgutil's PO token plugin** | Lets yt-dlp ask the provider for PO tokens | `yt-dlp-plugins\bgutil-ytdlp-pot-provider.zip` | GitHub's SHA-256 |
| **bgutil's PO token provider** | Makes the proof-of-origin tokens YouTube wants, so yt-dlp's requests look like the clients they claim to be | `bgutil-ytdlp-pot-provider\server`, with its packages installed by Deno | HTTPS from the project's repository, at the plugin's release tag; GitHub publishes no checksum for source archives |
| **Microsoft Visual C++ Redistributable** (x64) | `LatiBot.exe` will not start without it | installed into Windows | Microsoft's Authenticode signature |
| **Firefox** | Holds the bot's sign-in to YouTube | installed into Windows, if missing | Mozilla's Authenticode signature |

It also makes the folder `data\firefox-profile` for the bot's Firefox
profile.

Everything beside the bot is portable: nothing is added to `PATH`, and no
service is installed. The bot finds each program beside itself. It runs the
PO token provider itself while it runs, so there is nothing else to start.

## Running it

You need Windows 10 or 11, or Windows Server 2016 or later, 64-bit, with
Windows PowerShell 5.1 (built in) or PowerShell 7, and internet access.

1. **Stop the bot**, if it is running. It holds yt-dlp, Deno and the provider
   open, and the script refuses to run while it does.
2. **Open PowerShell as an administrator** the first time. The Visual C++
   Redistributable and Firefox install into Windows, which needs it. The
   rest does not, so later updates can run as an ordinary user; the script
   says what it skipped.
3. **Run it** from the bot's folder:

   ```powershell
   cd C:\path\to\LatiBot
   powershell -ExecutionPolicy Bypass -File .\Install-Dependencies.ps1
   ```

   `-ExecutionPolicy Bypass` applies to this run only. If Windows blocked
   the file because it came from another computer, run
   `Unblock-File .\Install-Dependencies.ps1` first.

It takes a few minutes the first time, most of it ffmpeg's download and the
provider's packages. Later runs download only what has a newer release.

| Option | Does |
|---|---|
| `-Destination <folder>` | Installs beside the bot in that folder, rather than the script's own |
| `-SignIn` | Afterwards, opens Firefox on the bot's profile to sign in to YouTube, and waits for it to close (below) |
| `-SkipFirefox` | Neither installs Firefox nor makes the profile |
| `-SkipPotProvider` | Leaves out the PO token provider and its plugin |
| `-SkipRedistributable` | Leaves the Visual C++ Redistributable as it is |
| `-Force` | Installs into `-Destination` even when `LatiBot.exe` is not there |

`Get-Help .\Install-Dependencies.ps1 -Full` says the same.

## Signing the bot in to YouTube

YouTube plays an age-restricted video only to a signed-in account that is
old enough. The bot signs in with a Firefox profile kept for it. yt-dlp reads
the profile's cookies itself each time it needs them, so there is no file to
export, and nothing to redo when YouTube refreshes the sign-in.

The bot only signs in when YouTube refuses a video without it. Everything
else is fetched signed out, as it would be with no account at all.

### Setting it up, once

1. **Use an account kept for the bot.** Turn on 2-Step Verification, and
   keep payment details off it. YouTube may limit or suspend an account used
   through yt-dlp, and yt-dlp's own documentation warns of it.
2. **Sign in.** Run `.\Install-Dependencies.ps1 -SignIn`, or open the profile
   yourself:

   ```powershell
   & "C:\Program Files\Mozilla Firefox\firefox.exe" -profile "C:\path\to\LatiBot\data\firefox-profile" -no-remote https://www.youtube.com/
   ```

   In that Firefox window:
   - sign in to YouTube with the bot's account;
   - open an age-restricted video and check that it plays. If YouTube asks to
     confirm the account's age, do it here; nothing the bot does can get
     past that;
   - **close Firefox** with Menu > Exit. Closing is what saves its newest
     cookies into `cookies.sqlite`, the one file yt-dlp reads.
3. **Tell the bot where the profile is.** Add this line to `.env` beside the
   bot, with the bot's own folder in it:

   ```
   LATIBOT_YTDLP_FIREFOX_PROFILE=C:\path\to\LatiBot\data\firefox-profile
   ```

4. **Start the bot**, and check its log for:

   ```
   music signs in when it must with the Firefox profile C:/path/to/LatiBot/data/firefox-profile: 40 cookie(s), 18 of them for youtube.com
   ```

   It counts the cookies; it never logs what they are. A warning after it
   says what is wrong:

   | Warning | Means |
   |---|---|
   | `... has no cookies.sqlite yet` | Firefox has never been opened with the profile: step 2 |
   | `... has no youtube.com SAPISID or __Secure-3PAPISID cookie` | The profile is not signed in to YouTube: step 2 |
   | `Firefox has cookies ... that it has not yet saved` | Firefox is open with the profile, or was not closed properly: close it with Menu > Exit |

### Afterwards

- **Keep Firefox closed** with this profile while the bot runs. Open, it
  keeps its newest cookies in a separate file that yt-dlp does not read.
- **Use the profile for nothing else.** Your own browsing belongs in your
  own profile.
- **If age-restricted videos stop playing**, with the log warning
  `yt-dlp could not read ... signed in either`, YouTube has ended the
  sign-in. Open the profile again as in step 2, sign in if YouTube asks,
  and close Firefox. The bot need not be restarted: yt-dlp reads the profile
  afresh each time.
- **Firefox updates itself.** yt-dlp reads Firefox's cookies database, so
  keep yt-dlp current too (below).
- The profile is a sign-in. Whoever has the folder is signed in as the
  account, so keep `data\` to yourself.

The bot can also sign in with a `cookies.txt` exported from a browser,
named by `LATIBOT_YTDLP_COOKIES`, as `docs/features/Music.md` §4.9 describes.
It has to be exported again whenever YouTube ends that sign-in; the profile
does not. When both are set, the profile is used.

## PO tokens

YouTube asks its clients for a proof-of-origin (PO) token with some
requests, and refuses, or slows, those that come without one. yt-dlp's
guide recommends a PO token provider plugin, and bgutil's is the one it
lists first.

There is nothing to run by hand. When the bot starts, it finds the provider
in `bgutil-ytdlp-pot-provider\server` and runs it with Deno, listening on
`127.0.0.1:4416`, which only this machine can reach. It tells yt-dlp's
plugin where it is, and starts it again if it stops. It stops with the bot.
The log says:

```
yt-dlp gets PO tokens from bgutil's provider, run with Deno from C:\path\to\LatiBot\bgutil-ytdlp-pot-provider\server, at http://127.0.0.1:4416
```

If something else on the machine already uses port 4416, set
`"pot_provider_port"` in `config.json` to a free one. To check yt-dlp sees
the plugin, run `.\yt-dlp.exe -v https://www.youtube.com/watch?v=<a video>`
from the bot's folder: the list of PO token providers in its output names
`bgutil:http`.

## Updating

Stop the bot, run the script again, and start the bot. It compares each part
with its latest release and downloads only what is newer.

yt-dlp is the part that goes out of date: YouTube changes, and yt-dlp's next
release follows. When YouTube videos start failing for no other reason, update
first. Running the script once a month keeps ahead of most of it.

## Afterwards, the bot's folder

```
LatiBot.exe, dpp.dll, dectalk.dll, dtalk_us.dic    the bot
yt-dlp.exe, ffmpeg.exe, ffprobe.exe, deno.exe       music's programs
yt-dlp-plugins\bgutil-ytdlp-pot-provider.zip        the PO token plugin
bgutil-ytdlp-pot-provider\server\                   the PO token provider
bgutil-ytdlp-pot-provider\VERSION.txt               its version, for updates
data\firefox-profile\                               the bot's Firefox profile
Install-Dependencies.ps1, Install-Dependencies.md   this
```

## When something goes wrong

| What you see | What to do |
|---|---|
| `LatiBot is running from ...` | Stop the bot, and run the script again |
| `... did not match its published SHA-256, so it was deleted` | A download was damaged or changed. Run it again; if it happens again, do not use it |
| `Deno could not install the provider's packages` | Its last lines say why. Usually the network, or canvas finding no prebuilt binary for this Windows; run it again, or use `-SkipPotProvider` and go without PO tokens |
| `... needs an administrator` | Run it again from PowerShell opened as an administrator |
| `LatiBot.exe` will not start, naming `MSVCP140.dll` | The Visual C++ Redistributable is missing or too old: run the script as an administrator |
| The log warns the PO token provider keeps stopping | Its own lines are in the log at debug level (`LATIBOT_LOG_LEVEL=debug`). A port in use stops it at once: change `pot_provider_port` |
| `The old copy at ... could not be deleted` | An update left the previous provider behind under a `.old-` name; delete that folder |

## Removing it

Delete the files and folders listed above from the bot's folder. Firefox and
the Visual C++ Redistributable are ordinary Windows programs, removed from
Settings > Apps.
