#pragma once

#include "core/modules/module.hpp"

#include <dpp/json.h>

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::linkstats {

/// The linkstats module (README.md, docs/features/Link_Stats.md):
/// `/linkstats`, and counting the reactions on replacements and image posts.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

/// Its config.json section at its defaults.
[[nodiscard]] auto config_defaults() -> nlohmann::ordered_json;

} // namespace latibot::linkstats
