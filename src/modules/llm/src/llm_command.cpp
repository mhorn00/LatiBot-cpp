#include "llm_command.hpp"

#include "advanced_triggers.hpp"
#include "aliases.hpp"
#include "core/capabilities/speech.hpp"
#include "core/commands/options.hpp"
#include "core/config/guild_settings.hpp"
#include "core/ports/clock.hpp"
#include "core/ports/http_client.hpp"
#include "core/ui/interaction.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "guards.hpp"
#include "tool_switches.hpp"

#include <dpp/cluster.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <utility>

namespace latibot::commands {
namespace {

/// Discord's limits on a modal: five fields, each up to 4000 characters.
constexpr std::size_t form_parts = 5;
constexpr std::size_t form_part_limit = 4000;

/// Room left in a message for a document or a diff before it goes in a
/// file instead: 2000, less the fences and a heading.
constexpr std::size_t inline_limit = 1800;

/// How many versions `history` lists.
constexpr std::size_t history_shown = 15;

/// The largest file `edit file:` reads, in bytes: the character limit, at
/// up to four bytes a character.
constexpr std::uint32_t document_file_limit = llm::document_length_limit * 4;

constexpr std::uint32_t trigger_pattern_limit = 200;
constexpr std::int64_t cooldown_limit = 86400;

/// Discord's longest modal title.
constexpr std::size_t form_title_limit = 45;

/// Discord's limit on a message.
constexpr std::size_t message_limit = 2000;

/// What to put in the persona option to mean LatiBot itself, for a trigger
/// moved back from a persona.
constexpr std::string_view latibot_choice = "LatiBot";

auto kind_label(llm::document_kind kind) -> std::string_view {
    switch (kind) {
    case llm::document_kind::personality:
        return "personality";
    case llm::document_kind::system:
        return "system instructions";
    case llm::document_kind::trigger_style:
        break;
    }
    return "trigger style";
}

/// For a label that starts a sentence: "The personality".
auto capitalised(std::string text) -> std::string {
    if (!text.empty() && text.front() >= 'a' && text.front() <= 'z') text.front() = static_cast<char>(text.front() - 'a' + 'A');
    return text;
}

auto seconds_now(const ports::clock& clock) -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::seconds>(clock.now());
}

auto manages_server(const dpp::interaction_create_t& event) -> bool {
    return invoker_permissions(event).can(dpp::p_manage_guild);
}

auto group_title(std::string_view group) -> std::string_view {
    if (group == "context") return "What it reads";
    if (group == "replies") return "Replies and rate limits";
    if (group == "conversation") return "Conversation mode";
    return "Talking to other bots";
}

/// One trigger, in a line; `persona` is the name of the persona that
/// answers it, empty for LatiBot.
auto describe_trigger(const llm::advanced_trigger& entry, std::string_view persona = {}) -> std::string {
    // A regular expression is code, and its * and _ would read as markdown.
    const std::string pattern =
        entry.mode == util::match_mode::regex ? std::format("`{}`", entry.pattern) : std::format("**{}**", entry.pattern);
    std::string line = std::format("`{}` {} ({}, {:.0f}%, ", entry.id, pattern, util::mode_label(entry.mode), entry.probability * 100);
    line += entry.cooldown.count() == 0 ? "no cooldown" : std::format("{}s cooldown", entry.cooldown.count());
    if (!persona.empty()) line += std::format(", as {}", persona);
    if (entry.respond_to_bots) line += ", answers bots";
    if (!entry.enabled) line += ", disabled";
    // The whole prompt: these are only ever shown to whoever asked, and a
    // list too long for a message is cut as a whole.
    line += std::format(") -> {}", entry.context_prompt);
    return line;
}

/// Reads a whole number from `low` to `high` out of a form's field.
auto read_number(std::string_view text, std::int64_t low, std::int64_t high) -> std::optional<std::int64_t> {
    const std::string_view trimmed = util::trim(text);
    std::int64_t value = 0;
    const auto [stop, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (error != std::errc{} || stop != trimmed.data() + trimmed.size() || value < low || value > high) return std::nullopt;
    return value;
}

/// How many options the subcommand was given, below its group.
auto given_options(const dpp::slashcommand_t& event) -> std::size_t {
    const dpp::command_interaction command = event.command.get_command_interaction();
    const std::vector<dpp::command_data_option>* options = &command.options;
    while (options->size() == 1 && ((*options)[0].type == dpp::co_sub_command_group || (*options)[0].type == dpp::co_sub_command)) {
        options = &(*options)[0].options;
    }
    return options->size();
}

auto document_group(std::string_view name, std::string_view description, bool personality) -> dpp::command_option {
    dpp::command_option group(dpp::co_sub_command_group, std::string(name), std::string(description));
    group.add_option(dpp::command_option(dpp::co_sub_command, "view", "Show what it says now."));

    dpp::command_option edit(dpp::co_sub_command, "edit", "Change it in a form, or replace it with a .txt or .md file.");
    edit.add_option(dpp::command_option(dpp::co_attachment, "file", "A .txt or .md file to replace it with.", false));
    group.add_option(edit);

    group.add_option(dpp::command_option(dpp::co_sub_command, "history", "Every version, newest first."));

    dpp::command_option diff(dpp::co_sub_command, "diff", "What changed between two versions.");
    diff.add_option(
        dpp::command_option(dpp::co_integer, "from", "The older version. The one before `to` by default.", false).set_min_value(0));
    diff.add_option(dpp::command_option(dpp::co_integer, "to", "The newer version. The current one by default.", false).set_min_value(0));
    group.add_option(diff);

    dpp::command_option revert(dpp::co_sub_command, "revert", "Go back to an earlier version, as a new version.");
    revert.add_option(dpp::command_option(dpp::co_integer, "version", "From history; 0 is the default.", true).set_min_value(0));
    group.add_option(revert);

    if (personality) {
        dpp::command_option editors(dpp::co_sub_command, "editors", "Show or set who may edit the personality (Manage Server).");
        editors.add_option(dpp::command_option(dpp::co_role, "role", "Who may edit it. @everyone lets anyone.", false));
        group.add_option(editors);
    }
    return group;
}

/// The models a persona can use: their names alone, to keep /llm within
/// Discord's size for a command.
auto persona_model_option(std::string_view description) -> dpp::command_option {
    dpp::command_option which(dpp::co_string, "model", std::string(description), false);
    for (const llm::model_info& known : llm::known_models()) {
        which.add_choice(dpp::command_option_choice(std::string(known.label), std::string(known.id)));
    }
    return which;
}

/// A persona option, completed from the server's personas.
auto persona_option(std::string_view description, bool required) -> dpp::command_option {
    return dpp::command_option(dpp::co_string, "persona", std::string(description), required)
        .set_auto_complete(true)
        .set_max_length(static_cast<std::int64_t>(llm::persona_name_limit));
}

/// A persona's voice, completed from the speech capability's.
auto voice_option(std::string_view description) -> dpp::command_option {
    return dpp::command_option(dpp::co_string, "voice", std::string(description), false).set_auto_complete(true);
}

auto match_mode_option() -> dpp::command_option {
    dpp::command_option mode(dpp::co_string, "mode", "Whether the pattern must stand alone, or is a regular expression.", false);
    mode.add_choice(dpp::command_option_choice("Whole word", std::string("whole_word")));
    mode.add_choice(dpp::command_option_choice("Anywhere in the message", std::string("substring")));
    mode.add_choice(dpp::command_option_choice("Regular expression", std::string("regex")));
    return mode;
}

/// Memories with the people they name as mentions, which Discord shows as
/// names, rather than the aliases the model wrote
/// (src/modules/llm/docs/Language_Model.md §3.8).
/// The guild's personas' names, by id, to say whose a personal memory is.
auto persona_names(const llm::persona_store* personas, dpp::snowflake guild) -> std::map<std::int64_t, std::string> {
    std::map<std::int64_t, std::string> names;
    if (personas == nullptr) return names;
    for (const llm::persona& each : personas->for_guild(guild)) {
        names.emplace(each.id, each.name);
    }
    return names;
}

auto named(std::vector<llm::memory> memories, dpp::snowflake guild, const llm::alias_store* aliases) -> std::vector<llm::memory> {
    if (aliases == nullptr) return memories;
    for (llm::memory& entry : memories) {
        entry.content = llm::aliases_as_mentions(entry.content, guild, *aliases);
    }
    return memories;
}

} // namespace

// --------------------------------------------------------------------------
// Decisions and rendering
// --------------------------------------------------------------------------

auto may_edit_personality(bool manages_server, dpp::snowflake guild, dpp::snowflake editor_role, std::span<const dpp::snowflake> roles)
    -> bool {
    return manages_server || editor_role == guild || std::ranges::find(roles, editor_role) != roles.end();
}

auto render_llm_status(const llm_overview& overview) -> std::string {
    const llm::model_info* model = llm::find_model(overview.model);
    std::string text = std::format("The language model is **{}** here, using {}.", overview.enabled ? "on" : "off",
                                   model == nullptr ? overview.model : std::string(model->label));
    if (!overview.has_key) text += " There is no API key for it on the bot, so it cannot answer.";
    if (overview.conversation) text += " Conversation mode is **on**: it keeps listening for a while after it answers.";
    text += std::format("\nSpent today: ${:.2f} of ${:.2f}. This month: ${:.2f} of ${:.2f}, ${:.2f} of it here.", overview.spend.today,
                        overview.caps.daily, overview.spend.this_month, overview.caps.monthly, overview.guild_this_month);
    if (overview.spend.over()) text += "\nThe spend cap is reached, so it stays quiet until it resets.";
    return text;
}

auto document_parts(std::string_view text) -> std::optional<std::vector<std::string>> {
    std::vector<std::string> parts;
    std::string current;

    const auto close = [&] {
        parts.push_back(std::move(current));
        current.clear();
    };

    for (std::string_view line : util::lines(text)) {
        // A line longer than a part is cut where a character starts.
        while (util::character_count(line) > form_part_limit) {
            if (!current.empty()) close();
            std::string head = util::truncate(line, form_part_limit + 1);
            head.resize(head.size() - std::string_view("…").size());
            line.remove_prefix(head.size());
            current = std::move(head);
            close();
        }

        const std::size_t wanted = util::character_count(current) + (current.empty() ? 0 : 1) + util::character_count(line);
        if (!current.empty() && wanted > form_part_limit) close();
        if (!current.empty()) current += '\n';
        current += line;
    }
    if (!current.empty() || parts.empty()) close();

    if (parts.size() > form_parts) return std::nullopt;
    return parts;
}

auto join_document_parts(std::span<const std::string> parts) -> std::string {
    std::string text;
    for (const std::string& part : parts) {
        if (part.empty()) continue;
        if (!text.empty()) text += '\n';
        text += part;
    }
    return text;
}

auto document_label(const document_target& target) -> std::string {
    if (target.ref.persona == 0) return std::format("the {}", kind_label(target.ref.kind));
    return std::format("{}'s {}", target.persona_name, kind_label(target.ref.kind));
}

auto document_form(const document_target& target, std::string_view current) -> std::optional<dpp::interaction_modal_response> {
    const auto parts = document_parts(current);
    if (!parts) return std::nullopt;

    const auto custom_id = ui::encode({.view = std::string(llm_document_form_view), .page = 0, .argument = llm::to_string(target.ref)});
    dpp::interaction_modal_response form(custom_id.value_or(std::string(llm_document_form_view)),
                                         util::truncate(std::format("Edit {}", document_label(target)), form_title_limit));

    for (std::size_t index = 0; index < form_parts; ++index) {
        if (index > 0) form.add_row();
        form.add_component(dpp::component()
                               .set_label(std::format("Part {} of {}", index + 1, form_parts))
                               .set_id(std::format("part{}", index + 1))
                               .set_type(dpp::cot_text)
                               .set_text_style(dpp::text_paragraph)
                               .set_required(false)
                               .set_max_length(form_part_limit)
                               .set_default_value(index < parts->size() ? (*parts)[index] : std::string{}));
    }
    return form;
}

auto render_document(const document_target& target, const llm::document_version& shown, bool is_default) -> dpp::message {
    const std::string label = capitalised(document_label(target));
    const std::string heading =
        is_default ? std::format("{} is the default:", label) : std::format("{}, version {}:", label, shown.version);
    if (util::is_blank(shown.content)) return dpp::message(std::format("{} is empty.", label));

    if (util::character_count(shown.content) <= inline_limit && !shown.content.contains("```")) {
        return dpp::message(std::format("{}\n```\n{}\n```", heading, shown.content));
    }
    dpp::message message(std::format("{} (attached, it is too long to show here)", heading));
    message.add_file(std::format("{}.md", llm::to_string(target.ref)), shown.content, "text/markdown");
    return message;
}

auto render_history(const document_target& target, std::span<const llm::document_version> versions) -> std::string {
    if (versions.empty()) return std::format("Nobody has edited {} here; it is the default (version 0).", document_label(target));

    std::string text = std::format("**Versions of {}**, newest first:\n", document_label(target));
    for (std::size_t index = 0; index < versions.size() && index < history_shown; ++index) {
        const llm::document_version& version = versions[index];
        text += std::format("`v{}` <t:{}:f> by <@{}>, {} characters", version.version, version.edited_at.time_since_epoch().count(),
                            version.edited_by, util::character_count(version.content));
        if (!version.note.empty()) text += std::format(" ({})", version.note);
        text += '\n';
    }
    if (versions.size() > history_shown) text += std::format("…and {} older, back to version 1.\n", versions.size() - history_shown);
    text += "Version 0 is the default.";
    return text;
}

auto describe_saved(const document_target& target, int version, std::string_view content) -> std::string {
    const std::size_t tokens = llm::estimate_tokens(content);
    std::string text = std::format("saved {} as version {} (about {} tokens)", document_label(target), version, tokens);
    if (tokens > llm::large_document_tokens) {
        text += std::format(
            "; that's long, and it's sent with every message the model answers, so it costs on every one. Under {} "
            "tokens is kinder on the budget.",
            llm::large_document_tokens);
    }
    return text;
}

auto find_voice(std::span<const std::string> voices, std::string_view wanted) -> std::optional<std::string> {
    const auto found =
        std::ranges::find_if(voices, [&](const std::string& each) { return util::equals_ignoring_case(each, util::trim(wanted)); });
    if (found == voices.end()) return std::nullopt;
    return *found;
}

auto render_personas(std::span<const llm::persona> personas) -> std::string {
    if (personas.empty()) return "No personas here yet. Make one with `/llm persona add`.";

    std::string text = "**Personas**: each answers replies to its messages, its advanced triggers, and those it talks with.\n";
    for (const llm::persona& who : personas) {
        text += std::format("- **{}**{}{}{}\n", who.name, who.model.empty() ? std::string{} : std::format(", on {}", who.model),
                            who.talks ? ", talks to the others" : "", who.enabled ? "" : " (off)");
    }
    return util::truncate(text, 2000);
}

auto render_persona(const llm::persona& who, std::string_view server_model, std::span<const llm::advanced_trigger> triggers,
                    double spent_this_month, bool speaks) -> std::string {
    const llm::model_info* model = llm::find_model(llm::answering_model(who, server_model));
    std::string text = std::format("**{}** is {}, and answers with {}{}.\n", who.name, who.enabled ? "on" : "off",
                                   model == nullptr ? std::string(llm::answering_model(who, server_model)) : std::string(model->label),
                                   who.model.empty() ? ", the server's model" : "");
    text +=
        who.avatar_url.empty() ? std::string("Its picture is the webhook's own.\n") : std::format("Its picture: <{}>\n", who.avatar_url);
    text += who.talks ? "It may message LatiBot and the other personas that talk, and they it.\n"
                      : "It does not talk to LatiBot or the other personas; `/llm persona edit talks:true` lets it.\n";
    if (speaks) {
        text += who.voice.empty() ? std::string("In a voice session it speaks in the default voice.\n")
                                  : std::format("In a voice session it speaks as {}.\n", who.voice);
    }
    text += std::format("Spent this month: ${:.2f}.\n", spent_this_month);
    if (triggers.empty()) {
        text += "No advanced triggers of its own; `/llm trigger add persona:` gives it one.";
    } else {
        text += "**Its advanced triggers**\n";
        for (const llm::advanced_trigger& entry : triggers) {
            text += describe_trigger(entry) + "\n";
        }
    }
    return util::truncate(text, 2000);
}

auto render_llm_settings(const std::map<std::string, std::int64_t, std::less<>>& values, bool enabled, bool conversation) -> dpp::message {
    std::string body = std::format("**Language model settings**: the model is **{}** here, and conversation mode is **{}**.\n",
                                   enabled ? "on" : "off", conversation ? "on" : "off");
    for (const std::string_view group : llm::setting_groups()) {
        body += std::format("\n__{}__\n", group_title(group));
        for (const llm::setting_spec& spec : llm::setting_specs()) {
            if (spec.group != group) continue;
            const auto found = values.find(spec.key);
            body +=
                std::format("{}: **{}**\n", spec.label, llm::describe_setting(spec, found == values.end() ? spec.fallback : found->second));
        }
    }
    body += "\nChanges apply to the next message.";

    dpp::message panel(body);

    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("Change…")
        .set_id(ui::encode({.view = std::string(llm_settings_pick_view), .page = 0, .argument = {}})
                    .value_or(std::string(llm_settings_pick_view)));
    for (const std::string_view group : llm::setting_groups()) {
        menu.add_select_option(dpp::select_option(std::string(group_title(group)), std::string(group)));
    }
    panel.add_component(dpp::component().set_type(dpp::cot_action_row).add_component(menu));

    const auto flip = ui::encode({.view = std::string(llm_switch_view), .page = 0, .argument = enabled ? "off" : "on"});
    const auto flip_conversation =
        ui::encode({.view = std::string(llm_switch_view), .page = 0, .argument = conversation ? "conversation off" : "conversation on"});
    const auto refresh = ui::encode({.view = std::string(llm_settings_view), .page = 0, .argument = {}});
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    row.add_component(dpp::component()
                          .set_type(dpp::cot_button)
                          .set_style(enabled ? dpp::cos_danger : dpp::cos_success)
                          .set_label(enabled ? "Turn off" : "Turn on")
                          .set_id(flip.value_or(std::string(llm_switch_view))));
    row.add_component(dpp::component()
                          .set_type(dpp::cot_button)
                          .set_style(conversation ? dpp::cos_danger : dpp::cos_success)
                          .set_label(conversation ? "Conversation off" : "Conversation on")
                          .set_id(flip_conversation.value_or(std::string(llm_switch_view))));
    row.add_component(dpp::component()
                          .set_type(dpp::cot_button)
                          .set_style(dpp::cos_secondary)
                          .set_label("Refresh")
                          .set_id(refresh.value_or(std::string(llm_settings_view))));
    panel.add_component(row);
    return panel;
}

auto llm_settings_form(std::string_view group, const std::map<std::string, std::int64_t, std::less<>>& values)
    -> std::optional<dpp::interaction_modal_response> {
    if (std::ranges::find(llm::setting_groups(), group) == llm::setting_groups().end()) return std::nullopt;

    const auto custom_id = ui::encode({.view = std::string(llm_settings_form_view), .page = 0, .argument = std::string(group)});
    dpp::interaction_modal_response form(custom_id.value_or(std::string(llm_settings_form_view)), std::string(group_title(group)));

    bool first = true;
    for (const llm::setting_spec& spec : llm::setting_specs()) {
        if (spec.group != group) continue;
        if (!first) form.add_row();
        first = false;

        const auto found = values.find(spec.key);
        const std::int64_t value = found == values.end() ? spec.fallback : found->second;
        form.add_component(dpp::component()
                               .set_label(std::string(spec.label))
                               .set_id(std::string(spec.key))
                               .set_type(dpp::cot_text)
                               .set_text_style(dpp::text_short)
                               .set_required(true)
                               .set_placeholder(spec.is_switch ? "yes or no" : std::format("{} to {}", spec.min, spec.max))
                               .set_default_value(llm::describe_setting(spec, value)));
    }
    return form;
}

auto trigger_form(const llm::advanced_trigger& entry) -> dpp::interaction_modal_response {
    const auto custom_id = ui::encode({.view = std::string(llm_trigger_form_view), .page = 0, .argument = std::to_string(entry.id)});
    dpp::interaction_modal_response form(custom_id.value_or(std::string(llm_trigger_form_view)),
                                         std::format("Edit advanced trigger {}", entry.id));

    // The same limits as /llm trigger add, so the form cannot save what the
    // command would refuse.
    form.add_component(dpp::component()
                           .set_label("Pattern")
                           .set_id("pattern")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_short)
                           .set_required(true)
                           .set_max_length(trigger_pattern_limit)
                           .set_default_value(entry.pattern));
    form.add_row();
    form.add_component(dpp::component()
                           .set_label("Prompt")
                           .set_id("prompt")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_paragraph)
                           .set_required(true)
                           .set_max_length(static_cast<std::uint32_t>(llm::context_prompt_limit))
                           .set_default_value(entry.context_prompt));
    form.add_row();
    form.add_component(
        dpp::component()
            .set_label("Match: word, anywhere or regex")
            .set_id("mode")
            .set_type(dpp::cot_text)
            .set_text_style(dpp::text_short)
            .set_required(true)
            .set_default_value(entry.mode == util::match_mode::whole_word ? "word" : std::string(util::mode_label(entry.mode))));
    form.add_row();
    form.add_component(dpp::component()
                           .set_label("Chance, percent")
                           .set_id("chance")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_short)
                           .set_required(true)
                           .set_placeholder("1 to 100")
                           .set_default_value(std::format("{:.0f}", entry.probability * 100)));
    form.add_row();
    form.add_component(dpp::component()
                           .set_label("Cooldown in seconds, 0 for none")
                           .set_id("cooldown")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_short)
                           .set_required(true)
                           .set_placeholder(std::format("0 to {}", cooldown_limit))
                           .set_default_value(std::to_string(entry.cooldown.count())));
    return form;
}

