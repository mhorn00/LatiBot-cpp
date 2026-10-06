#pragma once

#include "core/config/section.hpp"

namespace latibot::nicknames {

/// The "nicknames" section of config.json (docs/features/Nicknames.md §3).
struct nicknames_config {
    /// Whether to watch for nickname changes.
    ///
    /// This is the one setting that decides which intents the bot asks for:
    /// nickname changes only arrive with the privileged Server Members
    /// intent, which must also be switched on in the Discord developer
    /// portal. A bot that asks for an intent it was not granted is refused
    /// the gateway entirely, so this is the way to turn the request off.
    bool track_changes = true;
};

/// Its keys (docs/modules/Module_Plan_Final.md §8.2).
[[nodiscard]] inline auto nicknames_section() -> const config::section<nicknames_config>& {
    static const config::section<nicknames_config> table{
        "nicknames",
        {
            config::key("track_changes", &nicknames_config::track_changes,
                        "Records nickname changes as they happen. Needs the Server Members intent in the developer portal."),
        }};
    return table;
}

} // namespace latibot::nicknames
