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

constexpr auto schema_of(std::string_view module, std::span<const migration> steps) noexcept -> module_schema {
    return {.module = module, .steps = steps};
}

constexpr std::array<module_schema, 2> builtin{{
    schema_of("core", core_steps),
    schema_of("dectalk", dectalk_steps),
}};

} // namespace

auto core_schema() noexcept -> module_schema {
    return builtin[0];
}
auto dectalk_schema() noexcept -> module_schema {
    return builtin[1];
}
auto builtin_schemas() noexcept -> std::span<const module_schema> {
    return builtin;
}

} // namespace latibot::db