auto apply_trigger_form(llm::advanced_trigger& entry, const std::map<std::string, std::string, std::less<>>& fields)
    -> std::optional<std::string> {
    const auto field = [&fields](std::string_view name) -> std::string_view {
        const auto found = fields.find(name);
        return found == fields.end() ? std::string_view{} : std::string_view(found->second);
    };

    const std::string pattern(util::trim(field("pattern")));
    const std::string prompt(util::trim(field("prompt")));
    if (pattern.empty() || prompt.empty()) return "a trigger needs a pattern and a prompt; nothing was changed";

    const auto mode = util::match_mode_from_string(field("mode"));
    if (!mode) return std::format("\"{}\" isn't word, anywhere or regex; nothing was changed", util::trim(field("mode")));
    const auto chance = read_number(field("chance"), 1, 100);
    if (!chance) return std::format("\"{}\" isn't a percent from 1 to 100; nothing was changed", util::trim(field("chance")));
    const auto cooldown = read_number(field("cooldown"), 0, cooldown_limit);
    if (!cooldown) {
        return std::format("\"{}\" isn't a number of seconds from 0 to {}; nothing was changed", util::trim(field("cooldown")),
                           cooldown_limit);
    }
    if (const auto problem = util::pattern_error(pattern, *mode)) return std::format("that pattern won't work: {}", *problem);

    entry.pattern = pattern;
    entry.context_prompt = prompt;
    entry.mode = *mode;
    entry.probability = static_cast<double>(*chance) / 100.0;
    entry.cooldown = std::chrono::seconds{*cooldown};
    return std::nullopt;
}

