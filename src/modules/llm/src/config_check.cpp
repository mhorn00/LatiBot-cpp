#include "config_check.hpp"

#include "core/config/config_error.hpp"
#include "models.hpp"

#include <format>
#include <string>

namespace latibot::llm {

auto check_config(const llm_config& section) -> void {
    const auto provider = provider_from_string(section.provider);
    if (!provider) {
        throw config::config_error(R"(config key "llm.provider" has ")" + section.provider + R"(", expected: anthropic, openai)");
    }

    // Only models with a known price, since the spend caps are worked out
    // from it: a model the bot cannot price would spend without being counted.
    const model_info* model = find_model(section.model);
    if (model == nullptr) {
        std::string known;
        for (const model_info& candidate : known_models()) {
            if (!known.empty()) known += ", ";
            known += candidate.id;
        }
        throw config::config_error(std::format(
            R"(config key "llm.model" has "{}", which is not a model the bot knows the price of. Known: {})", section.model, known));
    }
    if (model->provider != *provider) {
        throw config::config_error(std::format(R"(config key "llm.model" names a model from {}, but "llm.provider" is {})",
                                               to_string(model->provider), to_string(*provider)));
    }
}

} // namespace latibot::llm
