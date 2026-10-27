#include "commands/channels.hpp"

#include "core/commands/options.hpp"
#include "core/util/log.hpp"

#include <dpp/channel.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <format>
#include <string_view>

namespace latibot::commands {
namespace {

/// What a rule leaves out, in a few words for a list.
auto what_it_ignores(events::channel_scope scope) -> std::string_view {
    switch (scope) {
    case events::channel_scope::everything:
        return "everything";
    case events::channel_scope::model:
        return "the language model";
    case events::channel_scope::bots:
        return "bots, LatiBot and its personas";
    }
    return "everything";
}

/// Every kind of channel people write in; a thread follows its parent's rule
/// unless it has one of its own.
auto channel_option(std::string_view description) -> dpp::command_option {
    return dpp::command_option(dpp::co_channel, "channel", std::string(description), true)
        .add_channel_type(dpp::CHANNEL_TEXT)
        .add_channel_type(dpp::CHANNEL_ANNOUNCEMENT)
        .add_channel_type(dpp::CHANNEL_VOICE)
        .add_channel_type(dpp::CHANNEL_STAGE)
        .add_channel_type(dpp::CHANNEL_FORUM)
        .add_channel_type(dpp::CHANNEL_MEDIA)
        .add_channel_type(dpp::CHANNEL_PUBLIC_THREAD)
        .add_channel_type(dpp::CHANNEL_PRIVATE_THREAD)
        .add_channel_type(dpp::CHANNEL_ANNOUNCEMENT_THREAD);
}

} // namespace

auto describe_ignored(dpp::snowflake channel, events::channel_scope scope, bool changed) -> std::string {
    switch (scope) {
    case events::channel_scope::everything:
        return changed ? std::format("ok, i'll ignore everything in <#{}>", channel)
                       : std::format("i was already ignoring everything in <#{}>", channel);
    case events::channel_scope::model:
        return changed ? std::format("ok, the language model will leave <#{}> alone", channel)
                       : std::format("the language model was already leaving <#{}> alone", channel);
    case events::channel_scope::bots:
        return changed ? std::format("ok, i'll ignore every bot in <#{}>, me and my personas included", channel)
                       : std::format("i was already ignoring every bot in <#{}>", channel);
    }
    return {};
}

auto render_ignored_channels(std::span<const std::pair<dpp::snowflake, events::channel_scope>> listed, dpp::snowflake log_channel)
    -> std::string {
    if (listed.empty() && log_channel.empty()) return "I hear every channel here. Leave one out with `/channels ignore`.";

    std::string body = "**Channels I leave out here**\n";
    for (const auto& [channel, scope] : listed) {
        body += std::format("<#{}>: {}{}\n", channel, what_it_ignores(scope), channel == log_channel ? " (my log channel)" : "");
    }
    const bool log_listed = std::ranges::any_of(listed, [&](const auto& entry) { return entry.first == log_channel; });
    if (!log_channel.empty() && !log_listed) {
        body += std::format("<#{}>: {} (my log channel, always)\n", log_channel, what_it_ignores(events::channel_scope::bots));
    }
    body += "\n_A thread follows its channel. Slash commands work everywhere._";
    return body;
}

channels_command::channels_command(events::ignored_channels& ignored, const events::log_channel& log)
    : info_{.name = "channels",
            .description = "Choose channels I ignore, entirely or in part.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_guild),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      ignored_(&ignored),
      log_(&log) {}

auto channels_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option ignore(dpp::co_sub_command, "ignore", "Leave a channel out, entirely or in part.");
    ignore.add_option(channel_option("The channel to leave out."));
    dpp::command_option what(dpp::co_string, "what", "What to ignore there. Everything, by default.", false);
    what.add_choice(dpp::command_option_choice("Everything", std::string(to_string(events::channel_scope::everything))));
    what.add_choice(dpp::command_option_choice("The language model", std::string(to_string(events::channel_scope::model))));
    what.add_choice(dpp::command_option_choice("Bots, as in a log channel", std::string(to_string(events::channel_scope::bots))));
    ignore.add_option(what);

    dpp::command_option hear(dpp::co_sub_command, "hear", "Stop ignoring a channel.");
    hear.add_option(channel_option("The channel to hear again."));

    const dpp::command_option list(dpp::co_sub_command, "list", "Show which channels I leave out.");

    payload.add_option(ignore);
    payload.add_option(hear);
    payload.add_option(list);
    return payload;
}

auto channels_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string action = subcommand_path(event.command.get_command_interaction());

    if (action == "ignore") {
        co_await this->ignore(event);
    } else if (action == "hear") {
        co_await this->hear(event);
    } else if (action == "list") {
        co_await this->list(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto channels_command::ignore(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto channel = snowflake_option(event, "channel");
    if (!channel || channel->empty()) {
        co_await event.co_reply(refusal(event, "which channel?"));
        co_return;
    }
    const std::string named = string_option(event, "what");
    const auto scope = named.empty() ? std::optional(events::channel_scope::everything) : events::channel_scope_from_string(named);
    if (!scope) {
        co_await event.co_reply(refusal(event, std::format("i don't know how to ignore \"{}\"", named)));
        co_return;
    }

    const bool changed = ignored_->ignore(event.command.guild_id, *channel, *scope);
    if (changed) {
        util::log().info("guild {} now ignores {} in channel {}, set by {}", event.command.guild_id, to_string(*scope), *channel,
                         describe_user(event.command.get_issuing_user()));
    }
    co_await event.co_reply(result(event, describe_ignored(*channel, *scope, changed)));
}

auto channels_command::hear(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto channel = snowflake_option(event, "channel");
    if (!channel || channel->empty()) {
        co_await event.co_reply(refusal(event, "which channel?"));
        co_return;
    }

    const bool removed = ignored_->hear(event.command.guild_id, *channel);
    if (removed) {
        util::log().info("guild {} hears channel {} again, set by {}", event.command.guild_id, *channel,
                         describe_user(event.command.get_issuing_user()));
    }

    std::string reply = removed ? std::format("ok, i'll hear <#{}> again", *channel) : std::format("i wasn't ignoring <#{}>", *channel);
    const auto destination = log_->status().destination;
    if (destination && destination->channel_id == *channel) {
        reply += "\n_It is my log channel, so i still ignore bots there: `/logs off` or another `/logs set` changes that._";
    }
    co_await event.co_reply(result(event, reply));
}

auto channels_command::list(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto listed = ignored_->for_guild(event.command.guild_id);
    const auto destination = log_->status().destination;
    const dpp::snowflake log_channel =
        destination && destination->guild_id == event.command.guild_id ? destination->channel_id : dpp::snowflake{};
    co_await event.co_reply(result(event, render_ignored_channels(listed, log_channel)));
}

} // namespace latibot::commands