auto read_llm_settings_form(std::string_view group, const std::map<std::string, std::string, std::less<>>& fields)
    -> std::variant<std::vector<std::pair<std::string_view, std::int64_t>>, std::string> {
    std::vector<std::pair<std::string_view, std::int64_t>> changes;
    for (const llm::setting_spec& spec : llm::setting_specs()) {
        if (spec.group != group) continue;
        const auto typed = fields.find(spec.key);
        if (typed == fields.end()) continue;

        std::string reason;
        const auto value = llm::parse_setting(spec, typed->second, reason);
        if (!value) return reason + "; nothing was changed";
        changes.emplace_back(spec.key, *value);
    }
    if (changes.empty()) return std::string("that form had nothing in it to change");
    return changes;
}

auto render_memories(std::span<const llm::memory> memories, std::size_t total, int page, std::optional<dpp::snowflake> subject,
                     const std::map<std::int64_t, std::string>& owners) -> dpp::message {
    const int current = ui::clamp_page(page, total, memories_per_page);
    std::string body = subject ? std::format("**What I remember about <@{}>**\n", *subject) : std::string("**What I remember here**\n");
    if (memories.empty()) body += "\nNothing yet.";
    for (const llm::memory& entry : memories) {
        body += std::format("`#{}` ", entry.id);
        if (entry.subject && !subject) body += std::format("(<@{}>) ", *entry.subject);
        if (entry.owner) {
            const auto owner = owners.find(*entry.owner);
            body += *entry.owner == 0       ? std::string("[LatiBot's own] ")
                    : owner != owners.end() ? std::format("[{}'s own] ", owner->second)
                                            : std::string("[a persona's own] ");
        }
        body += util::truncate(entry.content, 200);
        body += '\n';
    }
    if (total > 0) body += std::format("\n_{}_", ui::page_label(current, total, memories_per_page));

    dpp::message message(body);
    const std::string argument = subject ? std::to_string(static_cast<std::uint64_t>(*subject)) : std::string("all");
    if (const auto row =
            ui::controls({.view = std::string(memory_list_view), .page = current, .argument = argument}, total, memories_per_page)) {
        message.add_component(*row);
    }
    return message;
}

// --------------------------------------------------------------------------
// /llm
// --------------------------------------------------------------------------

