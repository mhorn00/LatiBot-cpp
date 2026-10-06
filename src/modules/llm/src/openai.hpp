#pragma once

#include "core/llm/provider.hpp"
#include "core/ports/http_client.hpp"

#include <string>
#include <string_view>

namespace latibot::llm {

inline constexpr std::string_view openai_url = "https://api.openai.com/v1/chat/completions";

/// The Chat Completions body for `call`
/// (docs/features/Language_Model.md §3.2).
///
/// Reasoning is off: Chat Completions only lets these models call functions
/// with `reasoning_effort` at `none`, and the bot's memory is functions. The
/// instructions are one system message, stable part first, since OpenAI
/// caches whatever prefix repeats without being told where it ends.
[[nodiscard]] auto openai_body(const request& call) -> nlohmann::json;

/// Reads a Chat Completions reply. A status outside 2xx is an `api_error`
/// carrying the API's own message.
[[nodiscard]] auto read_openai_reply(int status, std::string_view body) -> ports::result<response>;

/// OpenAI's models, through `http`.
class openai_provider final : public provider {
public:
    openai_provider(ports::http_client& http, std::string api_key);

    [[nodiscard]] auto name() const -> std::string_view override { return "openai"; }
    auto complete(request call) -> dpp::task<ports::result<response>> override;

private:
    ports::http_client* http_;
    std::string api_key_;
};

} // namespace latibot::llm
