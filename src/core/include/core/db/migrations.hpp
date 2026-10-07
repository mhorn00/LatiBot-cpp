#pragma once

#include <span>
#include <string_view>

namespace latibot::db {

class database;

/// One schema step: one of the old single list, or of a module's own
/// (core/db/schema_versions.hpp).
///
/// Steps are append-only: once a version has shipped, its SQL is never
/// edited, and a change becomes a new step (src/core/docs/Operations.md §5).
struct migration {
    int version;
    std::string_view name;
    std::string_view sql;
};

// The old single list, 1 to 15, which only adoption still runs, on a
// database from before modules. Remove after: you say so
// (docs/modules/Module_Plan_Final.md §7.2), with `schema`, both `migrate`s
// and the adoption step.

/// The old schema, in ascending version order.
[[nodiscard]] auto schema() noexcept -> std::span<const migration>;

/// Applies every migration newer than the database's `user_version`, each in
/// its own transaction, and returns the version afterwards.
///
/// A failing migration rolls back, leaving `user_version` at the last version
/// that applied cleanly, and rethrows.
auto migrate(database& db, std::span<const migration> migrations) -> int;

/// Migrates to the current schema.
auto migrate(database& db) -> int;

} // namespace latibot::db
