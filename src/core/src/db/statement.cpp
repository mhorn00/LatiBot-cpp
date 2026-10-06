#include "core/db/statement.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"

#include <sqlite3.h>

#include <utility>

namespace latibot::db {

statement::statement(database& owner, sqlite3_stmt* handle, std::unique_lock<std::recursive_mutex> lock)
    : owner_(&owner), handle_(handle), lock_(std::move(lock)) {}

statement::statement(statement&& other) noexcept : owner_(other.owner_), handle_(other.handle_), lock_(std::move(other.lock_)) {
    other.owner_ = nullptr;
    other.handle_ = nullptr;
}

auto statement::operator=(statement&& other) noexcept -> statement& {
    if (this != &other) {
        sqlite3_finalize(handle_);
        owner_ = std::exchange(other.owner_, nullptr);
        handle_ = std::exchange(other.handle_, nullptr);
        lock_ = std::move(other.lock_);
    }
    return *this;
}

statement::~statement() {
    // Finalize before the lock is released, so the handle never outlives the
    // critical section it was prepared in.
    sqlite3_finalize(handle_);
}

auto statement::check(int result_code, const char* context) const -> void {
    owner_->check(result_code, context);
}

auto statement::bind(int index, std::nullptr_t) -> statement& {
    check(sqlite3_bind_null(handle_, index), "cannot bind null");
    return *this;
}

auto statement::bind_int64(int index, std::int64_t value) -> statement& {
    check(sqlite3_bind_int64(handle_, index, value), "cannot bind integer");
    return *this;
}

auto statement::bind(int index, bool value) -> statement& {
    return bind_int64(index, value ? 1 : 0);
}

auto statement::bind(int index, double value) -> statement& {
    check(sqlite3_bind_double(handle_, index, value), "cannot bind real");
    return *this;
}

auto statement::bind(int index, std::string_view value) -> statement& {
    // SQLITE_TRANSIENT: SQLite copies the text, so the caller's buffer does
    // not have to outlive the bind.
    check(sqlite3_bind_text(handle_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT), "cannot bind text");
    return *this;
}

auto statement::bind(int index, const char* value) -> statement& {
    return value == nullptr ? bind(index, nullptr) : bind(index, std::string_view(value));
}

auto statement::bind(int index, std::span<const std::byte> value) -> statement& {
    check(sqlite3_bind_blob(handle_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT), "cannot bind blob");
    return *this;
}

auto statement::step() -> bool {
    const int result = sqlite3_step(handle_);
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw db_error(result, std::string("cannot step statement: ") + sqlite3_errmsg(sqlite3_db_handle(handle_)));
}

auto statement::run() -> void {
    while (step()) {
        // Statements that return rows are still drained, so callers can use
        // run() for "execute and ignore results".
    }
}

auto statement::reset() -> void {
    check(sqlite3_reset(handle_), "cannot reset statement");
}

auto statement::is_null(int column) const -> bool {
    return sqlite3_column_type(handle_, column) == SQLITE_NULL;
}

auto statement::column_count() const -> int {
    return sqlite3_column_count(handle_);
}

auto statement::column_int64(int column) const -> std::int64_t {
    return sqlite3_column_int64(handle_, column);
}

auto statement::column_double(int column) const -> double {
    return sqlite3_column_double(handle_, column);
}

auto statement::column_text(int column) const -> std::string {
    const auto* text = sqlite3_column_text(handle_, column);
    if (text == nullptr) return {};
    const int size = sqlite3_column_bytes(handle_, column);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

auto statement::column_blob(int column) const -> std::vector<std::byte> {
    const void* data = sqlite3_column_blob(handle_, column);
    const int size = sqlite3_column_bytes(handle_, column);
    if (data == nullptr || size <= 0) return {};
    const auto* bytes = static_cast<const std::byte*>(data);
    return std::vector<std::byte>(bytes, bytes + size);
}

} // namespace latibot::db
