#include "core/llm/tools.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

namespace latibot::llm {

auto tool_registry::add(tool_definition definition, tool_handler handler) -> void {
    if (std::ranges::any_of(tools_, [&](const entry& existing) { return existing.definition.name == definition.name; })) {
        throw std::invalid_argument(std::format("the tool \"{}\" is registered twice", definition.name));
    }
    tools_.push_back({.definition = std::move(definition), .handler = std::move(handler)});
}

auto tool_registry::definitions() const -> std::vector<tool_definition> {
    std::vector<tool_definition> all;
    all.reserve(tools_.size());
    for (const entry& tool : tools_) {
        all.push_back(tool.definition);
    }
    return all;
}

auto tool_registry::run(const tool_call& call, const tool_context& context) const -> tool_result {
    const auto found = std::ranges::find_if(tools_, [&](const entry& tool) { return tool.definition.name == call.name; });
    if (found == tools_.end()) {
        return {.call_id = call.id, .content = std::format("there is no tool called \"{}\"", call.name), .is_error = true};
    }

    try {
        tool_outcome outcome = found->handler(call.input, context);
        return {.call_id = call.id, .content = std::move(outcome.content), .is_error = outcome.is_error};
    } catch (const std::exception& error) {
        util::log().error("the {} tool threw in guild {}: {}", call.name, context.guild_id, error.what());
        return {.call_id = call.id, .content = "that tool failed; try something else", .is_error = true};
    }
}

auto run_tool_loop(provider& model, request call, const tool_registry& tools, const tool_context& context, int tool_rounds,
                   const std::function<void(const usage&)>& record) -> dpp::task<ports::result<loop_outcome>> {
    loop_outcome outcome;
    std::string said_along_the_way;

    for (int round = 0;; ++round) {
        call.allow_tools = round < tool_rounds;

        auto answered = co_await model.complete(call);
        ++outcome.requests;
        if (!answered.ok()) co_return answered.error();

        response& reply = answered.value();
        outcome.used += reply.used;
        if (record) record(reply.used);

        const bool wants_tools = reply.stop == stop_reason::tool_use && !reply.reply.calls.empty();
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
            results.results.push_back(tools.run(wanted, context));
        }
        call.conversation.push_back(std::move(reply.reply));
        call.conversation.push_back(std::move(results));
    }
}

} // namespace latibot::llm
