#pragma once

#include <string>
#include <string_view>

namespace latibot::util {

/// Number of non-overlapping occurrences of `needle` in `haystack`.
/// An empty needle counts zero.
[[nodiscard]] std::size_t count_occurrences(std::string_view haystack, std::string_view needle) noexcept;

/// The text with leading and trailing ASCII whitespace removed.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

/// True when the text is empty or only whitespace.
[[nodiscard]] bool is_blank(std::string_view text) noexcept;

/// How many characters UTF-8 text holds, counted as Discord counts them for
/// its length limits: one per code point, whatever its size in bytes.
[[nodiscard]] std::size_t character_count(std::string_view text) noexcept;

/// The text cut to at most `limit` characters, ending in "…" when anything
/// was cut. Cuts only where a character starts, so a multi-byte character is
/// never split.
[[nodiscard]] std::string truncate(std::string_view text, std::size_t limit);

/// True when text starting after `before` is inside a Discord spoiler.
///
/// Discord spoilers are delimited by `||`, so a link is spoilered when an odd
/// number of markers precede it (plan §9.1).
[[nodiscard]] bool is_inside_spoiler(std::string_view before) noexcept;

} // namespace latibot::util
