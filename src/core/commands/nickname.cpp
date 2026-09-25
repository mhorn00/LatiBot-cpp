#include "core/commands/nickname.hpp"

#include "core/ports/clock.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/guild.h>
#include <dpp/permissions.h>
#include <dpp/restresults.h>

#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace latibot::commands {
namespace {

/// The user an option names, and the name to show for them.
struct named_user {
    dpp::snowflake id;
    std::string name;
};

std::optional<named_user> user_option(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* id = std::get_if<dpp::snowflake>(&value);
    if (id == nullptr || id->empty()) {
        return std::nullopt;
    }

    // The interaction carries the user it resolved, so this needs no lookup.
    const auto found = event.command.resolved.users.find(*id);
    return named_user{.id = *id, .name = found == event.command.resolved.users.end() ? id->str() : found->second.username};
}

/// The `nickname` option, where absent means "clear it".
std::optional<std::string> nickname_option(const dpp::slashcommand_t& event) {
    const dpp::command_value value = event.get_parameter("nickname");
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->empty()) {
        return std::nullopt;
    }
    return *text;
}

/// What a failed edit should say. Discord's own message is more useful than
/// anything we could guess, and the two common refusals are worth naming.
std::string explain(const dpp::error_info& error) {
    // 50013 is Missing Permissions, which for a nickname almost always means
    // the target sits above the bot in the role list.
    if (error.code == 50013) {
        return "Discord says no: they are probably above me in the role list, or i am missing Manage Nicknames.";
    }
    return error.human_readable.empty() ? std::string("Discord refused that one, and did not say why.") : error.human_readable;
}

} // namespace

dpp::message render_nickname_history(std::span<const events::nickname_change> history, dpp::snowflake user_id, int page) {
    const int current = ui::clamp_page(page, history.size(), nicknames_per_page);
    const ui::page_range window = ui::range_for(current, history.size(), nicknames_per_page);

    std::string body = std::format("**Nickname history for <@{}>**\n", user_id.str());
    if (history.empty()) {
        body += "\nNothing recorded here yet.";
    } else {
        for (std::size_t index = window.begin; index < window.end; ++index) {
            body += events::describe_change(history[index]);
            body.push_back('\n');
        }
        body += std::format("\n_{}_", ui::page_label(current, history.size(), nicknames_per_page));
    }

    // Public, as `/nicknames` is configured to post it, so the ◀ / ▶ buttons
    // page for everybody who can see the message.
    dpp::message reply(body);

    // Mentions are how a name is shown for somebody who has left. In a public
    // message that would otherwise ping every person named in the history.
    reply.set_allowed_mentions();

    const auto row = ui::controls({.view = std::string(nickname_history_view), .page = current, .argument = user_id.str()}, history.size(),
                                  nicknames_per_page);
    if (row) {
        reply.add_component(*row);
    }
    return reply;
}

// --------------------------------------------------------------------------

nickname_command::nickname_command(events::nickname_store& store, events::pending_nicknames& pending, ports::clock& clock,
                                   dpp::cluster& cluster)
    : info_{.name = "nickname",
            .description = "Change somebody's nickname, and record who did it.",
            .aliases = {},
            .required_bot_permissions = dpp::p_manage_nicknames,
            .default_member_permissions = dpp::permission(dpp::p_manage_nicknames),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      store_(&store),
      pending_(&pending),
      clock_(&clock),
      cluster_(&cluster) {}

dpp::slashcommand nickname_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);

    payload.add_option(dpp::command_option(dpp::co_user, "user", "Whose nickname to change.", true));
    payload.add_option(dpp::command_option(dpp::co_string, "nickname", "The new nickname. Leave it out to clear theirs.", false)
                           .set_max_length(static_cast<std::int64_t>(nickname_length_limit)));
    return payload;
}

