#include "core/commands/trigger.hpp"

#include "core/ui/paginator.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <charconv>
#include <format>
#include <string_view>
#include <utility>

namespace latibot::commands {
namespace {

dpp::message ack(std::string_view text) {
    dpp::message reply(text);
    reply.set_flags(dpp::m_ephemeral);
    return reply;
}

std::string string_option(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* text = std::get_if<std::string>(&value);
    return text == nullptr ? std::string{} : *text;
}

std::optional<std::int64_t> int_option(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* number = std::get_if<std::int64_t>(&value);
    return number == nullptr ? std::nullopt : std::optional<std::int64_t>(*number);
}

std::string subcommand_of(const dpp::slashcommand_t& event) {
    const dpp::command_interaction interaction = event.command.get_command_interaction();
    return interaction.options.empty() ? std::string{} : interaction.options.front().name;
}

/// Splits a leading "<weight> |" off a response line.
std::optional<int> leading_weight(std::string_view& line) {
    const std::size_t bar = line.find('|');
    if (bar == std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view head = util::trim(line.substr(0, bar));
    if (head.empty()) {
        return std::nullopt;
    }

    int weight = 0;
    const char* begin = head.data();
    const char* end = begin + head.size();
    const auto [stop, error] = std::from_chars(begin, end, weight);
    if (error != std::errc{} || stop != end || weight < 0) {
        return std::nullopt;
    }

    line = line.substr(bar + 1);
    return weight;
}

} // namespace

std::vector<events::weighted_response> parse_responses(std::string_view text) {
    std::vector<events::weighted_response> responses;

    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t newline = text.find('\n', at);
        std::string_view line =
            text.substr(at, newline == std::string_view::npos ? std::string_view::npos : newline - at);
        at = newline == std::string_view::npos ? text.size() + 1 : newline + 1;

        const std::optional<int> weight = leading_weight(line);
        const std::string_view body = util::trim(line);
        if (body.empty()) {
            continue;
        }

        responses.push_back({.text = std::string(body), .weight = weight.value_or(1)});
    }

    return responses;
}

std::string format_responses(std::span<const events::weighted_response> responses) {
    std::string text;
    for (const events::weighted_response& option : responses) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        // The weight is only written back when it is not the default, so a
        // trigger nobody weighted round-trips as plain lines.
        if (option.weight == 1) {
            text += option.text;
        } else {
            text += std::format("{} | {}", option.weight, option.text);
        }
    }
    return text;
}

std::string describe(const events::trigger& entry) {
    const std::string mode = entry.mode == events::match_mode::substring ? "anywhere" : "whole word";
    const std::string cooldown =
        entry.cooldown.count() == 0 ? "no cooldown" : std::format("{}s", entry.cooldown.count());

    std::string line = std::format("`{}` **{}** ({}, {}", entry.id, entry.pattern, mode, cooldown);
    if (!entry.enabled) {
        line += ", disabled";
    }
    line += std::format(") -> {} response{}", entry.responses.size(), entry.responses.size() == 1 ? "" : "s");
    return line;
}

dpp::message render_trigger_list(const events::trigger_store& store, dpp::snowflake guild_id, int page) {
    const std::vector<events::trigger> all = store.for_guild(guild_id);
    const int current = ui::clamp_page(page, all.size(), triggers_per_page);
    const ui::page_range window = ui::range_for(current, all.size(), triggers_per_page);

    std::string body;
    if (all.empty()) {
        body = "No triggers here yet. Add one with `/trigger add`.";
    } else {
        for (std::size_t index = window.begin; index < window.end; ++index) {
            body += describe(all[index]);
            body.push_back('\n');
        }
        body += std::format("\n_{}_", ui::page_label(current, all.size(), triggers_per_page));
    }

    dpp::message reply(body);
    reply.set_flags(dpp::m_ephemeral);

    const auto row = ui::controls({.view = std::string(trigger_list_view), .page = current, .argument = {}}, all.size(),
                                  triggers_per_page);
    if (row) {
        reply.add_component(*row);
    }
    return reply;
}

// --------------------------------------------------------------------------

trigger_command::trigger_command(events::trigger_store& store)
    : info_{.name = "trigger",
            .description = "Manage this server's trigger responses.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_messages),
            .guild_only = true},
      store_(&store) {}

