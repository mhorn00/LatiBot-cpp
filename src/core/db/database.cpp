#include "core/db/database.hpp"

#include "core/db/error.hpp"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace latibot::db {
namespace {

std::string describe(sqlite3* handle, int result_code, const char* context) {
    std::string message = context;
    message += ": ";
    if (handle != nullptr) {
        message += sqlite3_errmsg(handle);
    } else {
        message += sqlite3_errstr(result_code);
    }
    message += " (code ";
    message += std::to_string(result_code);
    message += ")";
    return message;
}

} // namespace

database::database(const std::filesystem::path& path) {
    // SQLite takes UTF-8 paths; on Windows std::filesystem::path is UTF-16.
    const std::u8string utf8 = path.generic_u8string();
    const std::string utf8_path(reinterpret_cast<const char*>(utf8.data()), utf8.size());

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int result = sqlite3_open_v2(utf8_path.c_str(), &handle_, flags, nullptr);
    if (result != SQLITE_OK) {
        // sqlite3_open_v2 hands back a handle even on failure, so that the
        // error message can be read from it; it still has to be closed.
        const std::string message = describe(handle_, result, "cannot open database");
        sqlite3_close(handle_);
        handle_ = nullptr;
        throw db_error(result, message);
    }

    configure();
}

database::~database() {
    sqlite3_close(handle_);
}

void database::configure() {
    // WAL keeps readers from blocking writers. It is a no-op for in-memory
    // databases, which SQLite reports as "memory"; that is not an error.
    execute("PRAGMA journal_mode = WAL");
    execute("PRAGMA synchronous = NORMAL");
    execute("PRAGMA foreign_keys = ON");
    execute("PRAGMA busy_timeout = 5000");
}

void database::check(int result_code, const char* context) const {
    if (result_code != SQLITE_OK) {
        throw db_error(result_code, describe(handle_, result_code, context));
    }
}

void database::execute(std::string_view sql) {
    const std::unique_lock guard(mutex_);

    char* error_message = nullptr;
    const std::string statement_text(sql);
    const int result =
        sqlite3_exec(handle_, statement_text.c_str(), nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
        std::string message = "cannot execute SQL: ";
        message += error_message != nullptr ? error_message : sqlite3_errstr(result);
        sqlite3_free(error_message);
        throw db_error(result, message);
    }
    sqlite3_free(error_message);
}

statement database::prepare(std::string_view sql) {
    std::unique_lock guard(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const int result =
        sqlite3_prepare_v2(handle_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (result != SQLITE_OK) {
        throw db_error(result, describe(handle_, result, "cannot prepare statement"));
    }

    return statement(*this, stmt, std::move(guard));
}

std::int64_t database::last_insert_rowid() {
    const std::unique_lock guard(mutex_);
    return sqlite3_last_insert_rowid(handle_);
}

int database::changes() {
    const std::unique_lock guard(mutex_);
    return sqlite3_changes(handle_);
}

int database::user_version() {
    statement stmt = prepare("PRAGMA user_version");
    if (!stmt.step()) {
        throw db_error(SQLITE_ERROR, "PRAGMA user_version returned no row");
    }
    return stmt.get<int>(0);
}

void database::set_user_version(int version) {
    // PRAGMA does not take bound parameters, so the value is formatted in.
    // It is an int, so there is nothing to inject.
    execute("PRAGMA user_version = " + std::to_string(version));
}

transaction::transaction(database& db) : db_(&db), lock_(db.lock()) {
    db_->execute("BEGIN");
}

transaction::~transaction() {
    if (finished_) {
        return;
    }
    try {
        db_->execute("ROLLBACK");
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // A destructor must not throw, and there is nowhere to report this
        // while unwinding. Once the logger exists (plan v4 §19, phase 0e),
        // this should log the failure.
    }
}

void transaction::commit() {
    db_->execute("COMMIT");
    finished_ = true;
}

void transaction::rollback() {
    db_->execute("ROLLBACK");
    finished_ = true;
}

} // namespace latibot::db
