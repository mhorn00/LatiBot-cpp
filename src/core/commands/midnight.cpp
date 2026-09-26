#include "core/commands/midnight.hpp"

#include "core/commands/message_options.hpp"
#include "core/commands/options.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::commands {
namespace {

/// The entry `id` names in this guild, or a complaint to send back.
struct lookup {
    std::optional<events::midnight_entry> entry;
    std::string problem;
};

auto entry_named(const events::midnight_store& store, const dpp::slashcommand_t& event) -> lookup {
    const auto id = int_option(event, "id");
    if (!id) return {.entry = std::nullopt, .problem = "which one? `/midnight list` has the numbers"};

    auto found = store.find(*id, event.command.guild_id);
    if (!found) return {.entry = std::nullopt, .problem = std::format("there is no midnight message {} here", *id)};
    return {.entry = std::move(found), .problem = {}};
}

} // namespace

auto describe(const events::midnight_entry& entry) -> std::string {
    std::string line = std::format("`{}` <#{}> **{}**", entry.id, entry.channel_id.str(), entry.timezone);
    std::string notes = entry.enabled ? std::string{} : std::string("off");
    if (const std::string options = describe_message_options(entry.message_flags); !options.empty()) {
        notes += notes.empty() ? options : ", " + options;
    }
    if (!notes.empty()) line += std::format(" ({})", notes);
    line += std::format("\n> {}", entry.message);

    if (!entry.last_fired_date.empty()) line += std::format("\n_last posted {}_", entry.last_fired_date);
    return line;
}

auto render_midnight_list(std::span<const events::midnight_entry> entries) -> std::string {
    if (entries.empty()) return "Nothing posts at midnight here. Add one with `/midnight add`.";

    std::string body = "**Midnight messages**\n";
    for (const events::midnight_entry& entry : entries) {
        body += describe(entry);
        body.push_back('\n');
    }
    return body;
}

// --------------------------------------------------------------------------

midnight_command::midnight_command(events::midnight_store& store, ports::clock& clock)
    : info_{.name = "midnight",
            .description = "Post a message at midnight.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_guild),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = dpp::m_suppress_notifications},
            .subcommand_responses = {}},
      store_(&store),
      clock_(&clock) {}

auto midnight_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    // set_auto_complete, not a list of choices: there are hundreds of zones,
    // and Discord allows twenty-five.
    const auto timezone = [](bool required) {
        return dpp::command_option(dpp::co_string, "timezone", "Whose midnight, e.g. America/Chicago.", required).set_auto_complete(true);
    };

    dpp::command_option add(dpp::co_sub_command, "add", "Add a midnight message.");
    add.add_option(timezone(true));
    add.add_option(dpp::command_option(dpp::co_channel, "channel", "Where to post it.", true));
    add.add_option(dpp::command_option(dpp::co_string, "message", "What to post.", true).set_min_length(1).set_max_length(2000));
    add_message_options(add);

    dpp::command_option edit(dpp::co_sub_command, "edit", "Change a midnight message.");
    edit.add_option(dpp::command_option(dpp::co_integer, "id", "From /midnight list.", true).set_min_value(1));
    edit.add_option(timezone(false));
    edit.add_option(dpp::command_option(dpp::co_channel, "channel", "Where to post it.", false));
    edit.add_option(dpp::command_option(dpp::co_string, "message", "What to post.", false).set_max_length(2000));
    add_message_options(edit);

    dpp::command_option remove(dpp::co_sub_command, "remove", "Delete a midnight message.");
    remove.add_option(dpp::command_option(dpp::co_integer, "id", "From /midnight list.", true).set_min_value(1));

    dpp::command_option toggle(dpp::co_sub_command, "toggle", "Turn one off without deleting it.");
    toggle.add_option(dpp::command_option(dpp::co_integer, "id", "From /midnight list.", true).set_min_value(1));

    const dpp::command_option list(dpp::co_sub_command, "list", "Show this server's midnight messages.");

    payload.add_option(add);
    payload.add_option(edit);
    payload.add_option(remove);
    payload.add_option(toggle);
    payload.add_option(list);
    return payload;
}

