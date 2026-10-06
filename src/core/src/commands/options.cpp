#include "core/commands/options.hpp"

#include <variant>

namespace latibot::commands {
namespace {

/// The option as `T`, or nothing when it is missing or of another type.
template <typename T>
auto option_as(const dpp::slashcommand_t& event, const char* name) -> std::optional<T> {
    const dpp::command_value value = event.get_parameter(name);
    const auto* held = std::get_if<T>(&value);
    return held == nullptr ? std::nullopt : std::optional<T>(*held);
}

} // namespace

auto string_option(const dpp::slashcommand_t& event, const char* name) -> std::string {
    return option_as<std::string>(event, name).value_or(std::string{});
}

auto int_option(const dpp::slashcommand_t& event, const char* name) -> std::optional<std::int64_t> {
    return option_as<std::int64_t>(event, name);
}

auto bool_option(const dpp::slashcommand_t& event, const char* name) -> std::optional<bool> {
    return option_as<bool>(event, name);
}

auto snowflake_option(const dpp::slashcommand_t& event, const char* name) -> std::optional<dpp::snowflake> {
    return option_as<dpp::snowflake>(event, name);
}

auto invoker_permissions(const dpp::interaction_create_t& event) -> dpp::permission {
    const auto& resolved = event.command.resolved.member_permissions;
    const auto found = resolved.find(event.command.get_issuing_user().id);
    return found == resolved.end() ? dpp::permission{} : found->second;
}

} // namespace latibot::commands
