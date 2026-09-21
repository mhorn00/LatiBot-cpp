#pragma once

#include <chrono>
#include <filesystem>
#include <string_view>
#include <vector>

namespace latibot::db {

class database;

/// Copies a live database to `destination` with SQLite's online backup API,
/// which produces a consistent copy while the bot keeps running
/// (plan v4 §5.2).
///
/// The source connection is locked for the duration, so nothing else writes
/// to it mid-copy. An existing destination file is replaced.
void backup_to_file(database& source, const std::filesystem::path& destination);

/// Backups in `directory` named `<prefix>-YYYYMMDD-HHMMSS.db`, oldest first.
[[nodiscard]] std::vector<std::filesystem::path> list_backups(
    const std::filesystem::path& directory, std::string_view prefix);

/// Deletes all but the `keep` newest backups. Returns how many were removed.
int rotate_backups(const std::filesystem::path& directory, std::string_view prefix, int keep);

/// Writes a timestamped backup into `directory` and rotates it, keeping the
/// `keep` newest. The timestamp is UTC and is a parameter so tests are
/// deterministic.
std::filesystem::path create_backup(
    database& source, const std::filesystem::path& directory, std::string_view prefix, int keep,
    std::chrono::system_clock::time_point at = std::chrono::system_clock::now());

} // namespace latibot::db