auto midnight_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || focused->name != "timezone" || event.owner == nullptr) return;

    const auto* typed = std::get_if<std::string>(&focused->value);
    dpp::interaction_response reply(dpp::ir_autocomplete_reply);
    for (const std::string& zone : events::matching_timezones(typed == nullptr ? std::string_view{} : *typed, autocomplete_limit)) {
        reply.add_autocomplete_choice(dpp::command_option_choice(zone, zone));
    }

    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

auto midnight_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string action = subcommand_path(event.command.get_command_interaction());

    if (action == "add") {
        co_await this->add(event);
    } else if (action == "edit") {
        co_await this->edit(event);
    } else if (action == "remove") {
        co_await this->remove(event);
    } else if (action == "toggle") {
        co_await this->toggle(event);
    } else if (action == "list") {
        co_await this->list(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto midnight_command::add(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string timezone = string_option(event, "timezone");
    if (!events::is_known_timezone(timezone)) {
        co_await event.co_reply(
            refusal(event, std::format("\"{}\" is not a timezone i know. Pick one from the list as you type.", timezone)));
        co_return;
    }

    const auto channel = snowflake_option(event, "channel");
    if (!channel) {
        co_await event.co_reply(refusal(event, "which channel?"));
        co_return;
    }

    // Today is recorded as already posted, so this first posts at the next
    // midnight rather than thirty seconds from now (plan §10).
    events::midnight_entry entry{.id = 0,
                                 .guild_id = event.command.guild_id,
                                 .channel_id = *channel,
                                 .timezone = timezone,
                                 .message = string_option(event, "message"),
                                 .enabled = true,
                                 .last_fired_date = events::already_posted_today(timezone, clock_->now())};
    apply_message_options(event, entry.message_flags);

    const std::int64_t id = store_->add(entry);
    util::log().info("midnight message {} added in guild {} by {}: {} in channel {} ({})", id, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()), timezone, *channel, discord::describe_flags(entry.message_flags));

    co_await event.co_reply(result(event, std::format("ok, that posts in <#{}> at the next midnight in {}", channel->str(), timezone)));
}

auto midnight_command::edit(const dpp::slashcommand_t& event) -> dpp::task<void> {
    auto [found, problem] = entry_named(*store_, event);
    if (!found) {
        co_await event.co_reply(refusal(event, problem));
        co_return;
    }

    if (const std::string timezone = string_option(event, "timezone"); !timezone.empty()) {
        if (!events::is_known_timezone(timezone)) {
            co_await event.co_reply(refusal(event, std::format("\"{}\" is not a timezone i know", timezone)));
            co_return;
        }
        found->timezone = timezone;
    }
    if (const auto channel = snowflake_option(event, "channel")) found->channel_id = *channel;
    if (const std::string message = string_option(event, "message"); !message.empty()) found->message = message;
    apply_message_options(event, found->message_flags);

    store_->update(*found);
    util::log().info("midnight message {} edited in guild {} by {}: {} in channel {} ({})", found->id, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()), found->timezone, found->channel_id,
                     discord::describe_flags(found->message_flags));

    co_await event.co_reply(
        result(event, std::format("ok, {} posts in <#{}> at midnight in {}", found->id, found->channel_id.str(), found->timezone)));
}

auto midnight_command::remove(const dpp::slashcommand_t& event) -> dpp::task<void> {
    auto [found, problem] = entry_named(*store_, event);
    if (!found) {
        co_await event.co_reply(refusal(event, problem));
        co_return;
    }

    store_->remove(found->id, event.command.guild_id);
    util::log().info("midnight message {} removed from guild {} by {}", found->id, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()));

    co_await event.co_reply(result(event, std::format("gone: midnight message {}", found->id)));
}

auto midnight_command::toggle(const dpp::slashcommand_t& event) -> dpp::task<void> {
    auto [found, problem] = entry_named(*store_, event);
    if (!found) {
        co_await event.co_reply(refusal(event, problem));
        co_return;
    }

    found->enabled = !found->enabled;
    store_->update(*found);

    const std::string_view became = found->enabled ? "on" : "off";
    util::log().info("midnight message {} turned {} in guild {} by {}", found->id, became, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()));

    co_await event.co_reply(result(event, std::format("midnight message {} is {}", found->id, became)));
}

auto midnight_command::list(const dpp::slashcommand_t& event) -> dpp::task<void> {
    co_await event.co_reply(result(event, render_midnight_list(store_->for_guild(event.command.guild_id))));
}

} // namespace latibot::commands
