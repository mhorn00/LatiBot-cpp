#include "core/audio/voice_params.hpp"

#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace latibot::audio {
namespace {

constexpr std::array voices{
    builtin_voice{.name = "paul", .command = "[:np]", .description = "Perfect Paul, the default"},
    builtin_voice{.name = "betty", .command = "[:nb]", .description = "Beautiful Betty"},
    builtin_voice{.name = "harry", .command = "[:nh]", .description = "Huge Harry"},
    builtin_voice{.name = "frank", .command = "[:nf]", .description = "Frail Frank"},
    builtin_voice{.name = "dennis", .command = "[:nd]", .description = "Doctor Dennis"},
    builtin_voice{.name = "kit", .command = "[:nk]", .description = "Kit the Kid"},
    builtin_voice{.name = "ursula", .command = "[:nu]", .description = "Uppity Ursula"},
    builtin_voice{.name = "rita", .command = "[:nr]", .description = "Rough Rita"},
    builtin_voice{.name = "wendy", .command = "[:nw]", .description = "Whispery Wendy"},
    builtin_voice{.name = "val", .command = "[:nv]", .description = "the slot DECtalk saves edited voices into"},
};

// DECtalk's limits (ph/ph_vdefi.c). The formant ceilings are ZAPF and ZAPB,
// 6000 in a build whose sample rate is 11025 Hz.
constexpr std::array parameters{
    voice_parameter{.code = "ap", .label = "Average pitch, Hz", .min = 50, .max = 350},
    voice_parameter{.code = "pr", .label = "Pitch range, % of Paul's", .min = 0, .max = 250},
    voice_parameter{.code = "bf", .label = "Baseline fall, Hz", .min = 0, .max = 90},
    voice_parameter{.code = "hr", .label = "Hat rise, Hz", .min = 2, .max = 100},
    voice_parameter{.code = "sr", .label = "Stress rise, Hz", .min = 1, .max = 100},
    voice_parameter{.code = "sx", .label = "Sex: 1 male, 0 female", .min = 0, .max = 1},
    voice_parameter{.code = "hs", .label = "Head size, %", .min = 65, .max = 145},
    voice_parameter{.code = "as", .label = "Assertiveness, %", .min = 0, .max = 200},
    voice_parameter{.code = "ri", .label = "Richness, %", .min = 0, .max = 100},
    voice_parameter{.code = "sm", .label = "Smoothness, %", .min = 0, .max = 100},
    voice_parameter{.code = "br", .label = "Breathiness, dB", .min = 0, .max = 72},
    voice_parameter{.code = "lx", .label = "Lax breathiness, %", .min = 0, .max = 100},
    voice_parameter{.code = "la", .label = "Laryngealization, %", .min = 0, .max = 100},
    voice_parameter{.code = "qu", .label = "Quickness, %", .min = 0, .max = 100},
    voice_parameter{.code = "nf", .label = "Fixed open samples", .min = 0, .max = 100},
    voice_parameter{.code = "f4", .label = "4th formant, Hz", .min = 2000, .max = 6000},
    voice_parameter{.code = "b4", .label = "4th formant bandwidth, Hz", .min = 100, .max = 6000},
    voice_parameter{.code = "f5", .label = "5th formant, Hz", .min = 2500, .max = 6000},
    voice_parameter{.code = "b5", .label = "5th formant bandwidth, Hz", .min = 100, .max = 6000},
    voice_parameter{.code = "f7", .label = "Parallel 4th formant, Hz", .min = 2500, .max = 6000},
    voice_parameter{.code = "f8", .label = "Parallel 5th formant, Hz", .min = 2500, .max = 6000},
    voice_parameter{.code = "ft", .label = "Spectral tilt, %", .min = 0, .max = 100},
    voice_parameter{.code = "gv", .label = "Voicing gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "gh", .label = "Aspiration gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "gf", .label = "Frication gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "gn", .label = "Nasal gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "g5", .label = "Loudness, dB", .min = 0, .max = 87},
    voice_parameter{.code = "g1", .label = "5th formant gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "g2", .label = "4th formant gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "g3", .label = "3rd formant gain, dB", .min = 0, .max = 87},
    voice_parameter{.code = "g4", .label = "2nd formant gain, dB", .min = 0, .max = 87},
};

constexpr std::array groups{
    voice_parameter_group{.name = "Pitch", .codes = {"ap", "pr", "bf", "hr", "sr"}},
    voice_parameter_group{.name = "Character", .codes = {"sx", "hs", "as", "ri", "sm"}},
    voice_parameter_group{.name = "Breath", .codes = {"br", "lx", "la", "qu", "nf"}},
    voice_parameter_group{.name = "Formants", .codes = {"f4", "b4", "f5", "b5", ""}},
    voice_parameter_group{.name = "Parallel formants and tilt", .codes = {"f7", "f8", "ft", "", ""}},
    voice_parameter_group{.name = "Source gains", .codes = {"gv", "gh", "gf", "gn", "g5"}},
    voice_parameter_group{.name = "Formant gains", .codes = {"g1", "g2", "g3", "g4", ""}},
};

/// "[:nh]" as "nh", which is how the parser meets it.
auto selector_of(const builtin_voice& voice) -> std::string_view {
    return voice.command.substr(2, voice.command.size() - 3);
}

auto is_separator(char letter) -> bool {
    return letter == '[' || letter == ']' || letter == ':' || letter == ',' || letter == ' ' || letter == '\t' || letter == '\r' ||
           letter == '\n';
}

/// The text in lowercase words. Brackets, colons and commas only separate,
/// so "[:nh][:dv ap 200]" reads as "nh dv ap 200".
auto words_of(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> words;
    std::string word;
    for (const char letter : text) {
        if (!is_separator(letter)) {
            word.push_back(letter);
            continue;
        }
        if (!word.empty()) words.push_back(util::to_lower(word));
        word.clear();
    }
    if (!word.empty()) words.push_back(util::to_lower(word));
    return words;
}

} // namespace

