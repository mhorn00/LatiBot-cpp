#include "core/util/text.hpp"

namespace latibot::util {
namespace {

constexpr std::string_view whitespace = " \t\n\r\f\v";

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

bool is_inside_spoiler(std::string_view before) noexcept {
    return count_occurrences(before, "||") % 2 == 1;
}

} // namespace latibot::util
