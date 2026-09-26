#pragma once

#include <dpp/snowflake.h>

#include <cstdint>
#include <span>
#include <string>

namespace latibot::ports {

/// Audio out to a guild's voice connection (plan §13).
///
/// The speech queue talks to this rather than to DPP's voice client, so what
/// it sends and when can be tested without a connection. There is one voice
/// connection per guild, so everything is addressed by guild.
class voice_output {
public:
    virtual ~voice_output() = default;

    voice_output() = default;
    voice_output(const voice_output&) = delete;
    auto operator=(const voice_output&) -> voice_output& = delete;

    /// Whether the bot has a voice connection in `guild` that can take audio
    /// now. False while one is still being set up.
    [[nodiscard]] virtual auto ready(dpp::snowflake guild) -> bool = 0;

    /// Queues 48 kHz stereo PCM, then a marker named `marker`, which is
    /// reported back through the speech queue's `on_marker` once playback
    /// passes it. False when there is no connection to queue it on.
    virtual auto play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool = 0;

    /// Drops what is queued up to and including the next marker.
    virtual auto skip(dpp::snowflake guild) -> void = 0;

    /// Drops everything queued.
    virtual auto stop(dpp::snowflake guild) -> void = 0;
};

} // namespace latibot::ports
