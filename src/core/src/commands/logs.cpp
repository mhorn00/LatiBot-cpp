#include "commands/logs.hpp"

#include "core/commands/options.hpp"
#include "core/util/log.hpp"

#include <dpp/channel.h>
#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>
#include <dpp/permissions.h>

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::commands {
namespace {

/// The level option, offered as what each one means rather than as bare
/// names. Off is left out: that is `/logs off`.
auto level_option(bool required) -> dpp::command_option {
    dpp::command_option level(dpp::co_string, "level", "The lowest level to post.", required);
    level.add_choice(dpp::command_option_choice("error: failures only", std::string("error")));
    level.add_choice(dpp::command_option_choice("warn: and anything that went wrong", std::string("warn")));
    level.add_choice(dpp::command_option_choice("info: and what the bot did", std::string("info")));
    level.add_choice(dpp::command_option_choice("debug: and why", std::string("debug")));
    level.add_choice(dpp::command_option_choice("trace: everything, every message seen included", std::string("trace")));
    return level;
}

auto chosen_level(const dpp::slashcommand_t& event) -> std::optional<util::log_level> {
    const auto level = util::log_level_from_string(string_option(event, "level"));
    if (!level || *level == util::log_level::off) return std::nullopt;
    return level;
}

auto describe_level(util::log_level level) -> std::string {
    if (level == util::log_level::trace) return "everything";
    return std::format("**{}** and above", util::to_string(level));
}

constexpr std::string_view untrusted_refusal =
    "only the people listed in `trusted_users` in my config.json can choose where my log goes, since it covers every server i'm in";

constexpr std::string_view nowhere_reply = "my log isn't posted anywhere. `/logs set` picks a channel for it.";

} // namespace

auto may_configure_logs(const config::bootstrap& settings, dpp::snowflake user_id) -> bool {
    return std::ranges::find(settings.trusted_users, user_id) != settings.trusted_users.end();
}

auto render_log_channel(const events::log_channel_status& status, dpp::snowflake here) -> std::string {
    if (!status.destination) return std::string(nowhere_reply);

    const events::log_destination& destination = *status.destination;
    std::string body = std::format("my log goes to <#{}>", destination.channel_id.str());
    if (destination.guild_id != here) body += std::format(", in another server (`{}`)", destination.guild_id.str());
    body += std::format(": {}.", describe_level(destination.level));

    if (status.waiting > 0) body += std::format("\n{} line(s) waiting to be posted.", status.waiting);
    if (status.failure) {
        body += std::format("\nposting there is failing: {}. trying again in {}.", *status.failure, status.retry_in);
    }
    return body;
}

logs_command::logs_command(const config::bootstrap& settings, events::log_destination_store& store, events::log_channel& channel,
                           ports::discord_gateway& discord)
    : info_{.name = "logs",
            .description = "Choose the one channel my log is posted to.",
            .aliases = {},
            .required_bot_permissions = 0,
            // Hidden from everyone but administrators; trusted_users decides
            // who can actually use it.
            .default_member_permissions = dpp::permission(dpp::p_administrator),
            .guild_only = true,
            // The channel's first message is silent, as every line after it is.
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = dpp::m_suppress_notifications},
            .subcommand_responses = {}},
      settings_(&settings),
      store_(&store),
      channel_(&channel),
      discord_(&discord) {}

auto logs_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option set(dpp::co_sub_command, "set", "Post my log in this channel, and nowhere else.");
    set.add_option(dpp::command_option(dpp::co_channel, "channel", "Where to post it.", true)
                       .add_channel_type(dpp::CHANNEL_TEXT)
                       .add_channel_type(dpp::CHANNEL_ANNOUNCEMENT));
    set.add_option(level_option(false));

    dpp::command_option level(dpp::co_sub_command, "level", "Change which lines are posted.");
    level.add_option(level_option(true));

    const dpp::command_option off(dpp::co_sub_command, "off", "Stop posting my log.");
    const dpp::command_option show(dpp::co_sub_command, "show", "Where my log is posted, and whether that is working.");

    payload.add_option(set);
    payload.add_option(level);
    payload.add_option(off);
    payload.add_option(show);
    return payload;
}

auto logs_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    if (!may_configure_logs(*settings_, event.command.get_issuing_user().id)) {
        co_await event.co_reply(refusal(event, untrusted_refusal));
        co_return;
    }

    const std::string action = subcommand_path(event.command.get_command_interaction());
    if (action == "set") {
        co_await this->set(event);
    } else if (action == "level") {
        co_await this->level(event);
    } else if (action == "off") {
        co_await this->off(event);
    } else if (action == "show") {
        co_await this->show(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto logs_command::set(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto channel = snowflake_option(event, "channel");
    if (!channel || channel->empty()) {
        co_await event.co_reply(refusal(event, "which channel?"));
        co_return;
    }

    // Moving keeps the level unless a new one is given.
    const std::optional<events::log_destination> previous = store_->find();
    const util::log_level level = chosen_level(event).value_or(previous ? previous->level : util::log_level::info);
    const events::log_destination destination{.guild_id = event.command.guild_id, .channel_id = *channel, .level = level};

    // Posting the first message is the test that the bot can post there at
    // all, and it can take longer than a first answer's three seconds.
    co_await defer(event);
    dpp::message greeting(*channel, events::log_channel_greeting(level));
    greeting.set_allowed_mentions();
    const auto posted = co_await discord_->send_message(post(event, std::move(greeting)));
    if (!posted) {
        util::log().debug("/logs set could not post in channel {}: {}", *channel, posted.error().message);
        co_await answer_deferred(
            event, refusal(event, std::format("i can't post in <#{}> ({}), so nothing changed", channel->str(), posted.error().message)));
        co_return;
    }

    store_->save(destination);
    channel_->start(destination);

    // After start, so this is the first line the channel gets.
    util::log().info("the log is now posted to channel {} in guild {} at {}, set by {}", *channel, event.command.guild_id,
                     util::to_string(level), describe_user(event.command.get_issuing_user()));
    co_await answer_deferred(event,
                             result(event, std::format("ok, my log goes to <#{}> from now on: {}", channel->str(), describe_level(level))));
}

auto logs_command::level(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto level = chosen_level(event);
    if (!level) {
        co_await event.co_reply(refusal(event, "which level?"));
        co_return;
    }

    auto destination = store_->find();
    if (!destination) {
        co_await event.co_reply(refusal(event, nowhere_reply));
        co_return;
    }

    destination->level = *level;
    store_->save(*destination);
    channel_->start(*destination);

    util::log().info("the log channel now gets {} and above, set by {}", util::to_string(*level),
                     describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, std::format("ok, my log channel gets {} from now on", describe_level(*level))));
}

auto logs_command::off(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const bool was_on = store_->clear();
    channel_->stop();

    if (was_on) {
        util::log().info("the log is no longer posted to a channel, turned off by {}", describe_user(event.command.get_issuing_user()));
    }
    co_await event.co_reply(result(event, was_on ? "ok, my log isn't posted anywhere now" : "my log wasn't posted anywhere anyway"));
}

auto logs_command::show(const dpp::slashcommand_t& event) -> dpp::task<void> {
    co_await event.co_reply(result(event, render_log_channel(channel_->status(), event.command.guild_id)));
}

} // namespace latibot::commands
