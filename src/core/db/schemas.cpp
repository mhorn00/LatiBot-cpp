#include "core/db/schemas.hpp"

#include <array>

namespace latibot::db {
namespace {

// Append only, as the old migrations were: once a version has shipped, its
// SQL is never edited, and a change becomes the module's next version.

constexpr std::array<migration, 1> core_steps{{
    {.version = 1, .name = "guild_settings and allowed_bots", .sql = R"sql(
        CREATE TABLE guild_settings (
            guild_id INTEGER NOT NULL,
            key      TEXT    NOT NULL,
            value    TEXT    NOT NULL,
            PRIMARY KEY (guild_id, key)
        ) WITHOUT ROWID;

        -- Which other bots this server lets LatiBot hear. Empty by default:
        -- every bot is ignored until someone says otherwise, because two bots
        -- answering each other is a loop nobody asked for.
        CREATE TABLE allowed_bots (
            guild_id INTEGER NOT NULL,
            bot_id   INTEGER NOT NULL,
            PRIMARY KEY (guild_id, bot_id)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr std::array<migration, 1> triggers_steps{{
    {.version = 1, .name = "triggers", .sql = R"sql(
        CREATE TABLE triggers (
            id              INTEGER PRIMARY KEY,
            guild_id        INTEGER NOT NULL,
            pattern         TEXT    NOT NULL,
            match_mode      TEXT    NOT NULL,
            cooldown_s      INTEGER NOT NULL,
            enabled         INTEGER NOT NULL,

            -- Hearing a bot is not the same as answering it, so each trigger
            -- opts in.
            respond_to_bots INTEGER NOT NULL DEFAULT 0,

            -- How its replies are posted, as Discord's message flags: 4096 is
            -- SUPPRESS_NOTIFICATIONS, 4 is SUPPRESS_EMBEDS.
            message_flags   INTEGER NOT NULL DEFAULT 4096
        );

        CREATE INDEX triggers_by_guild ON triggers (guild_id);

        -- Rows rather than a list column, so each response can carry its own
        -- weight and be edited on its own. rowid order is the order they were
        -- added, which is the order the command and the panel show.
        CREATE TABLE trigger_responses (
            trigger_id INTEGER NOT NULL REFERENCES triggers (id) ON DELETE CASCADE,
            response   TEXT    NOT NULL,
            weight     INTEGER NOT NULL DEFAULT 1
        );

        CREATE INDEX trigger_responses_by_trigger ON trigger_responses (trigger_id);
     )sql"},
}};

constexpr std::array<migration, 1> nicknames_steps{{
    {.version = 1, .name = "nickname_history", .sql = R"sql(
        -- Every nickname a member has had here, however the change was made.
        -- Ids are stored raw and names resolved at display time, so a member
        -- who has left still has a readable history.
        CREATE TABLE nickname_history (
            id           INTEGER PRIMARY KEY,
            guild_id     INTEGER NOT NULL,
            user_id      INTEGER NOT NULL,

            -- NULL means the nickname was cleared, which is not the same as "".
            nickname     TEXT,

            -- Unix seconds. Compared against dates, so it is wall clock.
            changed_at   INTEGER NOT NULL,

            -- NULL means nobody could be named.
            changed_by   INTEGER,

            -- command | audit_log | seen | startup | imported: how far the
            -- attribution above can be trusted.
            source       TEXT    NOT NULL,

            -- The original timestamp text from nicknames.json, so the timezone
            -- conversion can be redone.
            imported_raw TEXT
        );

        -- Every read is "this member, newest first"; the partial index is for
        -- the audit log looking for a row it can still attribute.
        CREATE INDEX nickname_history_by_member ON nickname_history (guild_id, user_id, changed_at DESC);
        CREATE INDEX nickname_history_unattributed ON nickname_history (guild_id, user_id, changed_at)
            WHERE changed_by IS NULL;
     )sql"},
}};

constexpr std::array<migration, 1> links_steps{{
    {.version = 1, .name = "url rules and replacements", .sql = R"sql(
        -- Where links to a site go instead, in the order to try them. A rule
        -- is its rows for one domain; position 0 is first.
        CREATE TABLE url_rules (
            guild_id         INTEGER NOT NULL,

            -- Lowercase, without "www.": the host a link is looked up by.
            domain           TEXT    NOT NULL,
            position         INTEGER NOT NULL,
            host             TEXT    NOT NULL,

            -- "/en" for a mirror that translates when asked; NULL for none.
            translate_suffix TEXT,

            PRIMARY KEY (guild_id, domain, position)
        ) WITHOUT ROWID;

        -- Members who asked for their links to be left alone, per guild.
        CREATE TABLE url_opt_outs (
            guild_id INTEGER NOT NULL,
            user_id  INTEGER NOT NULL,
            PRIMARY KEY (guild_id, user_id)
        ) WITHOUT ROWID;

        -- Every mirror a rule has ever used, and never pruned: the recompute
        -- recognises the bot's old messages by these hosts, and they do not
        -- stop existing when a rule changes.
        CREATE TABLE known_mirrors (
            guild_id INTEGER NOT NULL,
            host     TEXT    NOT NULL,
            domain   TEXT    NOT NULL,
            PRIMARY KEY (guild_id, host)
        ) WITHOUT ROWID;

        -- One row per message the bot posted in place of somebody's links,
        -- or, with kind 'image', a person's own image or video whose
        -- reactions linkstats counts. Reaction statistics hang off it, so
        -- rows are kept after the message is gone.
        CREATE TABLE replacement_messages (
            message_id          INTEGER PRIMARY KEY,
            guild_id            INTEGER NOT NULL,
            channel_id          INTEGER NOT NULL,

            -- NULL for an old message whose original could not be found.
            original_message_id INTEGER,
            original_author_id  INTEGER,

            -- pending | ok | failed | retrying
            state               TEXT    NOT NULL,

            -- Unix seconds.
            created_at          INTEGER NOT NULL,
            retried_at          INTEGER,

            -- link | image
            kind                TEXT    NOT NULL DEFAULT 'link'
        );

        CREATE INDEX replacement_messages_by_author ON replacement_messages (guild_id, original_author_id);
        CREATE INDEX replacement_messages_by_kind ON replacement_messages (guild_id, kind);

        -- The links in one replacement, so Retry still knows what to try after
        -- a restart. Mirrors are not stored: Retry uses the rule as it is now.
        CREATE TABLE replacement_links (
            message_id   INTEGER NOT NULL REFERENCES replacement_messages (message_id) ON DELETE CASCADE,
            position     INTEGER NOT NULL,
            original_url TEXT    NOT NULL,
            domain       TEXT    NOT NULL,
            spoilered    INTEGER NOT NULL,
            PRIMARY KEY (message_id, position)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr std::array<migration, 1> linkstats_steps{{
    {.version = 1, .name = "reaction statistics", .sql = R"sql(
        -- Who reacted with what on a replacement message. The poster comes
        -- from replacement_messages, so one row answers both "who received"
        -- and "who gave". Kept forever.
        CREATE TABLE reactions (
            message_id INTEGER NOT NULL REFERENCES replacement_messages (message_id),
            user_id    INTEGER NOT NULL,

            -- u:<unicode> or c:<custom emoji id>
            emoji_key  TEXT    NOT NULL,

            -- Unix seconds, NULL when recomputed: Discord says who reacted,
            -- never when.
            reacted_at INTEGER,

            PRIMARY KEY (message_id, user_id, emoji_key)
        ) WITHOUT ROWID;

        CREATE INDEX reactions_by_user ON reactions (user_id);

        -- Every add and remove seen live, for questions nobody has asked yet.
        CREATE TABLE reaction_log (
            id         INTEGER PRIMARY KEY,
            message_id INTEGER NOT NULL,
            user_id    INTEGER NOT NULL,
            emoji_key  TEXT    NOT NULL,
            action     TEXT    NOT NULL,   -- add | remove
            at         INTEGER NOT NULL
        );

        -- What a key looks like, for showing it: a custom emoji is only an id
        -- in the rows above.
        CREATE TABLE emojis (
            emoji_key TEXT    PRIMARY KEY,
            name      TEXT    NOT NULL,
            animated  INTEGER NOT NULL DEFAULT 0
        ) WITHOUT ROWID;

        -- Emojis that should count as one: the same emote from another server,
        -- or one deleted and re-added. Applied when stats are read, so adding
        -- or removing one changes all of history at once.
        CREATE TABLE emoji_aliases (
            guild_id      INTEGER NOT NULL,
            emoji_key     TEXT    NOT NULL,
            canonical_key TEXT    NOT NULL,
            PRIMARY KEY (guild_id, emoji_key)
        ) WITHOUT ROWID;

        -- How far a /linkstats recompute got in each channel, so one that was
        -- cancelled or cut short by a restart carries on rather than starting
        -- over. A row belongs to one date range; a run over a different range
        -- starts that channel again.
        CREATE TABLE backfill_progress (
            guild_id          INTEGER NOT NULL,
            channel_id        INTEGER NOT NULL,

            -- The range, Unix seconds; until is NULL for "up to now".
            since             INTEGER NOT NULL,
            until             INTEGER,

            -- The oldest message looked at so far; NULL before the first page.
            oldest_scanned_id INTEGER,
            complete          INTEGER NOT NULL DEFAULT 0,
            updated_at        INTEGER NOT NULL,

            PRIMARY KEY (guild_id, channel_id)
        ) WITHOUT ROWID;

        -- What became of each custom emoji's image, for the bot's own copies
        -- of them (docs/features/Link_Stats.md 10).
        CREATE TABLE emoji_images (
            emoji_key    TEXT    PRIMARY KEY,   -- c:<id>

            -- fetched | lost (the CDN no longer has it) | too_big | failed
            state        TEXT    NOT NULL,

            -- SHA-256 of the image, hex; NULL unless fetched. Emojis with
            -- the same image share one copy.
            image_sha256 TEXT,
            animated     INTEGER NOT NULL DEFAULT 0,

            -- Unix seconds: when it was last tried, for retrying the lost
            -- and failed ones now and then.
            checked_at   INTEGER NOT NULL
        ) WITHOUT ROWID;

        CREATE INDEX emoji_images_by_image ON emoji_images (image_sha256);

        -- One application emoji per distinct image.
        CREATE TABLE emoji_copies (
            image_sha256 TEXT    PRIMARY KEY,
            copy_id      INTEGER NOT NULL,
            name         TEXT    NOT NULL UNIQUE,
            animated     INTEGER NOT NULL,
            created_at   INTEGER NOT NULL
        ) WITHOUT ROWID;

        -- Emojis somebody sent as a message of their own just after a post,
        -- or as a reply to it, which count as reactions to it
        -- (docs/features/Link_Stats.md 12).
        CREATE TABLE emote_reactions (
            message_id INTEGER NOT NULL REFERENCES replacement_messages (message_id),
            user_id    INTEGER NOT NULL,
            emoji_key  TEXT    NOT NULL,

            -- The message the emoji was sent in, which also dates it.
            source_id  INTEGER NOT NULL,

            PRIMARY KEY (message_id, user_id, emoji_key)
        ) WITHOUT ROWID;

        CREATE INDEX emote_reactions_by_source ON emote_reactions (source_id);

        -- What every statistic counts: the reactions, and the emotes sent as
        -- reactions that the same person did not also react with, so one
        -- person's emoji on one post counts once however it was given. An
        -- emote is dated by its message: a snowflake's top bits are
        -- milliseconds since 2015, Discord's epoch.
        CREATE VIEW counted_reactions AS
            SELECT message_id, user_id, emoji_key, reacted_at FROM reactions
            UNION ALL
            SELECT e.message_id, e.user_id, e.emoji_key, ((e.source_id >> 22) + 1420070400000) / 1000 FROM emote_reactions e
            WHERE NOT EXISTS (SELECT 1 FROM reactions r
                              WHERE r.message_id = e.message_id AND r.user_id = e.user_id AND r.emoji_key = e.emoji_key);
     )sql"},
}};

constexpr std::array<migration, 1> dectalk_steps{{
    {.version = 1, .name = "tts_voices", .sql = R"sql(
        -- Custom voices, per guild: a built-in voice and the [:dv] edits made
        -- to it, as "ap 200 pr 150". Names are stored in lowercase and never
        -- match a built-in voice's.
        CREATE TABLE tts_voices (
            guild_id   INTEGER NOT NULL,
            name       TEXT    NOT NULL,
            base_voice TEXT    NOT NULL,
            params     TEXT    NOT NULL,
            created_by INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            PRIMARY KEY (guild_id, name)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr std::array<migration, 1> llm_steps{{
    {.version = 1, .name = "the language model", .sql = R"sql(
        -- Every call to a model and what it cost. The spend caps are sums
        -- over this, so a restart does not reset them. The price is stored
        -- with each row rather than worked out when read, so a price change
        -- applies from then on.
        CREATE TABLE llm_usage (
            id                 INTEGER PRIMARY KEY,
            guild_id           INTEGER NOT NULL,
            model              TEXT    NOT NULL,
            input_tokens       INTEGER NOT NULL,
            output_tokens      INTEGER NOT NULL,
            cache_write_tokens INTEGER NOT NULL,
            cache_read_tokens  INTEGER NOT NULL,
            cost_usd           REAL    NOT NULL,
            at                 INTEGER NOT NULL      -- Unix seconds
        );

        CREATE INDEX llm_usage_by_time ON llm_usage (at);

        -- personality | system | trigger_style, one row per version. Nothing
        -- is ever overwritten: a revert is a new version with the old text,
        -- so it can itself be reverted.
        CREATE TABLE llm_documents (
            guild_id  INTEGER NOT NULL,
            kind      TEXT    NOT NULL,
            version   INTEGER NOT NULL,
            content   TEXT    NOT NULL,
            edited_by INTEGER NOT NULL,
            edited_at INTEGER NOT NULL,
            note      TEXT,
            PRIMARY KEY (guild_id, kind, version)
        ) WITHOUT ROWID;

        -- What the model chose to remember. subject_user_id is who it is
        -- about, NULL for the server in general; created_by is whom the model
        -- was answering when it wrote it.
        CREATE TABLE llm_memory (
            id              INTEGER PRIMARY KEY,
            guild_id        INTEGER NOT NULL,
            subject_user_id INTEGER,
            content         TEXT    NOT NULL,
            created_by      INTEGER,
            created_at      INTEGER NOT NULL
        );

        CREATE INDEX llm_memory_by_subject ON llm_memory (guild_id, subject_user_id);

        -- Full-text search over it, kept in step by the triggers below.
        CREATE VIRTUAL TABLE llm_memory_search USING fts5 (content, content = 'llm_memory', content_rowid = 'id');

        CREATE TRIGGER llm_memory_added AFTER INSERT ON llm_memory BEGIN
            INSERT INTO llm_memory_search (rowid, content) VALUES (new.id, new.content);
        END;
        CREATE TRIGGER llm_memory_removed AFTER DELETE ON llm_memory BEGIN
            INSERT INTO llm_memory_search (llm_memory_search, rowid, content) VALUES ('delete', old.id, old.content);
        END;
        CREATE TRIGGER llm_memory_changed AFTER UPDATE ON llm_memory BEGIN
            INSERT INTO llm_memory_search (llm_memory_search, rowid, content) VALUES ('delete', old.id, old.content);
            INSERT INTO llm_memory_search (rowid, content) VALUES (new.id, new.content);
        END;

        -- Who the model does not answer here: kind is user | role.
        CREATE TABLE llm_blacklist (
            guild_id  INTEGER NOT NULL,
            kind      TEXT    NOT NULL,
            target_id INTEGER NOT NULL,
            PRIMARY KEY (guild_id, kind, target_id)
        ) WITHOUT ROWID;

        -- Advanced triggers: a pattern, as the simple triggers match them,
        -- and a line telling the model what to say about it. probability is
        -- 0 to 1; the cooldown is per channel.
        CREATE TABLE llm_triggers (
            id             INTEGER PRIMARY KEY,
            guild_id       INTEGER NOT NULL,
            pattern        TEXT    NOT NULL,
            match_mode     TEXT    NOT NULL,
            context_prompt TEXT    NOT NULL,
            probability    REAL    NOT NULL,
            cooldown_s     INTEGER NOT NULL,
            enabled        INTEGER NOT NULL DEFAULT 1,
            created_by     INTEGER NOT NULL
        );

        CREATE INDEX llm_triggers_by_guild ON llm_triggers (guild_id);

        -- What the language model calls each person, in place of their
        -- Discord id and their name (docs/features/Language_Model.md 3.8).
        -- Random, one per person per server, and kept, so memories that name
        -- someone by alias still mean them later.
        CREATE TABLE llm_aliases (
            guild_id INTEGER NOT NULL,
            user_id  INTEGER NOT NULL,
            alias    TEXT    NOT NULL,

            -- What they were last seen called here, to put back into a reply
            -- when the bot's cache does not know them.
            name     TEXT    NOT NULL DEFAULT '',
            username TEXT    NOT NULL DEFAULT '',

            PRIMARY KEY (guild_id, user_id),
            UNIQUE (guild_id, alias)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr auto schema_of(std::string_view module, std::span<const migration> steps) noexcept -> module_schema {
    return {.module = module, .steps = steps};
}

// Each after the modules it requires: linkstats after links.
constexpr std::array<module_schema, 7> builtin{{
    schema_of("core", core_steps),
    schema_of("triggers", triggers_steps),
    schema_of("nicknames", nicknames_steps),
    schema_of("links", links_steps),
    schema_of("linkstats", linkstats_steps),
    schema_of("dectalk", dectalk_steps),
    schema_of("llm", llm_steps),
}};

} // namespace

auto core_schema() noexcept -> module_schema {
    return builtin[0];
}
auto triggers_schema() noexcept -> module_schema {
    return builtin[1];
}
auto nicknames_schema() noexcept -> module_schema {
    return builtin[2];
}
auto links_schema() noexcept -> module_schema {
    return builtin[3];
}
auto linkstats_schema() noexcept -> module_schema {
    return builtin[4];
}
auto dectalk_schema() noexcept -> module_schema {
    return builtin[5];
}
auto llm_schema() noexcept -> module_schema {
    return builtin[6];
}

auto builtin_schemas() noexcept -> std::span<const module_schema> {
    return builtin;
}

} // namespace latibot::db
