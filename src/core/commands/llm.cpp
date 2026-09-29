#include "core/commands/llm.hpp"

#include "core/commands/options.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/llm/advanced_triggers.hpp"
#include "core/llm/guards.hpp"
#include "core/ports/clock.hpp"
#include "core/ports/http_client.hpp"
#include "core/ui/interaction.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>

#include <algorithm>
#include <array>
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

auto label_of(llm::document_kind kind) -> std::string_view {
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

auto seconds_now(const ports::clock& clock) -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::seconds>(clock.now());
}

auto manages_server(const dpp::interaction_create_t& event) -> bool {
    return invoker_permissions(event).can(dpp::p_manage_guild);
}

auto group_title(std::string_view group) -> std::string_view {
    if (group == "context") return "What it reads";
    if (group == "replies") return "Replies and rate limits";
    return "Talking to other bots";
}

auto describe_trigger(const llm::advanced_trigger& entry) -> std::string {
    std::string line = std::format("`{}` **{}** ({}, {:.0f}%, ", entry.id, entry.pattern,
                                   entry.mode == events::match_mode::substring ? "anywhere" : "whole word", entry.probability * 100);
    line += entry.cooldown.count() == 0 ? "no cooldown" : std::format("{}s cooldown", entry.cooldown.count());
    if (!entry.enabled) line += ", disabled";
    line += std::format(") -> {}", util::truncate(entry.context_prompt, 120));
    return line;
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

auto match_mode_option() -> dpp::command_option {
    dpp::command_option mode(dpp::co_string, "mode", "Whether the pattern must stand alone.", false);
    mode.add_choice(dpp::command_option_choice("Whole word", std::string("whole_word")));
    mode.add_choice(dpp::command_option_choice("Anywhere in the message", std::string("substring")));
    return mode;
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

auto document_form(llm::document_kind kind, std::string_view current) -> std::optional<dpp::interaction_modal_response> {
    const auto parts = document_parts(current);
    if (!parts) return std::nullopt;

    const auto custom_id =
        ui::encode({.view = std::string(llm_document_form_view), .page = 0, .argument = std::string(llm::to_string(kind))});
    dpp::interaction_modal_response form(custom_id.value_or(std::string(llm_document_form_view)),
                                         std::format("Edit the {}", label_of(kind)));

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

auto render_document(llm::document_kind kind, const llm::document_version& shown, bool is_default) -> dpp::message {
    const std::string heading = is_default ? std::format("The {} is the default:", label_of(kind))
                                           : std::format("The {}, version {}:", label_of(kind), shown.version);
    if (util::is_blank(shown.content)) return dpp::message(std::format("The {} is empty.", label_of(kind)));

    if (util::character_count(shown.content) <= inline_limit && shown.content.find("```") == std::string::npos) {
        return dpp::message(std::format("{}\n```\n{}\n```", heading, shown.content));
    }
    dpp::message message(std::format("{} (attached, it is too long to show here)", heading));
    message.add_file(std::format("{}.md", llm::to_string(kind)), shown.content, "text/markdown");
    return message;
}

auto render_history(llm::document_kind kind, std::span<const llm::document_version> versions) -> std::string {
    if (versions.empty()) return std::format("Nobody has edited the {} here; it is the default (version 0).", label_of(kind));

    std::string text = std::format("**Versions of the {}**, newest first:\n", label_of(kind));
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

auto describe_saved(llm::document_kind kind, int version, std::string_view content) -> std::string {
    const std::size_t tokens = llm::estimate_tokens(content);
    std::string text = std::format("saved the {} as version {} (about {} tokens)", label_of(kind), version, tokens);
    if (tokens > llm::large_document_tokens) {
        text += std::format(
            "; that's long, and it's sent with every message the model answers, so it costs on every one. Under {} "
            "tokens is kinder on the budget.",
            llm::large_document_tokens);
    }
    return text;
}

auto render_llm_settings(const std::map<std::string, std::int64_t, std::less<>>& values, bool enabled) -> dpp::message {
    std::string body = std::format("**Language model settings**: the model is **{}** here.\n", enabled ? "on" : "off");
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

auto render_memories(std::span<const llm::memory> memories, std::size_t total, int page, std::optional<dpp::snowflake> subject)
    -> dpp::message {
    const int current = ui::clamp_page(page, total, memories_per_page);
    std::string body = subject ? std::format("**What I remember about <@{}>**\n", *subject) : std::string("**What I remember here**\n");
    if (memories.empty()) body += "\nNothing yet.";
    for (const llm::memory& entry : memories) {
        body += std::format("`#{}` ", entry.id);
        if (entry.subject && !subject) body += std::format("(<@{}>) ", *entry.subject);
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
    trigger.add_option(add);

    dpp::command_option edit(dpp::co_sub_command, "edit", "Change one.");
    edit.add_option(dpp::command_option(dpp::co_integer, "id", "From /llm trigger list.", true).set_min_value(1));
    edit.add_option(dpp::command_option(dpp::co_string, "pattern", "New text to look for.", false).set_max_length(trigger_pattern_limit));
    edit.add_option(dpp::command_option(dpp::co_string, "prompt", "New instruction.", false)
                        .set_max_length(static_cast<std::int64_t>(llm::context_prompt_limit)));
    edit.add_option(
        dpp::command_option(dpp::co_integer, "chance", "Percent of matches it answers.", false).set_min_value(1).set_max_value(100));
    edit.add_option(dpp::command_option(dpp::co_integer, "cooldown", "Seconds.", false).set_min_value(0).set_max_value(cooldown_limit));
    edit.add_option(match_mode_option());
    edit.add_option(dpp::command_option(dpp::co_boolean, "enabled", "Turn it on or off.", false));
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

    return payload;
}

auto llm_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string path = subcommand_path(event.command.get_command_interaction());
    const std::string_view group = std::string_view(path).substr(0, path.find(' '));
    const std::string_view action =
        path.find(' ') == std::string::npos ? std::string_view{} : std::string_view(path).substr(path.find(' ') + 1);

    if (path == "status") {
        co_await status(event);
        co_return;
    }
    if (path == "personality editors") {
        co_await editors(event);
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
    } else if (group == "trigger") {
        co_await trigger(event, action);
    } else if (group == "blacklist") {
        co_await blacklist(event, action);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto llm_command::status(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.bootstrap);
    const auto now = seconds_now(*services_.clock);
    const llm::model_info* model = llm::find_model(settings.model);
    const llm::spend_caps caps{.daily = services_.bootstrap->spend_cap_daily_usd, .monthly = services_.bootstrap->spend_cap_monthly_usd};

    const llm_overview overview{
        .enabled = settings.enabled,
        .model = settings.model,
        .has_key = model != nullptr && services_.has_provider(model->provider),
        .spend = llm::spend_status_at(*services_.usage, caps, now),
        .caps = caps,
        .guild_this_month = services_.usage->spent_between(guild, llm::month_start(now), now + std::chrono::seconds{1})};
    co_await event.co_reply(result(event, render_llm_status(overview)));
}

auto llm_command::switch_to(const dpp::slashcommand_t& event, bool on) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    services_.settings->set_bool(guild, llm::enabled_key, on);
    util::log().info("the language model turned {} in guild {} by {}", on ? "on" : "off", guild,
                     describe_user(event.command.get_issuing_user()));

    std::string reply = on ? "ok, i'll answer here when addressed" : "ok, i'll stay quiet here";
    const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.bootstrap);
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

auto llm_command::may_change(const dpp::slashcommand_t& event, llm::document_kind kind) const -> bool {
    const bool admin = manages_server(event);
    if (kind != llm::document_kind::personality) return admin;
    const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, event.command.guild_id, *services_.bootstrap);
    return may_edit_personality(admin, event.command.guild_id, settings.personality_role, event.command.member.get_roles());
}

auto llm_command::document(const dpp::slashcommand_t& event, llm::document_kind kind, std::string_view action) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const bool personality = kind == llm::document_kind::personality;

    // The personality is for everyone to read; the other two are admins'
    // business, since they can hold rules better kept out of sight.
    const bool reading = action == "view" || action == "history" || action == "diff";
    if (reading ? !(personality || manages_server(event)) : !may_change(event, kind)) {
        co_await event.co_reply(refusal(event, personality ? "you can't edit the personality here" : "that needs Manage Server"));
        co_return;
    }

    if (action == "view") {
        const auto current = services_.documents->current(guild, kind);
        const llm::document_version shown = current.value_or(llm::document_version{
            .version = 0, .content = std::string(llm::default_document(kind)), .edited_by = {}, .edited_at = {}, .note = {}});
        co_await event.co_reply(result(event, render_document(kind, shown, !current)));
    } else if (action == "edit") {
        co_await edit_document(event, kind);
    } else if (action == "history") {
        co_await event.co_reply(result(event, render_history(kind, services_.documents->history(guild, kind))));
    } else if (action == "diff") {
        co_await diff_document(event, kind);
    } else if (action == "revert") {
        co_await revert_document(event, kind);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto llm_command::edit_document(const dpp::slashcommand_t& event, llm::document_kind kind) -> dpp::task<void> {
    if (const auto file = snowflake_option(event, "file")) {
        co_await edit_from_file(event, kind, *file);
        co_return;
    }
    const auto form = document_form(kind, services_.documents->text(event.command.guild_id, kind));
    if (!form) {
        co_await event.co_reply(refusal(event, "it's too long to edit in a form; attach it as a .txt or .md file with `file:`"));
        co_return;
    }
    const auto opened = co_await event.co_dialog(*form);
    if (opened.is_error()) util::log().warn("could not open the {} form: {}", label_of(kind), opened.get_error().message);
}

auto llm_command::diff_document(const dpp::slashcommand_t& event, llm::document_kind kind) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto current = services_.documents->current(guild, kind);
    const int newest = current ? current->version : 0;
    const int to = static_cast<int>(int_option(event, "to").value_or(newest));
    const int from = static_cast<int>(int_option(event, "from").value_or(std::max(0, to - 1)));
    const auto older = services_.documents->version(guild, kind, from);
    const auto newer = services_.documents->version(guild, kind, to);
    if (!older || !newer) {
        co_await event.co_reply(refusal(event, std::format("there's no version {} of the {}", !older ? from : to, label_of(kind))));
        co_return;
    }

    const std::string diff = llm::diff_lines(older->content, newer->content);
    const std::string heading = std::format("The {} from version {} to {}:", label_of(kind), from, to);
    if (diff.empty()) {
        co_await event.co_reply(result(event, std::format("{} no change", heading)));
    } else if (util::character_count(diff) <= inline_limit && diff.find("```") == std::string::npos) {
        co_await event.co_reply(result(event, std::format("{}\n```diff\n{}```", heading, diff)));
    } else {
        dpp::message message(std::format("{} (attached, it is too long to show here)", heading));
        message.add_file(std::format("{}-v{}-v{}.diff", llm::to_string(kind), from, to), diff, "text/plain");
        co_await event.co_reply(result(event, std::move(message)));
    }
}

auto llm_command::revert_document(const dpp::slashcommand_t& event, llm::document_kind kind) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const int number = static_cast<int>(int_option(event, "version").value_or(0));
    const auto saved = services_.documents->revert(guild, kind, number, event.command.get_issuing_user().id, seconds_now(*services_.clock));
    if (!saved) {
        co_await event.co_reply(refusal(event, std::format("there's no version {} of the {}", number, label_of(kind))));
        co_return;
    }
    // Edits go to the log, not to the channel (plan §14.5).
    util::log().info("the {} in guild {} reverted to version {} by {}, as version {}", label_of(kind), guild, number,
                     describe_user(event.command.get_issuing_user()), *saved);
    co_await event.co_reply(result(event, describe_saved(kind, *saved, services_.documents->text(guild, kind))));
}

auto llm_command::edit_from_file(const dpp::slashcommand_t& event, llm::document_kind kind, dpp::snowflake attachment) -> dpp::task<void> {
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
    if (!downloaded.ok() || downloaded.value().status != 200) {
        util::log().warn("could not download {} for the {} in guild {}: {}", file->filename, label_of(kind), guild,
                         downloaded.ok() ? std::format("status {}", downloaded.value().status) : downloaded.error().message);
        co_await answer_deferred(event, refusal(event, "i couldn't download that file; try again"));
        co_return;
    }

    std::string text(util::trim(downloaded.value().body));
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    if (util::character_count(text) > llm::document_length_limit) {
        co_await answer_deferred(event, refusal(event, std::format("that's over the {}-character limit", llm::document_length_limit)));
        co_return;
    }

    const int version = services_.documents->save(guild, kind, text, event.command.get_issuing_user().id, seconds_now(*services_.clock),
                                                  std::format("from {}", file->filename));
    util::log().info("the {} in guild {} replaced from {} by {}: version {}, {} characters", label_of(kind), guild, file->filename,
                     describe_user(event.command.get_issuing_user()), version, util::character_count(text));
    co_await answer_deferred(event, result(event, describe_saved(kind, version, text)));
}

auto llm_command::editors(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto role = snowflake_option(event, "role");
    if (!role) {
        const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.bootstrap);
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
        std::string text = all.empty() ? std::string("No advanced triggers here yet. Add one with `/llm trigger add`.")
                                       : std::string("**Advanced triggers**\n");
        for (const llm::advanced_trigger& entry : all) {
            text += describe_trigger(entry) + "\n";
        }
        co_await event.co_reply(result(event, util::truncate(text, 2000)));
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
    if (const auto mode = events::match_mode_from_string(string_option(event, "mode"))) entry.mode = *mode;
    entry.enabled = bool_option(event, "enabled").value_or(entry.enabled);

    if (entry.pattern.empty() || entry.context_prompt.empty()) {
        co_await event.co_reply(refusal(event, "a trigger needs a pattern and a prompt"));
        co_return;
    }

    if (action == "add") {
        entry.id = services_.triggers->add(entry);
    } else if (!services_.triggers->update(entry)) {
        co_await event.co_reply(refusal(event, std::format("no advanced trigger `{}` here", entry.id)));
        co_return;
    }
    util::log().info("advanced trigger {} {} in guild {} by {}: {}", entry.id, action == "add" ? "added" : "updated", guild, who,
                     describe_trigger(entry));
    co_await event.co_reply(result(event, std::format("{} {}", action == "add" ? "added" : "updated", describe_trigger(entry))));
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
        const auto shown = services_.memories->list(guild, subject, 0, memories_per_page);
        co_await event.co_reply(result(event, render_memories(shown, services_.memories->count(guild, subject), 0, subject)));
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
    return render_llm_settings(values(guild), services_.settings->get_bool(guild, llm::enabled_key, false));
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
        const auto shown = services_.memories->list(guild, subject, static_cast<std::size_t>(page) * memories_per_page, memories_per_page);
        ui::update_panel(event, render_memories(shown, total, page, subject));
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
        const bool on = state.argument == "on";
        services_.settings->set_bool(guild, llm::enabled_key, on);
        util::log().info("the language model turned {} in guild {} by {} from the panel", on ? "on" : "off", guild,
                         describe_user(event.command.get_issuing_user()));
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

    if (state.view == llm_document_form_view) {
        const auto kind = llm::document_kind_from_string(state.argument);
        if (!kind) return false;

        // Checked again: the form could have been opened before a role was
        // taken away.
        const bool admin = manages_server(event);
        const llm::llm_settings settings = llm::load_llm_settings(*services_.settings, guild, *services_.bootstrap);
        const bool allowed = *kind == llm::document_kind::personality
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
                util::log().warn("the {} form from {} in guild {} came back without part {}; nothing saved", label_of(*kind), who, guild,
                                 index);
                ui::answer_privately(event, "that form came back incomplete, so nothing was saved; try again");
                return true;
            }
            parts.push_back(found->second);
        }
        const std::string text = join_document_parts(parts);
        if (text == services_.documents->text(guild, *kind)) {
            ui::answer_privately(event, "nothing changed, so nothing was saved");
            return true;
        }

        const int version =
            services_.documents->save(guild, *kind, text, event.command.get_issuing_user().id, seconds_now(*services_.clock));
        // Edits go to the log, not to the channel (plan §14.5).
        util::log().info("the {} in guild {} edited by {}: version {}, {} characters", label_of(*kind), guild, who, version,
                         util::character_count(text));
        ui::answer_privately(event, describe_saved(*kind, version, text));
        return true;
    }

    return false;
}

} // namespace latibot::commands
