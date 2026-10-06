#pragma once

#include "core/db/schema_versions.hpp"

#include <optional>
#include <string>

namespace latibot::llm {

/// The providers' API keys, from ANTHROPIC_API_KEY and OPENAI_API_KEY. Never
/// from config.json, which could be committed (docs/modules/Module_Plan_Final.md
/// §8.3). A provider exists only when its key is set.
struct provider_keys {
    std::optional<std::string> anthropic;
    std::optional<std::string> openai;
};

/// Reads them; an empty variable is no key.
[[nodiscard]] auto keys_from_environment() -> provider_keys;

/// The module's tables, version 1 first (docs/modules/Module_Plan_Final.md
/// §7.1). Defined in module.cpp.
[[nodiscard]] auto llm_schema() noexcept -> db::module_schema;

} // namespace latibot::llm
