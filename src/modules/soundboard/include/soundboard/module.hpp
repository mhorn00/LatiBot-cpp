#pragma once

#include "core/modules/module.hpp"

#include <dpp/json.h>

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::soundboard {

/// The soundboard module (README.md, src/modules/soundboard/docs/Soundboard.md):
/// `/soundboard`, which sends a soundboard sound as a voice message.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

/// Its config.json section at its defaults.
[[nodiscard]] auto config_defaults() -> nlohmann::ordered_json;

} // namespace latibot::soundboard
