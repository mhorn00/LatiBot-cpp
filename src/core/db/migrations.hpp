#pragma once

#include <span>
#include <string_view>

namespace latibot::db {

class database;

/// One schema step.
///
/// Migrations are append-only: once a version has shipped, its SQL is never
/// edited, and a change becomes a new migration (plan v4 §5.2).
struct migration {
    int version;
    std::string_view name;
    std::string_view sql;
};

/// The schema, in ascending version order.
[[nodiscard]] std::span<const migration> schema() noexcept;

/// Applies every migration newer than the database's `user_version`, each in
/// its own transaction, and returns the version afterwards.
///
/// A failing migration rolls back, leaving `user_version` at the last version
/// that applied cleanly, and rethrows.
int migrate(database& db, std::span<const migration> migrations);

/// Migrates to the current schema.
int migrate(database& db);

} // namespace latibot::db
