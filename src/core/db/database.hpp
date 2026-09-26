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
/// One connection, guarded by a recursive mutex (plan §5.2). The bot's load
/// is tiny, so a single serialized connection is simpler than a pool and
/// removes every question about which thread owns what. `prepare()` and
/// `transaction` hold the lock for as long as they live.
///
/// So one statement is always atomic, and anything longer is not. A store
/// method that runs more than one operation and needs them to agree takes
/// `lock()` or a `transaction` first: a write followed by `changes()` or
/// `last_insert_rowid()`, a read that decides a write, or several statements
/// that must be consistent. Without it another thread's statement can land
/// in between, and `changes()` would describe that one instead.
///
/// The mutex belongs to a thread. Never hold a statement, `lock()` or a
/// `transaction` across a `co_await`, which can resume on another thread.
class database {
public:
    /// Path accepted by SQLite for a private in-memory database, used by tests.
    static constexpr std::string_view in_memory = ":memory:";

    explicit database(const std::filesystem::path& path);
    ~database();

    database(const database&) = delete;
    auto operator=(const database&) -> database& = delete;
    database(database&&) = delete;
    auto operator=(database&&) -> database& = delete;

    /// Runs one or more statements that return no rows.
    auto execute(std::string_view sql) -> void;

    /// Prepares a single statement. The returned object holds the lock.
    [[nodiscard]] auto prepare(std::string_view sql) -> statement;

    /// Prepares and binds in one step: `db.prepare("... ?, ?", guild_id, key)`.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    [[nodiscard]] auto prepare(std::string_view sql, const Args&... args) -> statement {
        statement stmt = prepare(sql);
        stmt.bind_all(args...);
        return stmt;
    }

    [[nodiscard]] auto last_insert_rowid() -> std::int64_t;

    /// Rows changed by the most recent statement.
    [[nodiscard]] auto changes() -> int;

    /// Schema version, held in `PRAGMA user_version` (plan §5.2).
    [[nodiscard]] auto user_version() -> int;
    auto set_user_version(int version) -> void;

    /// The underlying handle, for SQLite APIs we don't wrap (the backup API).
    /// Callers must hold `lock()` while using it.
    [[nodiscard]] auto handle() noexcept -> sqlite3* { return handle_; }

    /// Locks the connection for a compound operation.
    [[nodiscard]] auto lock() -> std::unique_lock<std::recursive_mutex> { return std::unique_lock(mutex_); }

    /// Throws `db_error` unless `result_code` is SQLITE_OK.
    auto check(int result_code, const char* context) const -> void;

private:
    auto configure() -> void;

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
    auto operator=(const transaction&) -> transaction& = delete;

    auto commit() -> void;
    auto rollback() -> void;

private:
    database* db_;
    std::unique_lock<std::recursive_mutex> lock_;
    bool finished_ = false;
};

} // namespace latibot::db
