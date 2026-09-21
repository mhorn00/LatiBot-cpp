#pragma once

#include "core/db/statement.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>

struct sqlite3;

namespace latibot::db {

/// An open SQLite database.
///
/// One connection, guarded by a recursive mutex (plan v4 §5.2). The bot's load
/// is tiny, so a single serialized connection is simpler than a pool and
/// removes every question about which thread owns what. `prepare()` and
/// `transaction` hold the lock for as long as they live.
class database {
public:
    /// Path accepted by SQLite for a private in-memory database, used by tests.
    static constexpr std::string_view in_memory = ":memory:";

    explicit database(const std::filesystem::path& path);
    ~database();

    database(const database&) = delete;
    database& operator=(const database&) = delete;
    database(database&&) = delete;
    database& operator=(database&&) = delete;

    /// Runs one or more statements that return no rows.
    void execute(std::string_view sql);

    /// Prepares a single statement. The returned object holds the lock.
    [[nodiscard]] statement prepare(std::string_view sql);

    /// Prepares and binds in one step: `db.prepare("... ?, ?", guild_id, key)`.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    [[nodiscard]] statement prepare(std::string_view sql, const Args&... args) {
        statement stmt = prepare(sql);
        stmt.bind_all(args...);
        return stmt;
    }

    [[nodiscard]] std::int64_t last_insert_rowid();

    /// Rows changed by the most recent statement.
    [[nodiscard]] int changes();

    /// Schema version, held in `PRAGMA user_version` (plan v4 §5.2).
    [[nodiscard]] int user_version();
    void set_user_version(int version);

    /// The underlying handle, for SQLite APIs we don't wrap (the backup API).
    /// Callers must hold `lock()` while using it.
    [[nodiscard]] sqlite3* handle() noexcept { return handle_; }

    /// Locks the connection for a compound operation.
    [[nodiscard]] std::unique_lock<std::recursive_mutex> lock() {
        return std::unique_lock(mutex_);
    }

    /// Throws `db_error` unless `result_code` is SQLITE_OK.
    void check(int result_code, const char* context) const;

private:
    void configure();

    sqlite3* handle_ = nullptr;
    std::recursive_mutex mutex_;
};

/// Scoped transaction: commits on `commit()`, otherwise rolls back.
///
/// Holds the database lock for its lifetime, so a transaction is also a
/// critical section.
class transaction {
public:
    explicit transaction(database& db);
    ~transaction();

    transaction(const transaction&) = delete;
    transaction& operator=(const transaction&) = delete;

    void commit();
    void rollback();

private:
    database* db_;
    std::unique_lock<std::recursive_mutex> lock_;
    bool finished_ = false;
};

} // namespace latibot::db
