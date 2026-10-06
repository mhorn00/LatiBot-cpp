#include "core/db/schema_versions.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"
#include "core/db/statement.hpp"
#include "core/util/log.hpp"

#include <sqlite3.h>

#include <array>
#include <format>

namespace latibot::db {
namespace {

/// The last of the old, single-list migrations. Remove after: you say so.
constexpr int last_legacy_version = 15;

constexpr std::array<std::string_view, 8> legacy_modules{"core",  "triggers",  "nicknames", "midnight",
                                                         "links", "linkstats", "dectalk",   "llm"};

constexpr std::string_view create_schema_versions = R"sql(
    CREATE TABLE schema_versions (
        module  TEXT    PRIMARY KEY,
        version INTEGER NOT NULL
    ) WITHOUT ROWID;
)sql";

auto has_table(database& db, std::string_view name) -> bool {
    auto query = db.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?", name);
    return query.step();
}

auto is_empty(database& db) -> bool {
    auto query = db.prepare("SELECT count(*) FROM sqlite_master");
    return query.step() && query.get<std::int64_t>(0) == 0;
}

auto recorded_version(database& db, std::string_view module) -> int {
    auto query = db.prepare("SELECT version FROM schema_versions WHERE module = ?", module);
    return query.step() ? query.get<int>(0) : 0;
}

auto record_version(database& db, std::string_view module, int version) -> void {
    db.prepare(
          "INSERT INTO schema_versions (module, version) VALUES (?, ?) "
          "ON CONFLICT (module) DO UPDATE SET version = excluded.version",
          module, version)
        .run();
}

} // namespace

auto adopted_modules() noexcept -> std::span<const std::string_view> {
    return legacy_modules;
}

auto prepare_schema_versions(database& db) -> schema_origin {
    const auto guard = db.lock();
    if (has_table(db, "schema_versions")) return schema_origin::existing;

    const int legacy = db.user_version();
    if (legacy == 0 && is_empty(db)) {
        db.execute(create_schema_versions);
        util::log().info("a new database: each module creates its tables");
        return schema_origin::fresh;
    }

    // Remove after: you say so (docs/modules/Module_Plan_Final.md §7.2). The
    // old migrations, unedited, then every module of the time at version 1.
    if (legacy >= 1 && legacy <= last_legacy_version) {
        migrate(db);
        transaction tx(db);
        db.execute(create_schema_versions);
        for (const std::string_view module : legacy_modules) {
            record_version(db, module, 1);
        }
        tx.commit();
        util::log().info("the database was at schema version {}; each module now keeps its own version, starting at 1", legacy);
        return schema_origin::adopted;
    }

    throw db_error(SQLITE_ERROR, legacy == 0 ? "the database has tables but no schema_versions; it was not written by LatiBot"
                                             : std::format("the database is at user_version {} with no schema_versions; it was not "
                                                           "written by LatiBot",
                                                           legacy));
}

auto apply_schema(database& db, const module_schema& schema) -> int {
    // One lock for the whole run, so a second thread cannot interleave.
    const auto guard = db.lock();

    int version = recorded_version(db, schema.module);
    for (const migration& step : schema.steps) {
        if (step.version <= version) continue;
        if (step.version != version + 1) {
            throw db_error(SQLITE_ERROR, std::format("{} schema step {} ({}) does not follow version {}", schema.module, step.version,
                                                     step.name, version));
        }

        transaction tx(db);
        db.execute(step.sql);
        record_version(db, schema.module, step.version);
        tx.commit();

        // Info, not debug: a schema change is the one startup event worth
        // seeing in a log that was not turned up beforehand.
        util::log().info("applied {} schema {} ({})", schema.module, step.version, step.name);
        version = step.version;
    }
    return version;
}

auto recorded_versions(database& db) -> std::vector<std::pair<std::string, int>> {
    std::vector<std::pair<std::string, int>> versions;
    auto query = db.prepare("SELECT module, version FROM schema_versions ORDER BY module");
    while (query.step()) {
        versions.emplace_back(query.get<std::string>(0), query.get<int>(1));
    }
    return versions;
}

} // namespace latibot::db
