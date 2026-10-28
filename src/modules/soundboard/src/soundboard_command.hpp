#pragma once

#include "core/commands/registry.hpp"
#include "core/discord/voice_message.hpp"
#include "core/ports/result.hpp"

#include <dpp/snowflake.h>

#include <functional>
#include <string>
#include <string_view>

namespace latibot::ports {
class http_client;
}
namespace latibot::soundboard {
class clip_maker;
class sound_catalog;
struct sound;
} // namespace latibot::soundboard

namespace latibot::commands {

/// What `/soundboard` uses.
struct soundboard_services {
    const soundboard::sound_catalog* sounds = nullptr;
    soundboard::clip_maker* clips = nullptr;
    /// What the voice message is sent through.
    ports::http_client* http = nullptr;

    /// Told whenever someone looks through a server's sounds, so that ones
    /// added since they were last read turn up; may be empty.
    std::function<void(dpp::snowflake)> looked_in;

    /// The voice channel the bot is in here, 0 for none; may be empty, and
    /// then nothing plays in voice.
    std::function<dpp::snowflake(dpp::snowflake guild)> bot_voice_channel;

    /// Plays a sound in a voice channel the bot is in, with Discord's own
    /// soundboard (src/modules/soundboard/docs/Soundboard.md §4).
    std::function<dpp::task<ports::result<void>>(dpp::snowflake channel, const soundboard::sound& played)> play_in_voice;

    /// Posts a voice message in a channel as the bot, for the language
    /// model, whose tool answers no interaction.
    std::function<dpp::task<ports::result<dpp::snowflake>>(dpp::snowflake channel, discord::voice_message message)> post_voice;
};

/// Answers with a soundboard sound as a voice message
/// (src/modules/soundboard/docs/Soundboard.md §3), which needs no voice
/// channel; or, with `voice`, plays it where the bot is in voice (§4).
class soundboard_command final : public command {
public:
    explicit soundboard_command(soundboard_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    command_info info_;
    soundboard_services services_;
};

/// What `/soundboard` says to a sound it cannot find.
[[nodiscard]] auto unknown_sound_reply(std::string_view wanted) -> std::string;

} // namespace latibot::commands
