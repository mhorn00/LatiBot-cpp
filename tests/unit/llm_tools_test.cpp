#include "core/llm/tools.hpp"

#include "mocks/mock_llm.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

using latibot::llm::request;
using latibot::llm::speaker;
using latibot::llm::stop_reason;
using latibot::llm::tool_context;
using latibot::llm::tool_outcome;
using latibot::llm::tool_registry;
using latibot::llm::usage;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

const tool_context asked_by{.guild_id = dpp::snowflake{1U}, .channel_id = dpp::snowflake{2U}, .author_id = dpp::snowflake{3U}, .now = {}};

auto echo_tools() -> tool_registry {
    tool_registry tools;
    tools.add({.name = "echo", .description = "says it back", .input_schema = {{"type", "object"}}}, [](const json& input,
                                                                                                        const tool_context& context) {
        return tool_outcome{.content = std::format("{} from {}", input.value("text", std::string{}), context.author_id), .is_error = false};
    });
    return tools;
}

auto question() -> request {
    request call;
    call.model = "claude-haiku-4-5";
    call.conversation.push_back({.from = speaker::user, .text = "hi", .calls = {}, .results = {}, .raw = {}});
    return call;
}

} // namespace

TEST_CASE("a tool registered twice is refused", "[llm]") {
    tool_registry tools = echo_tools();
    CHECK_THROWS_AS(tools.add({.name = "echo", .description = {}, .input_schema = {}}, {}), std::invalid_argument);
}

TEST_CASE("an unknown tool, or one that throws, is an error the model reads", "[llm]") {
    tool_registry tools = echo_tools();
    tools.add({.name = "broken", .description = {}, .input_schema = {}},
              [](const json&, const tool_context&) -> tool_outcome { throw std::runtime_error("disk on fire"); });

    const auto missing = tools.run({.id = "a", .name = "nope", .input = {}}, asked_by);
    CHECK(missing.is_error);
    CHECK(missing.call_id == "a");

    const auto broken = tools.run({.id = "b", .name = "broken", .input = {}}, asked_by);
    CHECK(broken.is_error);
    // What went wrong stays in the log; the model only learns that it did.
    CHECK(broken.content.find("disk") == std::string::npos);
}

TEST_CASE("the tool loop runs what the model asks for and hands the result back", "[llm][coro]") {
    latibot::testing::mock_llm model;
    model.call_tool("echo", {{"text", "ping"}});
    model.answer("pong");
    const tool_registry tools = echo_tools();

    std::vector<usage> recorded;
    const auto outcome = latibot::llm::run_tool_loop(model, question(), tools, asked_by, 4, [&](const usage& used) {
                             recorded.push_back(used);
                         }).sync_wait_for(2s);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->ok());

    CHECK(outcome->value().text == "pong");
    CHECK(outcome->value().stop == stop_reason::finished);
    CHECK(outcome->value().requests == 2);
    CHECK(outcome->value().tools_run == std::vector<std::string>{"echo"});
    CHECK(outcome->value().used.input_tokens == 200);
    CHECK(recorded.size() == 2);

    REQUIRE(model.requests.size() == 2);
    const auto& second = model.requests[1].conversation;
    REQUIRE(second.size() == 3);
    CHECK(second[1].from == speaker::assistant);
    REQUIRE(second[2].results.size() == 1);
    CHECK(second[2].results[0].content == "ping from 3");
    CHECK(second[2].results[0].call_id == "call_1");
}

TEST_CASE("after the last round of tools the model has to answer", "[llm][coro]") {
    latibot::testing::mock_llm model;
    model.call_tool("echo", {{"text", "1"}});
    model.call_tool("echo", {{"text", "2"}});
    // Asked again with tools forbidden; a model that still asks is not
    // obeyed, and its turn ends the loop.
    model.call_tool("echo", {{"text", "3"}});
    const tool_registry tools = echo_tools();

    const auto outcome = latibot::llm::run_tool_loop(model, question(), tools, asked_by, 2, {}).sync_wait_for(2s);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->ok());

    REQUIRE(model.requests.size() == 3);
    CHECK(model.requests[0].allow_tools);
    CHECK(model.requests[1].allow_tools);
    CHECK_FALSE(model.requests[2].allow_tools);
    CHECK(outcome->value().tools_run.size() == 2);
}

TEST_CASE("a failure mid-loop is reported, and what was spent before it still counted", "[llm][coro]") {
    latibot::testing::mock_llm model;
    model.call_tool("echo", {{"text", "1"}});
    model.fail("overloaded", 529);
    const tool_registry tools = echo_tools();

    int recorded = 0;
    const auto outcome =
        latibot::llm::run_tool_loop(model, question(), tools, asked_by, 4, [&](const usage&) { ++recorded; }).sync_wait_for(2s);
    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(outcome->ok());
    CHECK(outcome->error().http_status == 529);
    CHECK(recorded == 1);
}

TEST_CASE("when the last turn says nothing, what was said along the way is kept", "[llm][coro]") {
    latibot::testing::mock_llm model;
    model.replies.emplace_back(latibot::llm::response{.reply = {.from = speaker::assistant,
                                                                .text = "one moment",
                                                                .calls = {{.id = "c", .name = "echo", .input = {{"text", "x"}}}},
                                                                .results = {},
                                                                .raw = {}},
                                                      .stop = stop_reason::tool_use,
                                                      .used = {}});
    model.answer("");
    const tool_registry tools = echo_tools();

    const auto outcome = latibot::llm::run_tool_loop(model, question(), tools, asked_by, 4, {}).sync_wait_for(2s);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->ok());
    CHECK(outcome->value().text == "one moment");
}
