#pragma once

#include "core/modules/module.hpp"

#include <dpp/json.h>

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::nicknames {

/// The nicknames module (README.md, docs/features/Nicknames.md): `/nickname`,
/// `/nicknames`, and the history of every nickname a member has had.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

/// Its config.json section at its defaults.
[[nodiscard]] auto config_defaults() -> nlohmann::ordered_json;

} // namespace latibot::nicknames
