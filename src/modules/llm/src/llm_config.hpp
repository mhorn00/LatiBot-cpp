#pragma once

#include "core/config/section.hpp"

#include <string>

namespace latibot::llm {

/// The "llm" section of config.json: which model answers, and what it is
/// allowed to cost (docs/features/Language_Model.md §3.2). Which providers
/// and models are allowed is `check_config`'s to say.
struct llm_config {
    std::string provider{"anthropic"};
    std::string model{"claude-haiku-4-5"};
    double spend_cap_daily_usd = 2.0;
    double spend_cap_monthly_usd = 20.0;
    int tool_rounds = 4;
};

/// Its keys (docs/modules/Module_Plan_Final.md §8.2).
[[nodiscard]] inline auto llm_section() -> const config::section<llm_config>& {
    static const config::section<llm_config> table{
        "llm",
        {
            config::key("provider", &llm_config::provider, "Who answers by default: anthropic or openai."),
            config::key("model", &llm_config::model, "The model that answers by default; a server can choose another."),
            config::key("spend_cap_daily_usd", &llm_config::spend_cap_daily_usd,
                        "The most the model may cost in a day, across every server.", config::at_least(0)),
            config::key("spend_cap_monthly_usd", &llm_config::spend_cap_monthly_usd, "The most it may cost in a month.",
                        config::at_least(0)),
            config::key("tool_rounds", &llm_config::tool_rounds, "How many times one answer may call its tools.", config::at_least(1)),
        }};
    return table;
}

} // namespace latibot::llm
