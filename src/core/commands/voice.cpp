#include "core/commands/voice.hpp"

#include "core/commands/basic.hpp"
#include "core/commands/options.hpp"
#include "core/config/guild_settings.hpp"
#include "core/discord/voice_state.hpp"
#include "core/events/voice_sessions.hpp"
#include "core/util/log.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/dispatcher.h>

#include <algorithm>
#include <format>

namespace latibot::commands {

auto voice_grace_for(const config::guild_settings& settings, dpp::snowflake guild) -> std::chrono::seconds {
    const std::int64_t seconds = settings.get_int(guild, events::voice_grace_key, events::default_voice_grace.count());
    return std::chrono::seconds{std::clamp<std::int64_t>(seconds, 0, max_voice_grace_seconds)};
}

voice_command::voice_command(events::voice_sessions& sessions, config::guild_settings& settings)
    : info_{.name = "voice",
            .description = "Start or end a voice session.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The room sees the bot arrive and go, as with /join (plan §6).
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {{"grace", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}}}},
      sessions_(&sessions),
      settings_(&settings) {}

auto voice_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_sub_command, "start", "Join your voice channel; /speak goes there from anywhere."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "stop", "End the voice session and leave."));

    dpp::command_option grace(dpp::co_sub_command, "grace", "Show or change how long I stay once everyone has left (Manage Server).");
    grace.add_option(dpp::command_option(dpp::co_integer, "seconds", "How long, in seconds.", false)
                         .set_min_value(0)
                         .set_max_value(max_voice_grace_seconds));
    payload.add_option(grace);
    return payload;
}

auto voice_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string subcommand = subcommand_path(event.command.get_command_interaction());
    if (subcommand == "start") {
        co_await start(event);
    } else if (subcommand == "stop") {
        co_await stop(event);
    } else if (subcommand == "grace") {
        co_await grace(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto voice_command::start(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    dpp::discord_client* shard = event.from();

    // The session goes where the caller is, moving the bot if it is
    // elsewhere in the guild: it is theirs.
    const dpp::snowflake target = discord::voice_channel_of(guild, caller);
    const join_decision decision = plan_join(target, discord::bot_voice_channel(shard, guild));
    if (decision.action == join_action::target_not_in_voice) {
        co_await event.co_reply(refusal(event, "you're not in a voice channel"));
        co_return;
    }
    if (decision.action != join_action::already_there) {
        if (shard == nullptr) {
            co_await event.co_reply(refusal(event, "i can't reach the gateway right now"));
            co_return;
        }
        shard->connect_voice(guild, decision.channel_id);
    }

    sessions_->start({.guild_id = guild, .voice_channel = target, .text_channel = event.command.channel_id, .started_by = caller});
    util::log().info("voice session started in guild {}, voice channel {}, by {}", guild, target,
                     describe_user(event.command.get_issuing_user()));

    dpp::message said(std::format("ok, voice session started in <#{}>; /speak goes there from anywhere in the server", target.str()));
    said.set_allowed_mentions();
    co_await event.co_reply(result(event, std::move(said)));
}

auto voice_command::stop(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    dpp::discord_client* shard = event.from();

    // Ending the session is the bot's own voice state changing, which the
    // shell sees and tidies up after; this only has to leave.
    const bool had_session = sessions_->find(guild).has_value();
    if (!had_session && discord::bot_voice_channel(shard, guild).empty()) {
        co_await event.co_reply(refusal(event, "there's no voice session to stop"));
        co_return;
    }
    if (shard != nullptr) shard->disconnect_voice(guild);
    sessions_->end(guild);

    util::log().info("voice session in guild {} stopped by {}", guild, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, "ok bye"));
}

auto voice_command::grace(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto seconds = int_option(event, "seconds");

    if (!seconds) {
        co_await event.co_reply(
            result(event, std::format("once everyone has left, i stay {} before leaving", voice_grace_for(*settings_, guild))));
        co_return;
    }

    // Default member permissions are per command, so this checks for itself
    // (plan §21.13).
    if (!invoker_permissions(event).can(dpp::p_manage_guild)) {
        co_await event.co_reply(refusal(event, "changing that needs Manage Server"));
        co_return;
    }

    settings_->set_int(guild, events::voice_grace_key, *seconds);
    util::log().info("voice grace in guild {} set to {}s by {}", guild, *seconds, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(
        result(event, std::format("once everyone has left, i'll now stay {} before leaving", voice_grace_for(*settings_, guild))));
}

} // namespace latibot::commands
