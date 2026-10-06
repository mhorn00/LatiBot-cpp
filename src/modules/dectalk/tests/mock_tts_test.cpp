// The TTS mock, which the dectalk module's tests speak through.

#include "mock_tts.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latibot::ports::api_error;

TEST_CASE("the TTS mock produces audio in proportion to the text", "[dectalk][coro]") {
    latibot::testing::mock_tts tts;
    tts.per_character = 10ms;

    const auto outcome = tts.synthesize({.text = "abcde"}).sync_wait_for(2s);

    REQUIRE(outcome.has_value());
    REQUIRE(outcome->has_value());
    // 11025 Hz does not divide evenly into milliseconds, so a whole number of
    // samples lands just under the requested length: 50 ms of audio is 551.25
    // samples, and 551 of them read back as 49 ms.
    CHECK(outcome->value().duration() >= 49ms);
    CHECK(outcome->value().duration() <= 50ms);
    CHECK_FALSE(outcome->value().samples.empty());
    REQUIRE(tts.requests.size() == 1);
    CHECK(tts.requests.front().text == "abcde");
}

TEST_CASE("the TTS mock can fail once and records stops", "[dectalk][coro]") {
    latibot::testing::mock_tts tts;
    tts.next_error = api_error{.http_status = 0, .message = "engine busy"};

    const auto failed = tts.synthesize({.text = "hello"}).sync_wait_for(2s);
    REQUIRE(failed.has_value());
    CHECK_FALSE(failed->has_value());

    // The scripted failure applies to one call only.
    const auto recovered = tts.synthesize({.text = "hello"}).sync_wait_for(2s);
    REQUIRE(recovered.has_value());
    CHECK(recovered->has_value());

    tts.stop();
    CHECK(tts.stop_count == 1);
}
