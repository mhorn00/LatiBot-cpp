// What each guild is saying, in order, and stopping it (plan §12.7, §13).

#include "core/audio/speech_queue.hpp"

#include "mocks/mock_voice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using latibot::audio::speech_outcome;
using latibot::audio::speech_queue;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake other_guild{200};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

auto audio(std::size_t samples = 960) -> std::vector<std::int16_t> {
    return std::vector<std::int16_t>(samples, 1);
}

struct fixture {
    latibot::testing::mock_voice voice;
    speech_queue queue{voice};

    fixture() { voice.connected[guild] = true; }

    auto say(dpp::snowflake owner, std::size_t samples = 960) -> speech_outcome {
        return queue.enqueue(guild, owner, queue.ticket(guild), audio(samples));
    }
};

} // namespace

TEST_CASE("speech plays at once on a ready connection, each followed by its marker", "[audio]") {
    fixture test;

    CHECK(test.say(alice, 960) == speech_outcome::playing);
    CHECK(test.say(bob, 1920) == speech_outcome::playing);

    REQUIRE(test.voice.plays.size() == 2);
    CHECK(test.voice.plays[0].samples == 960);
    CHECK(test.voice.plays[1].samples == 1920);
    CHECK(test.voice.plays[0].marker != test.voice.plays[1].marker);
    CHECK(test.queue.size(guild) == 2);
    CHECK(test.queue.current_owner(guild) == alice);
}

TEST_CASE("a finished utterance's marker moves the queue on", "[audio]") {
    fixture test;
    test.say(alice);
    test.say(bob);

    test.queue.on_marker(guild, test.voice.plays[0].marker);

    CHECK(test.queue.current_owner(guild) == bob);
    CHECK(test.queue.size(guild) == 1);

    // A later marker covers anything before it that was never reported.
    test.say(alice);
    test.queue.on_marker(guild, test.voice.plays[2].marker);
    CHECK(test.queue.size(guild) == 0);
    CHECK_FALSE(test.queue.current_owner(guild).has_value());
}

TEST_CASE("markers that are not the queue's, or for another guild, change nothing", "[audio]") {
    fixture test;
    test.say(alice);

    test.queue.on_marker(guild, "music:7");
    test.queue.on_marker(guild, "tts:not-a-number");
    test.queue.on_marker(other_guild, test.voice.plays[0].marker);

    CHECK(test.queue.size(guild) == 1);
}

TEST_CASE("speech waits for a connection still being set up, then plays in order", "[audio]") {
    fixture test;
    test.voice.connected[guild] = false;

    CHECK(test.say(alice, 100) == speech_outcome::waiting);
    CHECK(test.say(bob, 200) == speech_outcome::waiting);
    CHECK(test.voice.plays.empty());
    CHECK(test.queue.current_owner(guild) == std::nullopt); // nothing is playing yet
    CHECK(test.queue.size(guild) == 2);

    test.voice.connected[guild] = true;
    test.queue.on_ready(guild);

    REQUIRE(test.voice.plays.size() == 2);
    CHECK(test.voice.plays[0].samples == 100);
    CHECK(test.voice.plays[1].samples == 200);
    CHECK(test.queue.current_owner(guild) == alice);
}

TEST_CASE("new speech queues behind speech still waiting, even once connected", "[audio]") {
    // The connection can report ready before on_ready arrives; speaking the
    // newer utterance first would put them out of order.
    fixture test;
    test.voice.connected[guild] = false;
    test.say(alice, 100);

    test.voice.connected[guild] = true;
    CHECK(test.say(bob, 200) == speech_outcome::waiting);

    test.queue.on_ready(guild);
    REQUIRE(test.voice.plays.size() == 2);
    CHECK(test.voice.plays[0].samples == 100);
}

TEST_CASE("skip drops only the utterance playing now", "[audio]") {
    fixture test;
    test.say(alice);
    test.say(bob);

    CHECK(test.queue.skip(guild));

    CHECK(test.voice.skips == 1);
    CHECK(test.queue.current_owner(guild) == bob);

    // The skipped one's marker may still be reported; it is ignored.
    test.queue.on_marker(guild, test.voice.plays[0].marker);
    CHECK(test.queue.current_owner(guild) == bob);
}

TEST_CASE("skip with nothing playing does nothing", "[audio]") {
    fixture test;
    CHECK_FALSE(test.queue.skip(guild));
    CHECK(test.voice.skips == 0);
}

TEST_CASE("stop drops everything, playing and waiting", "[audio]") {
    fixture test;
    test.say(alice);
    test.voice.connected[guild] = false;
    test.say(bob);

    CHECK(test.queue.stop(guild) == 2);

    CHECK(test.voice.stops == 1);
    CHECK(test.queue.size(guild) == 0);

    // The waiting one is gone for good, not played once connected.
    test.voice.connected[guild] = true;
    test.queue.on_ready(guild);
    CHECK(test.voice.plays.size() == 1);
}

TEST_CASE("stop also stops speech still being synthesized", "[audio]") {
    fixture test;

    // Taken when /speak starts; stop arrives before the audio is ready.
    const std::uint64_t ticket = test.queue.ticket(guild);
    test.queue.stop(guild);

    CHECK(test.queue.enqueue(guild, alice, ticket, audio()) == speech_outcome::stopped);
    CHECK(test.voice.plays.empty());

    // A request started after the stop is not affected.
    CHECK(test.say(alice) == speech_outcome::playing);
}

TEST_CASE("stopping one guild leaves another alone", "[audio]") {
    fixture test;
    test.voice.connected[other_guild] = true;
    const std::uint64_t other_ticket = test.queue.ticket(other_guild);
    test.say(alice);

    test.queue.stop(guild);

    CHECK(test.queue.enqueue(other_guild, bob, other_ticket, audio()) == speech_outcome::playing);
}

TEST_CASE("forgetting a guild drops its speech without touching the connection", "[audio]") {
    fixture test;
    const std::uint64_t ticket = test.queue.ticket(guild);
    test.say(alice);

    test.queue.forget(guild);

    CHECK(test.voice.stops == 0);
    CHECK(test.queue.size(guild) == 0);
    CHECK(test.queue.enqueue(guild, alice, ticket, audio()) == speech_outcome::stopped);
}