llm_command::llm_command(llm_command_services services)
    : info_{.name = "llm",
            .description = "The language model: switch it, choose it, and edit what it knows about this server.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages | dpp::p_read_message_history,
            .default_member_permissions = std::nullopt,
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(std::move(services)) {}

auto llm_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    payload.add_option(dpp::command_option(dpp::co_sub_command, "status", "Whether it is on, which model, and what it has spent."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "on", "Let it answer here (Manage Server)."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "off", "Stop it answering here (Manage Server)."));

    dpp::command_option model(dpp::co_sub_command, "model", "Choose the model this server uses (Manage Server).");
    dpp::command_option which(dpp::co_string, "name", "The model.", true);
    for (const llm::model_info& known : llm::known_models()) {
        which.add_choice(dpp::command_option_choice(
            std::format("{} (${:g} / ${:g} per million tokens)", known.label, known.input_price, known.output_price),
            std::string(known.id)));
    }
    model.add_option(which);
    payload.add_option(model);

    payload.add_option(dpp::command_option(dpp::co_sub_command, "settings", "Open the settings panel (Manage Server)."));

    dpp::command_option conversation(dpp::co_sub_command_group, "conversation", "Keep talking for a while after answering.");
    conversation.add_option(dpp::command_option(dpp::co_sub_command, "on", "Join in when a message seems meant for it (Manage Server)."));
    conversation.add_option(dpp::command_option(dpp::co_sub_command, "off", "Answer only when addressed (Manage Server)."));
    payload.add_option(conversation);

    payload.add_option(document_group("personality", "How the bot comes across.", true));
    payload.add_option(document_group("system", "Instructions that are not up for negotiation (Manage Server).", false));
    payload.add_option(document_group("style", "How advanced-trigger replies are written (Manage Server).", false));

    dpp::command_option trigger(dpp::co_sub_command_group, "trigger",
                                "Advanced triggers: phrases the model speaks up about (Manage Server).");
    dpp::command_option add(dpp::co_sub_command, "add", "Add one.");
    add.add_option(dpp::command_option(dpp::co_string, "pattern", "The text to look for.", true)
                       .set_min_length(1)
                       .set_max_length(trigger_pattern_limit));
    add.add_option(dpp::command_option(dpp::co_string, "prompt", "What to say about it, in a line.", true)
                       .set_min_length(1)
                       .set_max_length(static_cast<std::int64_t>(llm::context_prompt_limit)));
    add.add_option(dpp::command_option(dpp::co_integer, "chance", "Percent of matches it answers. 100 by default.", false)
                       .set_min_value(1)
                       .set_max_value(100));
    add.add_option(dpp::command_option(dpp::co_integer, "cooldown", "Seconds between replies in one channel. 300 by default.", false)
                       .set_min_value(0)
                       .set_max_value(cooldown_limit));
    add.add_option(match_mode_option());
    add.add_option(persona_option("A persona to answer it, rather than LatiBot.", false));
    add.add_option(dpp::command_option(dpp::co_boolean, "bots", "Also answer allowed bots, LatiBot and other personas.", false));
    trigger.add_option(add);

    dpp::command_option edit(dpp::co_sub_command, "edit", "Change one. Given only the id, it opens in a form to edit.");
    edit.add_option(dpp::command_option(dpp::co_integer, "id", "From /llm trigger list.", true).set_min_value(1));
    edit.add_option(dpp::command_option(dpp::co_string, "pattern", "New text to look for.", false).set_max_length(trigger_pattern_limit));
    edit.add_option(dpp::command_option(dpp::co_string, "prompt", "New instruction.", false)
                        .set_max_length(static_cast<std::int64_t>(llm::context_prompt_limit)));
    edit.add_option(
        dpp::command_option(dpp::co_integer, "chance", "Percent of matches it answers.", false).set_min_value(1).set_max_value(100));
    edit.add_option(dpp::command_option(dpp::co_integer, "cooldown", "Seconds.", false).set_min_value(0).set_max_value(cooldown_limit));
    edit.add_option(match_mode_option());
    edit.add_option(dpp::command_option(dpp::co_boolean, "enabled", "Turn it on or off.", false));
    edit.add_option(persona_option("Who answers it: a persona, or LatiBot.", false));
    edit.add_option(dpp::command_option(dpp::co_boolean, "bots", "Also answer allowed bots, LatiBot and other personas.", false));
    trigger.add_option(edit);

    dpp::command_option remove(dpp::co_sub_command, "remove", "Delete one.");
    remove.add_option(dpp::command_option(dpp::co_integer, "id", "From /llm trigger list.", true).set_min_value(1));
    trigger.add_option(remove);
    trigger.add_option(dpp::command_option(dpp::co_sub_command, "list", "Show them."));
    payload.add_option(trigger);

    dpp::command_option blacklist(dpp::co_sub_command_group, "blacklist", "Who the model does not answer (Manage Server).");
    for (const auto& [verb, what] : std::array<std::pair<const char*, const char*>, 2>{
             {{"add", "Stop answering someone, or a role."}, {"remove", "Answer them again."}}}) {
        dpp::command_option change(dpp::co_sub_command, verb, what);
        change.add_option(dpp::command_option(dpp::co_user, "user", "A member.", false));
        change.add_option(dpp::command_option(dpp::co_role, "role", "Everyone with a role.", false));
        blacklist.add_option(change);
    }
    blacklist.add_option(dpp::command_option(dpp::co_sub_command, "list", "Show it."));
    payload.add_option(blacklist);

    dpp::command_option names(dpp::co_sub_command_group, "names", "More names someone goes by, hidden from the model (Manage Server).");
    for (const auto& [verb, what] : std::array<std::pair<const char*, const char*>, 2>{
             {{"add", "Hide another name of theirs behind their alias."}, {"remove", "Stop treating a name as theirs."}}}) {
        dpp::command_option change(dpp::co_sub_command, verb, what);
        change.add_option(dpp::command_option(dpp::co_user, "user", "Whose name it is.", true));
        change.add_option(dpp::command_option(dpp::co_string, "name", "A name they go by.", true)
                              .set_min_length(static_cast<std::int64_t>(llm::shortest_name))
                              .set_max_length(static_cast<std::int64_t>(llm::alias_name_limit)));
        names.add_option(change);
    }
    dpp::command_option list_names(dpp::co_sub_command, "list", "Show the names set by hand.");
    list_names.add_option(dpp::command_option(dpp::co_user, "user", "Only theirs.", false));
    names.add_option(list_names);
    payload.add_option(names);

    // The tools, by name, as choices: Discord takes 25 at most.
    dpp::command_option tool_group(dpp::co_sub_command_group, "tools", "What the model may look up and do here (Manage Server).");
    tool_group.add_option(dpp::command_option(dpp::co_sub_command, "list", "Show every tool, and whether it is on here."));
    for (const auto& [verb, what] : std::array<std::pair<const char*, const char*>, 2>{
             {{"on", "Let the model use a tool here."}, {"off", "Stop the model using a tool here."}}}) {
        dpp::command_option change(dpp::co_sub_command, verb, what);
        // Completed as typed: there are more tools than Discord's 25 choices.
        change.add_option(dpp::command_option(dpp::co_string, "tool", "Which tool.", true).set_auto_complete(true));
        tool_group.add_option(change);
    }
    payload.add_option(tool_group);

    dpp::command_option persona(dpp::co_sub_command_group, "persona", "Others the model answers as, under their own name.");
    dpp::command_option create(dpp::co_sub_command, "add", "Make one (Manage Server).");
    create.add_option(dpp::command_option(dpp::co_string, "name", "What it is called.", true)
                          .set_min_length(1)
                          .set_max_length(static_cast<std::int64_t>(llm::persona_name_limit)));
    create.add_option(dpp::command_option(dpp::co_string, "avatar", "An https:// link to its picture.", false)
                          .set_max_length(static_cast<std::int64_t>(llm::avatar_url_limit)));
    create.add_option(persona_model_option("Its model. The server's by default."));
    create.add_option(
        dpp::command_option(dpp::co_boolean, "talks", "Let it message LatiBot and the other personas. Off by default.", false));
    if (services_.speech != nullptr) create.add_option(voice_option("The voice it speaks in, in a voice session."));
    persona.add_option(create);

    dpp::command_option change(dpp::co_sub_command, "edit", "Change one (Manage Server).");
    change.add_option(persona_option("Which.", true));
    change.add_option(dpp::command_option(dpp::co_string, "name", "A new name.", false)
                          .set_max_length(static_cast<std::int64_t>(llm::persona_name_limit)));
    change.add_option(dpp::command_option(dpp::co_string, "avatar", "A new picture link, or none.", false)
                          .set_max_length(static_cast<std::int64_t>(llm::avatar_url_limit)));
    change.add_option(persona_model_option("A model of its own."));
    change.add_option(dpp::command_option(dpp::co_boolean, "server_model", "Go back to the server's model.", false));
    change.add_option(dpp::command_option(dpp::co_boolean, "enabled", "Turn it on or off.", false));
    change.add_option(dpp::command_option(dpp::co_boolean, "talks", "Let it message LatiBot and the other personas.", false));
    if (services_.speech != nullptr) change.add_option(voice_option("The voice it speaks in, in a voice session."));
    persona.add_option(change);

    dpp::command_option drop(dpp::co_sub_command, "remove", "Delete one, its documents and triggers (Manage Server).");
    drop.add_option(persona_option("Which.", true));
    persona.add_option(drop);

    persona.add_option(dpp::command_option(dpp::co_sub_command, "list", "Every persona here."));
    dpp::command_option view(dpp::co_sub_command, "view", "One persona: its model, picture and triggers.");
    view.add_option(persona_option("Which.", true));
    persona.add_option(view);

    dpp::command_option document(dpp::co_sub_command, "document", "Its system instructions or personality.");
    document.add_option(persona_option("Whose.", true));
    dpp::command_option which_document(dpp::co_string, "which", "Which document.", true);
    which_document.add_choice(dpp::command_option_choice("System instructions (Manage Server)", std::string("system")));
    which_document.add_choice(dpp::command_option_choice("Personality", std::string("personality")));
    document.add_option(which_document);
    dpp::command_option action(dpp::co_string, "action", "What to do. View by default.", false);
    for (const char* each : {"view", "edit", "history", "diff", "revert"}) {
        action.add_choice(dpp::command_option_choice(each, std::string(each)));
    }
    document.add_option(action);
    document.add_option(dpp::command_option(dpp::co_attachment, "file", "edit: a .txt or .md file to replace it with.", false));
    document.add_option(dpp::command_option(dpp::co_integer, "version", "revert: from history; 0 is empty.", false).set_min_value(0));
    document.add_option(dpp::command_option(dpp::co_integer, "from", "diff: the older version.", false).set_min_value(0));
    document.add_option(dpp::command_option(dpp::co_integer, "to", "diff: the newer version.", false).set_min_value(0));
    persona.add_option(document);
    payload.add_option(persona);

    return payload;
}

auto llm_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || event.owner == nullptr) return;

    const auto* typed = std::get_if<std::string>(&focused->value);
    const std::string wanted = typed == nullptr ? std::string{} : util::to_lower(util::trim(*typed));
    std::vector<std::string> names;
    if (focused->name == "persona" && services_.personas != nullptr) {
        for (const llm::persona& who : services_.personas->for_guild(event.command.guild_id)) {
            names.push_back(who.name);
        }
    } else if (focused->name == "voice" && services_.speech != nullptr) {
        names = services_.speech->voices(event.command.guild_id);
    } else if (focused->name == "tool" && services_.tools != nullptr) {
        names = services_.tools->names();
        std::ranges::sort(names);
    } else {
        return;
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

auto llm_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string path = subcommand_path(event.command.get_command_interaction());
    const std::string_view group = std::string_view(path).substr(0, path.find(' '));
    const std::string_view action = !path.contains(' ') ? std::string_view{} : std::string_view(path).substr(path.find(' ') + 1);

    if (path == "status") {
        co_await status(event);
        co_return;
    }
    if (path == "personality editors") {
        co_await editors(event);
        co_return;
    }
    if (group == "persona") {
        co_await persona(event, action);
        co_return;
    }
    if (const auto kind = llm::document_kind_from_string(group); kind && !action.empty()) {
        co_await document(event, *kind, action);
        co_return;
    }

    // Everything else changes how the model behaves for the whole server.
    if (!manages_server(event)) {
        co_await event.co_reply(refusal(event, "that needs Manage Server"));
        co_return;
    }
    co_await manage(event, path, group, action);
}

auto llm_command::manage(const dpp::slashcommand_t& event, std::string_view path, std::string_view group, std::string_view action)
    -> dpp::task<void> {
    if (path == "on" || path == "off") {
        co_await switch_to(event, path == "on");
    } else if (path == "model") {
        co_await model(event);
    } else if (path == "settings") {
        const llm_panels panels(services_);
        co_await event.co_reply(result(event, panels.settings_panel(event.command.guild_id)));
    } else if (group == "conversation" && (action == "on" || action == "off")) {
        co_await switch_conversation(event, action == "on");
    } else if (group == "trigger") {
        co_await trigger(event, action);
    } else if (group == "blacklist") {
        co_await blacklist(event, action);
    } else if (group == "names") {
        co_await names(event, action);
    } else if (group == "tools") {
        co_await tools(event, action);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto llm_command::status(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.section);
    const auto now = seconds_now(*services_.clock);
    const llm::model_info* model = llm::find_model(settings.model);
    const llm::spend_caps caps{.daily = services_.section->spend_cap_daily_usd, .monthly = services_.section->spend_cap_monthly_usd};

    const llm_overview overview{
        .enabled = settings.enabled,
        .conversation = settings.conversation.enabled,
        .model = settings.model,
        .has_key = model != nullptr && services_.has_provider(model->provider),
        .spend = llm::spend_status_at(*services_.usage, caps, now),
        .caps = caps,
        .guild_this_month = services_.usage->spent_between(guild, llm::month_start(now), now + std::chrono::seconds{1})};
    co_await event.co_reply(result(event, render_llm_status(overview)));
}

auto llm_command::switch_conversation(const dpp::slashcommand_t& event, bool on) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    services_.settings->set_bool(guild, llm::conversation_key, on);
    util::log().info("conversation mode turned {} in guild {} by {}", on ? "on" : "off", guild,
                     describe_user(event.command.get_issuing_user()));

    std::string reply = on ? "ok, after i answer someone here i'll keep listening for a few minutes, and join in when a message "
                             "seems meant for me"
                           : "ok, i'll answer here only when addressed";
    if (on) {
        const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.section);
        const llm::model_info* checker = llm::find_model(services_.section->check_model);
        if (!settings.enabled) {
            reply += ", once the model itself is on: that's /llm on";
        } else if (checker == nullptr || !services_.has_provider(checker->provider)) {
            reply += ", but there's no API key on the bot for the model that decides, so it can't yet";
        }
    }
    co_await event.co_reply(result(event, reply));
}

