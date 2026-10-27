#pragma once

#include <string>

namespace latibot::capabilities {

/// The bot's status, the one it shows in every server, as `/status` sets it
/// and keeps it across restarts (src/core/docs/Basic_Commands.md §2).
/// Offered by the core, for the language model's `set_status` tool.
class bot_status {
public:
    virtual ~bot_status() = default;

    bot_status() = default;
    bot_status(const bot_status&) = delete;
    auto operator=(const bot_status&) -> bot_status& = delete;

    /// `type` as `/status` spells it: playing, watching, listening,
    /// competing or custom.
    virtual auto set(std::string type, std::string text) -> void = 0;
};

} // namespace latibot::capabilities
