#include "core/llm/models.hpp"

#include "core/util/text.hpp"

#include <algorithm>
#include <array>

namespace latibot::llm {
namespace {

// Checked against the providers' pricing pages in September 2026. None of
// these take `temperature`: every current Claude model answers a non-default
// one with a 400 (plan §14.1), so no request sends it. OpenAI's models take
// a reasoning effort instead of Claude's, set by the OpenAI provider.
constexpr std::array<model_info, 8> models{{
    {.id = "claude-haiku-4-5",
     .label = "Claude Haiku 4.5",
     .provider = provider_kind::anthropic,
     .input_price = 1.0,
     .output_price = 5.0,
     .cache_write_price = 1.25,
     .cache_read_price = 0.10,
     .takes_effort = false},
    {.id = "claude-sonnet-5",
     .label = "Claude Sonnet 5",
     .provider = provider_kind::anthropic,
     .input_price = 2.0,
     .output_price = 10.0,
     .cache_write_price = 2.50,
     .cache_read_price = 0.20,
     .takes_effort = true},
    {.id = "claude-sonnet-5-5",
     .label = "Claude Sonnet 5.5",
     .provider = provider_kind::anthropic,
     .input_price = 2.0,
     .output_price = 10.0,
     .cache_write_price = 2.50,
     .cache_read_price = 0.20,
     .takes_effort = true},
    {.id = "claude-opus-5-5",
     .label = "Claude Opus 5.5",
     .provider = provider_kind::anthropic,
     .input_price = 4.0,
     .output_price = 20.0,
     .cache_write_price = 5.0,
     .cache_read_price = 0.20,
     .takes_effort = true},
    {.id = "claude-opus-5",
     .label = "Claude Opus 5",
     .provider = provider_kind::anthropic,
     .input_price = 5.0,
     .output_price = 25.0,
     .cache_write_price = 6.25,
     .cache_read_price = 0.50,
     .takes_effort = true},
    {.id = "claude-fable-5-1",
     .label = "Claude Fable 5.1",
     .provider = provider_kind::anthropic,
     .input_price = 10.0,
     .output_price = 50.0,
     .cache_write_price = 12.50,
     .cache_read_price = 0.25,
     .takes_effort = true},
    // OpenAI caches on its own and reports only what it read, so a write is
    // priced as ordinary input.
    {.id = "gpt-6-luna",
     .label = "GPT-6 Luna",
     .provider = provider_kind::openai,
     .input_price = 0.10,
     .output_price = 0.50,
     .cache_write_price = 0.10,
     .cache_read_price = 0.01,
     .takes_effort = false},
    {.id = "gpt-6-sol",
     .label = "GPT-6 Sol",
     .provider = provider_kind::openai,
     .input_price = 2.0,
     .output_price = 10.0,
     .cache_write_price = 2.0,
     .cache_read_price = 0.20,
     .takes_effort = false},
}};

constexpr double tokens_per_price_unit = 1'000'000.0;

} // namespace

auto to_string(provider_kind kind) noexcept -> std::string_view {
    return kind == provider_kind::openai ? "openai" : "anthropic";
}

auto provider_from_string(std::string_view name) -> std::optional<provider_kind> {
    const std::string key = util::to_lower(util::trim(name));
    if (key == "anthropic") return provider_kind::anthropic;
    if (key == "openai") return provider_kind::openai;
    return std::nullopt;
}

auto known_models() noexcept -> std::span<const model_info> {
    return models;
}

auto find_model(std::string_view id) noexcept -> const model_info* {
    const auto found = std::ranges::find(models, id, &model_info::id);
    return found == models.end() ? nullptr : &*found;
}

auto cost_usd(const model_info& model, const usage& used) noexcept -> double {
    const auto priced = [](std::int64_t tokens, double price) { return static_cast<double>(tokens) * price / tokens_per_price_unit; };
    return priced(used.input_tokens, model.input_price) + priced(used.output_tokens, model.output_price) +
           priced(used.cache_write_tokens, model.cache_write_price) + priced(used.cache_read_tokens, model.cache_read_price);
}

auto to_string(stop_reason reason) noexcept -> std::string_view {
    switch (reason) {
    case stop_reason::finished:
        return "finished";
    case stop_reason::tool_use:
        return "tool use";
    case stop_reason::max_tokens:
        return "out of tokens";
    case stop_reason::refusal:
        return "refused";
    case stop_reason::other:
        break;
    }
    return "other";
}

} // namespace latibot::llm
