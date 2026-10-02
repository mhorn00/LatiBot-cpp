#include "core/db/migrations.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"
#include "core/util/log.hpp"

#include <sqlite3.h>

#include <array>
#include <string>

namespace latibot::db {
namespace {

// Append only. Never edit a migration that has shipped.
constexpr std::array<migration, 15> all_migrations{{
    {.version = 1, .name = "guild_settings", .sql = R"sql(
        CREATE TABLE guild_settings (
            guild_id INTEGER NOT NULL,
            key      TEXT    NOT NULL,
            value    TEXT    NOT NULL,
            PRIMARY KEY (guild_id, key)
        ) WITHOUT ROWID;
     )sql"},
    {.version = 2, .name = "triggers", .sql = R"sql(
        CREATE TABLE triggers (
            id         INTEGER PRIMARY KEY,
            guild_id   INTEGER NOT NULL,
            pattern    TEXT    NOT NULL,
            match_mode TEXT    NOT NULL,
            cooldown_s INTEGER NOT NULL,
            enabled    INTEGER NOT NULL
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
    {.version = 3, .name = "bot_allowlist", .sql = R"sql(
        -- Which other bots this server lets LatiBot hear (plan v4 5.4, 14.4).
        -- Empty by default: every bot is ignored until someone says otherwise,
        -- because two bots answering each other is a loop nobody asked for.
        CREATE TABLE allowed_bots (
            guild_id INTEGER NOT NULL,
            bot_id   INTEGER NOT NULL,
            PRIMARY KEY (guild_id, bot_id)
        ) WITHOUT ROWID;

        -- Hearing a bot is not the same as answering it, so each trigger opts
        -- in separately. Existing triggers keep the old behaviour.
        ALTER TABLE triggers ADD COLUMN respond_to_bots INTEGER NOT NULL DEFAULT 0;
     )sql"},
    {.version = 4, .name = "nickname_history", .sql = R"sql(
        -- Every nickname a member has had here, however the change was made
        -- (plan v4 8). Ids are stored raw and names resolved at display time,
        -- so a member who has left still has a readable history.
        CREATE TABLE nickname_history (
            id           INTEGER PRIMARY KEY,
            guild_id     INTEGER NOT NULL,
            user_id      INTEGER NOT NULL,

            -- NULL means the nickname was cleared, which is not the same as "".
            nickname     TEXT,

            -- Unix seconds. Compared against dates, so it is wall clock.
            changed_at   INTEGER NOT NULL,

            -- NULL means nobody could be named. The Java bot guessed "they did
            -- it themselves", which was usually wrong (plan v4 8.1).
            changed_by   INTEGER,

            -- command | audit_log | seen | startup | imported: how far the
            -- attribution above can be trusted.
            source       TEXT    NOT NULL,

            -- The original timestamp text from nicknames.json, so the timezone
            -- conversion can be redone (plan v4 8.3).
            imported_raw TEXT
        );

        -- Every read is "this member, newest first"; the partial index is for
        -- the audit log looking for a row it can still attribute.
        CREATE INDEX nickname_history_by_member ON nickname_history (guild_id, user_id, changed_at DESC);
        CREATE INDEX nickname_history_unattributed ON nickname_history (guild_id, user_id, changed_at)
            WHERE changed_by IS NULL;
     )sql"},
    {.version = 5, .name = "midnight_messages", .sql = R"sql(
        -- A message posted once per local day, per timezone (plan v4 10).
        CREATE TABLE midnight_messages (
            id              INTEGER PRIMARY KEY,
            guild_id        INTEGER NOT NULL,
            channel_id      INTEGER NOT NULL,

            -- An IANA name. Different entries in one guild may use different
            -- zones, which is the point of there being any number of them.
            timezone        TEXT    NOT NULL,

            message         TEXT    NOT NULL,
            enabled         INTEGER NOT NULL DEFAULT 1,

            -- The local date this last posted, YYYY-MM-DD, NULL for never.
            -- Saved rather than counted from, so a restart at 00:00:30 does
            -- not post a second time.
            last_fired_date TEXT
        );

        CREATE INDEX midnight_messages_by_guild ON midnight_messages (guild_id);
     )sql"},
    {.version = 6, .name = "url_replacement", .sql = R"sql(
        -- Where links to a site go instead, in the order to try them
        -- (plan v4 9). A rule is its rows for one domain; position 0 is first.
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

        -- Members who asked for their links to be left alone. Per guild, and
        -- kept: the Java /toggle forgot on every restart.
        CREATE TABLE url_opt_outs (
            guild_id INTEGER NOT NULL,
            user_id  INTEGER NOT NULL,
            PRIMARY KEY (guild_id, user_id)
        ) WITHOUT ROWID;

        -- Every mirror a rule has ever used, and never pruned: the backfill
        -- recognises the bot's old messages by these hosts, and they do not
        -- stop existing when a rule changes (plan v4 9.7).
        CREATE TABLE known_mirrors (
            guild_id INTEGER NOT NULL,
            host     TEXT    NOT NULL,
            domain   TEXT    NOT NULL,
            PRIMARY KEY (guild_id, host)
        ) WITHOUT ROWID;

        -- One row per message the bot posted in place of somebody's links.
        -- Reaction statistics hang off it, so rows are kept after the message
        -- is gone (plan v4 9.6).
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
            retried_at          INTEGER
        );

        CREATE INDEX replacement_messages_by_author ON replacement_messages (guild_id, original_author_id);

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
    {.version = 7, .name = "reaction_stats", .sql = R"sql(
        -- Who reacted with what on a replacement message (plan v4 9.6). The
        -- poster comes from replacement_messages, so one row answers both
        -- "who received" and "who gave". Kept forever.
        CREATE TABLE reactions (
            message_id INTEGER NOT NULL REFERENCES replacement_messages (message_id),
            user_id    INTEGER NOT NULL,

            -- u:<unicode> or c:<custom emoji id>
            emoji_key  TEXT    NOT NULL,

            -- Unix seconds, NULL when backfilled: Discord says who reacted,
            -- never when (plan v4 9.7).
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
     )sql"},
    {.version = 8, .name = "backfill_progress", .sql = R"sql(
        -- How far a /linkstats recompute got in each channel, so one that was
        -- cancelled or cut short by a restart carries on rather than starting
        -- over (plan v4 9.7). A row belongs to one date range; a run over a
        -- different range starts that channel again.
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
     )sql"},
    {.version = 9, .name = "message_flags", .sql = R"sql(
        -- Whether a trigger's replies and a midnight message post silently, and
        -- whether with link previews, as Discord's message flags: 4096 is
        -- SUPPRESS_NOTIFICATIONS, 4 is SUPPRESS_EMBEDS. Both were always
        -- silent until now, so that is where existing rows start.
        ALTER TABLE triggers ADD COLUMN message_flags INTEGER NOT NULL DEFAULT 4096;
        ALTER TABLE midnight_messages ADD COLUMN message_flags INTEGER NOT NULL DEFAULT 4096;
     )sql"},
    {.version = 10, .name = "tts_voices", .sql = R"sql(
        -- Custom voices, per guild (plan 12.6): a built-in voice and the
        -- [:dv] edits made to it, as "ap 200 pr 150". Names are stored in
        -- lowercase and never match a built-in voice's.
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
    {.version = 11, .name = "llm", .sql = R"sql(
        -- Every call to a model and what it cost (plan 14.6). The spend caps
        -- are sums over this, so a restart does not reset them. The price is
        -- stored with each row rather than worked out when read, so a price
        -- change applies from then on.
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

        -- personality | system | trigger_style, one row per version (plan
        -- 14.5). Nothing is ever overwritten: a revert is a new version with
        -- the old text, so it can itself be reverted.
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

        -- What the model chose to remember (plan 14.5). subject_user_id is
        -- who it is about, NULL for the server in general; created_by is
        -- whom the model was answering when it wrote it.
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

        -- Who the model does not answer here: kind is user | role (plan 14.6).
        CREATE TABLE llm_blacklist (
            guild_id  INTEGER NOT NULL,
            kind      TEXT    NOT NULL,
            target_id INTEGER NOT NULL,
            PRIMARY KEY (guild_id, kind, target_id)
        ) WITHOUT ROWID;

        -- Advanced triggers (plan 14.3): a pattern, as the simple triggers
        -- match them, and a line telling the model what to say about it.
        -- probability is 0 to 1; the cooldown is per channel.
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
     )sql"},
    {.version = 12, .name = "media_posts", .sql = R"sql(
        -- What a row is: one of the bot's link replacements, or an image or
        -- video a person posted, whose reactions are counted too once a
        -- server turns that on (docs/features/Link_Stats.md 9). An image
        -- row is the person's own message: original_message_id is itself,
        -- original_author_id the poster, and it has no replacement_links.
        ALTER TABLE replacement_messages ADD COLUMN kind TEXT NOT NULL DEFAULT 'link';   -- link | image

        CREATE INDEX replacement_messages_by_kind ON replacement_messages (guild_id, kind);
     )sql"},
    {.version = 13, .name = "emoji_copies", .sql = R"sql(
        -- The bot's own copies of the custom emojis it has seen, as
        -- application emojis, so a statistic still shows an emote after its
        -- server deletes it (docs/features/Link_Stats.md 10).

        -- What became of each custom emoji's image.
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
     )sql"},
    {.version = 14, .name = "emote_reactions", .sql = R"sql(
        -- Emojis somebody sent as a message of their own just after a post,
        -- or as a reply to it, which count as reactions to it
        -- (docs/features/Link_Stats.md 12). Apart from reactions, which a
        -- recompute reads back from Discord's reaction lists; these only the
        -- messages say.
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
    {.version = 15, .name = "llm_aliases", .sql = R"sql(
        -- What the language model calls each person, in place of their
        -- Discord id and their name (docs/features/Language_Model.md 3.8).
        -- Random, one per person per server, and kept, so memories that
        -- name someone by alias still mean them later.
        CREATE TABLE llm_aliases (
            guild_id INTEGER NOT NULL,
            user_id  INTEGER NOT NULL,
            alias    TEXT    NOT NULL,

            -- What they were last seen called here, to put back into a
            -- reply when the bot's cache does not know them.
            name     TEXT    NOT NULL DEFAULT '',
            username TEXT    NOT NULL DEFAULT '',

            PRIMARY KEY (guild_id, user_id),
            UNIQUE (guild_id, alias)
        ) WITHOUT ROWID;
     )sql"},
}};

} // namespace

auto schema() noexcept -> std::span<const migration> {
    return all_migrations;
}

auto migrate(database& db, std::span<const migration> migrations) -> int {
    // One lock for the whole run, so a second thread cannot interleave.
    const auto guard = db.lock();

    int version = db.user_version();

    for (const migration& step : migrations) {
        if (step.version <= version) continue;
        if (step.version != version + 1) {
            throw db_error(SQLITE_ERROR, "migration " + std::to_string(step.version) + " (" + std::string(step.name) +
                                             ") does not follow version " + std::to_string(version));
        }

        transaction tx(db);
        db.execute(step.sql);
        db.set_user_version(step.version);
        tx.commit();

        // Info, not debug: a schema change is the one startup event worth
        // seeing in a log that was not turned up beforehand.
        util::log().info("applied migration {} ({})", step.version, step.name);
        version = step.version;
    }

    return version;
}

auto migrate(database& db) -> int {
    return migrate(db, schema());
}

} // namespace latibot::db
