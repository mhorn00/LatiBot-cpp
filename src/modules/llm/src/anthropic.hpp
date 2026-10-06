#pragma once

#include "core/ports/http_client.hpp"
#include "provider.hpp"

#include <string>
#include <string_view>

namespace latibot::llm {

inline constexpr std::string_view anthropic_url = "https://api.anthropic.com/v1/messages";
inline constexpr std::string_view anthropic_version = "2023-06-01";

/// The Messages API body for `call` (docs/features/Language_Model.md §3.2).
///
/// Model-aware: an effort setting goes only to the models that take one, and
/// nothing sends `temperature`, which current models reject. The stable
/// part of the instructions carries the one cache breakpoint, which caches
/// the tools with it, since the cache covers tools, then system, in that
/// order (docs/features/Language_Model.md §3.3).
[[nodiscard]] auto anthropic_body(const request& call) -> nlohmann::json;

/// Reads a Messages API reply. A status outside 2xx is an `api_error`
/// carrying the API's own message.
[[nodiscard]] auto read_anthropic_reply(int status, std::string_view body) -> ports::result<response>;

/// Claude, through `http`.
class anthropic_provider final : public provider {
public:
    anthropic_provider(ports::http_client& http, std::string api_key);

    [[nodiscard]] auto name() const -> std::string_view override { return "anthropic"; }
    auto complete(request call) -> dpp::task<ports::result<response>> override;

private:
    ports::http_client* http_;
    std::string api_key_;
};

} // namespace latibot::llm
