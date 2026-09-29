#pragma once

#include "core/llm/provider.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace latibot::llm {

enum class provider_kind : std::uint8_t { anthropic, openai };

[[nodiscard]] auto to_string(provider_kind kind) noexcept -> std::string_view;
[[nodiscard]] auto provider_from_string(std::string_view name) -> std::optional<provider_kind>;

/// A model the bot may use, what it costs, and what its requests may carry
/// (plan §14.1).
///
/// A model is only usable when it is in this table: the spend cap is worked
/// out from these prices, and a model without one would spend without being
/// counted. Prices are US dollars per million tokens, from each provider's
/// pricing page when the entry was added.
struct model_info {
    /// Exactly as the API takes it.
    std::string_view id;

    /// For people: "Claude Haiku 4.5".
    std::string_view label;

    provider_kind provider = provider_kind::anthropic;

    double input_price = 0;
    double output_price = 0;
    double cache_write_price = 0;
    double cache_read_price = 0;

    /// Whether requests carry an effort setting. Sent to a model without it,
    /// it is a 400; the thinking models take `low`, which keeps thinking on
    /// but short, rather than switching it off (plan §14.1).
    bool takes_effort = false;
};

/// Every model the bot knows, cheapest first within a provider.
[[nodiscard]] auto known_models() noexcept -> std::span<const model_info>;

/// The model with this id, or null.
[[nodiscard]] auto find_model(std::string_view id) noexcept -> const model_info*;

/// What a call cost, in US dollars.
[[nodiscard]] auto cost_usd(const model_info& model, const usage& used) noexcept -> double;

} // namespace latibot::llm
