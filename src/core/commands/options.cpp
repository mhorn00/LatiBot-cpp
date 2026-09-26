#include "core/commands/options.hpp"

#include <variant>

namespace latibot::commands {
namespace {

/// The option as `T`, or nothing when it is missing or of another type.
template <typename T>
std::optional<T> option_as(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* held = std::get_if<T>(&value);
    return held == nullptr ? std::nullopt : std::optional<T>(*held);
}

} // namespace

std::string string_option(const dpp::slashcommand_t& event, const char* name) {
    return option_as<std::string>(event, name).value_or(std::string{});
}

std::optional<std::int64_t> int_option(const dpp::slashcommand_t& event, const char* name) {
    return option_as<std::int64_t>(event, name);
}

std::optional<bool> bool_option(const dpp::slashcommand_t& event, const char* name) {
    return option_as<bool>(event, name);
}

std::optional<dpp::snowflake> snowflake_option(const dpp::slashcommand_t& event, const char* name) {
    return option_as<dpp::snowflake>(event, name);
}

dpp::permission invoker_permissions(const dpp::slashcommand_t& event) {
    const auto& resolved = event.command.resolved.member_permissions;
    const auto found = resolved.find(event.command.get_issuing_user().id);
    return found == resolved.end() ? dpp::permission{} : found->second;
}

} // namespace latibot::commands
