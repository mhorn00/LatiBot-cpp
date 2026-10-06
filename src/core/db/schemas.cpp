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

constexpr std::array<module_schema, 3> builtin{{
    schema_of("core", core_steps),
    schema_of("dectalk", dectalk_steps),
    schema_of("llm", llm_steps),
}};

} // namespace

auto core_schema() noexcept -> module_schema {
    return builtin[0];
}
auto dectalk_schema() noexcept -> module_schema {
    return builtin[1];
}
auto llm_schema() noexcept -> module_schema {
    return builtin[2];
}

auto builtin_schemas() noexcept -> std::span<const module_schema> {
    return builtin;
}

} // namespace latibot::db