auto builtin_voices() -> std::span<const builtin_voice> {
    return voices;
}

auto find_builtin_voice(std::string_view name) -> const builtin_voice* {
    const std::string wanted = util::to_lower(util::trim(name));
    const auto found = std::ranges::find(voices, wanted, &builtin_voice::name);
    return found == voices.end() ? nullptr : &*found;
}

auto voice_parameters() -> std::span<const voice_parameter> {
    return parameters;
}

auto find_voice_parameter(std::string_view code) -> const voice_parameter* {
    const std::string wanted = util::to_lower(util::trim(code));
    const auto found = std::ranges::find(parameters, wanted, &voice_parameter::code);
    return found == parameters.end() ? nullptr : &*found;
}

auto voice_parameter_groups() -> std::span<const voice_parameter_group> {
    return groups;
}

auto custom_voice::set(std::string_view code, int value) -> bool {
    const voice_parameter* parameter = find_voice_parameter(code);
    if (parameter == nullptr) return false;
    const int kept = std::clamp(value, parameter->min, parameter->max);

    const auto existing = std::ranges::find(edits, parameter->code, &std::pair<std::string, int>::first);
    if (existing != edits.end()) {
        existing->second = kept;
        return true;
    }

    // In front of the first edit to a parameter later in the table.
    const auto position = [](std::string_view which) { return std::ranges::find(parameters, which, &voice_parameter::code); };
    const auto before = std::ranges::find_if(edits, [&](const auto& edit) { return position(edit.first) > position(parameter->code); });
    edits.emplace(before, std::string(parameter->code), kept);
    return true;
}

auto custom_voice::clear(std::string_view code) -> void {
    const std::string wanted = util::to_lower(util::trim(code));
    std::erase_if(edits, [&](const auto& edit) { return edit.first == wanted; });
}

auto custom_voice::get(std::string_view code) const -> std::optional<int> {
    const std::string wanted = util::to_lower(util::trim(code));
    const auto found = std::ranges::find(edits, wanted, &std::pair<std::string, int>::first);
    return found == edits.end() ? std::nullopt : std::optional<int>(found->second);
}

auto custom_voice::dv_parameters() const -> std::string {
    std::string written;
    for (const auto& [code, value] : edits) {
        if (!written.empty()) written += ' ';
        written += std::format("{} {}", code, value);
    }
    return written;
}

auto parse_custom_voice(std::string_view text, std::string_view base) -> parsed_voice {
    parsed_voice parsed;
    const builtin_voice* base_voice = find_builtin_voice(base);
    parsed.voice.base = std::string(base_voice == nullptr ? voices.front().name : base_voice->name);

    const std::vector<std::string> words = words_of(text);
    for (std::size_t at = 0; at < words.size(); ++at) {
        const std::string& current = words[at];
        if (current == "dv" || current == "define_voice") continue;

        if (const auto selects = std::ranges::find(voices, current, selector_of); selects != voices.end()) {
            parsed.voice.base = std::string(selects->name);
            continue;
        }
        if (current == "name" && at + 1 < words.size() && find_builtin_voice(words[at + 1]) != nullptr) {
            parsed.voice.base = std::string(find_builtin_voice(words[++at])->name);
            continue;
        }
        if (current == "save") {
            parsed.problems.emplace_back("\"save\" isn't needed: saving the voice keeps it");
            continue;
        }

        const voice_parameter* parameter = find_voice_parameter(current);
        if (parameter == nullptr) {
            parsed.problems.push_back(std::format("\"{}\" isn't a voice parameter", current));
            continue;
        }

        int value = 0;
        const bool has_number = at + 1 < words.size();
        const std::string_view number = has_number ? std::string_view(words[at + 1]) : std::string_view{};
        const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (!has_number || error != std::errc{} || end != number.data() + number.size()) {
            parsed.problems.push_back(std::format("{} needs a number after it", parameter->code));
            continue;
        }
        ++at;

        const int kept = std::clamp(value, parameter->min, parameter->max);
        if (kept != value) {
            parsed.problems.push_back(
                std::format("{} goes from {} to {}, so {} became {}", parameter->code, parameter->min, parameter->max, value, kept));
        }
        parsed.voice.set(parameter->code, value);
    }
    return parsed;
}

auto voice_preamble(const ports::voice_settings& settings) -> std::string {
    const builtin_voice* voice = find_builtin_voice(settings.voice);
    if (voice == nullptr) voice = voices.data();

    std::string preamble(voice->command);
    const int rate = std::clamp(settings.rate, min_rate, max_rate);
    if (rate != default_rate) preamble += std::format("[:rate {}]", rate);

    // Read back and written out again rather than pasted in: this goes in
    // front of the sanitized text, so nothing in it is checked later.
    const std::string edits = parse_custom_voice(settings.custom_params, voice->name).voice.dv_parameters();
    if (!edits.empty()) preamble += std::format("[:dv {}]", edits);
    return preamble;
}

} // namespace latibot::audio
