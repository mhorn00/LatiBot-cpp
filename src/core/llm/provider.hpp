#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/json.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::llm {

// The conversation as the bot sees it, whichever provider answers (plan
// §14.1). Each provider translates this into its own request and its reply
// back into this, so everything above the providers is written once.

enum class speaker : std::uint8_t { user, assistant };

/// A tool the model asked to run.
struct tool_call {
    std::string id;
    std::string name;
    nlohmann::json input;
};

/// What running one came to, handed back on the next request.
struct tool_result {
    std::string call_id;
    std::string content;
    bool is_error = false;
};

/// One turn of the conversation.
struct turn {
    speaker from = speaker::user;
    std::string text;

    /// Assistant turns: the tools it asked for.
    std::vector<tool_call> calls;

    /// User turns: what those tools returned.
    std::vector<tool_result> results;

    /// An assistant turn exactly as the provider returned it, which is how
    /// it is sent back. Anthropic requires the thinking blocks of a turn
    /// that called a tool to come back unchanged, signatures included, and
    /// only the provider knows what those look like. Null for a turn built
    /// here.
    nlohmann::json raw;
};

/// A tool the model may call: what it is for, and the JSON schema of its
/// input.
struct tool_definition {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
};

/// Everything one call to a model needs.
struct request {
    std::string model;

    /// The part of the instructions that is the same from one message to the
    /// next: the fixed rules and the guild's documents. Marked for prompt
    /// caching, since it is sent with every request (plan §14.5).
    std::string stable_system;

    /// The part that changes with every message, such as the memories that
    /// matched it. After the cached part, so it does not spoil the cache.
    std::string varying_system;

    std::vector<turn> conversation;
    std::vector<tool_definition> tools;

    /// Whether the model may call a tool this time. False on the last round
    /// of the tool loop, so it has to answer; the tools stay declared, since
    /// the conversation already holds calls to them.
    bool allow_tools = true;

    /// Thinking included, on the models that think (plan §14.1).
    int max_output_tokens = 1024;
};

/// Tokens a call used, as the provider counted them.
struct usage {
    /// Input neither read from nor written to the prompt cache.
    std::int64_t input_tokens = 0;
    std::int64_t output_tokens = 0;
    std::int64_t cache_write_tokens = 0;
    std::int64_t cache_read_tokens = 0;

    auto operator+=(const usage& other) noexcept -> usage& {
        input_tokens += other.input_tokens;
        output_tokens += other.output_tokens;
        cache_write_tokens += other.cache_write_tokens;
        cache_read_tokens += other.cache_read_tokens;
        return *this;
    }

    friend auto operator==(const usage&, const usage&) -> bool = default;
};

/// Why the model stopped.
enum class stop_reason : std::uint8_t {
    /// It said what it had to say.
    finished,
    /// It wants tools run.
    tool_use,
    /// It ran out of `max_output_tokens`.
    max_tokens,
    /// It declined to answer.
    refusal,
    /// Anything else the provider reports.
    other,
};

[[nodiscard]] auto to_string(stop_reason reason) noexcept -> std::string_view;

struct response {
    turn reply;
    stop_reason stop = stop_reason::other;
    usage used;
};

/// A company's model API (plan §14.1).
///
/// A reply the provider refused, a rate limit or an overloaded model comes
/// back as an `api_error` carrying the HTTP status and the provider's own
/// message, as does a reply that cannot be read.
class provider {
public:
    virtual ~provider() = default;

    provider() = default;
    provider(const provider&) = delete;
    auto operator=(const provider&) -> provider& = delete;

    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    virtual auto complete(request call) -> dpp::task<ports::result<response>> = 0;
};

} // namespace latibot::llm
