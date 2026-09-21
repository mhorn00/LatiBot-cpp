#include "core/commands/registry.hpp"

#include "core/util/log.hpp"

#include <exception>
#include <utility>

namespace latibot::commands {

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
    command* target = find(name);
    if (target == nullptr) {
        util::log().warn("no command registered for \"{}\"", name);
        co_return;
    }

    try {
        co_await target->execute(event);
    } catch (const std::exception& error) {
        util::log().error("command \"{}\" threw: {}", name, error.what());
    } catch (...) {
        util::log().error("command \"{}\" threw an unknown exception", name);
    }
}

} // namespace latibot::commands
