#pragma once

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::testing {

/// An option as Discord delivers it inside a command interaction.
inline auto option(std::string name, dpp::command_value value, dpp::command_option_type type) -> dpp::command_data_option {
    dpp::command_data_option made;
    made.name = std::move(name);
    made.type = type;
    made.value = std::move(value);
    return made;
}

inline auto bool_option(std::string name, bool value) -> dpp::command_data_option {
    return option(std::move(name), value, dpp::co_boolean);
}

/// A slash command event for code that reads which subcommand ran and what
/// options it was given, which is everything short of a cluster to reply to.
///
/// `path` is empty, a subcommand ("test") or a group and a subcommand
/// ("alias add"); `options` go on the innermost level, where Discord puts
/// them.
inline auto slash_event(std::string_view command, std::string_view path, std::vector<dpp::command_data_option> options = {})
    -> dpp::slashcommand_t {
    dpp::command_interaction interaction;
    interaction.name = std::string(command);

    std::vector<std::string> names;
    for (std::size_t at = 0; at < path.size();) {
        const std::size_t space = std::min(path.find(' ', at), path.size());
        names.emplace_back(path.substr(at, space - at));
        at = space + 1;
    }

    if (names.empty()) {
        interaction.options = std::move(options);
    } else {
        dpp::command_data_option innermost = option(names.back(), {}, dpp::co_sub_command);
        innermost.options = std::move(options);
        if (names.size() == 1) {
            interaction.options.push_back(std::move(innermost));
        } else {
            dpp::command_data_option group = option(names.front(), {}, dpp::co_sub_command_group);
            group.options.push_back(std::move(innermost));
            interaction.options.push_back(std::move(group));
        }
    }

    dpp::slashcommand_t event;
    event.command.data = std::move(interaction);
    return event;
}

} // namespace latibot::testing
