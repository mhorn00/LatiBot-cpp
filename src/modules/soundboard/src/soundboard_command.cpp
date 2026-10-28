#include "soundboard_command.hpp"

#include "clips.hpp"
#include "core/commands/options.hpp"
#include "core/discord/voice_message.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "sounds.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>

#include <cstdint>
#include <format>
#include <utility>
#include <variant>

namespace latibot::commands {

auto unknown_sound_reply(std::string_view wanted) -> std::string {
    return std::format("i can't see a soundboard sound called \"{}\"", util::truncate(util::trim(wanted), 100));
}

soundboard_command::soundboard_command(soundboard_services services)
    : info_{.name = "soundboard",
            .description = "Send a soundboard sound as a voice message, or play it in voice.",
            .aliases = {},
            .required_bot_permissions = 0,
            // Shown to whoever may use the soundboard in voice.
            .default_member_permissions = dpp::permission(dpp::p_use_soundboard),
            .guild_only = true,
            // The voice message is for the room, as /chat's is; it arrives
            // silently.
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(std::move(services)) {}

auto soundboard_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "sound", "Which sound. Start typing its name.", true)
                           .set_auto_complete(true)
                           .set_max_length(100));
    payload.add_option(dpp::command_option(dpp::co_boolean, "voice", "Play it in the voice channel the bot is in instead.", false));
    return payload;
}

auto soundboard_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || focused->name != "sound" || event.owner == nullptr) return;

    const auto* typed = std::get_if<std::string>(&focused->value);
    const dpp::snowflake guild = event.command.guild_id;
    if (services_.looked_in) services_.looked_in(guild);

    dpp::interaction_response reply(dpp::ir_autocomplete_reply);
    for (const soundboard::sound_choice& choice : services_.sounds->suggest(typed == nullptr ? std::string_view{} : *typed, guild)) {
        reply.add_autocomplete_choice(dpp::command_option_choice(choice.label, choice.value));
    }
    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

auto soundboard_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const std::string wanted = string_option(event, "sound");
    const auto found = services_.sounds->find(wanted, guild);
    if (!found) {
        if (services_.looked_in) services_.looked_in(guild);
        co_await event.co_reply(refusal(event, unknown_sound_reply(wanted)));
        co_return;
    }

    if (bool_option(event, "voice").value_or(false)) {
        const dpp::snowflake channel = services_.bot_voice_channel ? services_.bot_voice_channel(guild) : dpp::snowflake{};
        if (channel.empty() || !services_.play_in_voice) {
            co_await event.co_reply(refusal(event, "i'm not in a voice channel; /join me first, or leave voice off"));
            co_return;
        }
        const auto played = co_await services_.play_in_voice(channel, *found);
        if (!played.has_value()) {
            util::log().warn("/soundboard could not play sound {} ({}) in voice in guild {}: {}", found->id, found->name, guild,
                             played.error().message);
            co_await event.co_reply(refusal(event, std::format("Discord wouldn't play that in voice: {}", played.error().message)));
            co_return;
        }
        util::log().info("played soundboard sound {} ({}) in voice channel {} for {} in guild {}", found->id, found->name, channel,
                         describe_user(event.command.get_issuing_user()), guild);
        co_await event.co_reply(result(event, std::format("playing {} in <#{}>", found->name, channel.str())));
        co_return;
    }

    // Answered directly rather than deferred, as /chat is, so the voice
    // message is the reply itself. That has to happen within three seconds:
    // a sound played before is ready at once, and a new one is a small
    // download and two short runs of ffmpeg.
    auto made = co_await services_.clips->make(*found);
    if (!made.has_value()) {
        util::log().warn("/soundboard could not make sound {} ({}) a voice message in guild {}: {}", found->id, found->name, guild,
                         made.error().message);
        co_await event.co_reply(refusal(event, std::format("couldn't make that a voice message: {}", made.error().message)));
        co_return;
    }

    const std::chrono::milliseconds duration = made->duration;
    const auto flags = static_cast<std::uint64_t>(responses_for(event).result);
    const auto sent = co_await discord::answer_with_voice_message(*services_.http, event, flags, std::move(made.value()));
    if (!sent.has_value()) {
        // Nothing answered the interaction, so a refusal still can.
        util::log().error("/soundboard in guild {} could not send its voice message: {}", guild, sent.error().message);
        co_await event.co_reply(refusal(event, "couldn't send the voice message; it's in the log"));
        co_return;
    }
    util::log().info("sent soundboard sound {} ({}, {}) as a voice message for {} in guild {}", found->id, found->name, duration,
                     describe_user(event.command.get_issuing_user()), guild);
}

} // namespace latibot::commands
