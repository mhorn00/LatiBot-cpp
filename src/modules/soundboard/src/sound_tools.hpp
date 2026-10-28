#pragma once

#include "soundboard_command.hpp"

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::commands {

/// `play_sound`, the language model's /soundboard
/// (src/modules/soundboard/docs/Soundboard.md §5): in voice when the bot is
/// in a voice channel here, otherwise as a voice message.
auto add_sound_tools(capabilities::model_tools& tools, soundboard_services services) -> void;

} // namespace latibot::commands
