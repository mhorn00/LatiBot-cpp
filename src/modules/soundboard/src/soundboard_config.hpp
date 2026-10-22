#pragma once

#include "core/config/section.hpp"

#include <filesystem>

namespace latibot::soundboard {

/// The "soundboard" section of config.json (src/modules/soundboard/docs/Soundboard.md §5).
struct soundboard_config {
    /// Empty: `ffmpeg` (`.exe` on Windows) beside the bot, then on PATH.
    /// Music has a key of its own, since either module can be left out.
    std::filesystem::path ffmpeg_path;
};

/// Its keys (docs/modules/Module_Plan_Final.md §8.2).
[[nodiscard]] inline auto soundboard_section() -> const config::section<soundboard_config>& {
    static const config::section<soundboard_config> table{
        "soundboard",
        {
            config::key("ffmpeg_path", &soundboard_config::ffmpeg_path, "Where ffmpeg is. Empty: beside the bot, then PATH."),
        }};
    return table;
}

} // namespace latibot::soundboard
