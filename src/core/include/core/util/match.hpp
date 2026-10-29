#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::util {

// What the simple triggers and the language model's advanced triggers share
// (src/modules/triggers/docs/Triggers.md §2.1, src/modules/llm/docs/Language_Model.md §2.4):
// how a pattern matches a message, and when one may fire again. Here rather
// than with either, so that neither needs the other.

/// How a trigger's pattern is compared against a message.
///
/// A pattern is literal text, standing alone as a word or anywhere, unless it
/// is asked to be a regular expression.
enum class match_mode : std::uint8_t {
    /// "420" fires on "420" but not on "4200".
    whole_word,
    /// "420" fires on "4200" too.
    substring,
    /// A PCRE2 regular expression, found anywhere in the message: `\b42[0-9]\b`.
    regex,
};

[[nodiscard]] auto to_string(match_mode mode) noexcept -> std::string_view;
[[nodiscard]] auto match_mode_from_string(std::string_view name) -> std::optional<match_mode>;

/// The mode as people read it in a list: "whole word", "anywhere", "regex".
[[nodiscard]] auto mode_label(match_mode mode) noexcept -> std::string_view;

/// Why `pattern` cannot be used in `mode`, or nothing when it can. Only a
/// regular expression can be wrong: "missing closing parenthesis at 4".
[[nodiscard]] auto pattern_error(std::string_view pattern, match_mode mode) -> std::optional<std::string>;

/// Whether `content` fires `pattern`. Case-insensitive. A regular expression
/// that does not compile, or that gives up at its match limit, fires nothing.
[[nodiscard]] auto matches(std::string_view content, std::string_view pattern, match_mode mode) -> bool;

/// Whether a trigger may fire again in a channel.
///
/// `last_fired` is empty when it has not fired there yet. A zero cooldown
/// always allows it, which is the documented way to turn cooldowns off.
[[nodiscard]] auto off_cooldown(std::optional<std::chrono::steady_clock::time_point> last_fired, std::chrono::steady_clock::time_point now,
                                std::chrono::seconds cooldown) -> bool;

} // namespace latibot::util
