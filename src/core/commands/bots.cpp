#include "core/commands/bots.hpp"

#include "core/util/log.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::commands {
namespace {

std::string subcommand_of(const dpp::slashcommand_t& event) {
    const dpp::command_interaction interaction = event.command.get_command_interaction();
    return interaction.options.empty() ? std::string{} : interaction.options.front().name;
}

/// The bot the command names, and whether Discord agrees it is a bot.
struct named_bot {
    dpp::snowflake id;
    std::string name;
    bool is_bot = false;
};

std::optional<named_bot> bot_option(const dpp::slashcommand_t& event) {
    const dpp::command_value value = event.get_parameter("bot");
    const auto* id = std::get_if<dpp::snowflake>(&value);
    if (id == nullptr || *id == 0) {
        return std::nullopt;
    }

    // The interaction carries the user it resolved, so this needs no lookup.
    const auto found = event.command.resolved.users.find(*id);
    if (found == event.command.resolved.users.end()) {
        return named_bot{.id = *id, .name = id->str(), .is_bot = false};
    }
    return named_bot{.id = *id, .name = found->second.username, .is_bot = found->second.is_bot()};
}

} // namespace

std::string render_allowed_bots(std::span<const std::pair<dpp::snowflake, std::string>> known) {
    if (known.empty()) {
        return "No bots are allowed here. Every bot is ignored until you add one with `/bots allow`.";
    }

    std::string body = "**Bots this server lets me hear**\n";
    for (const auto& [id, name] : known) {
        body += name.empty() ? std::format("`{}` (not in this server any more)\n", id.str()) : std::format("`{}` {}\n", id.str(), name);
    }
    body += "\n_Hearing is not answering: a trigger also needs `bots:true`._";
    return body;
}

bots_command::bots_command(events::bot_allowlist& allowlist)
    : info_{.name = "bots",
            .description = "Choose which other bots I may hear.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_guild),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      allowlist_(&allowlist) {}

dpp::slashcommand bots_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option allow(dpp::co_sub_command, "allow", "Let me hear this bot.");
    allow.add_option(dpp::command_option(dpp::co_user, "bot", "The bot to allow.", true));

    dpp::command_option deny(dpp::co_sub_command, "deny", "Go back to ignoring this bot.");
    deny.add_option(dpp::command_option(dpp::co_user, "bot", "The bot to ignore.", true));

    const dpp::command_option list(dpp::co_sub_command, "list", "Show which bots I may hear.");

    payload.add_option(allow);
    payload.add_option(deny);
    payload.add_option(list);
    return payload;
}

dpp::task<void> bots_command::execute(const dpp::slashcommand_t& event) {
    const std::string action = subcommand_of(event);

    if (action == "allow") {
        co_await this->allow(event);
    } else if (action == "deny") {
        co_await this->deny(event);
    } else if (action == "list") {
        co_await this->list(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

dpp::task<void> bots_command::allow(const dpp::slashcommand_t& event) {
    const auto chosen = bot_option(event);
    if (!chosen) {
        co_await event.co_reply(refusal(event, "which bot?"));
        co_return;
    }

    // Allowing a human would be a no-op that looks like it worked, since the
    // pipeline only consults the list for messages from bots.
    if (!chosen->is_bot) {
        co_await event.co_reply(refusal(event, std::format("{} is not a bot, and i already hear everyone else", chosen->name)));
        co_return;
    }
    if (chosen->id == event.command.application_id) {
        co_await event.co_reply(refusal(event, "i already ignore myself on purpose, and that one is not negotiable"));
        co_return;
    }

    const bool added = allowlist_->allow(event.command.guild_id, chosen->id);
    if (added) {
        util::log().info("guild {} now hears bot {} ({}), allowed by {}", event.command.guild_id, chosen->name, chosen->id,
                         describe_user(event.command.get_issuing_user()));
    }
    co_await event.co_reply(result(event, added ? std::format("ok, i'll listen to {} now", chosen->name)
                                                : std::format("i was already listening to {}", chosen->name)));
}

dpp::task<void> bots_command::deny(const dpp::slashcommand_t& event) {
    const auto chosen = bot_option(event);
    if (!chosen) {
        co_await event.co_reply(refusal(event, "which bot?"));
        co_return;
    }

    const bool removed = allowlist_->deny(event.command.guild_id, chosen->id);
    if (removed) {
        util::log().info("guild {} no longer hears bot {} ({}), denied by {}", event.command.guild_id, chosen->name, chosen->id,
                         describe_user(event.command.get_issuing_user()));
    }
    co_await event.co_reply(result(event, removed ? std::format("ok, back to ignoring {}", chosen->name)
                                                  : std::format("i was not listening to {} anyway", chosen->name)));
}

dpp::task<void> bots_command::list(const dpp::slashcommand_t& event) {
    std::vector<std::pair<dpp::snowflake, std::string>> known;
    for (const dpp::snowflake id : allowlist_->for_guild(event.command.guild_id)) {
        const dpp::user* found = dpp::find_user(id);
        known.emplace_back(id, found == nullptr ? std::string{} : found->username);
    }

    co_await event.co_reply(result(event, render_allowed_bots(known)));
}

} // namespace latibot::commands
