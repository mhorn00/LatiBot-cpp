#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <string>
#include <vector>

namespace latibot::ports {

/// Where Discord keeps the bot's slash commands: once for every server, and
/// separately in each server that was given its own.
///
/// Only `--unregister-commands` uses it. The bot itself registers through
/// DPP's cluster directly, once it is connected.
class command_host {
public:
    virtual ~command_host() = default;

    command_host() = default;
    command_host(const command_host&) = delete;
    auto operator=(const command_host&) -> command_host& = delete;

    /// The names of the commands registered for every server.
    virtual auto global_commands() -> dpp::task<result<std::vector<std::string>>> = 0;
    virtual auto delete_global_commands() -> dpp::task<result<void>> = 0;

    /// The servers the bot is in.
    virtual auto guilds() -> dpp::task<result<std::vector<dpp::snowflake>>> = 0;

    /// The names of the commands registered in one server only.
    virtual auto guild_commands(dpp::snowflake guild_id) -> dpp::task<result<std::vector<std::string>>> = 0;
    virtual auto delete_guild_commands(dpp::snowflake guild_id) -> dpp::task<result<void>> = 0;
};

} // namespace latibot::ports
