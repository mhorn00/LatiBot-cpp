#pragma once

#include "core/modules/module.hpp"

#include <dpp/json.h>

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::llm {

/// The language model module (README.md, docs/features/Language_Model.md):
/// `/llm`, `/memory`, and answering when addressed or when an advanced
/// trigger fires.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

/// Its config.json section at its defaults.
[[nodiscard]] auto config_defaults() -> nlohmann::ordered_json;

} // namespace latibot::llm
