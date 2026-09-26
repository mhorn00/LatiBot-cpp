#pragma once

#include <string>
#include <utility>
#include <variant>

namespace latibot::ports {

/// A failed call to something outside the bot: Discord, an LLM provider, the
/// speech engine.
struct api_error {
    /// HTTP status where there is one, 0 for transport or local failures.
    int http_status = 0;
    std::string message;
};

/// Either a value or an `api_error`.
///
/// Ports return this instead of throwing: a Discord call failing is ordinary
/// and every caller has to handle it, which is easy to forget with exceptions
/// and easy to see in the type. (std::expected is C++23; this project is on
/// C++20.)
template <typename T>
// Moving a result is only as noexcept as moving a T, and some of the DPP
// payloads we carry allocate when moved (MSVC's node-based containers do).
// No result is used where a throwing move would matter, so this is a property
// of the payload types rather than something to fix here.
// NOLINTNEXTLINE(bugprone-exception-escape)
class result {
public:
    result(T value) : data_(std::move(value)) {}         // NOLINT(google-explicit-constructor)
    result(api_error error) : data_(std::move(error)) {} // NOLINT(google-explicit-constructor)

    [[nodiscard]] auto ok() const noexcept -> bool { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return ok(); }

    /// Throws std::bad_variant_access when the result holds an error.
    [[nodiscard]] auto value() const -> const T& { return std::get<T>(data_); }
    [[nodiscard]] auto value() -> T& { return std::get<T>(data_); }

    [[nodiscard]] auto value_or(T fallback) const -> T { return ok() ? value() : std::move(fallback); }

    /// Throws std::bad_variant_access when the result holds a value.
    [[nodiscard]] auto error() const -> const api_error& { return std::get<api_error>(data_); }

private:
    std::variant<T, api_error> data_;
};

/// Specialisation for calls that return nothing but can still fail.
template <>
class result<void> {
public:
    result() = default;
    result(api_error error) // NOLINT(google-explicit-constructor)
        : error_(std::move(error)), ok_(false) {}

    [[nodiscard]] auto ok() const noexcept -> bool { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] auto error() const -> const api_error& { return error_; }

private:
    api_error error_;
    bool ok_ = true;
};

} // namespace latibot::ports
