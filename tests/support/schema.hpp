#pragma once

#include "core/db/database.hpp"
#include "core/db/schema_versions.hpp"
#include "core/db/schemas.hpp"

#include <vector>

namespace latibot::testing {

/// The core's schema and those of the features still in it. A module's own
/// tests add its schema; tests/app has every module's.
inline auto all_schemas() -> std::vector<db::module_schema> {
    return {db::builtin_schemas().begin(), db::builtin_schemas().end()};
}

/// Gives `db` only what the bot itself creates before any module is built:
/// the core's tables and those of the features still in it.
inline auto create_builtin_schema(db::database& db) -> void {
    db::prepare_schema_versions(db);
    for (const db::module_schema& schema : db::builtin_schemas()) {
        db::apply_schema(db, schema);
    }
}

/// Gives `db` the core's tables and those of the features still in it, as a
/// new database gets them (docs/modules/Module_Plan_Final.md §7). Safe to
/// call again on the same database, as the next start would.
inline auto create_schema(db::database& db) -> void {
    db::prepare_schema_versions(db);
    for (const db::module_schema& schema : all_schemas()) {
        db::apply_schema(db, schema);
    }
}

} // namespace latibot::testing
