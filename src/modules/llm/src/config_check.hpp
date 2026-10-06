#pragma once

namespace latibot::config {
struct bootstrap;
}

namespace latibot::llm {

/// Refuses `config.json`'s language model keys when the bot could not keep
/// its spending in check with them: a provider it does not know, a model it
/// has no price for, or a model from the other provider
/// (docs/features/Language_Model.md §3.2). Throws `config::config_error`,
/// which stops startup with the reason.
///
/// The LLM's own, rather than the config reader's, so the core reads
/// `config.json` without knowing the models
/// (docs/modules/Module_Plan_Final.md §2.2, K2).
auto check_config(const config::bootstrap& config) -> void;

} // namespace latibot::llm
