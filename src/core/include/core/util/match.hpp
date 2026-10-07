#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace latibot::util {

// What the simple triggers and the language model's advanced triggers share
// (src/modules/triggers/docs/Triggers.md §2.1, src/modules/llm/docs/Language_Model.md §2.4):
// how a pattern matches a message, and when one may fire again. Here rather
// than with either, so that neither needs the other.

/// How a trigger's pattern is compared against a message.
///
/// Users do not write regular expressions: a pattern is literal text, and the
/// only choice is whether it has to stand alone as a word.
enum class match_mode : std::uint8_t {
    /// "420" fires on "420" but not on "4200".
    whole_word,
    /// "420" fires on "4200" too.
    substring,
};

[[nodiscard]] auto to_string(match_mode mode) noexcept -> std::string_view;
[[nodiscard]] auto match_mode_from_string(std::string_view name) -> std::optional<match_mode>;

/// Whether `content` fires `pattern`. Case-insensitive, and the pattern is
/// literal text rather than a regular expression.
[[nodiscard]] auto matches(std::string_view content, std::string_view pattern, match_mode mode) -> bool;

/// Whether a trigger may fire again in a channel.
///
/// `last_fired` is empty when it has not fired there yet. A zero cooldown
/// always allows it, which is the documented way to turn cooldowns off.
[[nodiscard]] auto off_cooldown(std::optional<std::chrono::steady_clock::time_point> last_fired, std::chrono::steady_clock::time_point now,
                                std::chrono::seconds cooldown) -> bool;

} // namespace latibot::util
