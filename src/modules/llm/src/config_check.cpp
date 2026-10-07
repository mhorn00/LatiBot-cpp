#include "config_check.hpp"

#include "core/config/config_error.hpp"
#include "models.hpp"

#include <format>
#include <string>
#include <string_view>
#include <tuple>

namespace latibot::llm {

namespace {

/// Only models with a known price, since the spend caps are worked out from
/// it: a model the bot cannot price would spend without being counted.
auto priced(std::string_view key, const std::string& id) -> const model_info& {
    const model_info* model = find_model(id);
    if (model != nullptr) return *model;

    std::string known;
    for (const model_info& candidate : known_models()) {
        if (!known.empty()) known += ", ";
        known += candidate.id;
    }
    throw config::config_error(
        std::format(R"(config key "llm.{}" has "{}", which is not a model the bot knows the price of. Known: {})", key, id, known));
}

} // namespace

auto check_config(const llm_config& section) -> void {
    const auto provider = provider_from_string(section.provider);
    if (!provider) {
        throw config::config_error(R"(config key "llm.provider" has ")" + section.provider + R"(", expected: anthropic, openai)");
    }

    const model_info* model = &priced("model", section.model);
    // Its provider is whichever has the model; it needs no key of its own
    // until a guild turns conversation mode on.
    std::ignore = priced("check_model", section.check_model);
    if (model->provider != *provider) {
        throw config::config_error(std::format(R"(config key "llm.model" names a model from {}, but "llm.provider" is {})",
                                               to_string(model->provider), to_string(*provider)));
    }
}

} // namespace latibot::llm
