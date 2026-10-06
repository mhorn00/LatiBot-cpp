#include "core/util/match.hpp"

#include "core/util/text.hpp"

#include <cctype>
#include <string>

namespace latibot::util {
namespace {

/// Word characters for the purposes of whole-word matching: what sits either
/// side of "420" in "4200" but not in "it's 420 somewhere".
auto is_word_character(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return byte == '_' || std::isalnum(byte) != 0;
}

} // namespace

auto to_string(match_mode mode) noexcept -> std::string_view {
    return mode == match_mode::substring ? "substring" : "whole_word";
}

auto match_mode_from_string(std::string_view name) -> std::optional<match_mode> {
    // The panel's form is free text, and the panel itself says "whole word",
    // so that has to read back too.
    const std::string key = to_lower(trim(name));
    if (key == "whole_word" || key == "word" || key == "whole word" || key == "whole-word") return match_mode::whole_word;
    if (key == "substring" || key == "anywhere") return match_mode::substring;
    return std::nullopt;
}

auto matches(std::string_view content, std::string_view pattern, match_mode mode) -> bool {
    if (pattern.empty()) return false;

    const std::string haystack = to_lower(content);
    const std::string needle = to_lower(pattern);

    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        if (mode == match_mode::substring) return true;

        // Every occurrence is checked, not just the first: "4200 and 420"
        // should fire even though the first hit is inside a longer word.
        const bool open_left = at == 0 || !is_word_character(haystack[at - 1]);
        const std::size_t after = at + needle.size();
        const bool open_right = after >= haystack.size() || !is_word_character(haystack[after]);
        if (open_left && open_right) return true;
    }

    return false;
}

auto off_cooldown(std::optional<std::chrono::steady_clock::time_point> last_fired, std::chrono::steady_clock::time_point now,
                  std::chrono::seconds cooldown) -> bool {
    if (cooldown <= std::chrono::seconds::zero() || !last_fired) return true;
    return now - *last_fired >= cooldown;
}

} // namespace latibot::util
