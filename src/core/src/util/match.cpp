#include "core/util/match.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <array>
#include <cctype>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace latibot::util {
namespace {

/// How much work one message may cost a regular expression before it gives
/// up and fires nothing. A pattern like `(a+)+$` would otherwise take minutes
/// on a line of a's, and patterns come from whoever runs /trigger.
constexpr std::uint32_t regex_match_limit = 100'000;
constexpr std::uint32_t regex_depth_limit = 10'000;

/// How many compiled patterns are kept. Past it the cache starts over, which
/// costs one compile per pattern.
constexpr std::size_t regex_cache_limit = 512;

/// Caseless as the literal modes are, with Unicode's idea of case and of
/// `\w`, and a message that is not valid UTF-8 matched as best it can rather
/// than refused.
constexpr std::uint32_t regex_options = PCRE2_CASELESS | PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;

using compiled_regex = std::shared_ptr<pcre2_code>;

struct match_data_deleter {
    auto operator()(pcre2_match_data* data) const noexcept -> void { pcre2_match_data_free(data); }
};

struct match_context_deleter {
    auto operator()(pcre2_match_context* context) const noexcept -> void { pcre2_match_context_free(context); }
};

/// The pattern compiled, or why it would not.
auto compile(std::string_view pattern, std::string* error) -> compiled_regex {
    int code = 0;
    PCRE2_SIZE offset = 0;
    pcre2_code* made = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), regex_options, &code, &offset, nullptr);
    if (made == nullptr) {
        if (error != nullptr) {
            std::array<PCRE2_UCHAR, 256> message{};
            pcre2_get_error_message(code, message.data(), message.size());
            *error = std::format("{}, at character {}", reinterpret_cast<const char*>(message.data()), offset + 1);
        }
        return nullptr;
    }
    return {made, pcre2_code_free};
}

/// The pattern compiled once, for every message after the first. A pattern
/// that does not compile is remembered as null, so it is not tried again.
auto cached(std::string_view pattern) -> compiled_regex {
    static std::mutex mutex;
    static std::map<std::string, compiled_regex, std::less<>> cache;

    const std::scoped_lock lock(mutex);
    if (const auto found = cache.find(pattern); found != cache.end()) return found->second;
    if (cache.size() >= regex_cache_limit) cache.clear();
    return cache.emplace(std::string(pattern), compile(pattern, nullptr)).first->second;
}

/// The limits every match runs under. Read-only once made, so the threads
/// DPP delivers messages on can share it.
auto limits() -> pcre2_match_context* {
    static const std::unique_ptr<pcre2_match_context, match_context_deleter> context = [] {
        std::unique_ptr<pcre2_match_context, match_context_deleter> made(pcre2_match_context_create(nullptr));
        if (made) {
            pcre2_set_match_limit(made.get(), regex_match_limit);
            pcre2_set_depth_limit(made.get(), regex_depth_limit);
        }
        return made;
    }();
    return context.get();
}

/// Whether `code` finds anything in `content`.
auto search(const pcre2_code& code, std::string_view content) -> bool {
    const std::unique_ptr<pcre2_match_data, match_data_deleter> data(pcre2_match_data_create_from_pattern(&code, nullptr));
    if (!data) return false;

    const int found = pcre2_match(&code, reinterpret_cast<PCRE2_SPTR>(content.data()), content.size(), 0, 0, data.get(), limits());
    if (found < 0 && found != PCRE2_ERROR_NOMATCH) {
        // Most likely the match limit, on a pattern that backtracks badly.
        std::array<PCRE2_UCHAR, 256> message{};
        pcre2_get_error_message(found, message.data(), message.size());
        log().debug("a regular expression gave up on a message: {}", reinterpret_cast<const char*>(message.data()));
    }
    return found >= 0;
}

/// Word characters for the purposes of whole-word matching: what sits either
/// side of "420" in "4200" but not in "it's 420 somewhere".
auto is_word_character(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return byte == '_' || std::isalnum(byte) != 0;
}

} // namespace

auto to_string(match_mode mode) noexcept -> std::string_view {
    switch (mode) {
    case match_mode::substring:
        return "substring";
    case match_mode::regex:
        return "regex";
    case match_mode::whole_word:
        break;
    }
    return "whole_word";
}

auto mode_label(match_mode mode) noexcept -> std::string_view {
    switch (mode) {
    case match_mode::substring:
        return "anywhere";
    case match_mode::regex:
        return "regex";
    case match_mode::whole_word:
        break;
    }
    return "whole word";
}

auto match_mode_from_string(std::string_view name) -> std::optional<match_mode> {
    // The panel's form is free text, and the panel itself says "whole word",
    // so that has to read back too.
    const std::string key = to_lower(trim(name));
    if (key == "whole_word" || key == "word" || key == "whole word" || key == "whole-word") return match_mode::whole_word;
    if (key == "substring" || key == "anywhere") return match_mode::substring;
    if (key == "regex" || key == "regexp" || key == "regular expression") return match_mode::regex;
    return std::nullopt;
}

auto pattern_error(std::string_view pattern, match_mode mode) -> std::optional<std::string> {
    if (mode != match_mode::regex) return std::nullopt;

    std::string error;
    const compiled_regex code = compile(pattern, &error);
    if (!code) return error;

    // Such a pattern would answer every message, including one that is only
    // an attachment, which is never what was meant.
    if (search(*code, "")) return std::string("it matches an empty message, so it would fire on every message");
    return std::nullopt;
}

auto matches(std::string_view content, std::string_view pattern, match_mode mode) -> bool {
    if (pattern.empty()) return false;

    if (mode == match_mode::regex) {
        const compiled_regex code = cached(pattern);
        return code && search(*code, content);
    }

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
