#pragma once

#include "core/config/section.hpp"

#include <cstdint>

namespace latibot::linkstats {

/// The "linkstats" section of config.json (docs/features/Link_Stats.md §10).
struct linkstats_config {
    /// How many reactions an emote needs before the bot keeps its own copy
    /// of it, as an application emoji. 0 turns copying off. Raising it
    /// deletes the copies that no longer qualify, on the next rounds.
    std::int64_t emoji_copy_min_uses = 1;
};

/// Its keys (docs/modules/Module_Plan_Final.md §8.2).
[[nodiscard]] inline auto linkstats_section() -> const config::section<linkstats_config>& {
    static const config::section<linkstats_config> table{
        "linkstats",
        {
            config::key("emoji_copy_min_uses", &linkstats_config::emoji_copy_min_uses,
                        "Reactions an emote needs before the bot keeps its own copy of it; 0 turns copying off.", config::at_least(0)),
        }};
    return table;
}

} // namespace latibot::linkstats
