#include "join_command.hpp"

#include "core/commands/options.hpp"
#include "core/util/log.hpp"
#include "voice/voice_state.hpp"

#include <dpp/dpp.h>

#include <format>
#include <string>

namespace latibot::commands {
namespace {

/// The voice channel the bot is connected to in this guild, or 0.
auto bot_voice_channel(const dpp::slashcommand_t& event) -> dpp::snowflake {
    return discord::bot_voice_channel(event.from(), event.command.guild_id);
}

} // namespace

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

auto plan_join(dpp::snowflake target_channel, dpp::snowflake bot_channel) noexcept -> join_decision {
    if (target_channel.empty()) return {.action = join_action::target_not_in_voice, .channel_id = {}};
    if (bot_channel.empty()) return {.action = join_action::connect, .channel_id = target_channel};
    if (bot_channel == target_channel) return {.action = join_action::already_there, .channel_id = target_channel};
    return {.action = join_action::move, .channel_id = target_channel};
}

auto describe_join(join_action action, dpp::snowflake followed) -> std::string {
    return std::format("{} <@{}>", action == join_action::move ? "ok moving to" : "ok joining", followed.str());
}

// --------------------------------------------------------------------------
// /join
// --------------------------------------------------------------------------

join_command::join_command()
    : info_{.name = "join",
            .description = "Join the voice channel you are in, or another user's.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The room sees the bot come and go, so it sees why
            // (src/modules/voice/docs/Voice_Channels.md §2.1).
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}} {}

auto join_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_user, "user", "Whose channel to join.", false));
    return payload;
}

auto join_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    const dpp::snowflake target = snowflake_option(event, "user").value_or(caller);
    const bool following_someone_else = target != caller;

    const join_decision decision = plan_join(discord::voice_channel_of(event.command.guild_id, target), bot_voice_channel(event));

    dpp::discord_client* shard = event.from();
    switch (decision.action) {
    case join_action::target_not_in_voice:
        util::log().debug("/join: {} is not in a voice channel in guild {}", target, event.command.guild_id);
        co_await event.co_reply(
            refusal(event, following_someone_else ? "they're not in a voice channel" : "you're not in a voice channel"));
        co_return;

    case join_action::already_there:
        co_await event.co_reply(
            refusal(event, following_someone_else ? "i'm already in their voice channel" : "i'm already in your voice channel"));
        co_return;

    case join_action::connect:
    case join_action::move:
        if (shard == nullptr) {
            co_await event.co_reply(refusal(event, "i can't reach the gateway right now"));
            co_return;
        }
        shard->connect_voice(event.command.guild_id, decision.channel_id);
        util::log().info("{} voice channel {} in guild {} for {}", decision.action == join_action::move ? "moved to" : "joined",
                         decision.channel_id, event.command.guild_id, describe_user(event.command.get_issuing_user()));
        dpp::message said(describe_join(decision.action, target));
        said.set_allowed_mentions();
        co_await event.co_reply(result(event, std::move(said)));
        co_return;
    }
}

// --------------------------------------------------------------------------
// /leave
// --------------------------------------------------------------------------

leave_command::leave_command()
    : info_{.name = "leave",
            .description = "Leave the voice channel.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The room sees the bot come and go, so it sees why
            // (src/modules/voice/docs/Voice_Channels.md §2.1).
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}} {}

auto leave_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    dpp::discord_client* shard = event.from();
    if (shard == nullptr || bot_voice_channel(event).empty()) {
        co_await event.co_reply(refusal(event, "i'm not in a voice channel"));
        co_return;
    }

    shard->disconnect_voice(event.command.guild_id);
    util::log().info("left the voice channel in guild {} for {}", event.command.guild_id, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, "ok bye"));
}

} // namespace latibot::commands
