#include "core/audio/dectalk_sanitizer.hpp"

#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>

namespace latibot::audio {
namespace {

/// DECtalk's inline commands in this build: the US English table in
/// cmd/c_us_cde.h, less the MSDOS, ARM7 and HLSYN entries.
constexpr auto command_names = std::to_array<std::string_view>({
    "rate",      "latin", "name",         "np",          "nb",     "nh",    "nf",     "nd",     "nk",       "nu",      "nr",
    "nw",        "nv",    "comma",        "cp",          "period", "pp",    "volume", "vs",     "index",    "error",   "phoneme",
    "log",       "mode",  "say",          "punctuation", "skip",   "pause", "play",   "resume", "sync",     "dial",    "tone",
    "pronounce", "pitch", "define_voice", "dv",          "debug",  "setv",  "loadv",  "gender", "preamble", "version", "spf",
});

enum class rule : std::uint8_t { anyone, trusted_only, nobody };

auto rule_for(std::string_view name) -> rule {
    if (name == "play" || name == "log" || name == "debug" || name == "loadv" || name == "setv") return rule::trusted_only;
    if (name == "pause" || name == "resume") return rule::nobody;
    return rule::anyone;
}

auto is_space(char letter) -> bool {
    return letter == ' ' || letter == '\t' || letter == '\r' || letter == '\n';
}

auto is_control(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return (byte < 0x20 && !is_space(letter)) || byte == 0x7F;
}

auto lower(char letter) -> char {
    return (letter >= 'A' && letter <= 'Z') ? static_cast<char>(letter - 'A' + 'a') : letter;
}

/// Characters a rebuilt parameter may hold: enough for numbers, keywords
/// and dial strings, and nothing DECtalk reads as structure.
auto is_plain_parameter(std::string_view token) -> bool {
    return !token.empty() && std::ranges::all_of(token, [](char letter) {
        return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_' ||
               letter == '-' || letter == '+' || letter == '#' || letter == '*';
    });
}

/// A trusted command's parameter, such as a path for `[:play]`: anything but
/// the quote that will surround it, a bracket or a control character.
auto is_quotable_parameter(std::string_view token) -> bool {
    return !token.empty() && std::ranges::none_of(token, [](char letter) { return letter == '"' || letter == ']' || is_control(letter); });
}

/// Resolves a command name the way cm_cmd_match_comm does: one letter at a
/// time, narrowing the candidates, and settled once one is left. Letters
/// after a settled match that stop matching it begin its parameters, so the
/// second value says how many letters the name used.
struct matched_name {
    std::string_view name;
    std::size_t used = 0;
};

auto match_name(std::string_view word) -> std::optional<matched_name> {
    std::vector<std::string_view> candidates(command_names.begin(), command_names.end());

    for (std::size_t at = 0; at < word.size(); ++at) {
        const char letter = lower(word[at]);
        std::vector<std::string_view> next;
        for (const std::string_view candidate : candidates) {
            if (at < candidate.size() && candidate[at] == letter) next.push_back(candidate);
        }
        if (next.empty()) {
            if (candidates.size() == 1) return matched_name{.name = candidates.front(), .used = at};
            return std::nullopt;
        }
        candidates = std::move(next);
    }
    if (candidates.size() == 1) return matched_name{.name = candidates.front(), .used = word.size()};
    return std::nullopt;
}

/// Reads one command's parameters from `text`, starting at `at`: tokens
/// separated by spaces or commas, a token starting with `"` or `<` running
/// to its closing `"` or `>`. Stops before a `:` that starts a token (the
/// next command in the bracket), at `]`, or at a `.` that starts a token.
struct parameters {
    std::vector<std::string> tokens;
    std::size_t end = 0;
};

auto read_parameters(std::string_view text, std::size_t at) -> parameters {
    parameters read;
    std::string token;
    auto finish = [&] {
        if (!token.empty()) read.tokens.push_back(std::move(token));
        token.clear();
    };

    while (at < text.size()) {
        const char letter = text[at];
        if (token.empty() && (letter == '"' || letter == '<')) {
            const char closer = letter == '"' ? '"' : '>';
            const std::size_t closing = text.find(closer, at + 1);
            const std::size_t stop = closing == std::string_view::npos ? text.size() : closing;
            read.tokens.emplace_back(text.substr(at + 1, stop - at - 1));
            at = closing == std::string_view::npos ? text.size() : closing + 1;
            continue;
        }
        if (is_space(letter) || letter == ',') {
            finish();
            ++at;
            continue;
        }
        if (letter == ']' || (token.empty() && (letter == ':' || letter == '.'))) break;
        token.push_back(letter);
        ++at;
    }
    finish();
    read.end = at;
    return read;
}

/// Writes one command as `[:name parameters]`, or records why it was left
/// out.
auto rebuild(std::string_view name, std::vector<std::string> tokens, speech_trust trust, sanitized_speech& into) -> void {
    const rule applies = rule_for(name);
    if (applies == rule::nobody || (applies == rule::trusted_only && trust != speech_trust::trusted)) {
        into.removed.emplace_back(name);
        return;
    }

    if (name == "dv" || name == "define_voice") {
        const auto saves = std::ranges::remove_if(tokens, [](const std::string& token) { return util::to_lower(token) == "save"; });
        if (saves.begin() != tokens.end()) into.removed.emplace_back("dv save");
        tokens.erase(saves.begin(), saves.end());
    }

    std::string written = "[:";
    written += name;
    for (const std::string& token : tokens) {
        if (is_plain_parameter(token)) {
            written += ' ' + token;
        } else if (applies == rule::trusted_only && is_quotable_parameter(token)) {
            written += " \"" + token + '"';
        }
    }
    written += ']';
    into.text += written;
}

/// Reads the command bracket whose `:` is at `at`, adding what it keeps to
/// `into`. Returns where the text after the bracket starts.
auto read_command_bracket(std::string_view text, std::size_t at, speech_trust trust, sanitized_speech& into) -> std::size_t {
    while (at < text.size() && text[at] == ':') {
        ++at;
        while (at < text.size() && is_space(text[at])) {
            ++at;
        }

        std::size_t word_end = at;
        while (word_end < text.size() && ((lower(text[word_end]) >= 'a' && lower(text[word_end]) <= 'z') || text[word_end] == '_')) {
            ++word_end;
        }
        const std::string_view word = text.substr(at, word_end - at);

        const auto matched = match_name(word);
        parameters read = read_parameters(text, matched ? at + matched->used : word_end);
        if (matched) {
            rebuild(matched->name, std::move(read.tokens), trust, into);
        } else if (!word.empty()) {
            into.removed.emplace_back(util::to_lower(word));
        }
        at = read.end;

        // A '.' ends a command without ending the bracket; the next ':'
        // starts another, and anything else is dropped up to it.
        while (at < text.size() && text[at] != ':' && text[at] != ']') {
            ++at;
        }
    }
    return at < text.size() ? at + 1 : at;
}

/// `sanitize_speech`, once control characters are gone.
auto sanitize_clean(std::string_view text, speech_trust trust) -> sanitized_speech {
    sanitized_speech result;
    result.text.reserve(text.size());

    std::size_t at = 0;
    while (at < text.size()) {
        const char letter = text[at];
        if (letter != '[') {
            result.text.push_back(letter);
            ++at;
            continue;
        }

        // DECtalk skips spaces, and further '[', between '[' and ':'.
        std::size_t next = at + 1;
        while (next < text.size() && (is_space(text[next]) || text[next] == '[')) {
            ++next;
        }
        if (next < text.size() && text[next] == ':') {
            at = read_command_bracket(text, next, trust, result);
            continue;
        }

        // A phoneme bracket, or a literal '[' when phoneme input is off.
        // Kept only when it closes before any other '[', so nothing inside
        // it can open a command.
        const std::size_t close = text.find_first_of("[]", at + 1);
        if (close == std::string_view::npos || text[close] == '[') {
            ++at; // an unclosed '[': drop it, keep what follows
            continue;
        }
        result.text += text.substr(at, close - at + 1);
        at = close + 1;
    }
    return result;
}

} // namespace

auto sanitize_speech(std::string_view text, speech_trust trust) -> sanitized_speech {
    // Control characters go first, before anything is read: removed later,
    // one between '[' and ':' would turn a phoneme bracket into a command.
    std::string cleaned;
    cleaned.reserve(text.size());
    std::ranges::copy_if(text, std::back_inserter(cleaned), [](char letter) { return !is_control(letter); });
    return sanitize_clean(cleaned, trust);
}

} // namespace latibot::audio
