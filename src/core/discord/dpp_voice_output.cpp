#include "core/discord/dpp_voice_output.hpp"

#include "core/discord/voice_state.hpp"
#include "core/util/log.hpp"

#include <dpp/cluster.h>
#include <dpp/discordvoiceclient.h>
#include <dpp/exception.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace latibot::discord {
namespace {

/// One Opus frame: 20 ms of 48 kHz stereo, as DPP asks to be given.
constexpr std::size_t samples_per_packet = dpp::send_audio_raw_max_length / sizeof(std::int16_t);

} // namespace

dpp_voice_output::dpp_voice_output(dpp::cluster& cluster) : cluster_(&cluster) {}

auto dpp_voice_output::ready(dpp::snowflake guild) -> bool {
    return ready_voice_client(*cluster_, guild) != nullptr;
}

auto dpp_voice_output::play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool {
    dpp::discord_voice_client* client = ready_voice_client(*cluster_, guild);
    if (client == nullptr) return false;

    // DPP takes the samples as a mutable pointer to unsigned 16-bit values;
    // a copy of each packet keeps the caller's audio const. Queued all at
    // once, as DPP's documentation asks: it encodes up front rather than in
    // real time.
    std::array<std::uint16_t, samples_per_packet> packet{};
    try {
        for (std::size_t at = 0; at < audio.size(); at += samples_per_packet) {
            const std::size_t count = std::min(samples_per_packet, audio.size() - at);
            std::ranges::transform(audio.subspan(at, count), packet.begin(),
                                   [](std::int16_t sample) { return static_cast<std::uint16_t>(sample); });
            client->send_audio_raw(packet.data(), count * sizeof(std::int16_t));
        }
        client->insert_marker(marker);
    } catch (const dpp::exception& error) {
        util::log().error("could not queue audio in guild {}: {}", guild, error.what());
        return false;
    }
    return true;
}

auto dpp_voice_output::skip(dpp::snowflake guild) -> void {
    if (dpp::discord_voice_client* client = ready_voice_client(*cluster_, guild)) client->skip_to_next_marker();
}

auto dpp_voice_output::stop(dpp::snowflake guild) -> void {
    if (dpp::discord_voice_client* client = ready_voice_client(*cluster_, guild)) client->stop_audio();
}

} // namespace latibot::discord
