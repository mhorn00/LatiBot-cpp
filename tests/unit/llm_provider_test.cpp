#include "core/llm/anthropic.hpp"
#include "core/llm/models.hpp"
#include "core/llm/openai.hpp"

#include "mocks/mock_http.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using latibot::llm::request;
using latibot::llm::speaker;
using latibot::llm::stop_reason;
using latibot::llm::turn;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

auto one_question(std::string model) -> request {
    request call;
    call.model = std::move(model);
    call.stable_system = "rules";
    call.varying_system = "memories";
    call.conversation.push_back({.from = speaker::user, .text = "hello", .calls = {}, .results = {}, .raw = {}});
    call.tools.push_back({.name = "recall", .description = "look something up", .input_schema = {{"type", "object"}}});
    call.max_output_tokens = 700;
    return call;
}

auto answered_with_tool() -> turn {
    return {.from = speaker::assistant,
            .text = {},
            .calls = {{.id = "call_1", .name = "recall", .input = {{"query", "pizza"}}}},
            .results = {},
            .raw = {}};
}

auto tool_answer() -> turn {
    return {
        .from = speaker::user, .text = {}, .calls = {}, .results = {{.call_id = "call_1", .content = "none", .is_error = true}}, .raw = {}};
}

} // namespace

// --------------------------------------------------------------------------
// Models and prices
// --------------------------------------------------------------------------

TEST_CASE("every model has a price, and the ids are the API's own", "[llm]") {
    for (const auto& model : latibot::llm::known_models()) {
        CHECK(model.input_price > 0);
        CHECK(model.output_price > 0);
        CHECK(model.cache_read_price > 0);
        CHECK(latibot::llm::find_model(model.id) == &model);
    }
    // The default in config.json has to be one of them.
    CHECK(latibot::llm::find_model("claude-haiku-4-5") != nullptr);
    CHECK(latibot::llm::find_model("claude-haiku-4-5-20251001") == nullptr);
}

TEST_CASE("a call costs its tokens at the model's prices, cache included", "[llm]") {
    const auto* haiku = latibot::llm::find_model("claude-haiku-4-5");
    REQUIRE(haiku != nullptr);

    const latibot::llm::usage used{
        .input_tokens = 1'000'000, .output_tokens = 100'000, .cache_write_tokens = 200'000, .cache_read_tokens = 1'000'000};
    // $1 in, $0.50 out, $0.25 writing the cache, $0.10 reading it.
    CHECK(latibot::llm::cost_usd(*haiku, used) == Catch::Approx(1.85));
}

TEST_CASE("provider names are read case-insensitively", "[llm]") {
    CHECK(latibot::llm::provider_from_string(" Anthropic ") == latibot::llm::provider_kind::anthropic);
    CHECK(latibot::llm::provider_from_string("openai") == latibot::llm::provider_kind::openai);
    CHECK_FALSE(latibot::llm::provider_from_string("mistral").has_value());
}

// --------------------------------------------------------------------------
// Anthropic
// --------------------------------------------------------------------------

TEST_CASE("an Anthropic request caches the stable instructions and nothing after them", "[llm]") {
    const json body = latibot::llm::anthropic_body(one_question("claude-haiku-4-5"));

    REQUIRE(body["system"].size() == 2);
    CHECK(body["system"][0]["text"] == "rules");
    CHECK(body["system"][0]["cache_control"]["type"] == "ephemeral");
    CHECK(body["system"][1]["text"] == "memories");
    CHECK_FALSE(body["system"][1].contains("cache_control"));
    CHECK(body["max_tokens"] == 700);
    CHECK(body["tools"][0]["input_schema"]["type"] == "object");
}

TEST_CASE("an Anthropic request never sends temperature, and sends effort only to models that take it", "[llm]") {
    const json haiku = latibot::llm::anthropic_body(one_question("claude-haiku-4-5"));
    CHECK_FALSE(haiku.contains("temperature"));
    CHECK_FALSE(haiku.contains("output_config"));
    CHECK_FALSE(haiku.contains("thinking"));

    const json sonnet = latibot::llm::anthropic_body(one_question("claude-sonnet-5-5"));
    CHECK_FALSE(sonnet.contains("temperature"));
    CHECK(sonnet["output_config"]["effort"] == "low");
    // Thinking is left at the model's default; switching it off is a 400 on
    // some of them.
    CHECK_FALSE(sonnet.contains("thinking"));
}

TEST_CASE("the last Anthropic round forbids tools but still declares them", "[llm]") {
    request call = one_question("claude-haiku-4-5");
    call.allow_tools = false;
    const json body = latibot::llm::anthropic_body(call);

    CHECK(body["tools"].size() == 1);
    CHECK(body["tool_choice"]["type"] == "none");
}

