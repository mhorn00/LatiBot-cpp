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
constexpr std::array<migration, 5> all_migrations{{
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

        // Info, not debug: a schema change is the one startup event worth
        // seeing in a log that was not turned up beforehand.
        util::log().info("applied migration {} ({})", step.version, step.name);
        version = step.version;
    }

    return version;
}

int migrate(database& db) {
    return migrate(db, schema());
}

} // namespace latibot::db
