// Voice sessions and leaving an empty channel (plan §13).

#include "core/events/voice_sessions.hpp"

#include "mocks/mock_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using namespace std::chrono_literals;
using latibot::events::auto_leave;
using latibot::events::voice_sessions;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake other_guild{200};

auto thirty_seconds(dpp::snowflake /*guild*/) -> std::chrono::seconds {
    return 30s;
}

} // namespace

TEST_CASE("a voice session is kept until it ends, one per guild", "[events]") {
    voice_sessions sessions;
    sessions.start(
        {.guild_id = guild, .voice_channel = dpp::snowflake{1}, .text_channel = dpp::snowflake{2}, .started_by = dpp::snowflake{3}});

    REQUIRE(sessions.find(guild).has_value());
    CHECK(sessions.find(guild)->text_channel == dpp::snowflake{2});
    CHECK_FALSE(sessions.find(other_guild).has_value());

    // Starting again replaces it.
    sessions.start(
        {.guild_id = guild, .voice_channel = dpp::snowflake{5}, .text_channel = dpp::snowflake{6}, .started_by = dpp::snowflake{3}});
    CHECK(sessions.find(guild)->voice_channel == dpp::snowflake{5});

    const auto ended = sessions.end(guild);
    REQUIRE(ended.has_value());
    CHECK(ended->text_channel == dpp::snowflake{6});
    CHECK_FALSE(sessions.find(guild).has_value());
    CHECK_FALSE(sessions.end(guild).has_value());
}

TEST_CASE("a session follows the bot when it is moved", "[events]") {
    voice_sessions sessions;
    sessions.start(
        {.guild_id = guild, .voice_channel = dpp::snowflake{1}, .text_channel = dpp::snowflake{2}, .started_by = dpp::snowflake{3}});

    sessions.moved(guild, dpp::snowflake{9});
    sessions.moved(other_guild, dpp::snowflake{9}); // no session there: nothing to do

    CHECK(sessions.find(guild)->voice_channel == dpp::snowflake{9});
    CHECK_FALSE(sessions.find(other_guild).has_value());
}

TEST_CASE("the bot leaves once it has been alone for the grace period", "[events]") {
    latibot::testing::mock_clock clock;
    auto_leave leaving(clock);

    leaving.observe(guild, true, 0);
    clock.advance(29s);
    CHECK(leaving.due(thirty_seconds).empty());

    clock.advance(1s);
    CHECK(leaving.due(thirty_seconds) == std::vector<dpp::snowflake>{guild});

    // Reported once: the caller leaves, and the next check has nothing.
    CHECK(leaving.due(thirty_seconds).empty());
}

TEST_CASE("someone coming back within the grace period keeps the bot", "[events]") {
    latibot::testing::mock_clock clock;
    auto_leave leaving(clock);

    leaving.observe(guild, true, 0);
    clock.advance(20s);
    leaving.observe(guild, true, 1);
    clock.advance(20s);

    CHECK(leaving.due(thirty_seconds).empty());

    // Alone again starts the wait again, from now.
    leaving.observe(guild, true, 0);
    clock.advance(29s);
    CHECK(leaving.due(thirty_seconds).empty());
}

TEST_CASE("being seen alone again does not restart the wait", "[events]") {
    latibot::testing::mock_clock clock;
    auto_leave leaving(clock);

    leaving.observe(guild, true, 0);
    clock.advance(20s);
    leaving.observe(guild, true, 0); // someone else's voice state changed elsewhere
    clock.advance(10s);

    CHECK(leaving.due(thirty_seconds) == std::vector<dpp::snowflake>{guild});
}

TEST_CASE("a bot that is not in voice, or has left, is not waited on", "[events]") {
    latibot::testing::mock_clock clock;
    auto_leave leaving(clock);

    leaving.observe(guild, false, 0);
    leaving.observe(other_guild, true, 0);
    leaving.forget(other_guild);
    clock.advance(1h);

    CHECK(leaving.due(thirty_seconds).empty());
}

TEST_CASE("each guild waits its own grace period", "[events]") {
    latibot::testing::mock_clock clock;
    auto_leave leaving(clock);

    leaving.observe(guild, true, 0);
    leaving.observe(other_guild, true, 0);
    clock.advance(10s);

    const auto grace = [](dpp::snowflake which) { return which == guild ? 5s : 60s; };
    CHECK(leaving.due(grace) == std::vector<dpp::snowflake>{guild});
}
