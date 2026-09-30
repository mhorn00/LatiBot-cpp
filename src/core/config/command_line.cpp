#include "core/config/command_line.hpp"

#include "core/config/bootstrap.hpp"

#include <format>

namespace latibot::config {

auto command_line::parse(std::span<const std::string_view> arguments) -> command_line {
    command_line parsed;
    bool has_path = false;

    for (const std::string_view argument : arguments) {
        if (argument == "--unregister-commands") {
            parsed.unregister_commands = true;
        } else if (argument.starts_with("-")) {
            throw config_error(std::format(R"(unknown option "{}"; {})", argument, command_line_usage));
        } else if (has_path) {
            throw config_error(std::format(R"(more than one config file given ("{}" and "{}"); {})", parsed.config_path.generic_string(),
                                           argument, command_line_usage));
        } else {
            parsed.config_path = argument;
            has_path = true;
        }
    }
    return parsed;
}

} // namespace latibot::config
