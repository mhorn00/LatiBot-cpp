#pragma once

#include "llm_config.hpp"

namespace latibot::llm {

/// Refuses the llm section when the bot could not keep its spending in check
/// with it: a provider it does not know, a model it has no price for, or a
/// model from the other provider (docs/features/Language_Model.md §3.2).
/// Throws `config::config_error`, which stops startup with the reason.
auto check_config(const llm_config& section) -> void;

} // namespace latibot::llm
