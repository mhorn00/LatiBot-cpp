#pragma once

#include <chrono>
#include <filesystem>
#include <string_view>
#include <vector>

namespace latibot::db {

class database;

/// Copies a live database to `destination` with SQLite's online backup API,
/// which produces a consistent copy while the bot keeps running
/// (plan §5.2).
///
/// The source connection is locked for the duration, so nothing else writes
/// to it mid-copy. An existing destination file is replaced.
auto backup_to_file(database& source, const std::filesystem::path& destination) -> void;

/// Backups in `directory` named `<prefix>-YYYYMMDD-HHMMSS.db`, oldest first.
[[nodiscard]] auto list_backups(const std::filesystem::path& directory, std::string_view prefix) -> std::vector<std::filesystem::path>;

/// Deletes all but the `keep` newest backups. Returns how many were removed.
auto rotate_backups(const std::filesystem::path& directory, std::string_view prefix, int keep) -> int;

/// Writes a timestamped backup into `directory` and rotates it, keeping the
/// `keep` newest. The timestamp is UTC and is a parameter so tests are
/// deterministic.
auto create_backup(database& source, const std::filesystem::path& directory, std::string_view prefix, int keep,
                   std::chrono::system_clock::time_point at = std::chrono::system_clock::now()) -> std::filesystem::path;

} // namespace latibot::db