auto llm_command::switch_to(const dpp::slashcommand_t& event, bool on) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    services_.settings->set_bool(guild, llm::enabled_key, on);
    util::log().info("the language model turned {} in guild {} by {}", on ? "on" : "off", guild,
                     describe_user(event.command.get_issuing_user()));

    std::string reply = on ? "ok, i'll answer here when addressed" : "ok, i'll stay quiet here";
    const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.section);
    const llm::model_info* model = llm::find_model(settings.model);
    if (on && (model == nullptr || !services_.has_provider(model->provider))) {
        reply += ", but there's no API key for its model on the bot, so it can't yet";
    }
    co_await event.co_reply(result(event, reply));
}

auto llm_command::model(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string wanted = string_option(event, "name");
    const llm::model_info* chosen = llm::find_model(wanted);
    if (chosen == nullptr) {
        co_await event.co_reply(refusal(event, std::format("i don't know a model called \"{}\"", wanted)));
        co_return;
    }
    if (!services_.has_provider(chosen->provider)) {
        co_await event.co_reply(refusal(
            event, std::format("there's no {} API key on the bot, so {} can't be used", llm::to_string(chosen->provider), chosen->label)));
        co_return;
    }

    services_.settings->set(event.command.guild_id, llm::model_key, chosen->id);
    util::log().info("the language model in guild {} set to {} by {}", event.command.guild_id, chosen->id,
                     describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, std::format("ok, this server now uses {}", chosen->label)));
}

auto llm_command::may_change(const dpp::slashcommand_t& event, const document_target& target) const -> bool {
    const bool admin = manages_server(event);
    if (target.ref.kind != llm::document_kind::personality) return admin;
    const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, event.command.guild_id, *services_.section);
    return may_edit_personality(admin, event.command.guild_id, settings.personality_role, event.command.member.get_roles());
}

auto llm_command::document(const dpp::slashcommand_t& event, const document_target& target, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const bool personality = target.ref.kind == llm::document_kind::personality;

    // The personality is for everyone to read; the other two are admins'
    // business, since they can hold rules better kept out of sight.
    const bool reading = action == "view" || action == "history" || action == "diff";
    if (reading ? !(personality || manages_server(event)) : !may_change(event, target)) {
        co_await event.co_reply(
            refusal(event, personality ? std::format("you can't edit {} here", document_label(target)) : "that needs Manage Server"));
        co_return;
    }

    if (action == "view") {
        const auto current = services_.documents->current(guild, target.ref);
        const llm::document_version shown = current.value_or(llm::document_version{
            .version = 0, .content = std::string(llm::default_document(target.ref)), .edited_by = {}, .edited_at = {}, .note = {}});
        co_await event.co_reply(result(event, render_document(target, shown, !current)));
    } else if (action == "edit") {
        co_await edit_document(event, target);
    } else if (action == "history") {
        co_await event.co_reply(result(event, render_history(target, services_.documents->history(guild, target.ref))));
    } else if (action == "diff") {
        co_await diff_document(event, target);
    } else if (action == "revert") {
        co_await revert_document(event, target);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto llm_command::edit_document(const dpp::slashcommand_t& event, const document_target& target) -> dpp::task<void> {
    if (const auto file = snowflake_option(event, "file")) {
        co_await edit_from_file(event, target, *file);
        co_return;
    }
    const auto form = document_form(target, services_.documents->text(event.command.guild_id, target.ref));
    if (!form) {
        co_await event.co_reply(refusal(event, "it's too long to edit in a form; attach it as a .txt or .md file with `file:`"));
        co_return;
    }
    const auto opened = co_await event.co_dialog(*form);
    if (opened.is_error()) util::log().warn("could not open the form for {}: {}", document_label(target), opened.get_error().message);
}

auto llm_command::diff_document(const dpp::slashcommand_t& event, const document_target& target) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto current = services_.documents->current(guild, target.ref);
    const int newest = current ? current->version : 0;
    const int to = static_cast<int>(int_option(event, "to").value_or(newest));
    const int from = static_cast<int>(int_option(event, "from").value_or(std::max(0, to - 1)));
    const auto older = services_.documents->version(guild, target.ref, from);
    const auto newer = services_.documents->version(guild, target.ref, to);
    if (!older || !newer) {
        co_await event.co_reply(refusal(event, std::format("there's no version {} of {}", !older ? from : to, document_label(target))));
        co_return;
    }

    const std::string diff = llm::diff_lines(older->content, newer->content);
    const std::string heading = std::format("{} from version {} to {}:", capitalised(document_label(target)), from, to);
    if (diff.empty()) {
        co_await event.co_reply(result(event, std::format("{} no change", heading)));
    } else if (util::character_count(diff) <= inline_limit && !diff.contains("```")) {
        co_await event.co_reply(result(event, std::format("{}\n```diff\n{}```", heading, diff)));
    } else {
        dpp::message message(std::format("{} (attached, it is too long to show here)", heading));
        message.add_file(std::format("{}-v{}-v{}.diff", llm::to_string(target.ref), from, to), diff, "text/plain");
        co_await event.co_reply(result(event, std::move(message)));
    }
}

auto llm_command::revert_document(const dpp::slashcommand_t& event, const document_target& target) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const int number = static_cast<int>(int_option(event, "version").value_or(0));
    const auto saved =
        services_.documents->revert(guild, target.ref, number, event.command.get_issuing_user().id, seconds_now(*services_.clock));
    if (!saved) {
        co_await event.co_reply(refusal(event, std::format("there's no version {} of {}", number, document_label(target))));
        co_return;
    }
    // Edits go to the log, not to the channel
    // (src/modules/llm/docs/Language_Model.md §3.5).
    util::log().info("{} in guild {} reverted to version {} by {}, as version {}", document_label(target), guild, number,
                     describe_user(event.command.get_issuing_user()), *saved);
    co_await event.co_reply(result(event, describe_saved(target, *saved, services_.documents->text(guild, target.ref))));
}

auto llm_command::edit_from_file(const dpp::slashcommand_t& event, const document_target& target, dpp::snowflake attachment)
    -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;

    const dpp::attachment* file = nullptr;
    try {
        file = &event.command.get_resolved_attachment(attachment);
    } catch (const dpp::logic_exception&) {
        file = nullptr;
    }
    const std::string name = file == nullptr ? std::string{} : util::to_lower(file->filename);
    if (file == nullptr || !(name.ends_with(".txt") || name.ends_with(".md"))) {
        co_await event.co_reply(refusal(event, "attach a .txt or .md file"));
        co_return;
    }
    if (file->size > document_file_limit) {
        co_await event.co_reply(
            refusal(event, std::format("that file is too big; a document is at most {} characters", llm::document_length_limit)));
        co_return;
    }

    co_await defer(event);
    const auto downloaded =
        co_await services_.http->send({.url = file->url, .method = ports::http_method::get, .body = {}, .content_type = {}, .headers = {}});
    if (!downloaded.has_value() || downloaded.value().status != 200) {
        util::log().warn("could not download {} for {} in guild {}: {}", file->filename, document_label(target), guild,
                         downloaded.has_value() ? std::format("status {}", downloaded.value().status) : downloaded.error().message);
        co_await answer_deferred(event, refusal(event, "i couldn't download that file; try again"));
        co_return;
    }

    std::string text(util::trim(downloaded.value().body));
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    if (util::character_count(text) > llm::document_length_limit) {
        co_await answer_deferred(event, refusal(event, std::format("that's over the {}-character limit", llm::document_length_limit)));
        co_return;
    }

    const int version = services_.documents->save(guild, target.ref, text, event.command.get_issuing_user().id,
                                                  seconds_now(*services_.clock), std::format("from {}", file->filename));
    util::log().info("{} in guild {} replaced from {} by {}: version {}, {} characters", document_label(target), guild, file->filename,
                     describe_user(event.command.get_issuing_user()), version, util::character_count(text));
    co_await answer_deferred(event, result(event, describe_saved(target, version, text)));
}

auto llm_command::editors(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto role = snowflake_option(event, "role");
    if (!role) {
        const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.section);
        co_await event.co_reply(
            result(event, settings.personality_role == guild
                              ? std::string("anyone can edit the personality here")
                              : std::format("people with <@&{}> can edit the personality here, and anyone with Manage Server",
                                            settings.personality_role)));
        co_return;
    }
    if (!manages_server(event)) {
        co_await event.co_reply(refusal(event, "changing who may edit it needs Manage Server"));
        co_return;
    }

    services_.settings->set(guild, llm::personality_role_key, std::to_string(static_cast<std::uint64_t>(*role)));
    util::log().info("personality editors in guild {} set to role {} by {}", guild, *role, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, *role == guild ? std::string("ok, anyone can edit the personality")
                                                         : std::format("ok, people with <@&{}> can edit the personality", *role)));
}

