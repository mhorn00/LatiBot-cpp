// Exercises the mocks themselves, and the coroutine-plus-mock pattern that
// every feature from Phase 1 on is written against (plan v4 §17.3).

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "mocks/mock_http.hpp"
#include "mocks/mock_tts.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latibot::ports::api_error;

namespace {

/// A stand-in for a real feature: talks to Discord through the port, and
/// reports what happened rather than doing the I/O itself.
auto post_then_edit(latibot::ports::discord_gateway& gateway) -> dpp::task<std::string> {
    const auto sent = co_await gateway.send_message(dpp::message(dpp::snowflake{42}, "hello"));
    if (!sent.ok()) co_return "send failed: " + sent.error().message;

    dpp::message updated = sent.value();
    updated.content = "edited";
    const auto edited = co_await gateway.edit_message(updated);
    if (!edited.ok()) co_return "edit failed";

    co_return "ok:" + std::to_string(static_cast<std::uint64_t>(sent.value().id));
}

} // namespace

TEST_CASE("mock_clock moves both clocks together", "[ports]") {
    latibot::testing::mock_clock clock;

    const auto wall_start = clock.now();
    const auto steady_start = clock.steady_now();

    clock.advance(90s);

    CHECK(clock.now() - wall_start == 90s);
    CHECK(clock.steady_now() - steady_start == 90s);
}

TEST_CASE("a coroutine feature runs against the Discord mock", "[ports][coro]") {
    latibot::testing::mock_discord discord;

    // sync_wait_for turns a hung coroutine into a failed test rather than a
    // hung suite.
    const auto outcome = post_then_edit(discord).sync_wait_for(2s);

    REQUIRE(outcome.has_value());
    CHECK(*outcome == "ok:1001");

    REQUIRE(discord.sent.size() == 1);
    CHECK(discord.sent.front().content == "hello");
    REQUIRE(discord.edited.size() == 1);
    CHECK(discord.edited.front().content == "edited");
}

TEST_CASE("the Discord mock can script a failure", "[ports][coro]") {
    latibot::testing::mock_discord discord;
    discord.send_results.emplace_back(api_error{.http_status = 500, .message = "Internal Server Error"});

    const auto outcome = post_then_edit(discord).sync_wait_for(2s);

    REQUIRE(outcome.has_value());
    CHECK(*outcome == "send failed: Internal Server Error");
    CHECK(discord.edited.empty());
}

TEST_CASE("the Discord mock hands out scripted history pages", "[ports][coro]") {
    latibot::testing::mock_discord discord;

    std::vector<dpp::message> page;
    page.emplace_back(dpp::snowflake{42}, "older");
    discord.message_pages.emplace_back(page);

    const auto first = discord.get_messages(dpp::snowflake{42}, dpp::snowflake{0}, 100).sync_wait_for(2s);
    REQUIRE(first.has_value());
    REQUIRE(first->ok());
    CHECK(first->value().size() == 1);

    // With nothing left queued the mock reports an empty page, which is how a
    // backfill learns it has reached the end.
    const auto second = discord.get_messages(dpp::snowflake{42}, dpp::snowflake{0}, 100).sync_wait_for(2s);
    REQUIRE(second.has_value());
    REQUIRE(second->ok());
    CHECK(second->value().empty());

    REQUIRE(discord.history_requests.size() == 2);
    CHECK(discord.history_requests.front().limit == 100);
}

TEST_CASE("the HTTP mock replays responses in order and records requests", "[ports][coro]") {
    latibot::testing::mock_http http;
    http.queue(200, R"({"ok":true})");
    http.queue(429, R"({"error":"rate limited"})");

    latibot::ports::http_request request;
    request.url = "https://api.anthropic.com/v1/messages";
    request.body = "{}";

    const auto first = http.send(request).sync_wait_for(2s);
    REQUIRE(first.has_value());
    REQUIRE(first->ok());
    CHECK(first->value().status == 200);

    const auto second = http.send(request).sync_wait_for(2s);
    REQUIRE(second.has_value());
    REQUIRE(second->ok());
    // A rate limit is a response, not a transport failure: the caller needs
    // the status and body to decide what to do.
    CHECK(second->value().status == 429);

    const auto third = http.send(request).sync_wait_for(2s);
    REQUIRE(third.has_value());
    CHECK_FALSE(third->ok());

    REQUIRE(http.requests.size() == 3);
    CHECK(http.requests.front().url == "https://api.anthropic.com/v1/messages");
}

TEST_CASE("the TTS mock produces audio in proportion to the text", "[ports][coro]") {
    latibot::testing::mock_tts tts;
    tts.per_character = 10ms;

    const auto outcome = tts.synthesize("abcde", {}).sync_wait_for(2s);

    REQUIRE(outcome.has_value());
    REQUIRE(outcome->ok());
    // 11025 Hz does not divide evenly into milliseconds, so a whole number of
    // samples lands just under the requested length: 50 ms of audio is 551.25
    // samples, and 551 of them read back as 49 ms.
    CHECK(outcome->value().duration() >= 49ms);
    CHECK(outcome->value().duration() <= 50ms);
    CHECK_FALSE(outcome->value().samples.empty());
    REQUIRE(tts.requests.size() == 1);
    CHECK(tts.requests.front().first == "abcde");
}

TEST_CASE("the TTS mock can fail once and records stops", "[ports][coro]") {
    latibot::testing::mock_tts tts;
    tts.next_error = api_error{.http_status = 0, .message = "engine busy"};

    const auto failed = tts.synthesize("hello", {}).sync_wait_for(2s);
    REQUIRE(failed.has_value());
    CHECK_FALSE(failed->ok());

    // The scripted failure applies to one call only.
    const auto recovered = tts.synthesize("hello", {}).sync_wait_for(2s);
    REQUIRE(recovered.has_value());
    CHECK(recovered->ok());

    tts.stop();
    CHECK(tts.stop_count == 1);
}
