#include "core/util/text.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>

namespace latibot::util {
namespace {

constexpr std::string_view whitespace = " \t\n\r\f\v";

/// A UTF-8 continuation byte, 10xxxxxx: the second or later byte of a
/// character, never the start of one.
constexpr auto is_continuation(char byte) noexcept -> bool {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

/// What std::tolower does in the "C" locale, without depending on which
/// locale is set.
constexpr auto lower_ascii(char letter) noexcept -> char {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

} // namespace

auto count_occurrences(std::string_view haystack, std::string_view needle) noexcept -> std::size_t {
    if (needle.empty()) return 0;

    std::size_t count = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string_view::npos; pos = haystack.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

auto trim(std::string_view text) noexcept -> std::string_view {
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

auto is_blank(std::string_view text) noexcept -> bool {
    return trim(text).empty();
}

auto to_lower(std::string_view text) -> std::string {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), lower_ascii);
    return lowered;
}

auto equals_ignoring_case(std::string_view lhs, std::string_view rhs) noexcept -> bool {
    return std::ranges::equal(lhs, rhs, [](char left, char right) { return lower_ascii(left) == lower_ascii(right); });
}

auto lines(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (true) {
        const std::size_t newline = text.find('\n', at);
        std::string_view line = text.substr(at, newline == std::string_view::npos ? std::string_view::npos : newline - at);
        if (line.ends_with('\r')) line.remove_suffix(1);
        found.push_back(line);

        if (newline == std::string_view::npos) return found;
        at = newline + 1;
    }
}

auto character_count(std::string_view text) noexcept -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count_if(text, [](char byte) { return !is_continuation(byte); }));
}

auto truncate(std::string_view text, std::size_t limit) -> std::string {
    if (character_count(text) <= limit) return std::string(text);
    if (limit == 0) return {};

    // Keep limit - 1 characters, leaving room for the ellipsis: stop at the
    // first byte that would start one more.
    std::size_t kept = 0;
    std::size_t cut = 0;
    for (; cut < text.size(); ++cut) {
        if (is_continuation(text[cut])) continue;
        if (kept == limit - 1) break;
        ++kept;
    }
    return std::string(text.substr(0, cut)) + "…";
}

auto parse_snowflake(std::string_view text) -> std::optional<dpp::snowflake> {
    text = trim(text);
    std::uint64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || stop != text.data() + text.size() || value == 0) return std::nullopt;
    return dpp::snowflake(value);
}

} // namespace latibot::util
