# Steal this emote

Right-click a message with custom emotes or a sticker and pick **Apps → Steal
this emote**. The bot answers only you with the message's emotes and stickers,
one to a page, to name and add to a server in `trusted_guilds`.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](../../../../docs/User_Guide.md#steal-this-emote-apps-menu)
says how to use it.

| | |
|---|---|
| **Module** | `emotes`: [its README](../README.md) lists what it owns |
| **Code** | `src/modules/emotes/src/steal_command.*`; `create_guild_emoji`, `create_guild_sticker`, `guild_sticker_count`, `emoji_servers` and `edit_interaction_response` on `core/ports/discord_gateway.hpp` |
| **Tests** | `src/modules/emotes/tests/` (`latibot_emotes_tests`) |
| **Tables** | none |
| **Status** | Not yet seen working in Discord; §5 says what to check |

## 1. Intent

Keeping an emote or sticker you saw elsewhere means saving its image, finding
the server settings and uploading it by hand. The bot can already see it and
upload one, so it can do all that from the message.

## 2. Behaviour

### 2.1 Who can use it

Anyone who can run it can add to any server it offers, whatever their own
permissions there, and whether or not they are in it. A server is offered only
when:

- **it is listed in `trusted_guilds`** in `config.json`. Listing a server is
  what says that everyone may do this, and may see its name;
- the bot is in it, and has Create Expressions or Manage Expressions there.

The bot's permission and the server's room are checked again when **Add** is
pressed.

### 2.2 Which emotes and stickers

Every custom emote written in the message's text, in order, each once, and then
the message's stickers. Unicode emoji have no image to take. Emotes inside `code`
are left out, since Discord shows them there as text. Reactions, and a forwarded
message's contents, are not read. A message with none gets `there are no custom
emotes or stickers in that message`.

Stickers in Lottie format, which most of Discord's own are, can't be taken:
Discord lets only verified and partnered servers upload those. A line on the
panel counts them, and a message with nothing else gets a reply saying why.

### 2.3 The panel

One emote or sticker to a page, showing its image, with ◀ ▶ when there are
several:

- A **menu of servers** with room for it, by name, each saying how much:
  `12 of 50 animated slots free`. Discord counts still emotes, animated emotes
  and stickers separately, so a server can be full for one and not another. A
  line under the panel counts the servers left out for being full.
- **Name** opens a form, filled with its own name. An emote's name is 2 to 32
  letters, numbers and underscores; a sticker's is any 2 to 30 characters.
- **Add to …** is ready once a server is picked. It shows **Adding…** at once,
  and then the panel says how it went.

Each keeps its own name and server while you page. Once added to a server, its
menu stops offering that one, and it can still go to others.

### 2.4 Room

A server has 50 emote slots of each kind, 100 at boost level 1, 150 at level 2
and 250 at level 3, and 5 sticker slots, 15, 30 and 60 at those levels. Emote
counts come from the bot's cache, which Discord keeps up to date. The cache does
not hold stickers, so those are counted by asking Discord: for every server when
the panel opens, and for the chosen one when **Add** is pressed. Discord still
has the last word, and its refusal is shown when it disagrees.

### 2.5 Expiry

A request lasts until nobody has used it for 15 minutes, or the bot restarts.
After that the panel says to pick the command again.

## 3. How it works

`find_emotes` reads `<:name:id>` and `<a:name:id>` from the text Discord sends
with the interaction, so the bot need not be able to read the channel.

`find_stickers` reads the `sticker_items` sent with it the same way.

`steal_panel` keeps each request in memory, because a message's emotes will not
fit in a `custom_id`: a page and the request's id ride there instead (views
`stealpage`, `stealpick`, `stealname`, `stealform`, `stealadd`). For a message
with a sticker, `steal_panel::open` makes one `guild_sticker_count` call per
server, so the command defers its answer first.

An upload can take longer than the three seconds an answer has, so **Add**
answers with the panel showing **Adding…**, and a detached task downloads the
image and uploads it, then edits the panel with that press's token. It redraws
whichever emote the panel is showing by then, and a second press cannot start a
second upload of that emote, because the first has disabled the button and the
request remembers it is `adding`.

An emote's image comes from `cdn.discordapp.com/emojis/<id>.png`, or `.gif` when
it is animated. A sticker's comes from `cdn.discordapp.com/stickers/<id>.png`,
APNG included, or for a GIF from `media.discordapp.net/stickers/<id>.gif`. One
over Discord's limit, 256 KiB for an emote and 512 KiB for a sticker, is fetched
again at `?size=96`.

`create_guild_sticker` builds its own multipart body, since DPP's
`guild_sticker_create` sends the fields as `payload_json`, where Discord
documents form fields. The sticker's name is also its tag: a message carries no
tags, and Discord treats them as free text.

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| 2026-10-30 | A module of its own, `emotes` | It shares nothing with linkstats' emoji copies but the CDN address, and linkstats' are the bot's own, not a server's |
| 2026-10-31 | Adds only to servers in `trusted_guilds`, for anyone who can run the command | The owner runs every server the bot is in and trusts its users; the first version asked for the person's own permission in each, which got in the way of friends filling the spare emote servers |
| 2026-10-31 | Stickers share the emote command and panel | The same steps apply, and a message can hold both |
| 2026-10-31 | Sticker uploads don't use DPP's `guild_sticker_create` | It sends `payload_json`, not the form fields Discord documents |
| 2026-10-30 | Everyone can run the command | Who may add is settled by the server being listed |
| 2026-10-30 | One emote a page | Each has its own name and server to choose, and a message has room for only one menu of servers per emote |
| 2026-10-30 | Requests in memory, for 15 idle minutes | As Fire a trigger's. The state will not fit in a `custom_id`, and nothing here needs to outlast a restart |
| 2026-10-30 | Full servers left out of the menu and counted | A menu option cannot be disabled, and one that can only fail is worse than a line saying why it is gone |

## 5. Limits, and what is still to check

- A menu holds 25 options, so with room in more than 25 servers it shows the
  first 25 by name, and a line saying so.
- A sticker's tags and description aren't copied, since the message doesn't carry
  them: its name becomes its tag, and the description stays empty.
- DPP does not read Discord's `MORE_EMOJI` server feature, so a server with it is
  treated as having its boost level's slots: 50 at level 0, where it really has
  200. It stops being offered early rather than failing.
- Opening the panel on a message with a sticker makes one REST call per server
  the bot can add to.
- Sticker counts in the menu are from when the panel opened, plus what it added.
- **Still to check in Discord:** the Apps menu entry, a real upload of a still and
  an animated emote, of a PNG, APNG and GIF sticker (that an APNG stays animated
  especially), and the panel's edit after **Adding…**.
