#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

struct sqlite3_stmt;

namespace latibot::db {

class database;

namespace detail {

template <typename>
inline constexpr bool always_false = false;

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {
    using value_type = T;
};

} // namespace detail

/// A prepared statement.
///
/// Holding one keeps the database locked (plan §5.2: a single connection
/// guarded by a mutex). Statements are therefore meant to be short-lived: keep
/// one for the duration of a query, not as a member.
class statement {
public:
    statement(statement&& other) noexcept;
    statement& operator=(statement&& other) noexcept;
    statement(const statement&) = delete;
    statement& operator=(const statement&) = delete;
    ~statement();

    /// Parameter indices are 1-based, as in SQLite.
    statement& bind(int index, std::nullptr_t);
    statement& bind(int index, bool value);
    statement& bind(int index, double value);
    statement& bind(int index, std::string_view value);
    statement& bind(int index, const char* value);
    statement& bind(int index, std::span<const std::byte> value);

    /// Covers every integer width. Discord snowflakes are unsigned 64-bit and
    /// are stored as SQLite INTEGER, which is signed 64-bit; real snowflakes
    /// stay well inside the positive range.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    statement& bind(int index, T value) {
        return bind_int64(index, static_cast<std::int64_t>(value));
    }

    template <typename T>
    statement& bind(int index, const std::optional<T>& value) {
        return value ? bind(index, *value) : bind(index, nullptr);
    }

    /// Binds every argument in order, starting at parameter 1.
    template <typename... Args>
    statement& bind_all(const Args&... args) {
        int index = 0;
        (bind(++index, args), ...);
        return *this;
    }

    /// Advances to the next row. True when a row is available, false when the
    /// statement is finished.
    [[nodiscard]] bool step();

    /// Runs a statement that returns no rows (INSERT, UPDATE, DDL).
    void run();

    /// Re-runs the statement from the start; bound values are kept.
    void reset();

    /// Column indices are 0-based, as in SQLite.
    [[nodiscard]] bool is_null(int column) const;
    [[nodiscard]] int column_count() const;

    template <typename T>
    [[nodiscard]] T get(int column) const {
        if constexpr (detail::is_optional<T>::value) {
            using value_type = detail::is_optional<T>::value_type;
            if (is_null(column)) {
                return std::nullopt;
            }
            return get<value_type>(column);
        } else if constexpr (std::same_as<T, std::string>) {
            return column_text(column);
        } else if constexpr (std::same_as<T, bool>) {
            return column_int64(column) != 0;
        } else if constexpr (std::integral<T>) {
            return static_cast<T>(column_int64(column));
        } else if constexpr (std::floating_point<T>) {
            return static_cast<T>(column_double(column));
        } else if constexpr (std::same_as<T, std::vector<std::byte>>) {
            return column_blob(column);
        } else {
            static_assert(detail::always_false<T>, "unsupported column type");
        }
    }

private:
    friend class database;

    statement(database& owner, sqlite3_stmt* handle, std::unique_lock<std::recursive_mutex> lock);

    statement& bind_int64(int index, std::int64_t value);

    [[nodiscard]] std::int64_t column_int64(int column) const;
    [[nodiscard]] double column_double(int column) const;
    [[nodiscard]] std::string column_text(int column) const;
    [[nodiscard]] std::vector<std::byte> column_blob(int column) const;

    void check(int result_code, const char* context) const;

    database* owner_ = nullptr;
    sqlite3_stmt* handle_ = nullptr;
    std::unique_lock<std::recursive_mutex> lock_;
};

} // namespace latibot::db
