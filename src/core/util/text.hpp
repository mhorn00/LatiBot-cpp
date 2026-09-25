#pragma once

#include <dpp/snowflake.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::util {

/// Number of non-overlapping occurrences of `needle` in `haystack`.
/// An empty needle counts zero.
[[nodiscard]] std::size_t count_occurrences(std::string_view haystack, std::string_view needle) noexcept;

/// The text with leading and trailing ASCII whitespace removed.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

/// True when the text is empty or only whitespace.
[[nodiscard]] bool is_blank(std::string_view text) noexcept;

/// The text with its ASCII letters lowercased and every other byte, UTF-8
/// included, left as it is.
[[nodiscard]] std::string to_lower(std::string_view text);

/// Whether two texts are the same but for the case of ASCII letters.
[[nodiscard]] bool equals_ignoring_case(std::string_view lhs, std::string_view rhs) noexcept;

/// Every line of `text`: split on '\n', with a '\r' before it dropped, so
/// CRLF text reads the same. A last line with no newline after it is kept,
/// and text that ends in a newline has an empty last line.
[[nodiscard]] std::vector<std::string_view> lines(std::string_view text);

/// How many characters UTF-8 text holds, counted as Discord counts them for
/// its length limits: one per code point, whatever its size in bytes.
[[nodiscard]] std::size_t character_count(std::string_view text) noexcept;

/// The text cut to at most `limit` characters, ending in "…" when anything
/// was cut. Cuts only where a character starts, so a multi-byte character is
/// never split.
[[nodiscard]] std::string truncate(std::string_view text, std::size_t limit);

/// A Discord ID written as text: digits only, surrounding whitespace allowed.
/// Nothing for anything else, including 0, rather than the leading digits of
/// "123abc" or a "-1" wrapped round to a huge number, which is what
/// std::stoull would give.
[[nodiscard]] std::optional<dpp::snowflake> parse_snowflake(std::string_view text);

} // namespace latibot::util
