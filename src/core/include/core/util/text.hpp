#pragma once

#include <dpp/snowflake.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::util {

/// Number of non-overlapping occurrences of `needle` in `haystack`.
/// An empty needle counts zero.
[[nodiscard]] auto count_occurrences(std::string_view haystack, std::string_view needle) noexcept -> std::size_t;

/// The text with leading and trailing ASCII whitespace removed.
[[nodiscard]] auto trim(std::string_view text) noexcept -> std::string_view;

/// True when the text is empty or only whitespace.
[[nodiscard]] auto is_blank(std::string_view text) noexcept -> bool;

/// The text with its ASCII letters lowercased and every other byte, UTF-8
/// included, left as it is.
[[nodiscard]] auto to_lower(std::string_view text) -> std::string;

/// Whether two texts are the same but for the case of ASCII letters.
[[nodiscard]] auto equals_ignoring_case(std::string_view lhs, std::string_view rhs) noexcept -> bool;

/// Every line of `text`: split on '\n', with a '\r' before it dropped, so
/// CRLF text reads the same. A last line with no newline after it is kept,
/// and text that ends in a newline has an empty last line.
[[nodiscard]] auto lines(std::string_view text) -> std::vector<std::string_view>;

/// How many characters UTF-8 text holds, counted as Discord counts them for
/// its length limits: one per code point, whatever its size in bytes.
[[nodiscard]] auto character_count(std::string_view text) noexcept -> std::size_t;

/// The text cut to at most `limit` characters, ending in "…" when anything
/// was cut. Cuts only where a character starts, so a multi-byte character is
/// never split.
[[nodiscard]] auto truncate(std::string_view text, std::size_t limit) -> std::string;

/// The lines, each followed by a newline, in at most `budget` characters.
/// When they all fit they are kept whole; otherwise each is cut to an equal
/// share, so a page of long lines still fits in a message and every line
/// still shows how it starts.
[[nodiscard]] auto fit_lines(const std::vector<std::string>& lines, std::size_t budget) -> std::string;

/// A Discord ID written as text: digits only, surrounding whitespace allowed.
/// Nothing for anything else, including 0, rather than the leading digits of
/// "123abc" or a "-1" wrapped round to a huge number, which is what
/// std::stoull would give.
[[nodiscard]] auto parse_snowflake(std::string_view text) -> std::optional<dpp::snowflake>;

/// Text from outside, such as a track's title, made safe to put in a
/// message: markdown characters escaped so they show as themselves, line
/// breaks made spaces, and every `@` followed by a zero-width space, so
/// `@everyone` or a mention in it can never ping anyone.
[[nodiscard]] auto plain_text(std::string_view text) -> std::string;

} // namespace latibot::util