auto llm_command::trigger(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const commands::user_label who = describe_user(event.command.get_issuing_user());

    if (action == "list") {
        const auto all = services_.triggers->for_guild(guild);
        const auto personas = services_.personas == nullptr ? std::vector<llm::persona>{} : services_.personas->for_guild(guild);
        std::string text = all.empty() ? std::string("No advanced triggers here yet. Add one with `/llm trigger add`.")
                                       : std::string("**Advanced triggers**\n");
        for (const llm::advanced_trigger& entry : all) {
            const auto owner = std::ranges::find(personas, entry.persona_id, &llm::persona::id);
            text += describe_trigger(entry, owner == personas.end() ? std::string_view{} : std::string_view(owner->name)) + "\n";
        }
        co_await event.co_reply(result(event, util::truncate(text, message_limit)));
        co_return;
    }

    if (action == "remove") {
        const std::int64_t id = int_option(event, "id").value_or(0);
        if (!services_.triggers->remove(id, guild)) {
            co_await event.co_reply(refusal(event, std::format("no advanced trigger `{}` here", id)));
            co_return;
        }
        util::log().info("advanced trigger {} removed from guild {} by {}", id, guild, who);
        co_await event.co_reply(result(event, std::format("removed advanced trigger `{}`", id)));
        co_return;
    }

    llm::advanced_trigger entry;
    if (action == "edit") {
        const std::int64_t id = int_option(event, "id").value_or(0);
        auto found = services_.triggers->find(id, guild);
        if (!found) {
            co_await event.co_reply(refusal(event, std::format("no advanced trigger `{}` here", id)));
            co_return;
        }
        entry = std::move(*found);

        // Given nothing to change, it opens a form holding the trigger as it
        // is, since a command's options cannot start out filled in.
        if (given_options(event) <= 1) {
            const auto opened = co_await event.co_dialog(trigger_form(entry));
            if (opened.is_error()) util::log().warn("could not open the form for advanced trigger {}: {}", id, opened.get_error().message);
            co_return;
        }
    } else if (action == "add") {
        entry.guild_id = guild;
        entry.created_by = event.command.get_issuing_user().id;
        entry.cooldown = llm::default_advanced_cooldown;
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
        co_return;
    }

    // An edit changes what was given and leaves the rest.
    if (const std::string pattern(util::trim(string_option(event, "pattern"))); !pattern.empty()) entry.pattern = pattern;
    if (const std::string prompt(util::trim(string_option(event, "prompt"))); !prompt.empty()) entry.context_prompt = prompt;
    if (const auto chance = int_option(event, "chance")) entry.probability = static_cast<double>(*chance) / 100.0;
    if (const auto cooldown = int_option(event, "cooldown")) entry.cooldown = std::chrono::seconds{*cooldown};
    if (const auto mode = util::match_mode_from_string(string_option(event, "mode"))) entry.mode = *mode;
    entry.enabled = bool_option(event, "enabled").value_or(entry.enabled);
    entry.respond_to_bots = bool_option(event, "bots").value_or(entry.respond_to_bots);

    // Who answers it: a persona, or LatiBot itself
    // (src/modules/llm/docs/Language_Model.md §2.11).
    std::string answered_by;
    if (const std::string named(util::trim(string_option(event, "persona"))); !named.empty()) {
        if (util::equals_ignoring_case(named, latibot_choice)) {
            entry.persona_id = 0;
        } else {
            const auto owner = named_persona(event, "persona");
            if (!owner) {
                co_await event.co_reply(refusal(event, std::format("there's no persona called \"{}\" here", named)));
                co_return;
            }
            entry.persona_id = owner->id;
            answered_by = owner->name;
        }
    } else if (entry.persona_id != 0 && services_.personas != nullptr) {
        if (const auto owner = services_.personas->find(entry.persona_id, guild)) answered_by = owner->name;
    }

    if (entry.pattern.empty() || entry.context_prompt.empty()) {
        co_await event.co_reply(refusal(event, "a trigger needs a pattern and a prompt"));
        co_return;
    }
    if (const auto problem = util::pattern_error(entry.pattern, entry.mode)) {
        co_await event.co_reply(refusal(event, std::format("that pattern won't work: {}", *problem)));
        co_return;
    }

    if (action == "add") {
        entry.id = services_.triggers->add(entry);
    } else if (!services_.triggers->update(entry)) {
        co_await event.co_reply(refusal(event, std::format("no advanced trigger `{}` here", entry.id)));
        co_return;
    }
    util::log().info("advanced trigger {} {} in guild {} by {}: {}", entry.id, action == "add" ? "added" : "updated", guild, who,
                     describe_trigger(entry, answered_by));
    co_await event.co_reply(result(
        event,
        util::truncate(std::format("{} {}", action == "add" ? "added" : "updated", describe_trigger(entry, answered_by)), message_limit)));
}

auto render_tools(const std::vector<llm::tool_definition>& tools, const std::set<std::string, std::less<>>& off) -> std::string {
    std::string text = "**What the model may use here**\n";
    for (const llm::tool_definition& tool : tools) {
        const std::size_t stop = tool.description.find(". ");
        const std::string_view first =
            std::string_view(tool.description).substr(0, stop == std::string::npos ? std::string::npos : stop + 1);
        const bool latibot_only = std::ranges::find(llm::latibot_only_tools, tool.name) != llm::latibot_only_tools.end();
        text += std::format("{} `{}`{}: {}\n", off.contains(tool.name) ? "⛔" : "✅", tool.name, latibot_only ? " (LatiBot only)" : "",
                            util::truncate(first, 110));
    }
    constexpr std::string_view footer = "\n_`/llm tools off` turns one off for LatiBot and every persona here._";
    if (text.size() + footer.size() <= 2000) return text + std::string(footer);

    // Too many to describe in one message: their names, each marked, which
    // is what turning one off needs.
    text = "**What the model may use here**\n";
    for (const llm::tool_definition& tool : tools) {
        text += std::format("{} `{}`  ", off.contains(tool.name) ? "⛔" : "✅", tool.name);
    }
    text += "\n";
    return util::truncate(text + std::string(footer), 2000);
}

auto llm_command::tools(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    if (services_.tools == nullptr) {
        co_await event.co_reply(refusal(event, "the model has no tools in this build"));
        co_return;
    }
    if (action == "list") {
        co_await event.co_reply(result(event, render_tools(services_.tools->definitions(), llm::tools_off(*services_.settings, guild))));
        co_return;
    }

    const std::string name = string_option(event, "tool");
    if (!services_.tools->contains(name)) {
        co_await event.co_reply(refusal(event, std::format("there is no tool called `{}`", name)));
        co_return;
    }
    const bool on = action == "on";
    const bool changed = llm::switch_tool(*services_.settings, guild, name, on);
    if (changed) {
        util::log().info("{} turned the model's {} tool {} in guild {}", describe_user(event.command.get_issuing_user()), name,
                         on ? "on" : "off", guild);
    }
    co_await event.co_reply(result(event, changed ? std::format("ok, `{}` is {} here", name, on ? "on" : "off")
                                                  : std::format("`{}` was already {} here", name, on ? "on" : "off")));
}

auto render_alias_names(const std::map<dpp::snowflake, std::vector<std::string>>& names, dpp::snowflake only) -> std::string {
    std::string text;
    for (const auto& [user, theirs] : names) {
        if (!only.empty() && user != only) continue;
        std::string joined;
        for (const std::string& name : theirs) {
            if (!joined.empty()) joined += ", ";
            joined += std::format("`{}`", name);
        }
        text += std::format("- <@{}>: {}\n", user, joined);
    }
    if (text.empty()) {
        text = only.empty() ? "No names are set by hand here." : std::format("No names are set by hand for <@{}>.", only);
    } else {
        text = "**Names hidden behind each person's alias**\n" + text;
    }
    text += "\n_Their server nickname, display name and username are hidden anyway._";
    return util::truncate(text, 2000);
}

auto llm_command::names(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto user = snowflake_option(event, "user");

    if (action == "list") {
        co_await event.co_reply(result(event, render_alias_names(services_.aliases->added_names(guild), user.value_or(dpp::snowflake{}))));
        co_return;
    }

    const std::string name(util::trim(string_option(event, "name")));
    if (!user || user->empty() || name.empty()) {
        co_await event.co_reply(refusal(event, "give a user and a name"));
        co_return;
    }
    // Shorter names are mostly words, and are never looked for.
    if (util::character_count(name) < llm::shortest_name) {
        co_await event.co_reply(
            refusal(event, std::format("a name needs at least {} characters, or it would hide ordinary words", llm::shortest_name)));
        co_return;
    }
    if (*user == event.command.application_id) {
        co_await event.co_reply(refusal(event, "my own name isn't hidden: the model knows it is me"));
        co_return;
    }

    const auto who = describe_user(event.command.get_issuing_user());
    if (action == "add") {
        const std::optional<dpp::snowflake> taken = services_.aliases->add_name(guild, *user, name);
        if (taken && *taken == *user) {
            co_await event.co_reply(result(event, std::format("`{}` was already <@{}>'s", name, *user)));
        } else if (taken) {
            co_await event.co_reply(refusal(event, std::format("`{}` already stands for <@{}>; remove it from them first", name, *taken)));
        } else {
            util::log().info("{} hid the name \"{}\" behind {}'s alias in guild {}", who, name, *user, guild);
            co_await event.co_reply(
                result(event, std::format("ok, the model will see `{}` as <@{}>'s alias, never the name", name, *user)));
        }
        co_return;
    }
    if (action == "remove") {
        const bool removed = services_.aliases->remove_name(guild, *user, name);
        if (removed) util::log().info("{} stopped hiding the name \"{}\" behind {}'s alias in guild {}", who, name, *user, guild);
        co_await event.co_reply(result(event, removed ? std::format("ok, `{}` is no longer <@{}>'s", name, *user)
                                                      : std::format("`{}` wasn't one of <@{}>'s names", name, *user)));
        co_return;
    }
    co_await event.co_reply(refusal(event, "i don't know that subcommand"));
}

