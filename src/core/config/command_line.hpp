#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace latibot::config {

/// What `LatiBot.exe` was started with.
struct command_line {
    std::filesystem::path config_path = "config.json";

    /// `--unregister-commands`: delete every slash command this bot has
    /// registered with Discord, then exit instead of running. For a test
    /// instance sharing a server with the real one, whose commands would
    /// otherwise sit beside the real ones as duplicates.
    bool unregister_commands = false;

    /// Parses the arguments after the program's own name: at most one
    /// config path, and the flags above in any order. Throws `config_error`
    /// for anything else, so a mistyped flag is not read as a config path.
    [[nodiscard]] static auto parse(std::span<const std::string_view> arguments) -> command_line;
};

/// One line saying how to start the bot, for the error a bad argument gets.
inline constexpr std::string_view command_line_usage = "usage: LatiBot [config.json] [--unregister-commands]";

} // namespace latibot::config
