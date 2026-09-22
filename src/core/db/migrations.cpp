#include "core/db/migrations.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"

#include <sqlite3.h>

#include <array>
#include <string>

namespace latibot::db {
namespace {

// Append only. Never edit a migration that has shipped.
constexpr std::array<migration, 1> all_migrations{{
    {.version = 1, .name = "guild_settings", .sql = R"sql(
        CREATE TABLE guild_settings (
            guild_id INTEGER NOT NULL,
            key      TEXT    NOT NULL,
            value    TEXT    NOT NULL,
            PRIMARY KEY (guild_id, key)
        ) WITHOUT ROWID;
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
