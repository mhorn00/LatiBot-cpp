#include "core/db/migrations.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"

#include <sqlite3.h>

#include <array>
#include <string>

namespace latibot::db {
namespace {

// Append only. Never edit a migration that has shipped.
constexpr std::array<migration, 3> all_migrations{{
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
}};

} // namespace

std::span<const migration> schema() noexcept {
    return all_migrations;
}

int migrate(database& db, std::span<const migration> migrations) {
    // One lock for the whole run, so a second thread cannot interleave.
    const auto guard = db.lock();

    int version = db.user_version();

    for (const migration& step : migrations) {
        if (step.version <= version) {
            continue;
        }
        if (step.version != version + 1) {
            throw db_error(SQLITE_ERROR, "migration " + std::to_string(step.version) + " (" + std::string(step.name) +
                                             ") does not follow version " + std::to_string(version));
        }

        transaction tx(db);
        db.execute(step.sql);
        db.set_user_version(step.version);
        tx.commit();

        version = step.version;
    }

    return version;
}

int migrate(database& db) {
    return migrate(db, schema());
}

} // namespace latibot::db
