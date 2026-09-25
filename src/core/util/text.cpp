#include "core/util/text.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>

namespace latibot::util {
namespace {

constexpr std::string_view whitespace = " \t\n\r\f\v";

/// A UTF-8 continuation byte, 10xxxxxx: the second or later byte of a
/// character, never the start of one.
constexpr bool is_continuation(char byte) noexcept {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

} // namespace

std::size_t count_occurrences(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string_view::npos; pos = haystack.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

std::string_view trim(std::string_view text) noexcept {
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

bool is_blank(std::string_view text) noexcept {
    return trim(text).empty();
}

std::size_t character_count(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(text, [](char byte) { return !is_continuation(byte); }));
}

std::string truncate(std::string_view text, std::size_t limit) {
    if (character_count(text) <= limit) {
        return std::string(text);
    }
    if (limit == 0) {
        return {};
    }

    // Keep limit - 1 characters, leaving room for the ellipsis: stop at the
    // first byte that would start one more.
    std::size_t kept = 0;
    std::size_t cut = 0;
    for (; cut < text.size(); ++cut) {
        if (is_continuation(text[cut])) {
            continue;
        }
        if (kept == limit - 1) {
            break;
        }
        ++kept;
    }
    return std::string(text.substr(0, cut)) + "…";
}

std::optional<dpp::snowflake> parse_snowflake(std::string_view text) {
    text = trim(text);
    std::uint64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || stop != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return dpp::snowflake(value);
}

} // namespace latibot::util
