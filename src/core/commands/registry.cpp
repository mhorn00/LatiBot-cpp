#include "core/commands/registry.hpp"

#include "core/util/log.hpp"

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
        co_return;
    }

    const auto started = std::chrono::steady_clock::now();
    try {
        co_await target->execute(event);
    } catch (const std::exception& error) {
        util::log().error("{} threw: {}", what, error.what());
        co_return;
    } catch (...) {
        util::log().error("{} threw an unknown exception", what);
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
