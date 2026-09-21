#include "core/db/statement.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"

#include <sqlite3.h>

#include <utility>

namespace latibot::db {

statement::statement(database& owner, sqlite3_stmt* handle,
                     std::unique_lock<std::recursive_mutex> lock)
    : owner_(&owner), handle_(handle), lock_(std::move(lock)) {}

statement::statement(statement&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_), lock_(std::move(other.lock_)) {
    other.owner_ = nullptr;
    other.handle_ = nullptr;
}

statement& statement::operator=(statement&& other) noexcept {
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

void statement::check(int result_code, const char* context) const {
    owner_->check(result_code, context);
}

statement& statement::bind(int index, std::nullptr_t) {
    check(sqlite3_bind_null(handle_, index), "cannot bind null");
    return *this;
}

statement& statement::bind_int64(int index, std::int64_t value) {
    check(sqlite3_bind_int64(handle_, index, value), "cannot bind integer");
    return *this;
}

statement& statement::bind(int index, bool value) {
    return bind_int64(index, value ? 1 : 0);
}

statement& statement::bind(int index, double value) {
    check(sqlite3_bind_double(handle_, index, value), "cannot bind real");
    return *this;
}

statement& statement::bind(int index, std::string_view value) {
    // SQLITE_TRANSIENT: SQLite copies the text, so the caller's buffer does
    // not have to outlive the bind.
    check(sqlite3_bind_text(handle_, index, value.data(), static_cast<int>(value.size()),
                            SQLITE_TRANSIENT),
          "cannot bind text");
    return *this;
}

statement& statement::bind(int index, const char* value) {
    return value == nullptr ? bind(index, nullptr) : bind(index, std::string_view(value));
}

statement& statement::bind(int index, std::span<const std::byte> value) {
    check(sqlite3_bind_blob(handle_, index, value.data(), static_cast<int>(value.size()),
                            SQLITE_TRANSIENT),
          "cannot bind blob");
    return *this;
}

bool statement::step() {
    const int result = sqlite3_step(handle_);
    if (result == SQLITE_ROW) {
        return true;
    }
    if (result == SQLITE_DONE) {
        return false;
    }
    throw db_error(result, std::string("cannot step statement: ") +
                               sqlite3_errmsg(sqlite3_db_handle(handle_)));
}

void statement::run() {
    while (step()) {
        // Statements that return rows are still drained, so callers can use
        // run() for "execute and ignore results".
    }
}

void statement::reset() {
    check(sqlite3_reset(handle_), "cannot reset statement");
}

bool statement::is_null(int column) const {
    return sqlite3_column_type(handle_, column) == SQLITE_NULL;
}

int statement::column_count() const {
    return sqlite3_column_count(handle_);
}

std::int64_t statement::column_int64(int column) const {
    return sqlite3_column_int64(handle_, column);
}

double statement::column_double(int column) const {
    return sqlite3_column_double(handle_, column);
}

std::string statement::column_text(int column) const {
    const auto* text = sqlite3_column_text(handle_, column);
    if (text == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(handle_, column);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

std::vector<std::byte> statement::column_blob(int column) const {
    const void* data = sqlite3_column_blob(handle_, column);
    const int size = sqlite3_column_bytes(handle_, column);
    if (data == nullptr || size <= 0) {
        return {};
    }
    const auto* bytes = static_cast<const std::byte*>(data);
    return std::vector<std::byte>(bytes, bytes + size);
}

} // namespace latibot::db