TEST_CASE("an Anthropic request sends tool calls and their results in the API's shape", "[llm]") {
    request call = one_question("claude-haiku-4-5");
    call.conversation.push_back(answered_with_tool());
    turn results = tool_answer();
    results.text = "and also";
    call.conversation.push_back(results);

    const json messages = latibot::llm::anthropic_body(call)["messages"];
    REQUIRE(messages.size() == 3);
    CHECK(messages[1]["role"] == "assistant");
    CHECK(messages[1]["content"][0]["type"] == "tool_use");
    CHECK(messages[1]["content"][0]["input"]["query"] == "pizza");

    // Results before any text in the same turn, which the API insists on.
    CHECK(messages[2]["content"][0]["type"] == "tool_result");
    CHECK(messages[2]["content"][0]["tool_use_id"] == "call_1");
    CHECK(messages[2]["content"][0]["is_error"] == true);
    CHECK(messages[2]["content"][1]["text"] == "and also");
}

TEST_CASE("an assistant turn Anthropic wrote goes back exactly as it came, thinking included", "[llm]") {
    const std::string reply = R"({
        "content": [
            {"type": "thinking", "thinking": "", "signature": "sig-abc"},
            {"type": "tool_use", "id": "toolu_1", "name": "recall", "input": {"query": "x"}}
        ],
        "stop_reason": "tool_use",
        "usage": {"input_tokens": 10, "output_tokens": 5}
    })";
    const auto read = latibot::llm::read_anthropic_reply(200, reply);
    REQUIRE(read.has_value());

    request call = one_question("claude-sonnet-5-5");
    call.conversation.push_back(read.value().reply);
    const json sent = latibot::llm::anthropic_body(call)["messages"][1]["content"];

    REQUIRE(sent.size() == 2);
    CHECK(sent[0]["type"] == "thinking");
    CHECK(sent[0]["signature"] == "sig-abc");
}

TEST_CASE("an Anthropic reply is read into text, calls, usage and a stop reason", "[llm]") {
    const std::string reply = R"({
        "content": [
            {"type": "text", "text": "let me look"},
            {"type": "tool_use", "id": "toolu_1", "name": "recall", "input": {"query": "pizza"}},
            {"type": "text", "text": "hm"}
        ],
        "stop_reason": "tool_use",
        "usage": {"input_tokens": 12, "output_tokens": 34, "cache_creation_input_tokens": 56, "cache_read_input_tokens": 78}
    })";

    const auto read = latibot::llm::read_anthropic_reply(200, reply);
    REQUIRE(read.has_value());
    const auto& answer = read.value();
    CHECK(answer.reply.text == "let me look\nhm");
    REQUIRE(answer.reply.calls.size() == 1);
    CHECK(answer.reply.calls[0].id == "toolu_1");
    CHECK(answer.reply.calls[0].input["query"] == "pizza");
    CHECK(answer.stop == stop_reason::tool_use);
    CHECK(answer.used == latibot::llm::usage{.input_tokens = 12, .output_tokens = 34, .cache_write_tokens = 56, .cache_read_tokens = 78});
}

TEST_CASE("an Anthropic error carries the status and the API's own message", "[llm]") {
    const auto read =
        latibot::llm::read_anthropic_reply(529, R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})");
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().http_status == 529);
    CHECK(read.error().message == "Anthropic answered 529: overloaded_error: Overloaded");
}

TEST_CASE("an Anthropic reply of the wrong shape is an error, not a crash", "[llm]") {
    CHECK_FALSE(latibot::llm::read_anthropic_reply(200, "not json").has_value());
    CHECK_FALSE(latibot::llm::read_anthropic_reply(200, R"({"content": 5})").has_value());

    // Fields of the wrong type cost the field, not the reply.
    const auto odd =
        latibot::llm::read_anthropic_reply(200, R"({"content": [{"type": "text", "text": 5}], "usage": {"input_tokens": "x"}})");
    REQUIRE(odd.has_value());
    CHECK(odd.value().reply.text.empty());
    CHECK(odd.value().used.input_tokens == 0);
}

TEST_CASE("the Anthropic provider sends its key and version, and posts to the Messages API", "[llm][coro]") {
    latibot::testing::mock_http http;
    http.queue(200, R"({"content":[{"type":"text","text":"hi"}],"stop_reason":"end_turn","usage":{"input_tokens":1,"output_tokens":1}})");
    latibot::llm::anthropic_provider provider(http, "test-key");

    const auto answered = provider.complete(one_question("claude-haiku-4-5")).sync_wait_for(2s);
    REQUIRE(answered.has_value());
    REQUIRE(answered->has_value());
    CHECK(answered->value().reply.text == "hi");
    CHECK(answered->value().stop == stop_reason::finished);

    REQUIRE(http.requests.size() == 1);
    const auto& sent = http.requests[0];
    CHECK(sent.url == latibot::llm::anthropic_url);
    CHECK(sent.method == latibot::ports::http_method::post);
    CHECK(sent.headers == std::vector<std::pair<std::string, std::string>>{{"x-api-key", "test-key"}, {"anthropic-version", "2023-06-01"}});
    CHECK(json::parse(sent.body)["model"] == "claude-haiku-4-5");
}