auto llm_command::blacklist(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;

    if (action == "list") {
        const auto entries = services_.blacklist->list(guild);
        std::string text =
            entries.empty() ? std::string("Nobody is on the blacklist here.") : std::string("**The model does not answer**\n");
        for (const llm::block_entry& entry : entries) {
            text += entry.kind == llm::block_kind::role ? std::format("- anyone with <@&{}>\n", entry.target)
                                                        : std::format("- <@{}>\n", entry.target);
        }
        co_await event.co_reply(result(event, util::truncate(text, 2000)));
        co_return;
    }

    const auto user = snowflake_option(event, "user");
    const auto role = snowflake_option(event, "role");
    if (user.has_value() == role.has_value()) {
        co_await event.co_reply(refusal(event, "give a user or a role, one of them"));
        co_return;
    }
    const llm::block_kind kind = user ? llm::block_kind::user : llm::block_kind::role;
    const dpp::snowflake target = user ? *user : *role;
    const std::string named = user ? std::format("<@{}>", target) : std::format("anyone with <@&{}>", target);

    const bool adding = action == "add";
    const bool changed = adding ? services_.blacklist->add(guild, kind, target) : services_.blacklist->remove(guild, kind, target);
    if (changed) {
        util::log().info("{} {} {} the language model's blacklist in guild {}", describe_user(event.command.get_issuing_user()),
                         adding ? "added" : "removed", std::format("{} {}", llm::to_string(kind), target), guild);
    }
    std::string reply;
    if (adding) {
        reply = changed ? std::format("ok, i won't answer {}", named) : std::format("{} was already on the list", named);
    } else {
        reply = changed ? std::format("ok, i'll answer {} again", named) : std::format("{} wasn't on the list", named);
    }
    co_await event.co_reply(result(event, reply));
}

auto llm_command::named_persona(const dpp::slashcommand_t& event, const char* option) const -> std::optional<llm::persona> {
    const std::string name(util::trim(string_option(event, option)));
    if (name.empty() || services_.personas == nullptr) return std::nullopt;
    return services_.personas->find_named(event.command.guild_id, name);
}

auto llm_command::persona(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    if (services_.personas == nullptr) {
        co_await event.co_reply(refusal(event, "personas aren't available on this bot"));
        co_return;
    }

    if (action == "list") {
        co_await event.co_reply(result(event, render_personas(services_.personas->for_guild(guild))));
        co_return;
    }
    if (action == "add") {
        if (!manages_server(event)) {
            co_await event.co_reply(refusal(event, "that needs Manage Server"));
            co_return;
        }
        co_await add_persona(event);
        co_return;
    }

    // Everything else is about one of them.
    const auto who = named_persona(event, "persona");
    if (!who) {
        co_await event.co_reply(
            refusal(event, std::format("there's no persona called \"{}\" here", util::trim(string_option(event, "persona")))));
        co_return;
    }

    if (action == "view") {
        auto triggers = services_.triggers->for_guild(guild);
        std::erase_if(triggers, [&](const llm::advanced_trigger& entry) { return entry.persona_id != who->id; });
        const auto now = seconds_now(*services_.clock);
        const double spent = services_.usage->spent_by_persona(who->id, llm::month_start(now), now + std::chrono::seconds{1});
        const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.section);
        co_await event.co_reply(result(event, render_persona(*who, settings.model, triggers, spent, services_.speech != nullptr)));
        co_return;
    }
    if (action == "document") {
        // Its documents go the way LatiBot's do, permissions and all.
        const llm::document_kind kind =
            string_option(event, "which") == "system" ? llm::document_kind::system : llm::document_kind::personality;
        const std::string chosen = string_option(event, "action");
        co_await document(event, document_target(llm::document_ref(kind, who->id), who->name), chosen.empty() ? "view" : chosen);
        co_return;
    }

    if (!manages_server(event)) {
        co_await event.co_reply(refusal(event, "that needs Manage Server"));
        co_return;
    }
    if (action == "edit") {
        co_await edit_persona(event, *who);
    } else if (action == "remove") {
        services_.personas->remove(who->id, guild);
        util::log().info("persona {} \"{}\" removed from guild {} by {}", who->id, who->name, guild,
                         describe_user(event.command.get_issuing_user()));
        co_await event.co_reply(result(event, std::format("removed {}, with its documents and triggers", who->name)));
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto llm_command::add_persona(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    if (services_.personas->count(guild) >= llm::persona_limit) {
        co_await event.co_reply(refusal(event, std::format("a server can have at most {} personas", llm::persona_limit)));
        co_return;
    }

    llm::persona made{.id = 0,
                      .guild_id = guild,
                      .name = std::string(util::trim(string_option(event, "name"))),
                      .avatar_url = std::string(util::trim(string_option(event, "avatar"))),
                      .model = string_option(event, "model"),
                      .enabled = true,
                      .created_by = event.command.get_issuing_user().id,
                      .created_at = seconds_now(*services_.clock),
                      .talks = bool_option(event, "talks").value_or(false),
                      .voice = {}};

    std::optional<std::string> problem = llm::persona_name_problem(made.name);
    if (!problem) problem = choose_voice(event, made);
    if (!problem) problem = llm::avatar_problem(made.avatar_url);
    if (!problem && services_.personas->find_named(guild, made.name))
        problem = std::format("there's already a persona called {}", made.name);
    if (!problem && !made.model.empty()) {
        const llm::model_info* model = llm::find_model(made.model);
        if (model == nullptr) {
            problem = std::format("i don't know a model called \"{}\"", made.model);
        } else if (!services_.has_provider(model->provider)) {
            problem = std::format("there's no {} API key on the bot, so {} can't be used", llm::to_string(model->provider), model->label);
        }
    }
    if (problem) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    made.id = services_.personas->add(made);
    util::log().info("persona {} \"{}\" made in guild {} by {}{}", made.id, made.name, guild,
                     describe_user(event.command.get_issuing_user()),
                     made.model.empty() ? std::string{} : std::format(", on {}", made.model));

    std::string reply = std::format(
        "made {0}: it answers replies to its messages, and its advanced triggers (`/llm trigger add persona:{0}`). Tell it who "
        "it is with `/llm persona document persona:{0} which:system action:edit`",
        made.name);
    if (!llm::load_llm_settings(*services_.settings, guild, *services_.section).enabled) reply += ". The model is off here: /llm on";
    co_await event.co_reply(result(event, reply));
}

auto llm_command::edit_persona(const dpp::slashcommand_t& event, llm::persona who) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    std::optional<std::string> problem;

    if (const std::string name(util::trim(string_option(event, "name"))); !name.empty() && name != who.name) {
        problem = llm::persona_name_problem(name);
        const auto taken = services_.personas->find_named(guild, name);
        if (!problem && taken && taken->id != who.id) problem = std::format("there's already a persona called {}", name);
        who.name = name;
    }
    if (const std::string avatar(util::trim(string_option(event, "avatar"))); !avatar.empty()) {
        const bool none = util::equals_ignoring_case(avatar, "none");
        if (!problem && !none) problem = llm::avatar_problem(avatar);
        who.avatar_url = none ? std::string{} : avatar;
    }
    if (const std::string model = string_option(event, "model"); !model.empty()) {
        const llm::model_info* found = llm::find_model(model);
        if (!problem && found == nullptr) problem = std::format("i don't know a model called \"{}\"", model);
        if (!problem && !services_.has_provider(found->provider)) {
            problem = std::format("there's no {} API key on the bot, so {} can't be used", llm::to_string(found->provider), found->label);
        }
        who.model = model;
    }
    if (bool_option(event, "server_model").value_or(false)) who.model.clear();
    who.enabled = bool_option(event, "enabled").value_or(who.enabled);
    who.talks = bool_option(event, "talks").value_or(who.talks);
    if (!problem) problem = choose_voice(event, who);

    if (problem) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }
    services_.personas->update(who);
    util::log().info(
        "persona {} in guild {} changed by {}: \"{}\", {}, {}{}", who.id, guild, describe_user(event.command.get_issuing_user()), who.name,
        who.model.empty() ? std::string("the server's model") : who.model, who.enabled ? "on" : "off",
        who.talks ? ", talks to the others" : "", who.voice.empty() ? std::string{} : std::format(", speaks as {}", who.voice));
    co_await event.co_reply(result(event, std::format("updated {}", who.name)));
}

auto llm_command::choose_voice(const dpp::slashcommand_t& event, llm::persona& who) const -> std::optional<std::string> {
    const std::string wanted(util::trim(string_option(event, "voice")));
    if (wanted.empty() || services_.speech == nullptr) return std::nullopt;

    const std::vector<std::string> voices = services_.speech->voices(event.command.guild_id);
    const std::optional<std::string> found = find_voice(voices, wanted);
    if (!found) return std::format("i don't know a voice called \"{}\"", wanted);
    // The default voice is kept as no choice at all, so it follows the
    // default should that ever change.
    who.voice = *found == voices.front() ? std::string{} : *found;
    return std::nullopt;
}

// --------------------------------------------------------------------------
// /memory
// --------------------------------------------------------------------------

memory_command::memory_command(llm_command_services services)
    : info_{.name = "memory",
            .description = "What the language model remembers here.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = std::nullopt,
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(std::move(services)) {}

auto memory_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option list(dpp::co_sub_command, "list", "What it remembers: about you, or anyone (Manage Server).");
    list.add_option(dpp::command_option(dpp::co_user, "user", "Whose. Yourself unless you have Manage Server.", false));
    payload.add_option(list);

    dpp::command_option forget(dpp::co_sub_command, "forget", "Remove one memory: one about you, or any (Manage Server).");
    forget.add_option(dpp::command_option(dpp::co_integer, "id", "From /memory list, without the #.", true).set_min_value(1));
    payload.add_option(forget);

    dpp::command_option clear(dpp::co_sub_command, "clear", "Remove everything about you, or about anyone (Manage Server).");
    clear.add_option(dpp::command_option(dpp::co_user, "user", "Whose. Yourself unless you have Manage Server.", false));
    clear.add_option(dpp::command_option(dpp::co_boolean, "everything", "Everything here, about everyone (Manage Server).", false));
    payload.add_option(clear);
    return payload;
}

