#include "trigger_command.hpp"

#include "core/commands/message_options.hpp"
#include "core/commands/options.hpp"
#include "core/ui/interaction.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <string_view>
#include <utility>

namespace latibot::commands {
namespace {

/// The longest pattern and response list, in characters, for the command
/// and the panel's form alike.
constexpr std::uint32_t pattern_length_limit = 200;
constexpr std::uint32_t responses_length_limit = 2000;

/// Discord's limit on a select option's label.
constexpr std::size_t select_option_limit = 100;

/// The longest cooldown, in seconds: a day.
constexpr std::int64_t cooldown_limit = 86400;

/// Room for a page of triggers in a message, less its heading, page label
/// and note. Eight at a full 200 characters of pattern would pass Discord's
/// 2000, and one message too long makes Discord refuse the whole panel.
constexpr std::size_t lines_budget = 1650;

/// The longest note under the panel.
constexpr std::size_t note_limit = 200;

/// Discord's limit on a message.
constexpr std::size_t message_limit = 2000;

/// What to put in the persona option to mean LatiBot itself, for a trigger
/// given back from a persona.
constexpr std::string_view latibot_choice = "LatiBot";

/// The mode as the form's field takes it, which is how its label names it.
auto form_mode(events::match_mode mode) -> std::string_view {
    return mode == events::match_mode::whole_word ? "word" : util::mode_label(mode);
}

/// A chance as a whole percent: 0.25 is "25".
auto percent(double chance) -> std::string {
    return std::format("{:.0f}", chance * 100);
}

/// Reads a whole number from `low` to `high` out of free text.
auto read_number(std::string_view text, std::int64_t low, std::int64_t high) -> std::optional<std::int64_t> {
    const std::string_view trimmed = util::trim(text);
    std::int64_t value = 0;
    const auto [stop, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (error != std::errc{} || stop != trimmed.data() + trimmed.size() || value < low || value > high) return std::nullopt;
    return value;
}

/// How many options the subcommand was given.
auto given_options(const dpp::slashcommand_t& event) -> std::size_t {
    const dpp::command_interaction command = event.command.get_command_interaction();
    const std::vector<dpp::command_data_option>* options = &command.options;
    while (options->size() == 1 && ((*options)[0].type == dpp::co_sub_command_group || (*options)[0].type == dpp::co_sub_command)) {
        options = &(*options)[0].options;
    }
    return options->size();
}

/// Who answers a model trigger, as its line says: nothing for LatiBot.
auto answered_by(const events::trigger& entry, std::span<const capabilities::persona_name> personas) -> std::string {
    if (entry.persona_id == 0) return {};
    const auto found = std::ranges::find(personas, entry.persona_id, &capabilities::persona_name::id);
    if (found == personas.end()) return ", as a persona that is gone";
    return std::format(", as {}{}", found->name, found->enabled ? "" : " (off)");
}

/// Splits a leading "<weight> |" off a response line.
auto leading_weight(std::string_view& line) -> std::optional<int> {
    const std::size_t bar = line.find('|');
    if (bar == std::string_view::npos) return std::nullopt;

    const std::string_view head = util::trim(line.substr(0, bar));
    if (head.empty()) return std::nullopt;

    int weight = 0;
    const char* begin = head.data();
    const char* end = begin + head.size();
    const auto [stop, error] = std::from_chars(begin, end, weight);
    if (error != std::errc{} || stop != end || weight < 0) return std::nullopt;

    line = line.substr(bar + 1);
    return weight;
}

} // namespace

auto parse_responses(std::string_view text) -> std::vector<events::weighted_response> {
    std::vector<events::weighted_response> responses;

    for (std::string_view line : util::lines(text)) {
        const std::optional<int> weight = leading_weight(line);
        const std::string_view body = util::trim(line);
        if (body.empty()) continue;

        responses.push_back({.text = std::string(body), .weight = weight.value_or(1)});
    }

    return responses;
}

auto format_responses(std::span<const events::weighted_response> responses) -> std::string {
    std::string text;
    for (const events::weighted_response& option : responses) {
        if (!text.empty()) text.push_back('\n');
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

auto describe(const events::trigger& entry, std::span<const capabilities::persona_name> personas) -> std::string {
    const std::string cooldown = entry.cooldown.count() == 0 ? "no cooldown" : std::format("{}s", entry.cooldown.count());

    // What sets it off: its pattern, or being addressed or replied to.
    std::string line;
    if (entry.condition == events::trigger_condition::text) {
        // A regular expression is code, and its * and _ would read as markdown.
        const std::string pattern =
            entry.mode == events::match_mode::regex ? std::format("`{}`", entry.pattern) : std::format("**{}**", entry.pattern);
        line = std::format("`{}` {} ({}, {}", entry.id, pattern, util::mode_label(entry.mode), cooldown);
    } else if (entry.condition == events::trigger_condition::addressed) {
        line = std::format("`{}` *LatiBot addressed* ({}", entry.id, cooldown);
    } else {
        const std::string whose =
            entry.persona_id == 0 ? std::string("LatiBot") : std::string(util::trim(answered_by(entry, personas)).substr(4));
        line = std::format("`{}` *a reply to {}* ({}", entry.id, whose, cooldown);
    }

    const events::trigger_kind kind = entry.kind();
    if (kind == events::trigger_kind::model && entry.condition == events::trigger_condition::text)
        line += std::format(", {}%", percent(entry.chance));
    if (kind != events::trigger_kind::responses && entry.condition != events::trigger_condition::reply)
        line += answered_by(entry, personas);
    if (!entry.enabled) line += ", disabled";
    if (entry.respond_to_bots) line += ", answers bots";
    if (entry.reply && entry.persona_id == 0) line += ", replies";
    if (!entry.builtin.empty()) line += ", built in";

    // The model's answers are posted as it posts them, so the silent and
    // previews choices are only a fixed reply's.
    switch (kind) {
    case events::trigger_kind::analysis:
        line += std::format(") -> if the check says yes to \"{}\", the model answers", entry.question);
        return entry.prompt.empty() ? line : line + std::format(": {}", entry.prompt);
    case events::trigger_kind::model:
        if (entry.condition == events::trigger_condition::text) return line + std::format(") -> asks the model: {}", entry.prompt);
        return entry.prompt.empty() ? line + ") -> the model answers" : line + std::format(") -> the model answers: {}", entry.prompt);
    case events::trigger_kind::responses:
        break;
    }
    if (const std::string options = describe_message_options(entry.message_flags); !options.empty()) line += ", " + options;
    line += std::format(") -> {} response{}", entry.responses.size(), entry.responses.size() == 1 ? "" : "s");
    return line;
}

auto describe_in_full(std::string_view heading, const events::trigger& entry, std::span<const capabilities::persona_name> personas)
    -> std::string {
    std::string text = std::format("{} {}", heading, describe(entry, personas));
    if (!entry.asks_model()) {
        text += ':';
        for (const events::weighted_response& option : entry.responses) {
            text += option.weight == 1 ? std::format("\n- {}", option.text) : std::format("\n- {} | {}", option.weight, option.text);
        }
    }
    return util::truncate(text, message_limit);
}

auto render_trigger_list(const events::trigger_store& store, dpp::snowflake guild_id, int page, const capabilities::model_replies* model)
    -> dpp::message {
    const std::vector<events::trigger> all = store.for_guild(guild_id);
    const int current = ui::clamp_page(page, all.size(), triggers_per_page);
    const ui::page_range window = ui::range_for(current, all.size(), triggers_per_page);

    std::string body;
    if (all.empty()) {
        body = "No triggers here yet. Add one with `/trigger add`.";
    } else {
        const std::vector<capabilities::persona_name> personas =
            model == nullptr ? std::vector<capabilities::persona_name>{} : model->personas(guild_id);
        std::vector<std::string> lines;
        for (std::size_t index = window.begin; index < window.end; ++index) {
            lines.push_back(describe(all[index], personas));
        }
        body += util::fit_lines(lines, lines_budget);
        body += std::format("\n_{}_", ui::page_label(current, all.size(), triggers_per_page));
    }

    dpp::message reply(body);

    const auto row = ui::controls({.view = std::string(trigger_list_view), .page = current, .argument = {}}, all.size(), triggers_per_page);
    if (row) reply.add_component(*row);
    return reply;
}

// --------------------------------------------------------------------------

trigger_command::trigger_command(events::trigger_store& store, const capabilities::model_replies* model)
    : info_{.name = "trigger",
            .description = "Manage this server's trigger responses.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_messages),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      store_(&store),
      model_(model) {}

auto trigger_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option mode(dpp::co_string, "mode", "Whether the pattern must stand alone, or is a regular expression.", false);
    mode.add_choice(dpp::command_option_choice("Whole word", std::string("whole_word")));
    mode.add_choice(dpp::command_option_choice("Anywhere in the message", std::string("substring")));
    mode.add_choice(dpp::command_option_choice("Regular expression", std::string("regex")));

    // The model's options only when it is built in
    // (src/modules/triggers/docs/Triggers.md §2.6).
    const auto add_model_options = [this](dpp::command_option& subcommand, bool editing) {
        if (model_ == nullptr) return;
        subcommand.add_option(dpp::command_option(dpp::co_string, "prompt",
                                                  editing ? "Makes it ask the model this, in a line, instead."
                                                          : "Ask the model to say this, in a line, instead of responses.",
                                                  false)
                                  .set_max_length(static_cast<std::int64_t>(events::prompt_limit)));
        subcommand.add_option(dpp::command_option(dpp::co_string, "question",
                                                  editing ? "Makes it an analysis: the check model's yes-or-no question first."
                                                          : "An analysis: a yes-or-no question for the check model first, then the "
                                                            "model answers.",
                                                  false)
                                  .set_max_length(static_cast<std::int64_t>(events::question_limit)));
        subcommand.add_option(dpp::command_option(dpp::co_integer, "chance",
                                                  editing ? "Percent of matches the model answers."
                                                          : "Percent of matches the model "
                                                            "answers. 100 by default.",
                                                  false)
                                  .set_min_value(1)
                                  .set_max_value(100));
        subcommand.add_option(
            dpp::command_option(dpp::co_string, "persona", "A persona to answer it rather than LatiBot; LatiBot to give it back.", false)
                .set_auto_complete(true));
        if (editing) return;
        dpp::command_option when(dpp::co_string, "when", "What sets it off. Its pattern, by default.", false);
        when.add_choice(dpp::command_option_choice("Its pattern matches", std::string("text")));
        when.add_choice(dpp::command_option_choice("LatiBot is @mentioned or named first", std::string("addressed")));
        when.add_choice(dpp::command_option_choice("A reply to whoever answers it", std::string("reply")));
        subcommand.add_option(when);
    };

    dpp::command_option add(dpp::co_sub_command, "add", "Add a trigger.");
    add.add_option(dpp::command_option(dpp::co_string, "pattern", "The text to look for.", false)
                       .set_min_length(1)
                       .set_max_length(pattern_length_limit));
    add.add_option(dpp::command_option(dpp::co_string, "responses", "One per line. Prefix with \"3 | \" to weight a line.", false)
                       .set_min_length(1)
                       .set_max_length(responses_length_limit));
    add_model_options(add, false);
    add.add_option(mode);
    add.add_option(dpp::command_option(dpp::co_integer, "cooldown", "Seconds between replies in one channel. 0 for none.", false)
                       .set_min_value(0)
                       .set_max_value(cooldown_limit));
    add.add_option(dpp::command_option(dpp::co_boolean, "bots", "Also answer allowed bots. See /bots.", false));
    add.add_option(dpp::command_option(dpp::co_boolean, "reply",
                                       "Answer as a reply to the message. Off by default; on for the model's answers.", false));
    add_message_options(add);

    dpp::command_option edit(dpp::co_sub_command, "edit", "Change a trigger. Given only the id, it opens in a form.");
    edit.add_option(dpp::command_option(dpp::co_integer, "id", "From /trigger list.", true).set_min_value(1));
    edit.add_option(dpp::command_option(dpp::co_string, "pattern", "New text to look for.", false).set_max_length(pattern_length_limit));
    edit.add_option(
        dpp::command_option(dpp::co_string, "responses", "Replaces every response.", false).set_max_length(responses_length_limit));
    add_model_options(edit, true);
    edit.add_option(mode);
    edit.add_option(
        dpp::command_option(dpp::co_integer, "cooldown", "Seconds. 0 for none.", false).set_min_value(0).set_max_value(cooldown_limit));
    edit.add_option(dpp::command_option(dpp::co_boolean, "enabled", "Turn it on or off.", false));
    edit.add_option(dpp::command_option(dpp::co_boolean, "bots", "Also answer allowed bots. See /bots.", false));
    edit.add_option(dpp::command_option(dpp::co_boolean, "reply", "Answer as a reply to the message.", false));
    add_message_options(edit);

    dpp::command_option remove(dpp::co_sub_command, "remove", "Delete a trigger. A built-in one can only be turned off.");
    remove.add_option(dpp::command_option(dpp::co_integer, "id", "From /trigger list.", true).set_min_value(1));

    const dpp::command_option list(dpp::co_sub_command, "list", "Show this server's triggers.");
    const dpp::command_option panel(dpp::co_sub_command, "panel", "Open the trigger panel.");

    payload.add_option(add);
    payload.add_option(edit);
    payload.add_option(remove);
    payload.add_option(list);
    payload.add_option(panel);
    return payload;
}

auto trigger_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string action = subcommand_path(event.command.get_command_interaction());

    if (action == "add") {
        co_await this->add(event);
    } else if (action == "edit") {
        co_await this->edit(event);
    } else if (action == "remove") {
        co_await this->remove(event);
    } else if (action == "list") {
        co_await this->list(event, 0);
    } else if (action == "panel") {
        co_await this->panel(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto trigger_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || event.owner == nullptr || focused->name != "persona" || model_ == nullptr) return;

    const auto* typed = std::get_if<std::string>(&focused->value);
    const std::string wanted = typed == nullptr ? std::string{} : util::to_lower(util::trim(*typed));
    std::vector<std::string> names{std::string(latibot_choice)};
    for (const capabilities::persona_name& who : model_->personas(event.command.guild_id)) {
        names.push_back(who.name);
    }

    // Discord shows at most 25.
    dpp::interaction_response reply(dpp::ir_autocomplete_reply);
    std::size_t offered = 0;
    for (const std::string& name : names) {
        if (offered == 25) break;
        if (!wanted.empty() && !util::to_lower(name).contains(wanted)) continue;
        reply.add_autocomplete_choice(dpp::command_option_choice(name, name));
        ++offered;
    }
    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

auto trigger_command::personas(dpp::snowflake guild) const -> std::vector<capabilities::persona_name> {
    return model_ == nullptr ? std::vector<capabilities::persona_name>{} : model_->personas(guild);
}

auto trigger_command::apply_persona(const dpp::slashcommand_t& event, events::trigger& entry) const -> std::optional<std::string> {
    const std::string named(util::trim(string_option(event, "persona")));
    if (named.empty() || model_ == nullptr) return std::nullopt;
    if (util::equals_ignoring_case(named, latibot_choice)) {
        entry.persona_id = 0;
        return std::nullopt;
    }
    for (const capabilities::persona_name& who : model_->personas(event.command.guild_id)) {
        if (util::equals_ignoring_case(who.name, named)) {
            entry.persona_id = who.id;
            return std::nullopt;
        }
    }
    return std::format("there's no persona called \"{}\" here", named);
}

auto trigger_command::add(const dpp::slashcommand_t& event) -> dpp::task<void> {
    // Responses to pick from, a prompt for the model, or a question for the
    // check: responses or the model, not both
    // (src/modules/triggers/docs/Triggers.md §2.6).
    events::trigger entry{.guild_id = event.command.guild_id,
                          .pattern = std::string(util::trim(string_option(event, "pattern"))),
                          .mode = events::match_mode_from_string(string_option(event, "mode")).value_or(events::match_mode::whole_word),
                          .enabled = true,
                          .respond_to_bots = bool_option(event, "bots").value_or(false),
                          .responses = parse_responses(string_option(event, "responses"))};
    if (model_ != nullptr) {
        entry.prompt = std::string(util::trim(string_option(event, "prompt")));
        entry.question = std::string(util::trim(string_option(event, "question")));
        entry.condition = events::trigger_condition_from_string(string_option(event, "when")).value_or(events::trigger_condition::text);
    }
    if (!entry.responses.empty() && (!entry.prompt.empty() || !entry.question.empty())) {
        co_await event.co_reply(refusal(event, "a trigger has responses or asks the model, not both"));
        co_return;
    }
    if (entry.responses.empty() && entry.prompt.empty() && entry.question.empty() && entry.condition == events::trigger_condition::text) {
        co_await event.co_reply(refusal(event, model_ == nullptr
                                                   ? "that leaves no responses to pick from"
                                                   : "give it `responses:` to pick from, or a `prompt:` or `question:` for the model"));
        co_return;
    }

    // A comment on a match waits longer, since each is a model call; being
    // spoken to, or a check, never had a cooldown.
    const events::trigger_kind kind = entry.kind();
    std::chrono::seconds fallback = events::default_trigger_cooldown;
    if (kind == events::trigger_kind::model) {
        fallback = entry.condition == events::trigger_condition::text ? events::default_model_cooldown : std::chrono::seconds{0};
    } else if (kind == events::trigger_kind::analysis) {
        fallback = std::chrono::seconds{0};
    }
    entry.cooldown = std::chrono::seconds(int_option(event, "cooldown").value_or(fallback.count()));
    // The model's answers have always been replies.
    entry.reply = bool_option(event, "reply").value_or(entry.asks_model());
    apply_message_options(event, entry.message_flags);
    if (const auto chance = int_option(event, "chance"); chance && entry.asks_model()) entry.chance = static_cast<double>(*chance) / 100.0;
    if (const auto problem = apply_persona(event, entry)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }
    if (const auto problem = events::trigger_problem(entry)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    entry.id = store_->add(entry);
    util::log().info("trigger {} added in guild {} by {}: {}", entry.id, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()), describe(entry));
    co_await event.co_reply(result(event, describe_in_full("added", entry, personas(event.command.guild_id))));
}

auto trigger_command::edit(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto id = int_option(event, "id");
    if (!id) {
        co_await event.co_reply(refusal(event, "which trigger? run `/trigger list` for the ids"));
        co_return;
    }

    std::optional<events::trigger> entry = store_->find(*id, event.command.guild_id);
    if (!entry) {
        co_await event.co_reply(refusal(event, std::format("no trigger `{}` in this server", *id)));
        co_return;
    }
    // Given nothing to change, it opens a form holding the trigger as it is,
    // since a command's options cannot start out filled in.
    if (given_options(event) <= 1) {
        const auto opened = co_await event.co_dialog(trigger_form(0, &*entry, entry->kind(), trigger_command_form_view));
        if (opened.is_error()) util::log().warn("could not open the form for trigger {}: {}", *id, opened.get_error().message);
        co_return;
    }

    // Every option is optional: an edit changes what was given and leaves the
    // rest alone, so fixing a cooldown does not mean retyping the responses.
    // Responses, a prompt or a question also say which kind it is from now
    // on, except for a built-in one, which keeps its kind.
    const events::trigger before = *entry;
    const std::string pattern = std::string(util::trim(string_option(event, "pattern")));
    if (!pattern.empty()) entry->pattern = pattern;

    const std::string prompt = model_ == nullptr ? std::string{} : std::string(util::trim(string_option(event, "prompt")));
    const std::string question = model_ == nullptr ? std::string{} : std::string(util::trim(string_option(event, "question")));
    const std::string responses_text = string_option(event, "responses");
    if ((!prompt.empty() || !question.empty()) && !responses_text.empty()) {
        co_await event.co_reply(refusal(event, "a trigger has responses or asks the model, not both"));
        co_return;
    }
    if (!prompt.empty()) {
        entry->prompt = prompt;
        entry->responses.clear();
    }
    if (!question.empty()) {
        entry->question = question;
        entry->responses.clear();
    }
    if (!responses_text.empty()) {
        const auto responses = parse_responses(responses_text);
        if (responses.empty()) {
            co_await event.co_reply(refusal(event, "that leaves no responses to pick from"));
            co_return;
        }
        entry->responses = responses;
        entry->prompt.clear();
        entry->question.clear();
    }

    if (const auto mode = events::match_mode_from_string(string_option(event, "mode"))) entry->mode = *mode;
    if (const auto cooldown = int_option(event, "cooldown")) entry->cooldown = std::chrono::seconds(*cooldown);
    if (const auto chance = int_option(event, "chance")) entry->chance = static_cast<double>(*chance) / 100.0;
    if (const auto problem = apply_persona(event, *entry)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }
    if (!before.builtin.empty() && (entry->kind() != before.kind() || entry->persona_id != before.persona_id)) {
        co_await event.co_reply(refusal(event, "a built-in trigger keeps its kind and who answers it"));
        co_return;
    }
    // Checked once everything is read: a pattern that was fine as text may
    // not be as a regular expression.
    if (const auto problem = events::trigger_problem(*entry)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    entry->enabled = bool_option(event, "enabled").value_or(entry->enabled);
    entry->respond_to_bots = bool_option(event, "bots").value_or(entry->respond_to_bots);
    entry->reply = bool_option(event, "reply").value_or(entry->reply);
    apply_message_options(event, entry->message_flags);

    if (!store_->update(*entry)) {
        co_await event.co_reply(refusal(event, std::format("no trigger `{}` in this server", *id)));
        co_return;
    }

    util::log().info("trigger {} updated in guild {} by {}: {}", *id, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()), describe(*entry));
    co_await event.co_reply(result(event, describe_in_full("updated", *entry, personas(event.command.guild_id))));
}

auto trigger_command::remove(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto id = int_option(event, "id");
    if (!id) {
        co_await event.co_reply(refusal(event, "which trigger? run `/trigger list` for the ids"));
        co_return;
    }

    const auto entry = store_->find(*id, event.command.guild_id);
    if (entry && !entry->builtin.empty()) {
        co_await event.co_reply(refusal(
            event, std::format("trigger `{}` is built in, so it stays; `/trigger edit id:{} enabled:false` turns it off", *id, *id)));
        co_return;
    }
    if (!entry || !store_->remove(*id, event.command.guild_id)) {
        co_await event.co_reply(refusal(event, std::format("no trigger `{}` in this server", *id)));
        co_return;
    }

    util::log().info("trigger {} removed from guild {} by {}", *id, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, std::format("removed trigger `{}`", *id)));
}

auto trigger_command::list(const dpp::slashcommand_t& event, int page) -> dpp::task<void> {
    events::ensure_builtins(*store_, model_, event.command.guild_id);
    co_await event.co_reply(result(event, render_trigger_list(*store_, event.command.guild_id, page, model_)));
}

auto trigger_command::panel(const dpp::slashcommand_t& event) -> dpp::task<void> {
    events::ensure_builtins(*store_, model_, event.command.guild_id);
    co_await event.co_reply(result(event, render_trigger_panel(*store_, event.command.guild_id, 0, 0, false, {}, model_)));
}

// --------------------------------------------------------------------------
// The panel
// --------------------------------------------------------------------------

auto apply_form(events::trigger& entry, const form_fields& fields) -> form_outcome {
    // The form's fields are its kind's: responses; a prompt and a chance; or
    // a question and a prompt. Being addressed or replied to has no pattern.
    events::trigger changed = entry;
    if (changed.condition == events::trigger_condition::text) changed.pattern = std::string(util::trim(fields.pattern));
    switch (fields.kind) {
    case events::trigger_kind::responses:
        changed.responses = parse_responses(fields.responses);
        changed.prompt.clear();
        changed.question.clear();
        break;
    case events::trigger_kind::model:
        changed.prompt = std::string(util::trim(fields.prompt));
        changed.responses.clear();
        changed.question.clear();
        break;
    case events::trigger_kind::analysis:
        changed.question = std::string(util::trim(fields.question));
        changed.prompt = std::string(util::trim(fields.prompt));
        changed.responses.clear();
        if (changed.question.empty()) return {.refused = "an analysis needs a question for the check model", .note = {}};
        break;
    }

    // The optional fields are left as they were when they cannot be read,
    // rather than reset, and the note says so: someone typing "thirty" into
    // the cooldown box should neither lose the cooldown they had nor think
    // it changed.
    std::vector<std::string> kept;
    if (const std::string_view typed = util::trim(fields.mode); !typed.empty()) {
        if (const auto read = events::match_mode_from_string(typed)) {
            changed.mode = *read;
        } else {
            kept.push_back(
                std::format("\"{}\" isn't word, anywhere or regex, so it still matches {}", typed, util::mode_label(entry.mode)));
        }
    }

    if (const std::string_view typed = util::trim(fields.cooldown); !typed.empty()) {
        if (const auto seconds = read_number(typed, 0, cooldown_limit)) {
            changed.cooldown = std::chrono::seconds(*seconds);
        } else {
            kept.push_back(std::format("\"{}\" isn't a number of seconds from 0 to {}, so the cooldown stayed {}s", typed, cooldown_limit,
                                       entry.cooldown.count()));
        }
    }

    if (const std::string_view typed = util::trim(fields.chance); fields.kind == events::trigger_kind::model && !typed.empty()) {
        if (const auto read = read_number(typed, 1, 100)) {
            changed.chance = static_cast<double>(*read) / 100.0;
        } else {
            kept.push_back(std::format("\"{}\" isn't a percent from 1 to 100, so the chance stayed {}%", typed, percent(entry.chance)));
        }
    }

    // Refused rather than kept: there is no older pattern worth falling back
    // to, and the mode may be what made this one unusable.
    if (const auto problem = events::trigger_problem(changed)) return {.refused = *problem, .note = {}};
    entry = std::move(changed);

    std::string note;
    for (const std::string& line : kept) {
        if (!note.empty()) note += "; ";
        note += line;
    }
    return {.refused = std::nullopt, .note = std::move(note)};
}

namespace {

auto button(dpp::component_style style, std::string_view label, const std::string& id) -> dpp::component {
    return dpp::component().set_type(dpp::cot_button).set_style(style).set_label(std::string(label)).set_id(id);
}

/// The menu naming the triggers on this page, so its labels always match the
/// lines above it.
auto pick_menu(std::span<const events::trigger> page_of, int page, std::int64_t selected) -> std::optional<dpp::component> {
    const auto id = ui::encode({.view = std::string(trigger_pick_view), .page = page, .argument = {}});
    if (page_of.empty() || !id) return std::nullopt;

    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu).set_placeholder("Pick a trigger to edit or delete").set_id(*id);
    for (const events::trigger& entry : page_of) {
        // A pattern may be 200 characters and an option's label only 100,
        // and one label too long makes Discord refuse the whole panel. The
        // lines above the menu show the pattern in full.
        const std::string kind = std::format("{}{}", util::mode_label(entry.mode), entry.asks_model() ? ", asks the model" : "");
        menu.add_select_option(dpp::select_option(util::truncate(entry.pattern, select_option_limit), std::to_string(entry.id), kind)
                                   .set_default(entry.id == selected));
    }

    dpp::component row;
    row.set_type(dpp::cot_action_row).add_component(menu);
    return row;
}

/// Edit and Delete for the selected trigger, or the delete confirmation.
///
/// Confirming happens in the panel itself rather than in a second message, so
/// there is nothing left behind if it is ignored
/// (src/modules/triggers/docs/Triggers.md §2.5).
auto selection_row(std::span<const events::trigger> page_of, int page, std::int64_t selected, bool confirming_delete)
    -> std::optional<dpp::component> {
    if (selected == 0) return std::nullopt;

    const std::string chosen = std::to_string(selected);
    dpp::component row;
    row.set_type(dpp::cot_action_row);

    if (confirming_delete) {
        // Cancel carries the trigger, which puts it back as it was, picked.
        // Without it, Cancel would be the same panel state as ◀ on the first
        // page or ▶ on the last, and Discord refuses a message that has one
        // custom_id twice.
        const auto yes = ui::encode({.view = std::string(trigger_confirm_view), .page = page, .argument = chosen});
        const auto no = ui::encode({.view = std::string(trigger_panel_view), .page = page, .argument = chosen});
        if (!yes || !no) return std::nullopt;
        row.add_component(button(dpp::cos_danger, std::format("Delete {}", chosen), *yes));
        row.add_component(button(dpp::cos_secondary, "Cancel", *no));
        return row;
    }

    const auto edit = ui::encode({.view = std::string(trigger_edit_view), .page = page, .argument = chosen});
    const auto del = ui::encode({.view = std::string(trigger_delete_view), .page = page, .argument = chosen});
    const auto toggle = ui::encode({.view = std::string(trigger_toggle_view), .page = page, .argument = chosen});
    const auto bots = ui::encode({.view = std::string(trigger_bots_view), .page = page, .argument = chosen});
    if (!edit || !del || !toggle || !bots) return std::nullopt;

    // The labels say what pressing them does, which means reading the
    // trigger's current state rather than showing a fixed word.
    const auto found = std::ranges::find(page_of, selected, &events::trigger::id);
    const bool currently_on = found == page_of.end() || found->enabled;
    const bool answers_bots = found != page_of.end() && found->respond_to_bots;

    row.add_component(button(dpp::cos_primary, "Edit", *edit));
    row.add_component(button(dpp::cos_secondary, currently_on ? "Disable" : "Enable", *toggle));
    // A modal has room for this as a fifth field, but a yes/no typed into a
    // text box is a worse answer than a button that already knows the state.
    row.add_component(button(dpp::cos_secondary, answers_bots ? "Ignore bots" : "Answer bots", *bots));
    // A built-in trigger is turned off, never removed.
    if (found == page_of.end() || found->builtin.empty()) row.add_component(button(dpp::cos_danger, "Delete", *del));
    return row;
}

auto flipped(discord::message_flags flags, discord::message_flags bit) -> discord::message_flags {
    return static_cast<discord::message_flags>(flags ^ bit);
}

/// Each of the panel's on/off buttons, and what pressing it does.
constexpr std::array<std::pair<std::string_view, trigger_toggle>, 5> toggles{{
    {trigger_toggle_view,
     [](events::trigger& entry) -> std::string_view {
         entry.enabled = !entry.enabled;
         return entry.enabled ? "enabled" : "disabled";
     }},
    {trigger_bots_view,
     [](events::trigger& entry) -> std::string_view {
         entry.respond_to_bots = !entry.respond_to_bots;
         return entry.respond_to_bots ? "set to answer bots" : "set to ignore bots";
     }},
    {trigger_silent_view,
     [](events::trigger& entry) -> std::string_view {
         entry.message_flags = flipped(entry.message_flags, dpp::m_suppress_notifications);
         return (entry.message_flags & dpp::m_suppress_notifications) != 0 ? "set to reply silently" : "set to reply with notifications";
     }},
    {trigger_previews_view,
     [](events::trigger& entry) -> std::string_view {
         entry.message_flags = flipped(entry.message_flags, dpp::m_suppress_embeds);
         return (entry.message_flags & dpp::m_suppress_embeds) != 0 ? "set to hide link previews" : "set to show link previews";
     }},
    {trigger_reply_view,
     [](events::trigger& entry) -> std::string_view {
         entry.reply = !entry.reply;
         return entry.reply ? "set to answer as a reply" : "set to post plainly";
     }},
}};

/// How the selected trigger's answers are posted: as a reply or not, and
/// for fixed replies, silent or not and previews or not. A row of its own,
/// since the selection row already has four buttons and a row takes five.
/// A persona's answers are never replies, which a webhook cannot post, and
/// the model's are posted as it posts them, so one answered by a persona has
/// none of these.
auto reply_options_row(std::span<const events::trigger> page_of, int page, std::int64_t selected, bool confirming_delete)
    -> std::optional<dpp::component> {
    const auto found = std::ranges::find(page_of, selected, &events::trigger::id);
    if (selected == 0 || confirming_delete || found == page_of.end()) return std::nullopt;

    const std::string chosen = std::to_string(selected);
    dpp::component row;
    row.set_type(dpp::cot_action_row);

    if (found->persona_id == 0) {
        if (const auto reply = ui::encode({.view = std::string(trigger_reply_view), .page = page, .argument = chosen})) {
            row.add_component(button(dpp::cos_secondary, found->reply ? "Post plainly" : "Answer as a reply", *reply));
        }
    }
    if (!found->asks_model()) {
        const auto silent = ui::encode({.view = std::string(trigger_silent_view), .page = page, .argument = chosen});
        const auto previews = ui::encode({.view = std::string(trigger_previews_view), .page = page, .argument = chosen});
        if (!silent || !previews) return std::nullopt;

        const bool is_silent = (found->message_flags & dpp::m_suppress_notifications) != 0;
        const bool hides_previews = (found->message_flags & dpp::m_suppress_embeds) != 0;
        row.add_component(button(dpp::cos_secondary, is_silent ? "Reply with notifications" : "Reply silently", *silent));
        row.add_component(button(dpp::cos_secondary, hides_previews ? "Show link previews" : "Hide link previews", *previews));
    }
    return row.components.empty() ? std::nullopt : std::optional(row);
}

/// Add, and Add for the model when it is there, plus the paging buttons.
///
/// They share a row because a message has five at most, and the select menu,
/// the selection row and the reply options already take three.
auto footer_row(int page, std::size_t total, bool with_model) -> std::optional<dpp::component> {
    dpp::component row;
    row.set_type(dpp::cot_action_row);

    if (const auto add = ui::encode({.view = std::string(trigger_add_view), .page = page, .argument = {}})) {
        row.add_component(button(dpp::cos_success, "Add", *add));
    }
    if (with_model) {
        if (const auto add = ui::encode({.view = std::string(trigger_add_model_view), .page = page, .argument = "model"})) {
            row.add_component(button(dpp::cos_success, "Add for the model", *add));
        }
        if (const auto add = ui::encode({.view = std::string(trigger_add_model_view), .page = page, .argument = "analysis"})) {
            row.add_component(button(dpp::cos_success, "Add an analysis", *add));
        }
    }
    if (const auto paging =
            ui::controls({.view = std::string(trigger_panel_view), .page = page, .argument = {}}, total, triggers_per_page)) {
        for (const dpp::component& one : paging->components) {
            row.add_component(one);
        }
    }

    return row.components.empty() ? std::nullopt : std::optional(row);
}

} // namespace

auto toggle_for(std::string_view view) -> trigger_toggle {
    const auto found = std::ranges::find(toggles, view, &std::pair<std::string_view, trigger_toggle>::first);
    return found == toggles.end() ? nullptr : found->second;
}

auto render_trigger_panel(const events::trigger_store& store, dpp::snowflake guild_id, int page, std::int64_t selected,
                          bool confirming_delete, std::string_view note, const capabilities::model_replies* model) -> dpp::message {
    const std::vector<events::trigger> all = store.for_guild(guild_id);
    int current = ui::clamp_page(page, all.size(), triggers_per_page);

    // A trigger just added may sort onto another page; follow it there, so
    // the menu can show it selected. One deleted from another client is no
    // longer selected at all, rather than left with buttons for nothing.
    if (selected != 0) {
        if (const auto found = std::ranges::find(all, selected, &events::trigger::id); found != all.end()) {
            current = static_cast<int>(static_cast<std::size_t>(found - all.begin()) / triggers_per_page);
        } else {
            selected = 0;
        }
    }

    const ui::page_range window = ui::range_for(current, all.size(), triggers_per_page);
    const std::span<const events::trigger> page_of(all.data() + window.begin, window.size());

    std::string body = "**Trigger responses**\n";
    if (all.empty()) {
        body += "\nNothing here yet. **Add** one below.";
    } else {
        const std::vector<capabilities::persona_name> personas =
            model == nullptr ? std::vector<capabilities::persona_name>{} : model->personas(guild_id);
        std::vector<std::string> lines;
        for (const events::trigger& entry : page_of) {
            lines.push_back(describe(entry, personas));
        }
        body += util::fit_lines(lines, lines_budget);
        body += std::format("\n_{}_", ui::page_label(current, all.size(), triggers_per_page));
    }
    if (!note.empty()) body += std::format("\n-# {}", util::truncate(note, note_limit));

    dpp::message reply(body);

    if (const auto menu = pick_menu(page_of, current, selected)) reply.add_component(*menu);
    if (const auto row = selection_row(page_of, current, selected, confirming_delete)) reply.add_component(*row);
    if (const auto row = reply_options_row(page_of, current, selected, confirming_delete)) reply.add_component(*row);
    if (const auto footer = footer_row(current, all.size(), model != nullptr)) reply.add_component(*footer);

    return reply;
}

auto trigger_form(int page, const events::trigger* entry, events::trigger_kind kind, std::string_view view)
    -> dpp::interaction_modal_response {
    // A new trigger's kind rides in the argument where an existing one's id
    // does, so the form knows which fields came back.
    if (entry != nullptr) kind = entry->kind();
    std::string argument = entry == nullptr ? "responses" : std::to_string(entry->id);
    if (entry == nullptr && kind == events::trigger_kind::model) argument = "model";
    if (entry == nullptr && kind == events::trigger_kind::analysis) argument = "analysis";
    const auto custom_id = ui::encode({.view = std::string(view), .page = page, .argument = argument});

    std::string title = entry == nullptr ? "Add a trigger" : std::format("Edit trigger {}", entry->id);
    if (entry == nullptr && kind == events::trigger_kind::model) title = "Add a trigger for the model";
    if (entry == nullptr && kind == events::trigger_kind::analysis) title = "Add an analysis";
    dpp::interaction_modal_response form(custom_id.value_or(std::string(view) + ":0:0"), title);

    auto text_field = [&form, first = true](std::string label, std::string id, dpp::text_style_type style, bool required,
                                            std::uint32_t limit, std::string value, std::string placeholder = {}) mutable {
        if (!first) form.add_row();
        first = false;
        dpp::component field = dpp::component()
                                   .set_label(std::move(label))
                                   .set_id(std::move(id))
                                   .set_type(dpp::cot_text)
                                   .set_text_style(style)
                                   .set_required(required)
                                   .set_max_length(limit)
                                   .set_default_value(std::move(value));
        if (!placeholder.empty()) field.set_placeholder(std::move(placeholder));
        form.add_component(field);
    };

    // The same limits as /trigger add, so the form cannot save what the
    // command would refuse. Being addressed or replied to has no pattern.
    const bool on_text = entry == nullptr || entry->condition == events::trigger_condition::text;
    if (on_text) {
        text_field("Pattern", "pattern", dpp::text_short, true, pattern_length_limit, entry == nullptr ? "" : entry->pattern);
    }
    switch (kind) {
    case events::trigger_kind::responses:
        // Discord rejects a label over 45 characters with 50035, so the
        // weight syntax goes in the placeholder, which allows 100.
        text_field("Responses, one per line", "responses", dpp::text_paragraph, true, responses_length_limit,
                   entry == nullptr ? "" : format_responses(entry->responses), "nice\n3 | thrice as likely");
        break;
    case events::trigger_kind::model:
        text_field(on_text ? "What the model says about it, in a line" : "What the model keeps in mind, if anything", "prompt",
                   dpp::text_paragraph, on_text, static_cast<std::uint32_t>(events::prompt_limit), entry == nullptr ? "" : entry->prompt,
                   "Someone mentioned pineapple pizza. Defend it.");
        break;
    case events::trigger_kind::analysis:
        text_field("The check model's yes-or-no question", "question", dpp::text_paragraph, true,
                   static_cast<std::uint32_t>(events::question_limit), entry == nullptr ? "" : entry->question,
                   "Is this message talking to LatiBot?");
        text_field("What the model keeps in mind, if anything", "prompt", dpp::text_paragraph, false,
                   static_cast<std::uint32_t>(events::prompt_limit), entry == nullptr ? "" : entry->prompt);
        break;
    }
    if (on_text) {
        text_field("Match: word, anywhere or regex", "mode", dpp::text_short, false, 20,
                   std::string(form_mode(entry == nullptr ? events::match_mode::whole_word : entry->mode)));
    }
    if (kind == events::trigger_kind::model && on_text) {
        text_field("Chance it answers a match, percent", "chance", dpp::text_short, false, 3,
                   percent(entry == nullptr ? 1.0 : entry->chance), "1 to 100");
    }

    std::chrono::seconds cooldown = events::default_trigger_cooldown;
    if (entry != nullptr) {
        cooldown = entry->cooldown;
    } else if (kind == events::trigger_kind::model) {
        cooldown = events::default_model_cooldown;
    } else if (kind == events::trigger_kind::analysis) {
        cooldown = std::chrono::seconds{0};
    }
    text_field("Cooldown in seconds, 0 for none", "cooldown", dpp::text_short, false, 6, std::to_string(cooldown.count()));
    return form;
}

// --------------------------------------------------------------------------
// The panel's buttons and forms
// --------------------------------------------------------------------------

namespace {

/// The trigger a button carries, or 0 when it carries none.
auto argument_id(const ui::page_state& state) -> std::int64_t {
    std::int64_t id = 0;
    const char* begin = state.argument.data();
    const char* end = begin + state.argument.size();
    const auto [stop, error] = std::from_chars(begin, end, id);
    return error == std::errc{} && stop == end ? id : 0;
}

auto field(const ui::form_values& fields, std::string_view name) -> std::string {
    const auto found = fields.find(name);
    return found == fields.end() ? std::string{} : found->second;
}

} // namespace

trigger_panel::trigger_panel(events::trigger_store& store, const capabilities::model_replies* model) : store_(&store), model_(model) {}

auto trigger_panel::render(dpp::snowflake guild, int page, std::int64_t selected, bool confirming_delete, std::string_view note) const
    -> dpp::message {
    return render_trigger_panel(*store_, guild, page, selected, confirming_delete, note, model_);
}

auto trigger_panel::on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
    // Buttons carry the trigger they act on in the argument. The select menu
    // carries its choice in `chosen` instead, and is read below.
    const dpp::snowflake guild = event.command.guild_id;
    const std::int64_t id = chosen.empty() ? argument_id(state) : 0;

    // Paging, then picking, deleting, toggling, and the two forms. Every
    // branch but the forms re-renders the panel in place.
    if (state.view == trigger_list_view) {
        ui::update_panel(event, render_trigger_list(*store_, guild, state.page, model_));
    } else if (state.view == trigger_panel_view) {
        // Paging carries no trigger; Cancel carries the one it was deleting.
        ui::update_panel(event, render(guild, state.page, id));
    } else if (state.view == trigger_pick_view) {
        std::int64_t picked = 0;
        const auto [stop, error] = std::from_chars(chosen.data(), chosen.data() + chosen.size(), picked);
        if (error != std::errc{} || stop != chosen.data() + chosen.size()) picked = 0;
        ui::update_panel(event, render(guild, state.page, picked));
    } else if (state.view == trigger_delete_view) {
        ui::update_panel(event, render(guild, state.page, id, /*confirming_delete=*/true));
    } else if (state.view == trigger_confirm_view) {
        // Logged only when something went: a second press, or one from another
        // client, finds it already gone.
        if (const auto entry = store_->find(id, guild); entry && !entry->builtin.empty()) {
            ui::update_panel(event, render(guild, state.page, id, false, "a built-in trigger stays; Disable turns it off"));
            return true;
        }
        if (store_->remove(id, guild)) {
            util::log().info("trigger {} removed from guild {} by {} from the panel", id, guild,
                             describe_user(event.command.get_issuing_user()));
        }
        ui::update_panel(event, render(guild, state.page));
    } else if (const trigger_toggle change = toggle_for(state.view)) {
        toggle(event, id, change);
        ui::update_panel(event, render(guild, state.page, id));
    } else if (state.view == trigger_add_view) {
        event.dialog(trigger_form(state.page, nullptr));
    } else if (state.view == trigger_add_model_view) {
        if (model_ == nullptr) {
            ui::update_panel(event, render(guild, state.page, 0, false, "the language model isn't built in"));
            return true;
        }
        event.dialog(
            trigger_form(state.page, nullptr, state.argument == "analysis" ? events::trigger_kind::analysis : events::trigger_kind::model));
    } else if (state.view == trigger_edit_view) {
        if (const auto entry = store_->find(id, guild)) {
            event.dialog(trigger_form(state.page, &*entry));
        } else {
            ui::update_panel(event, render(guild, state.page, 0, false, "that trigger is gone"));
        }
    } else {
        return false;
    }
    return true;
}

auto trigger_panel::toggle(const dpp::interaction_create_t& event, std::int64_t id, trigger_toggle change) -> void {
    const dpp::snowflake guild = event.command.guild_id;
    auto entry = store_->find(id, guild);
    if (!entry) {
        // Deleted from another client while this panel was open. The caller
        // re-renders either way, which is what puts the panel back in step.
        util::log().debug("panel asked to change trigger {}, which is no longer in guild {}", id, guild);
        return;
    }

    const std::string_view became = change(*entry);
    if (!store_->update(*entry)) {
        util::log().debug("trigger {} in guild {} was removed before the panel could change it", id, guild);
        return;
    }
    util::log().info("trigger {} in guild {} {} by {} from the panel", id, guild, became, describe_user(event.command.get_issuing_user()));
}

auto trigger_panel::on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool {
    // The panel's form updates the panel; the one `/trigger edit` opens has
    // no panel, and answers privately.
    const bool from_command = state.view == trigger_command_form_view;
    if (state.view != trigger_form_view && !from_command) return false;

    const dpp::snowflake guild = event.command.guild_id;
    const std::int64_t id = argument_id(state);
    const user_label who = describe_user(event.command.get_issuing_user());
    const auto tell = [&](std::string_view text, std::int64_t selected) {
        if (from_command) {
            ui::answer_privately(event, text);
        } else {
            ui::update_panel(event, render(guild, state.page, selected, false, text));
        }
    };

    // Zero means add. Anything else has to still exist: somebody could have
    // deleted it from another client while the modal was open.
    events::trigger entry;
    events::trigger_kind kind = events::trigger_kind::responses;
    if (id != 0) {
        auto existing = store_->find(id, guild);
        if (!existing) {
            tell("that trigger was deleted while you edited it", 0);
            return true;
        }
        entry = std::move(*existing);
        kind = entry.kind();
    } else {
        entry.guild_id = guild;
        entry.cooldown = events::default_trigger_cooldown;
        if (state.argument == "model") kind = events::trigger_kind::model;
        if (state.argument == "analysis") kind = events::trigger_kind::analysis;
    }

    const ui::form_values values = ui::form_fields(event);
    const form_outcome applied = apply_form(entry, {.pattern = field(values, "pattern"),
                                                    .responses = field(values, "responses"),
                                                    .mode = field(values, "mode"),
                                                    .cooldown = field(values, "cooldown"),
                                                    .prompt = field(values, "prompt"),
                                                    .chance = field(values, "chance"),
                                                    .question = field(values, "question"),
                                                    .kind = kind});
    if (applied.refused) {
        util::log().debug("{} submitted an unusable trigger form in guild {}: {}", who, guild, *applied.refused);
        ui::answer_privately(event, *applied.refused);
        return true;
    }

    if (entry.asks_model() && model_ == nullptr) {
        ui::answer_privately(event, "the language model isn't built in");
        return true;
    }
    // A new one that asks the model answers as the model always has: as a
    // reply, after the cooldown its kind starts with, unless the form said
    // otherwise.
    if (id == 0 && entry.asks_model()) {
        entry.reply = true;
        if (util::is_blank(field(values, "cooldown"))) {
            entry.cooldown = kind == events::trigger_kind::analysis ? std::chrono::seconds{0} : events::default_model_cooldown;
        }
    }

    // Deleted from another client between opening the modal and saving it.
    if (id != 0 && !store_->update(entry)) {
        util::log().info("trigger {} in guild {} was removed before {}'s edit could be saved", id, guild, who);
        tell("that trigger was deleted while you edited it", 0);
        return true;
    }

    entry.id = id == 0 ? store_->add(entry) : entry.id;
    util::log().info("trigger {} {} in guild {} by {} from {}: {}", entry.id, id == 0 ? "added" : "updated", guild, who,
                     from_command ? "its form" : "the panel", describe(entry));
    if (from_command) {
        const std::vector<capabilities::persona_name> personas =
            model_ == nullptr ? std::vector<capabilities::persona_name>{} : model_->personas(guild);
        std::string text = describe_in_full("updated", entry, personas);
        if (!applied.note.empty()) text = util::truncate(std::format("{}\n-# {}", text, applied.note), message_limit);
        ui::answer_privately(event, text);
        return true;
    }
    const std::string note = applied.note.empty() ? std::format("{} trigger `{}`", id == 0 ? "added" : "saved", entry.id) : applied.note;
    ui::update_panel(event, render(guild, state.page, entry.id, false, note));
    return true;
}

} // namespace latibot::commands
