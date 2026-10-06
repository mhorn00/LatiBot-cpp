#pragma once

#include "core/ports/command_host.hpp"

#include <dpp/coro/task.h>

#include <cstddef>
#include <string>
#include <vector>

namespace latibot::commands {

/// What `unregister_all` did.
struct unregister_report {
    std::size_t global_deleted = 0;

    /// Across every server, and how many servers had any.
    std::size_t guild_deleted = 0;
    std::size_t guilds_with_commands = 0;
    std::size_t guilds_checked = 0;

    /// One line for each call Discord refused.
    std::vector<std::string> failures;

    [[nodiscard]] auto ok() const -> bool { return failures.empty(); }
};

/// Deletes every slash command the bot has registered: the global ones, then
/// each server's own. The bot registers only global ones, but a server's own
/// may be left from an older build, and they show as duplicates the same way.
///
/// A set with nothing in it is not deleted, so a second run makes no changes.
/// A failure is recorded and the rest carry on, so one server that refuses
/// does not leave the others as they were.
[[nodiscard]] auto unregister_all(ports::command_host& host) -> dpp::task<unregister_report>;

} // namespace latibot::commands