dpp::slashcommand trigger_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option mode(dpp::co_string, "mode", "Whether the pattern must stand alone.", false);
    mode.add_choice(dpp::command_option_choice("Whole word", std::string("whole_word")));
    mode.add_choice(dpp::command_option_choice("Anywhere in the message", std::string("substring")));

    dpp::command_option add(dpp::co_sub_command, "add", "Add a trigger.");
    add.add_option(dpp::command_option(dpp::co_string, "pattern", "The text to look for.", true)
                       .set_min_length(1)
                       .set_max_length(200));
    add.add_option(
        dpp::command_option(dpp::co_string, "responses", "One per line. Prefix with \"3 | \" to weight a line.", true)
            .set_min_length(1)
            .set_max_length(2000));
    add.add_option(mode);
    add.add_option(
        dpp::command_option(dpp::co_integer, "cooldown", "Seconds between replies in one channel. 0 for none.", false)
            .set_min_value(0)
            .set_max_value(86400));

    dpp::command_option edit(dpp::co_sub_command, "edit", "Change a trigger.");
    edit.add_option(dpp::command_option(dpp::co_integer, "id", "From /trigger list.", true).set_min_value(1));
    edit.add_option(dpp::command_option(dpp::co_string, "pattern", "New text to look for.", false).set_max_length(200));
    edit.add_option(
        dpp::command_option(dpp::co_string, "responses", "Replaces every response.", false).set_max_length(2000));
    edit.add_option(mode);
    edit.add_option(dpp::command_option(dpp::co_integer, "cooldown", "Seconds. 0 for none.", false)
                        .set_min_value(0)
                        .set_max_value(86400));
    edit.add_option(dpp::command_option(dpp::co_boolean, "enabled", "Turn it on or off.", false));

    dpp::command_option remove(dpp::co_sub_command, "remove", "Delete a trigger.");
    remove.add_option(dpp::command_option(dpp::co_integer, "id", "From /trigger list.", true).set_min_value(1));

    const dpp::command_option list(dpp::co_sub_command, "list", "Show this server's triggers.");

    payload.add_option(add);
    payload.add_option(edit);
    payload.add_option(remove);
    payload.add_option(list);
    return payload;
}

dpp::task<void> trigger_command::execute(const dpp::slashcommand_t& event) {
    const std::string action = subcommand_of(event);

    if (action == "add") {
        co_await this->add(event);
    } else if (action == "edit") {
        co_await this->edit(event);
    } else if (action == "remove") {
        co_await this->remove(event);
    } else if (action == "list") {
        co_await this->list(event, 0);
    } else {
        co_await event.co_reply(ack("i don't know that subcommand"));
    }
}

dpp::task<void> trigger_command::add(const dpp::slashcommand_t& event) {
    const std::string pattern = std::string(util::trim(string_option(event, "pattern")));
    if (pattern.empty()) {
        co_await event.co_reply(ack("a pattern of only whitespace would match everything"));
        co_return;
    }

    const std::vector<events::weighted_response> responses = parse_responses(string_option(event, "responses"));
    if (responses.empty()) {
        co_await event.co_reply(ack("that leaves no responses to pick from"));
        co_return;
    }

    const std::string mode_text = string_option(event, "mode");
    const events::match_mode mode = events::match_mode_from_string(mode_text).value_or(events::match_mode::whole_word);
    const auto cooldown = int_option(event, "cooldown");

    const std::int64_t id =
        store_->add({.guild_id = event.command.guild_id,
                     .pattern = pattern,
                     .mode = mode,
                     .cooldown = std::chrono::seconds(cooldown.value_or(events::default_trigger_cooldown.count())),
                     .enabled = true,
                     .responses = responses});

    co_await event.co_reply(ack(std::format("added trigger `{}` for `{}` with {} response{}", id, pattern,
                                            responses.size(), responses.size() == 1 ? "" : "s")));
}

dpp::task<void> trigger_command::edit(const dpp::slashcommand_t& event) {
    const auto id = int_option(event, "id");
    if (!id) {
        co_await event.co_reply(ack("which trigger? run `/trigger list` for the ids"));
        co_return;
    }

    std::optional<events::trigger> entry = store_->find(*id, event.command.guild_id);
    if (!entry) {
        co_await event.co_reply(ack(std::format("no trigger `{}` in this server", *id)));
        co_return;
    }

    // Every option is optional: an edit changes what was given and leaves the
    // rest alone, so fixing a cooldown does not mean retyping the responses.
    const std::string pattern = std::string(util::trim(string_option(event, "pattern")));
    if (!pattern.empty()) {
        entry->pattern = pattern;
    }

    const std::string responses_text = string_option(event, "responses");
    if (!responses_text.empty()) {
        const auto responses = parse_responses(responses_text);
        if (responses.empty()) {
            co_await event.co_reply(ack("that leaves no responses to pick from"));
            co_return;
        }
        entry->responses = responses;
    }

    if (const auto mode = events::match_mode_from_string(string_option(event, "mode"))) {
        entry->mode = *mode;
    }
    if (const auto cooldown = int_option(event, "cooldown")) {
        entry->cooldown = std::chrono::seconds(*cooldown);
    }

    const dpp::command_value enabled = event.get_parameter("enabled");
    if (const auto* flag = std::get_if<bool>(&enabled)) {
        entry->enabled = *flag;
    }

    if (!store_->update(*entry)) {
        co_await event.co_reply(ack(std::format("no trigger `{}` in this server", *id)));
        co_return;
    }

    co_await event.co_reply(ack(std::format("updated {}", describe(*entry))));
}

dpp::task<void> trigger_command::remove(const dpp::slashcommand_t& event) {
    const auto id = int_option(event, "id");
    if (!id) {
        co_await event.co_reply(ack("which trigger? run `/trigger list` for the ids"));
        co_return;
    }

    if (!store_->remove(*id, event.command.guild_id)) {
        co_await event.co_reply(ack(std::format("no trigger `{}` in this server", *id)));
        co_return;
    }

    co_await event.co_reply(ack(std::format("removed trigger `{}`", *id)));
}

dpp::task<void> trigger_command::list(const dpp::slashcommand_t& event, int page) {
    co_await event.co_reply(render_trigger_list(*store_, event.command.guild_id, page));
}

} // namespace latibot::commands
