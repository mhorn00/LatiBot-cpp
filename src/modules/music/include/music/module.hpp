#pragma once

#include "core/modules/module.hpp"

#include <dpp/json.h>

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::music {

/// The music module (README.md, src/modules/music/docs/Music.md): `/music`, yt-dlp,
/// ffmpeg and the PO token provider. Requires voice.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

/// Its config.json section at its defaults.
[[nodiscard]] auto config_defaults() -> nlohmann::ordered_json;

} // namespace latibot::music