auto memory_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string action = subcommand_path(event.command.get_command_interaction());
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    const bool admin = manages_server(event);

    if (action == "list") {
        const auto user = snowflake_option(event, "user");
        const std::optional<dpp::snowflake> subject = admin ? user : std::optional(user.value_or(caller));
        if (!admin && *subject != caller) {
            co_await event.co_reply(refusal(event, "you can see what i remember about you; anyone else needs Manage Server"));
            co_return;
        }
        const auto shown = named(services_.memories->list(guild, subject, 0, memories_per_page), guild, services_.aliases);
        co_await event.co_reply(result(event, render_memories(shown, services_.memories->count(guild, subject), 0, subject,
                                                              persona_names(services_.personas, guild))));
    } else if (action == "forget") {
        const std::int64_t id = int_option(event, "id").value_or(0);
        const auto entry = services_.memories->find(id, guild);
        if (!entry) {
            co_await event.co_reply(refusal(event, std::format("there's no memory #{} here", id)));
            co_return;
        }
        if (!admin && entry->subject != caller) {
            co_await event.co_reply(refusal(event, "you can remove memories about you; anyone else's needs Manage Server"));
            co_return;
        }
        services_.memories->remove(id, guild);
        util::log().info("memory #{} in guild {} removed by {}: \"{}\"", id, guild, describe_user(event.command.get_issuing_user()),
                         entry->content);
        co_await event.co_reply(result(event, std::format("forgot #{}", id)));
    } else if (action == "clear") {
        co_await clear(event, admin);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto memory_command::clear(const dpp::slashcommand_t& event, bool admin) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    const bool everything = bool_option(event, "everything").value_or(false);
    const auto user = snowflake_option(event, "user");
    if (!admin && (everything || (user && *user != caller))) {
        co_await event.co_reply(refusal(event, "you can clear what i remember about you; anything more needs Manage Server"));
        co_return;
    }
    if (everything && user) {
        co_await event.co_reply(refusal(event, "give a user, or everything, not both"));
        co_return;
    }

    const std::optional<dpp::snowflake> subject = everything ? std::nullopt : std::optional(user.value_or(caller));
    const int gone = services_.memories->clear(guild, subject);
    util::log().info("{} memories in guild {} cleared by {} ({})", gone, guild, describe_user(event.command.get_issuing_user()),
                     subject ? std::format("about {}", *subject) : std::string("everything"));
    co_await event.co_reply(result(event, std::format("forgot {} thing{}", gone, gone == 1 ? "" : "s")));
}

// --------------------------------------------------------------------------
// The panels
// --------------------------------------------------------------------------

llm_panels::llm_panels(llm_command_services services) : services_(std::move(services)) {}

auto llm_panels::values(dpp::snowflake guild) const -> std::map<std::string, std::int64_t, std::less<>> {
    std::map<std::string, std::int64_t, std::less<>> found;
    for (const llm::setting_spec& spec : llm::setting_specs()) {
        found.emplace(spec.key, llm::setting_value(*services_.settings, guild, spec));
    }
    return found;
}

auto llm_panels::settings_panel(dpp::snowflake guild) const -> dpp::message {
    return render_llm_settings(values(guild), services_.settings->get_bool(guild, llm::enabled_key, false),
                               services_.settings->get_bool(guild, llm::conversation_key, false));
}

auto llm_panels::on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
    const dpp::snowflake guild = event.command.guild_id;
    const bool admin = manages_server(event);

    if (state.view == memory_list_view) {
        const std::optional<dpp::snowflake> subject =
            state.argument == "all" ? std::nullopt : std::optional(dpp::snowflake(state.argument));
        if (!admin && subject != event.command.get_issuing_user().id) {
            ui::answer_privately(event, "that list needs Manage Server");
            return true;
        }
        const std::size_t total = services_.memories->count(guild, subject);
        const int page = ui::clamp_page(state.page, total, memories_per_page);
        const auto shown =
            named(services_.memories->list(guild, subject, static_cast<std::size_t>(page) * memories_per_page, memories_per_page), guild,
                  services_.aliases);
        ui::update_panel(event, render_memories(shown, total, page, subject, persona_names(services_.personas, guild)));
        return true;
    }

    if (state.view != llm_settings_view && state.view != llm_settings_pick_view && state.view != llm_switch_view) return false;

    // Discord only shows the panel to whoever opened it, but a button can
    // outlive a role change.
    if (!admin) {
        ui::answer_privately(event, "the settings need Manage Server");
        return true;
    }

    if (state.view == llm_settings_pick_view) {
        const auto form = llm_settings_form(chosen, values(guild));
        if (form) {
            event.dialog(*form);
        } else {
            ui::update_panel(event, settings_panel(guild));
        }
    } else if (state.view == llm_switch_view) {
        // "on" and "off" switch the model; "conversation on" and
        // "conversation off", conversation mode.
        const bool conversation = state.argument.starts_with("conversation ");
        const bool on = state.argument.ends_with("on");
        services_.settings->set_bool(guild, conversation ? llm::conversation_key : llm::enabled_key, on);
        util::log().info("{} turned {} in guild {} by {} from the panel", conversation ? "conversation mode" : "the language model",
                         on ? "on" : "off", guild, describe_user(event.command.get_issuing_user()));
        ui::update_panel(event, settings_panel(guild));
    } else {
        ui::update_panel(event, settings_panel(guild));
    }
    return true;
}

auto llm_panels::on_form(const dpp::form_submit_t& event, const ui::page_state& state) const -> bool {
    const dpp::snowflake guild = event.command.guild_id;
    const commands::user_label who = describe_user(event.command.get_issuing_user());

    if (state.view == llm_settings_form_view) {
        if (!manages_server(event)) {
            ui::answer_privately(event, "the settings need Manage Server");
            return true;
        }
        const auto read = read_llm_settings_form(state.argument, ui::form_fields(event));
        if (const auto* problem = std::get_if<std::string>(&read)) {
            ui::answer_privately(event, *problem);
            return true;
        }
        std::string changed;
        for (const auto& [key, value] : std::get<std::vector<std::pair<std::string_view, std::int64_t>>>(read)) {
            services_.settings->set_int(guild, key, value);
            if (!changed.empty()) changed += ", ";
            changed += std::format("{}={}", key, value);
        }
        util::log().info("language model settings in guild {} changed by {} from the panel: {}", guild, who, changed);
        ui::update_panel(event, settings_panel(guild));
        return true;
    }

    if (state.view == llm_trigger_form_view) {
        if (!manages_server(event)) {
            ui::answer_privately(event, "advanced triggers need Manage Server");
            return true;
        }
        std::int64_t id = 0;
        const auto [stop, error] = std::from_chars(state.argument.data(), state.argument.data() + state.argument.size(), id);
        if (error != std::errc{} || stop != state.argument.data() + state.argument.size()) return false;

        // Removed from another client while the form was open.
        auto entry = services_.triggers->find(id, guild);
        if (!entry) {
            ui::answer_privately(event, std::format("advanced trigger `{}` is gone, so nothing was saved", id));
            return true;
        }
        if (const auto problem = apply_trigger_form(*entry, ui::form_fields(event))) {
            ui::answer_privately(event, *problem);
            return true;
        }
        if (!services_.triggers->update(*entry)) {
            ui::answer_privately(event, std::format("advanced trigger `{}` is gone, so nothing was saved", id));
            return true;
        }

        std::string answered_by;
        if (entry->persona_id != 0 && services_.personas != nullptr) {
            if (const auto owner = services_.personas->find(entry->persona_id, guild)) answered_by = owner->name;
        }
        util::log().info("advanced trigger {} updated in guild {} by {} from its form: {}", id, guild, who,
                         describe_trigger(*entry, answered_by));
        ui::answer_privately(event, util::truncate(std::format("updated {}", describe_trigger(*entry, answered_by)), message_limit));
        return true;
    }

    if (state.view == llm_document_form_view) {
        const auto ref = llm::document_ref_from_string(state.argument);
        if (!ref) return false;

        // A persona's document, of a persona removed while the form was
        // open, has nowhere to go.
        document_target target(ref->kind);
        if (ref->persona != 0) {
            const auto owner = services_.personas == nullptr ? std::nullopt : services_.personas->find(ref->persona, guild);
            if (!owner) {
                ui::answer_privately(event, "that persona is gone, so nothing was saved");
                return true;
            }
            target = document_target(*ref, owner->name);
        }

        // Checked again: the form could have been opened before a role was
        // taken away.
        const bool admin = manages_server(event);
        const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.section);
        const bool allowed = ref->kind == llm::document_kind::personality
                                 ? may_edit_personality(admin, guild, settings.personality_role, event.command.member.get_roles())
                                 : admin;
        if (!allowed) {
            ui::answer_privately(event, "you can't edit that here");
            return true;
        }

        // Every part has to have come back, empty or not. One that did not
        // was misread rather than cleared, and saving without it would cut
        // that part out of the document.
        const ui::form_values fields = ui::form_fields(event);
        std::vector<std::string> parts;
        for (std::size_t index = 1; index <= form_parts; ++index) {
            const auto found = fields.find(std::format("part{}", index));
            if (found == fields.end()) {
                util::log().warn("the form for {} from {} in guild {} came back without part {}; nothing saved", document_label(target),
                                 who, guild, index);
                ui::answer_privately(event, "that form came back incomplete, so nothing was saved; try again");
                return true;
            }
            parts.push_back(found->second);
        }
        const std::string text = join_document_parts(parts);
        if (text == services_.documents->text(guild, *ref)) {
            ui::answer_privately(event, "nothing changed, so nothing was saved");
            return true;
        }

        const int version =
            services_.documents->save(guild, *ref, text, event.command.get_issuing_user().id, seconds_now(*services_.clock));
        // Edits go to the log, not to the channel
        // (src/modules/llm/docs/Language_Model.md §3.5).
        util::log().info("{} in guild {} edited by {}: version {}, {} characters", document_label(target), guild, who, version,
                         util::character_count(text));
        ui::answer_privately(event, describe_saved(target, version, text));
        return true;
    }

    return false;
}

} // namespace latibot::commands
