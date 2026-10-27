#include "tools.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

namespace latibot::llm {

auto tool_registry::add(tool_definition definition, tool_handler handler) -> void {
    add_async(std::move(definition), [handler = std::move(handler)](nlohmann::json input, tool_context context) -> dpp::task<tool_outcome> {
        co_return handler(input, context);
    });
}

auto tool_registry::add_async(tool_definition definition, async_tool_handler handler) -> void {
    if (contains(definition.name)) throw std::invalid_argument(std::format("the tool \"{}\" is registered twice", definition.name));
    tools_.push_back({.definition = std::move(definition), .handler = std::move(handler)});
}

auto tool_registry::definitions() const -> std::vector<tool_definition> {
    return definitions(tool_context{});
}

auto tool_registry::definitions(const tool_context& context) const -> std::vector<tool_definition> {
    std::vector<tool_definition> chosen;
    chosen.reserve(tools_.size());
    for (const entry& tool : tools_) {
        if (context.allows(tool.definition.name)) chosen.push_back(tool.definition);
    }
    return chosen;
}

auto tool_registry::names() const -> std::vector<std::string> {
    std::vector<std::string> all;
    all.reserve(tools_.size());
    for (const entry& tool : tools_) {
        all.push_back(tool.definition.name);
    }
    return all;
}

auto tool_registry::contains(std::string_view name) const -> bool {
    return std::ranges::any_of(tools_, [&](const entry& tool) { return tool.definition.name == name; });
}

auto tool_registry::run(tool_call call, tool_context context) const -> dpp::task<tool_result> {
    const auto found = std::ranges::find_if(tools_, [&](const entry& tool) { return tool.definition.name == call.name; });
    if (found == tools_.end()) {
        co_return tool_result{.call_id = call.id, .content = std::format("there is no tool called \"{}\"", call.name), .is_error = true};
    }
    if (!context.allows(call.name)) {
        co_return tool_result{.call_id = call.id, .content = std::format("the {} tool is turned off here", call.name), .is_error = true};
    }

    std::string failure;
    try {
        tool_outcome outcome = co_await found->handler(call.input, context);
        co_return tool_result{.call_id = call.id, .content = std::move(outcome.content), .is_error = outcome.is_error};
    } catch (const std::exception& error) {
        failure = error.what();
    }
    util::log().error("the {} tool threw in guild {}: {}", call.name, context.guild_id, failure);
    co_return tool_result{.call_id = call.id, .content = "that tool failed; try something else", .is_error = true};
}

auto run_tool_loop(provider& model, request call, const tool_registry& tools, const tool_context& context, int tool_rounds,
                   const std::function<void(const usage&)>& record) -> dpp::task<ports::result<loop_outcome>> {
    loop_outcome outcome;
    std::string said_along_the_way;

    for (int round = 0;; ++round) {
        call.allow_tools = round < tool_rounds;

        auto answered = co_await model.complete(call);
        ++outcome.requests;
        if (!answered.has_value()) co_return std::unexpected(answered.error());

        response& reply = answered.value();
        outcome.used += reply.used;
        if (record) record(reply.used);

        const bool wants_tools = reply.stop == stop_reason::tool_use && !reply.reply.calls.empty();
        util::log().trace("llm: round {} with {} stopped ({}), {} tool call(s){}", round + 1, call.model, to_string(reply.stop),
                          reply.reply.calls.size(), wants_tools && !call.allow_tools ? ", but tools are no longer allowed" : "");
        if (!wants_tools || !call.allow_tools) {
            outcome.stop = reply.stop;
            outcome.text = util::is_blank(reply.reply.text) ? said_along_the_way : reply.reply.text;
            co_return outcome;
        }

        if (!reply.reply.text.empty()) {
            if (!said_along_the_way.empty()) said_along_the_way += "\n";
            said_along_the_way += reply.reply.text;
        }

        turn results{.from = speaker::user, .text = {}, .calls = {}, .results = {}, .raw = {}};
        for (const tool_call& wanted : reply.reply.calls) {
            outcome.tools_run.push_back(wanted.name);
            results.results.push_back(co_await tools.run(wanted, context));
            util::log().trace("llm: tool {} {}", wanted.name, results.results.back().is_error ? "failed" : "ran");
        }
        call.conversation.push_back(std::move(reply.reply));
        call.conversation.push_back(std::move(results));
    }
}

} // namespace latibot::llm
