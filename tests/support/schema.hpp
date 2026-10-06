#pragma once

#include "core/db/database.hpp"
#include "core/db/schema_versions.hpp"
#include "core/db/schemas.hpp"

namespace latibot::testing {

/// Gives `db` every table the bot has, as a new database gets them: each
/// module's schema from its version 1 (docs/modules/Module_Plan_Final.md §7).
/// Safe to call again on the same database, as the next start would.
inline auto create_schema(db::database& db) -> void {
    db::prepare_schema_versions(db);
    for (const db::module_schema& schema : db::builtin_schemas()) {
        db::apply_schema(db, schema);
    }
}

} // namespace latibot::testing
