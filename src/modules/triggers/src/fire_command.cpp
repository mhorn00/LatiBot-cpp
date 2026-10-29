#include "fire_command.hpp"

#include "core/ui/interaction.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <iterator>
#include <utility>

namespace latibot::commands {
namespace {

/// Discord's limits on a select option's label and description.
constexpr std::size_t option_label_limit = 100;

/// What the menu calls a trigger: its pattern, or what sets it off.
auto menu_label(const events::trigger& entry) -> std::string {
    std::string label = std::format("{}: ", entry.id);
    switch (entry.condition) {
    case events::trigger_condition::text:
        label += entry.pattern;
        break;
    case events::trigger_condition::addressed:
        label += "LatiBot addressed";
        break;
    case events::trigger_condition::reply:
        label += entry.persona_id == 0 ? "a reply to LatiBot" : "a reply to a persona";
        break;
    }
    return util::truncate(label, option_label_limit);
}

auto menu_kind(const events::trigger& entry) -> std::string {
    switch (entry.kind()) {
    case events::trigger_kind::analysis:
        return "the model answers, without the check";
    case events::trigger_kind::model:
        return "the model answers";
    case events::trigger_kind::responses:
        break;
    }
    return std::format("{} response{}", entry.responses.size(), entry.responses.size() == 1 ? "" : "s");
}

auto read_id(std::string_view text) -> std::uint64_t {
    std::uint64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && stop == text.data() + text.size() ? value : 0;
}

} // namespace

auto as_incoming(const dpp::message& message, dpp::snowflake guild, dpp::snowflake me) -> events::incoming_message {
    events::incoming_message incoming;
    incoming.guild_id = guild;
    incoming.channel_id = message.channel_id;
    incoming.message_id = message.id;
    incoming.author_id = message.author.id;
    incoming.from_self = message.author.id == me;
    incoming.from_bot = message.author.is_bot();
    incoming.author_is_allowed_bot = incoming.from_bot && !incoming.from_self && message.webhook_id.empty();
    incoming.webhook_id = message.webhook_id;
    incoming.content = message.content;
    incoming.author_name = message.author.global_name.empty() ? message.author.username : message.author.global_name;
    incoming.mentions_bot = std::ranges::any_of(message.mentions, [me](const auto& mentioned) { return mentioned.first.id == me; });
    if (message.type == dpp::mt_reply) incoming.reply_to = message.message_reference.message_id;
    return incoming;
}

auto render_fire_menu(std::span<const events::trigger> triggers, int page, std::uint64_t request) -> dpp::message {
    if (triggers.empty()) return dpp::message("There are no triggers here to fire; add one with `/trigger add`.");

    const int current = ui::clamp_page(page, triggers.size(), fire_menu_size);
    const ui::page_range window = ui::range_for(current, triggers.size(), fire_menu_size);
    const std::string argument = std::to_string(request);

    dpp::message reply(
        std::format("Which trigger should answer that message? It answers whatever its pattern, chance, check or "
                    "cooldown say.\n_{}_",
                    ui::page_label(current, triggers.size(), fire_menu_size)));
    const auto id = ui::encode({.view = std::string(trigger_fire_view), .page = current, .argument = argument});
    if (!id) return dpp::message("That menu would not fit; try again.");

    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu).set_placeholder("Pick a trigger").set_id(*id);
    for (std::size_t index = window.begin; index < window.end; ++index) {
        const events::trigger& entry = triggers[index];
        menu.add_select_option(dpp::select_option(menu_label(entry), std::to_string(entry.id), menu_kind(entry)));
    }
    dpp::component row;
    row.set_type(dpp::cot_action_row).add_component(menu);
    reply.add_component(row);

    if (const auto paging = ui::controls({.view = std::string(trigger_fire_view), .page = current, .argument = argument}, triggers.size(),
                                         fire_menu_size)) {
        reply.add_component(*paging);
    }
    return reply;
}

fire_command::fire_command(events::trigger_store& store, events::trigger_responder& responder, const capabilities::model_replies* model,
                           std::function<dpp::snowflake()> me, carry_out act)
    : info_{.name = std::string(fire_command_name),
            .description = "Fire a trigger on this message.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_messages),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      store_(&store),
      responder_(&responder),
      model_(model),
      me_(std::move(me)),
      act_(std::move(act)) {}

auto fire_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    // A message's right-click menu takes no options and has no description.
    // DPP lowercases a slash command's name, which a menu's keeps as typed,
    // so the name is set again once the type says it is a menu.
    dpp::slashcommand payload = command::build(name, application_id);
    payload.set_type(dpp::ctxm_message);
    payload.set_name(name);
    return payload;
}

auto fire_command::fireable(dpp::snowflake guild) const -> std::vector<events::trigger> {
    std::vector<events::trigger> all = store_->for_guild(guild);
    std::erase_if(all, [](const events::trigger& entry) { return !entry.enabled; });
    return all;
}

auto fire_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto& resolved = event.command.resolved.messages;
    if (resolved.empty()) {
        co_await event.co_reply(refusal(event, "i couldn't see which message that was"));
        co_return;
    }
    events::ensure_builtins(*store_, model_, event.command.guild_id);

    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(mutex_);
        // Requests nobody picked for are forgotten once Discord would refuse
        // the pick anyway.
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(pending_, [now](const auto& each) { return now - each.second.made > request_lifetime; });
        id = next_++;
        pending_.emplace(id, request{.message = as_incoming(resolved.begin()->second, event.command.guild_id, me_()), .made = now});
    }
    co_await event.co_reply(result(event, render_fire_menu(fireable(event.command.guild_id), 0, id)));
}

auto fire_command::on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
    if (state.view != trigger_fire_view) return false;
    const dpp::snowflake guild = event.command.guild_id;
    const std::uint64_t id = read_id(state.argument);

    std::optional<events::incoming_message> target;
    {
        const std::scoped_lock lock(mutex_);
        if (const auto found = pending_.find(id); found != pending_.end()) target = found->second.message;
    }
    if (!target) {
        ui::update_panel(event, dpp::message("That request has expired; right-click the message and pick Fire a trigger again."));
        return true;
    }

    // ◀ / ▶ carry no choice.
    if (chosen.empty()) {
        ui::update_panel(event, render_fire_menu(fireable(guild), state.page, id));
        return true;
    }

    const auto entry = store_->find(static_cast<std::int64_t>(read_id(chosen)), guild);
    if (!entry) {
        ui::update_panel(event, dpp::message("That trigger is gone."));
        return true;
    }

    events::stage_result fired;
    responder_->fire(*entry, *target, fired, /*forced=*/true);
    util::log().info("trigger {} fired by hand in guild {} by {} on message {}: {}", entry->id, guild,
                     describe_user(event.command.get_issuing_user()), target->message_id,
                     fired.actions.empty() ? "nothing came of it" : "answering");
    const bool answered = !fired.actions.empty();
    if (answered) act_(std::move(fired.actions));
    {
        const std::scoped_lock lock(mutex_);
        pending_.erase(id);
    }
    ui::update_panel(event, dpp::message(answered ? std::format("Fired trigger `{}` on that message.", entry->id)
                                                  : std::format("Trigger `{}` had nothing to say there; the log says why.", entry->id)));
    return true;
}

} // namespace latibot::commands
