# Idea: self-hosted embeds (cobalt)

**Status:** parked. Not part of the port. Written down so it isn't forgotten.
**Date:** 2026-09-20
**Related:** [../porting/Porting_Plan_Final.md](../porting/Porting_Plan_Final.md) §9 (URL replacement)

---

## The idea

Instead of rewriting a link to a third-party embed-fixer mirror
(`fixupx.com` and friends), download the actual media with a self-hosted
[cobalt](https://github.com/imputnet/cobalt) instance, store it, and post an
embed built from our own copy.

Why it's appealing:

- **The content survives.** It stays even if the original poster deletes their
  post, the account goes private, or the platform removes it.
- **No dependence on mirror services.** Those break, get rate-limited, get
  blocked, or disappear. The current design already needs several mirrors per
  domain and a retry ladder because of exactly that.
- **Better-looking embeds,** under our control, with no third-party branding.
- **An archive.** Everything the server has shared over the years, searchable,
  as a side effect.

The trade is that we take on hosting: storage, bandwidth, uptime, and the
legal side of keeping other people's media on a machine we own.

---

## What cobalt is (verified 2026-09-20)

- A media downloader for **free, publicly accessible** content. It works "like
  a fancy proxy" and doesn't cache.
- **Licensing: AGPL-3.0 for most of the repo**, with separate licences for the
  API and web front-end components. If we ever expose our instance publicly,
  AGPL's network-use clause matters: users of the service can require the
  source. Self-hosting for ourselves and not modifying it keeps this simple,
  but it should be read properly before shipping.
- **Self-hosting:** Docker (a Dockerfile is provided) or Node.js.
- **API:**
  - `POST /` — the main endpoint. Body takes `url` plus optional settings
    (video quality, audio format and bitrate, download mode, filename style,
    metadata, subtitles, plus per-service options such as YouTube codec or
    Twitter GIF conversion). Requires `Accept: application/json` and
    `Content-Type: application/json`.
  - Responses come in four shapes: **tunnel/redirect** (a URL to fetch),
    **picker** (several media items, e.g. a multi-image post),
    **local-processing** (the client must remux or transcode the streams
    itself), and **error** (machine-readable codes).
  - `GET /tunnel` streams the processed file.
  - `GET /` reports instance info and supported services.
  - `POST /session` issues a JWT after a Cloudflare Turnstile challenge.
- **Auth is optional and per instance:** an API key in the `Authorization`
  header, or the Turnstile-issued bearer token. Our own instance can simply
  use a key.
- **Rate limits** on every endpoint except `GET /`, reported through
  `RateLimit-*` headers, `429` when exceeded.

Two consequences worth noting early:

1. **We need ffmpeg on our side anyway**, because of `local-processing`
   responses, and because media should be normalized for Discord (H.264 + AAC
   in MP4 with `faststart`).
2. **Some platforms need credentials or break periodically.** Cobalt tracks
   this upstream, but a self-hosted instance means we inherit the maintenance.

---

## Two delivery modes

### Mode A — upload the file to Discord (no hosting at all)

Download with cobalt, then attach the file to the bot's message.

- **Pros:** no domain, no web server, no storage, no bandwidth. Discord hosts
  it. The video plays inline. The `/chat` voice-message work in the main plan
  already builds the multipart upload path we'd reuse.
- **Cons:** the **upload size limit** applies (10 MB for a normal server at
  the time of writing, higher with boosts — worth re-checking). Plenty of
  video posts exceed that, so it needs a fallback. And Discord's copy can
  still go away if the message is deleted.
- **Effort: small.** This is the 80 % version of the idea and is worth doing
  first, even on its own.

### Mode B — host it ourselves and let Discord embed our page

Store the media, serve a small page per item with OpenGraph/Twitter-card
metadata, and post that URL.

- Discord's crawler fetches the URL and builds the embed from the meta tags.
  Video embeds need a direct MP4 (`og:video:url` / `twitter:player` with
  width, height and type); images are simpler.
- **Requirements:** a domain, HTTPS, a reverse proxy, a machine that stays up,
  storage, and bandwidth.
- **Cons:** if the host goes down or the domain lapses, **every old embed
  breaks at once** — worse than mirror links, which at least fail one at a
  time. Discord also caches embeds, so fixes don't always show up immediately.
- **Effort: medium**, mostly in operations rather than code.

Realistically: **A first, B later**, with A as B's fallback for small files.

---

## Sketch of the pipeline

```
message with a link
  → url rule says "self-host" for this domain
  → job queued (message id, url, requester)
  → cobalt POST /  → tunnel | picker | local-processing
  → fetch the stream(s)
  → ffmpeg: remux/transcode to H.264+AAC MP4, faststart; thumbnail; duration
  → size check
       ≤ upload limit → attach to a Discord message   (Mode A)
       > upload limit → store + serve a metadata page (Mode B)
  → record in media_assets, keyed by a hash of the source URL (dedupe)
  → suppress the original message's embed, post ours
  → any failure → fall back to today's mirror-link behaviour
```

The fallback matters: this feature should never make links *worse* than they
are today.

**Schema sketch:**

```sql
media_assets(id, source_url_hash UNIQUE, source_url, platform, kind,
             path, bytes, width, height, duration_ms, sha256,
             fetched_at, message_id NULL, state)
media_jobs(id, message_id, source_url, state, attempts, last_error, queued_at)
```

`url_rules` gains a per-rule **mode**: `mirror` (today) or `self_host`, so it
can be turned on for one domain at a time.

---

## What it would take

| Piece | Effort | Notes |
|---|---|---|
| Cobalt instance | S | Docker on the bot's host or a small VPS; API key; restart policy |
| HTTP client for cobalt | S | The raw-API/HTTP port from the main plan already covers this |
| Job queue + retries | M | Persisted in SQLite so restarts don't lose jobs; concurrency limit |
| ffmpeg step | M | Process launch, timeouts, temp files, normalize + thumbnail + probe |
| Mode A upload | S | Reuses the `/chat` multipart path |
| Storage + retention | M | Layout, dedupe by hash, quota, cleanup policy, backups |
| Web server + metadata pages | M | `cpp-httplib` or Drogon in-process, or nginx serving static files plus generated HTML |
| Domain + TLS + proxy | S–M | One-off ops work, then renewal |
| Admin commands | S | `/media stats`, `/media purge`, per-domain mode toggle |
| Tests | M | Mock cobalt responses (all four shapes), ffmpeg failures, size boundaries, fallback path |

Rough guess: **Mode A is a weekend.** Mode B is a few weekends plus ongoing
upkeep.

---

## Risks and open questions

**Legal and social**
- Downloading from some platforms conflicts with their terms of service.
  Hosting other people's media publicly raises copyright and DMCA questions.
  A personal, unlisted, small-server setup is a very different risk profile
  from a public service, and the design should stay firmly on the personal
  side: no public index, unguessable URLs, no search engine indexing.
- People may not want their posts archived permanently. A per-user opt-out,
  reusing the existing `url_opt_outs` table, and a "delete this" control for
  the original poster would both be worth having.
- AGPL: fine for a private instance; read carefully if it's ever exposed.

**Operational**
- **Storage growth.** Worth measuring first: multiply the server's current
  rate of replaced links by an average size. A retention policy (for example,
  keep everything under N MB forever, larger items for a year) can be added
  later, but the schema should allow it from the start.
- **Bandwidth and hotlinking**, if the pages are reachable by anyone with the
  URL. Discord's crawler and proxy will pull each item at least once.
- **Single point of failure.** Old embeds break together if hosting stops.
  Mitigations: keep the original link in the message text, keep the mirror
  fallback, and back up the media directory.
- **Cobalt churn.** Upstream changes, and platforms break it. That's
  maintenance we'd be signing up for.

**Technical unknowns to check when this is picked up**
1. Current Discord upload limits per boost level, and the largest MP4 Discord
   will actually embed from an external URL.
2. Whether `og:video` embeds behave well enough, or whether a direct `.mp4`
   URL embeds better.
3. How cobalt's `local-processing` responses work in practice, and how often
   they occur for the platforms this server actually posts.
4. Whether cobalt can run on the same machine as the bot without the
   resampling and voice work suffering during transcodes.
5. Whether picker responses (multi-image posts) should become an album of
   attachments or a gallery page.

---

## Why it's worth writing down now

Three things in the main plan should stay compatible with this, at no extra
cost:

- **`url_rules` is per domain**, so a per-rule mode column slots in later.
- **`replacement_messages`** already records the link between the original
  message and ours, which is what a media asset would hang off.
- **The `http_client` port and the multipart upload path** are both built for
  other reasons, and are most of Mode A.

Nothing else in the port needs to change for this to remain possible.
