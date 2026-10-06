#pragma once

#include "provider.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::llm {

class people;

/// Whom the model is answering, which is what a tool acts on behalf of.
struct tool_context {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    dpp::snowflake author_id;
    std::chrono::sys_seconds now;

    /// The people in this request, by alias: a tool reads aliases the model
    /// passes through it, and shows people to the model as aliases
    /// (docs/features/Language_Model.md §3.8). Null in a tool's own tests,
    /// where nobody is named.
    people* cast = nullptr;
};

/// What a tool handed back: text for the model, and whether it failed.
struct tool_outcome {
    std::string content;
    bool is_error = false;
};

using tool_handler = std::function<tool_outcome(const nlohmann::json& input, const tool_context& context)>;

/// The tools the model may call: a name, a JSON schema and a handler each
/// (docs/features/Language_Model.md §3.4).
///
/// A registry rather than a fixed set, so a later feature adds a tool
/// without touching the loop that runs them.
class tool_registry {
public:
    /// Throws std::invalid_argument when the name is taken: always a
    /// programming error, caught at startup.
    auto add(tool_definition definition, tool_handler handler) -> void;

    [[nodiscard]] auto definitions() const -> std::vector<tool_definition>;

    /// Runs one call. A tool that does not exist, or that throws, is an
    /// error for the model to read rather than an exception: the model can
    /// recover from a failed tool, and a reply should not be lost to one.
    [[nodiscard]] auto run(const tool_call& call, const tool_context& context) const -> tool_result;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return tools_.size(); }

private:
    struct entry {
        tool_definition definition;
        tool_handler handler;
    };

    std::vector<entry> tools_;
};

/// How a conversation with tools ended.
struct loop_outcome {
    /// What to post: the model's last text, or everything it said along the
    /// way when the last turn said nothing.
    std::string text;

    stop_reason stop = stop_reason::other;

    /// Every call's usage, added up.
    usage used;

    /// Requests made, one more than the rounds of tools run.
    int requests = 0;

    /// The tools that ran, in order, for the log.
    std::vector<std::string> tools_run;
};

/// Asks the model, runs the tools it calls, and asks again, until it answers
/// or `tool_rounds` rounds of tools have run
/// (docs/features/Language_Model.md §3.1). The request after the last round
/// forbids tools, so the model has to answer with what it has.
///
/// `record` is called with each request's usage as it arrives, so what was
/// spent is counted even when a later request fails.
auto run_tool_loop(provider& model, request call, const tool_registry& tools, const tool_context& context, int tool_rounds,
                   const std::function<void(const usage&)>& record) -> dpp::task<ports::result<loop_outcome>>;

} // namespace latibot::llm
