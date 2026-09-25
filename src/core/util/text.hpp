#pragma once

#include <string_view>

namespace latibot::util {

/// Number of non-overlapping occurrences of `needle` in `haystack`.
/// An empty needle counts zero.
[[nodiscard]] std::size_t count_occurrences(std::string_view haystack, std::string_view needle) noexcept;

/// The text with leading and trailing ASCII whitespace removed.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

/// True when the text is empty or only whitespace.
[[nodiscard]] bool is_blank(std::string_view text) noexcept;

/// True when text starting after `before` is inside a Discord spoiler.
///
/// Discord spoilers are delimited by `||`, so a link is spoilered when an odd
/// number of markers precede it (plan §9.1).
[[nodiscard]] bool is_inside_spoiler(std::string_view before) noexcept;

} // namespace latibot::util
