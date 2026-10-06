#pragma once

#include <expected>
#include <string>

namespace latibot::ports {

/// A failed call to something outside the bot: Discord, an LLM provider, the
/// speech engine.
struct api_error {
    /// HTTP status where there is one, 0 for transport or local failures.
    int http_status = 0;
    std::string message;
};

/// Either a value or an `api_error`; `result<void>` for a call that returns
/// nothing but can still fail.
///
/// Ports return this instead of throwing: a Discord call failing is ordinary
/// and every caller has to handle it, which is easy to forget with exceptions
/// and easy to see in the type. A failure is returned as
/// `std::unexpected(api_error{...})`.
template <typename T>
using result = std::expected<T, api_error>;

} // namespace latibot::ports
