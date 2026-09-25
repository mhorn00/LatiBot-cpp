#include "core/commands/registry.hpp"

#include "core/util/log.hpp"

#include <dpp/cluster.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <type_traits>
#include <utility>
#include <variant>

namespace latibot::commands {
namespace {

/// How much of one option's text the log keeps. Long enough to recognise what
/// was typed, short enough that a full-length `/say` stays one readable line.
constexpr std::size_t value_limit = 120;

void append_text(std::string& out, const std::string& text) {
    out.push_back('"');

    const bool truncated = text.size() > value_limit;
    for (const char letter : truncated ? std::string_view(text).substr(0, value_limit) : std::string_view(text)) {
        // The logger promises one line per message, and a pasted multi-line
        // response would otherwise break every tool that relies on it.
        switch (letter) {
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '"':
            out += "\\\"";
            break;
        default:
            out.push_back(letter);
        }
    }

    out.push_back('"');
    if (truncated) {
        out += std::format("…({} chars)", text.size());
    }
}

void append_value(std::string& out, const dpp::command_value& value) {
    std::visit(
        [&out](const auto& held) {
            using held_type = std::decay_t<decltype(held)>;

            if constexpr (std::is_same_v<held_type, std::monostate>) {
                out += "<unset>";
            } else if constexpr (std::is_same_v<held_type, std::string>) {
                append_text(out, held);
            } else if constexpr (std::is_same_v<held_type, bool>) {
                out += held ? "true" : "false";
            } else if constexpr (std::is_same_v<held_type, dpp::snowflake>) {
                out += held.str();
            } else {
                out += std::format("{}", held);
            }
        },
        value);
}

// Recursive, but Discord's own schema bounds the depth: a command holds a
// subcommand group, which holds a subcommand, which holds plain options. Three
// levels, enforced at registration by Discord itself.
// NOLINTNEXTLINE(misc-no-recursion)
void append_options(std::string& out, const std::vector<dpp::command_data_option>& options) {
    for (const dpp::command_data_option& option : options) {
        // A subcommand is not an argument, it is part of the command's name,
        // so it reads as "/trigger add pattern=…" rather than "add=…".
        if (option.type == dpp::co_sub_command || option.type == dpp::co_sub_command_group) {
            out.push_back(' ');
            out += option.name;
            append_options(out, option.options);
            continue;
        }

        out.push_back(' ');
        out += option.name;
        out.push_back('=');
        append_value(out, option.value);
    }
}

} // namespace

std::string describe_invocation(const dpp::command_interaction& interaction) {
    std::string line = "/" + interaction.name;
    append_options(line, interaction.options);
    return line;
}

user_label describe_user(const dpp::user& who) {
    return {.name = who.username, .id = who.id};
}

response_flags command_info::responses_for(std::string_view subcommand) const {
    response_flags flags = responses;
    const auto found = subcommand_responses.find(subcommand);
    if (found == subcommand_responses.end()) {
        return flags;
    }

    const response_overrides& differs = found->second;
    flags.result = differs.result.value_or(flags.result);
    flags.refusal = differs.refusal.value_or(flags.refusal);
    flags.post = differs.post.value_or(flags.post);
    return flags;
}

std::string subcommand_path(const dpp::command_interaction& interaction) {
    // At most a group and then a subcommand, which Discord enforces.
    std::string path;
    const std::vector<dpp::command_data_option>* level = &interaction.options;
    while (!level->empty()) {
        const dpp::command_data_option& first = level->front();
        if (first.type != dpp::co_sub_command && first.type != dpp::co_sub_command_group) {
            break;
        }
        if (!path.empty()) {
            path.push_back(' ');
        }
        path += first.name;
        level = &first.options;
    }
    return path;
}

std::vector<std::string> subcommand_paths(const dpp::slashcommand& payload) {
    std::vector<std::string> paths;
    for (const dpp::command_option& option : payload.options) {
        if (option.type == dpp::co_sub_command) {
            paths.push_back(option.name);
        } else if (option.type == dpp::co_sub_command_group) {
            for (const dpp::command_option& inner : option.options) {
                paths.push_back(option.name + " " + inner.name);
            }
        }
    }
    return paths;
}

response_flags command::responses_for(const dpp::slashcommand_t& event) const {
    return info().responses_for(subcommand_path(event.command.get_command_interaction()));
}

dpp::message command::result(const dpp::slashcommand_t& event, dpp::message message) const {
    return discord::apply_flags(message, responses_for(event).result);
}

dpp::message command::result(const dpp::slashcommand_t& event, std::string_view text) const {
    return result(event, dpp::message(text));
}

dpp::message command::refusal(const dpp::slashcommand_t& event, std::string_view text) const {
    dpp::message message(text);
    return discord::apply_flags(message, responses_for(event).refusal);
}

dpp::message command::post(const dpp::slashcommand_t& event, dpp::message message) const {
    // Ephemeral is cleared as well as never applied: only a reply to a
    // command can be ephemeral, so on a channel message it means nothing.
    const auto wanted = static_cast<discord::message_flags>(responses_for(event).post & discord::channel_message_flags);
    return discord::apply_flags(message, wanted);
}

// Recursive for the same reason `append_options` is, and bounded the same way:
// Discord allows a group, a subcommand, then plain options.
// NOLINTNEXTLINE(misc-no-recursion)
const dpp::command_option* focused_option(const std::vector<dpp::command_option>& options) {
    for (const dpp::command_option& option : options) {
        if (option.focused) {
            return &option;
        }
        if (const dpp::command_option* nested = focused_option(option.options); nested != nullptr) {
            return nested;
        }
    }
    return nullptr;
}

void command::autocomplete(const dpp::autocomplete_t& event) const {
    // Most commands have nothing to complete. Saying so at debug beats a
    // silent return when somebody is wondering why a box stays empty.
    util::log().debug("no completions to offer for /{}", event.name);
}

dpp::slashcommand command::build(const std::string& name, dpp::snowflake application_id) const {
    const command_info& details = info();

    dpp::slashcommand payload(name, details.description, application_id);
    payload.set_dm_permission(!details.guild_only);
    if (details.default_member_permissions) {
        payload.set_default_permissions(*details.default_member_permissions);
    }
    return payload;
}

namespace {

void check_flags(const command_info& details, std::string_view where, std::string_view kind, discord::message_flags flags,
                 discord::message_flags allowed) {
    if ((flags & ~allowed) != 0) {
        throw registry_error(std::format("/{}{}: {} flags include {}, which {} messages cannot carry", details.name, where, kind,
                                         discord::describe_flags(static_cast<discord::message_flags>(flags & ~allowed)), kind));
    }
}

void check_all(const command_info& details, std::string_view where, const response_flags& flags) {
    check_flags(details, where, "result", flags.result, discord::reply_flags);
    check_flags(details, where, "refusal", flags.refusal, discord::reply_flags);
    check_flags(details, where, "post", flags.post, discord::channel_message_flags);
}

} // namespace

void registry::check_responses(const command& candidate) {
    // The command's own flags first, which every subcommand starts from.
    const command_info& details = candidate.info();
    check_all(details, "", details.responses);
    if (details.subcommand_responses.empty()) {
        return;
    }

    // Each override has to name a subcommand the command really registers,
    // which only its payload knows, and the flags it ends up with, the
    // command's with the override on top, have to be valid too.
    const std::vector<std::string> paths = subcommand_paths(candidate.build(details.name, dpp::snowflake{}));
    for (const auto& [path, overrides] : details.subcommand_responses) {
        if (std::ranges::find(paths, path) == paths.end()) {
            std::string known;
            for (const std::string& one : paths) {
                known += known.empty() ? one : ", " + one;
            }
            throw registry_error(std::format("/{}: response flags for \"{}\", which is not one of its subcommands ({})", details.name, path,
                                             known.empty() ? "it has none" : known));
        }
        check_all(details, " " + path, details.responses_for(path));
    }
}

void registry::add(std::unique_ptr<command> new_command) {
    if (new_command == nullptr) {
        throw registry_error("cannot register a null command");
    }

    const command_info& details = new_command->info();
    if (details.name.empty()) {
        throw registry_error("cannot register a command with an empty name");
    }

    // Collect every name this command answers to, so a clash leaves the
    // registry untouched rather than half-registered.
    std::vector<std::string> names{details.name};
    names.insert(names.end(), details.aliases.begin(), details.aliases.end());

    for (const std::string& name : names) {
        if (by_name_.contains(name)) {
            throw registry_error("command name or alias \"" + name + "\" is already registered");
        }
    }

    check_responses(*new_command);

    command* stored = commands_.emplace_back(std::move(new_command)).get();
    for (const std::string& name : names) {
        by_name_.emplace(name, stored);
    }
}

command* registry::find(std::string_view name_or_alias) const {
    const auto found = by_name_.find(name_or_alias);
    return found == by_name_.end() ? nullptr : found->second;
}

std::vector<dpp::slashcommand> registry::build_all(dpp::snowflake application_id) const {
    std::vector<dpp::slashcommand> payloads;
    payloads.reserve(by_name_.size());

    for (const auto& [name, owner] : by_name_) {
        payloads.push_back(owner->build(name, application_id));
    }
    return payloads;
}

std::uint64_t registry::required_bot_permissions() const {
    std::uint64_t permissions = 0;
    for (const auto& stored : commands_) {
        permissions |= stored->info().required_bot_permissions;
    }
    return permissions;
}

namespace {

/// Replies, or follows up when the command had already replied before it
/// failed; either way the person who ran it hears something.
dpp::task<void> answer_anyway(const dpp::slashcommand_t& event, dpp::message message) {
    // An event with no cluster behind it cannot be answered. Only tests build
    // those.
    if (event.owner == nullptr) {
        co_return;
    }

    const auto replied = co_await event.co_reply(message);
    if (!replied.is_error()) {
        co_return;
    }

    const auto followed = co_await event.co_follow_up(message);
    if (followed.is_error()) {
        util::log().warn("could not tell {} what went wrong: {}", describe_user(event.command.get_issuing_user()),
                         followed.get_error().message);
    }
}

} // namespace

dpp::task<void> registry::dispatch(std::string name, const dpp::slashcommand_t& event) const {
    // Logged before the command runs, so an invocation that hangs or crashes
    // the process still leaves a record of what was asked.
    const user_label who = describe_user(event.command.get_issuing_user());
    const std::string what = describe_invocation(event.command.get_command_interaction());
    const dpp::snowflake guild = event.command.guild_id;

    // Two calls rather than one with a conditional, so the guild id reaches
    // the logger as an id and is coloured like every other.
    if (guild.empty()) {
        util::log().info("{} ran {} in a DM", who, what);
    } else {
        util::log().info("{} ran {} in guild {}", who, what, guild);
    }

    command* target = find(name);
    if (target == nullptr) {
        // Not an error: Discord can still deliver a command that was removed
        // from the code but not yet from the guild.
        util::log().warn("no command registered for \"{}\"", name);
        dpp::message unknown(unknown_command_reply);
        co_await answer_anyway(event, discord::apply_flags(unknown, response_flags{}.refusal));
        co_return;
    }

    // Noted here and answered below: a coroutine cannot co_await inside a
    // catch block.
    bool failed = false;
    const auto started = std::chrono::steady_clock::now();
    try {
        co_await target->execute(event);
    } catch (const std::exception& error) {
        util::log().error("{} threw: {}", what, error.what());
        failed = true;
    } catch (...) {
        util::log().error("{} threw an unknown exception", what);
        failed = true;
    }

    if (failed) {
        co_await answer_anyway(event, target->refusal(event, command_failed_reply));
        co_return;
    }

    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    util::log().debug("{} finished in {}", what, took);
}

void registry::offer_completions(std::string_view name, const dpp::autocomplete_t& event) const {
    const command* target = find(name);
    if (target == nullptr) {
        util::log().debug("no command registered for \"{}\" to autocomplete", name);
        return;
    }

    target->autocomplete(event);
}

} // namespace latibot::commands