dpp::task<void> nickname_command::execute(const dpp::slashcommand_t& event) {
    const auto target = user_option(event, "user");
    if (!target) {
        co_await event.co_reply(refusal(event, "whose nickname?"));
        co_return;
    }

    const dpp::snowflake guild_id = event.command.guild_id;
    const std::optional<std::string> wanted = nickname_option(event);

    // Discord does not let any bot change the server owner's nickname, so
    // saying so beats a refusal that reads like a permissions problem.
    if (const dpp::guild* guild = dpp::find_guild(guild_id); guild != nullptr && guild->owner_id == target->id) {
        co_await event.co_reply(
            refusal(event, std::format("{} owns this server, and Discord will not let me touch the owner's nickname.", target->name)));
        co_return;
    }

    // Recorded before the change is asked for, with the invoker against it:
    // this is the attribution Discord's audit log gets wrong (plan §8.1).
    const events::nickname_change change{.guild_id = guild_id,
                                         .user_id = target->id,
                                         .nickname = wanted,
                                         .changed_at = clock_->now(),
                                         .changed_by = event.command.get_issuing_user().id,
                                         .source = events::nickname_source::command,
                                         .imported_raw = {}};

    const std::int64_t row = store_->record(change);
    pending_->expect(guild_id, target->id, wanted, clock_->now());

    dpp::guild_member member;
    member.guild_id = guild_id;
    member.user_id = target->id;
    member.set_nickname(wanted.value_or(std::string{}));

    // Discord's answer can take longer than the three seconds a first
    // response has, behind DPP's rate limiter, so say an answer is coming.
    co_await defer(event);
    const dpp::confirmation_callback_t outcome = co_await cluster_->co_guild_edit_member(member);
    if (outcome.is_error()) {
        // History should never claim something that did not happen.
        store_->remove(row);
        pending_->forget(guild_id, target->id, wanted);

        const dpp::error_info error = outcome.get_error();
        util::log().warn("could not set {}'s nickname in guild {}: {} ({})", target->id, guild_id, error.message, error.code);
        co_await answer_deferred(event, refusal(event, explain(error)));
        co_return;
    }

    util::log().info("{} set {}'s nickname in guild {} to {}", describe_user(event.command.get_issuing_user()), target->id, guild_id,
                     wanted ? std::format("\"{}\"", *wanted) : "nothing");

    co_await answer_deferred(event, result(event, wanted ? std::format("ok, {} is now **{}**", target->name, *wanted)
                                                         : std::format("ok, cleared {}'s nickname", target->name)));
}

// --------------------------------------------------------------------------

nicknames_command::nicknames_command(events::nickname_store& store)
    : info_{.name = "nicknames",
            .description = "Show every nickname somebody has had here.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = std::nullopt,
            .guild_only = true,
            // Public, unlike almost every other list this bot posts: a nickname
            // history is something a room reads together, and half its point is
            // being shown to the person it is about.
            .responses = {.result = 0, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      store_(&store) {}

dpp::slashcommand nicknames_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);

    payload.add_option(dpp::command_option(dpp::co_user, "user", "Whose history to show.", true));
    return payload;
}

dpp::task<void> nicknames_command::execute(const dpp::slashcommand_t& event) {
    const auto target = user_option(event, "user");
    if (!target) {
        co_await event.co_reply(refusal(event, "whose nicknames?"));
        co_return;
    }

    const std::vector<events::nickname_change> history = store_->history(event.command.guild_id, target->id);

    if (history.size() > nickname_attachment_threshold) {
        // Ten pages of buttons is worse than one file. Public for the same
        // reason the paged version is, so the file can be opened by anybody
        // in the channel rather than only by whoever asked.
        dpp::message reply(std::format("**Nickname history for <@{}>** — {} entries, attached.", target->id.str(), history.size()));
        reply.set_allowed_mentions();
        reply.add_file(std::format("nicknames-{}.txt", target->id.str()),
                       events::render_history_text(history, std::format("{} ({})", target->name, target->id.str())), "text/plain");

        co_await event.co_reply(result(event, std::move(reply)));
        co_return;
    }

    co_await event.co_reply(result(event, render_nickname_history(history, target->id, 0)));
}

} // namespace latibot::commands
