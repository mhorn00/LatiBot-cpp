#include "core/util/text.hpp"

namespace latibot::util {

std::size_t count_occurrences(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string_view::npos;
         pos = haystack.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

bool is_inside_spoiler(std::string_view before) noexcept {
    return count_occurrences(before, "||") % 2 == 1;
}

} // namespace latibot::util
