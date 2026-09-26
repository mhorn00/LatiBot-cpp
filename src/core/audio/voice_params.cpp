#include "core/audio/voice_params.hpp"

#include "core/util/text.hpp"

#include <algorithm>
#include <array>
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

} // namespace

auto builtin_voices() -> std::span<const builtin_voice> {
    return voices;
}

auto find_builtin_voice(std::string_view name) -> const builtin_voice* {
    const std::string wanted = util::to_lower(util::trim(name));
    const auto found = std::ranges::find(voices, wanted, &builtin_voice::name);
    return found == voices.end() ? nullptr : &*found;
}

auto voice_preamble(const ports::voice_settings& settings) -> std::string {
    const builtin_voice* voice = find_builtin_voice(settings.voice);
    if (voice == nullptr) voice = voices.data();

    std::string preamble(voice->command);
    const int rate = std::clamp(settings.rate, min_rate, max_rate);
    if (rate != default_rate) preamble += std::format("[:rate {}]", rate);
    if (!settings.custom_params.empty()) preamble += std::format("[:dv {}]", settings.custom_params);
    return preamble;
}

} // namespace latibot::audio