TEST_CASE("a transport failure reaches the caller as an error", "[llm][coro]") {
    latibot::testing::mock_http http;
    http.queue_error("connection reset");
    latibot::llm::anthropic_provider provider(http, "test-key");

    const auto answered = provider.complete(one_question("claude-haiku-4-5")).sync_wait_for(2s);
    REQUIRE(answered.has_value());
    REQUIRE_FALSE(answered->has_value());
    CHECK(answered->error().message == "connection reset");
}

// --------------------------------------------------------------------------
// OpenAI
// --------------------------------------------------------------------------

TEST_CASE("an OpenAI request puts the instructions in one system message, stable part first", "[llm]") {
    const json body = latibot::llm::openai_body(one_question("gpt-6-luna"));

    CHECK(body["messages"][0]["role"] == "system");
    CHECK(body["messages"][0]["content"] == "rules\n\nmemories");
    CHECK(body["messages"][1]["content"] == "hello");
    CHECK(body["max_completion_tokens"] == 700);
    // Chat Completions only calls functions with reasoning off.
    CHECK(body["reasoning_effort"] == "none");
    CHECK(body["tools"][0]["function"]["name"] == "recall");
    CHECK_FALSE(body.contains("temperature"));
}

TEST_CASE("an OpenAI request sends each tool result as its own message", "[llm]") {
    request call = one_question("gpt-6-luna");
    call.conversation.push_back(answered_with_tool());
    call.conversation.push_back(tool_answer());
    call.allow_tools = false;

    const json body = latibot::llm::openai_body(call);
    const json& messages = body["messages"];
    REQUIRE(messages.size() == 4);
    CHECK(messages[2]["tool_calls"][0]["function"]["arguments"] == R"({"query":"pizza"})");
    CHECK(messages[3]["role"] == "tool");
    CHECK(messages[3]["tool_call_id"] == "call_1");
    CHECK(messages[3]["content"] == "error: none");
    CHECK(body["tool_choice"] == "none");
}

TEST_CASE("an OpenAI reply is read into calls, and cached input is counted apart", "[llm]") {
    const std::string reply = R"({
        "choices": [{
            "message": {"role": "assistant", "content": null,
                        "tool_calls": [{"id": "call_9", "type": "function",
                                        "function": {"name": "remember", "arguments": "{\"content\":\"likes tea\"}"}}]},
            "finish_reason": "tool_calls"
        }],
        "usage": {"prompt_tokens": 100, "completion_tokens": 20, "prompt_tokens_details": {"cached_tokens": 60}}
    })";

    const auto read = latibot::llm::read_openai_reply(200, reply);
    REQUIRE(read.has_value());
    const auto& answer = read.value();
    CHECK(answer.stop == stop_reason::tool_use);
    REQUIRE(answer.reply.calls.size() == 1);
    CHECK(answer.reply.calls[0].input["content"] == "likes tea");
    CHECK(answer.used == latibot::llm::usage{.input_tokens = 40, .output_tokens = 20, .cache_write_tokens = 0, .cache_read_tokens = 60});
    // Sent back as it came.
    CHECK(answer.reply.raw["tool_calls"][0]["id"] == "call_9");
}

TEST_CASE("an OpenAI refusal is a refusal, with its explanation as the text", "[llm]") {
    const auto read = latibot::llm::read_openai_reply(
        200, R"({"choices":[{"message":{"role":"assistant","content":null,"refusal":"no"},"finish_reason":"stop"}]})");
    REQUIRE(read.has_value());
    CHECK(read.value().stop == stop_reason::refusal);
    CHECK(read.value().reply.text == "no");
}

TEST_CASE("an OpenAI error carries the API's message", "[llm]") {
    const auto read = latibot::llm::read_openai_reply(401, R"({"error":{"message":"Incorrect API key provided"}})");
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().message == "OpenAI answered 401: Incorrect API key provided");
}

TEST_CASE("the OpenAI provider authenticates with a bearer token", "[llm][coro]") {
    latibot::testing::mock_http http;
    http.queue(200, R"({"choices":[{"message":{"role":"assistant","content":"hey"},"finish_reason":"stop"}]})");
    latibot::llm::openai_provider provider(http, "test-key");

    const auto answered = provider.complete(one_question("gpt-6-luna")).sync_wait_for(2s);
    REQUIRE(answered.has_value());
    REQUIRE(answered->has_value());
    CHECK(answered->value().reply.text == "hey");
    REQUIRE(http.requests.size() == 1);
    CHECK(http.requests[0].url == latibot::llm::openai_url);
    CHECK(http.requests[0].headers == std::vector<std::pair<std::string, std::string>>{{"Authorization", "Bearer test-key"}});
}
