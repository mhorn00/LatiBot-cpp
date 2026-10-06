#pragma once

#include "core/db/migrations.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::db {

class database;

/// One module's schema: its steps, version 1 first, recorded under its name
/// in `schema_versions` (docs/modules/Module_Plan_Final.md §7). The core's is
/// recorded as "core".
struct module_schema {
    std::string_view module;
    std::span<const migration> steps;
};

/// How `prepare_schema_versions` found the database.
enum class schema_origin : std::uint8_t {
    /// Already had `schema_versions`.
    existing,
    /// Empty: `schema_versions` was created, and every module creates its
    /// tables from its version 1.
    fresh,
    /// Written by a bot from before modules, with `user_version` 1 to 15. The
    /// old migrations brought it to 15, and every module that existed then
    /// was recorded at version 1, which is what 15 is.
    adopted,
};

/// Gets the database ready for per-module schemas (§7.2). Throws `db_error`
/// for a database this bot did not write: tables but no versions, or a
/// `user_version` above 15.
auto prepare_schema_versions(database& db) -> schema_origin;

/// Applies `schema`'s steps above the version recorded for its module, each
/// in its own transaction with the new version, and returns the version
/// afterwards. A module with no row starts at 0, so its tables are created.
/// A failing step rolls back and rethrows, leaving the last version that
/// applied.
auto apply_schema(database& db, const module_schema& schema) -> int;

/// Every module's recorded version, by name.
[[nodiscard]] auto recorded_versions(database& db) -> std::vector<std::pair<std::string, int>>;

/// The modules that existed when the schema was one list, each at version 1
/// since migration 15. Adoption records all of them, built or not, so a
/// module built in later does not try to create tables that are already
/// there. Remove after: you say so (§7.2), with the old migrations.
[[nodiscard]] auto adopted_modules() noexcept -> std::span<const std::string_view>;

} // namespace latibot::db
