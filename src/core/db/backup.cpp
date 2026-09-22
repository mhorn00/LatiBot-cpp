#include "core/db/backup.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace latibot::db {
namespace {

std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string backup_file_name(std::string_view prefix, std::chrono::system_clock::time_point at) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(at);
    return std::format("{}-{:%Y%m%d-%H%M%S}.db", prefix, seconds);
}

} // namespace

void backup_to_file(database& source, const std::filesystem::path& destination) {
    // Hold the source lock for the whole copy: with a single connection this
    // is what makes the snapshot consistent.
    const auto guard = source.lock();

    std::error_code remove_error;
    std::filesystem::remove(destination, remove_error);

    sqlite3* target = nullptr;
    const int open_result = sqlite3_open_v2(to_utf8(destination).c_str(), &target,
                                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (open_result != SQLITE_OK) {
        const std::string message =
            std::string("cannot open backup destination: ") + sqlite3_errstr(open_result);
        sqlite3_close(target);
        throw db_error(open_result, message);
    }

    sqlite3_backup* backup = sqlite3_backup_init(target, "main", source.handle(), "main");
    if (backup == nullptr) {
        const int code = sqlite3_errcode(target);
        const std::string message = std::string("cannot start backup: ") + sqlite3_errmsg(target);
        sqlite3_close(target);
        throw db_error(code, message);
    }

    // -1 copies the whole database in one step; nothing else can write to the
    // source while we hold its lock, so there is no reason to copy in pages.
    const int step_result = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);

    if (step_result != SQLITE_DONE) {
        const std::string message = std::string("backup failed: ") + sqlite3_errstr(step_result);
        sqlite3_close(target);
        throw db_error(step_result, message);
    }

    const int close_result = sqlite3_close(target);
    if (close_result != SQLITE_OK) {
        throw db_error(close_result, std::string("cannot close backup destination: ") +
                                         sqlite3_errstr(close_result));
    }
}

std::vector<std::filesystem::path> list_backups(const std::filesystem::path& directory,
                                                std::string_view prefix) {
    std::vector<std::filesystem::path> backups;

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return backups;
    }

    const std::string match = std::string(prefix) + "-";
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = to_utf8(entry.path().filename());
        if (name.starts_with(match) && name.ends_with(".db")) {
            backups.push_back(entry.path());
        }
    }

    // The timestamp format sorts chronologically as text, so the names alone
    // order the backups; file times would change if a file were copied.
    std::ranges::sort(backups);
    return backups;
}

int rotate_backups(const std::filesystem::path& directory, std::string_view prefix, int keep) {
    if (keep < 0) {
        return 0;
    }

    std::vector<std::filesystem::path> backups = list_backups(directory, prefix);
    if (std::cmp_less_equal(backups.size(), keep)) {
        return 0;
    }

    const std::size_t excess = backups.size() - static_cast<std::size_t>(keep);
    int removed = 0;
    for (std::size_t i = 0; i < excess; ++i) {
        std::error_code error;
        if (std::filesystem::remove(backups[i], error)) {
            ++removed;
        }
    }
    return removed;
}

std::filesystem::path create_backup(database& source, const std::filesystem::path& directory,
                                    std::string_view prefix, int keep,
                                    std::chrono::system_clock::time_point at) {
    std::filesystem::create_directories(directory);

    const std::filesystem::path destination = directory / backup_file_name(prefix, at);
    backup_to_file(source, destination);
    rotate_backups(directory, prefix, keep);

    return destination;
}

} // namespace latibot::db
