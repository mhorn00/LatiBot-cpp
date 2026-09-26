// The real DECtalk, driven through the tts_engine port (plan §12.3, §17.5).
//
// These run the engine rather than a mock: what they check is how DECtalk
// behaves, which is what the design rests on. It needs no audio device, so
// they run in CI as well.

#include "core/audio/dectalk_engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using latibot::audio::dectalk_engine;
using latibot::ports::pcm_audio;
using latibot::ports::speech_request;

namespace {

auto say(dectalk_engine& engine, speech_request request) -> pcm_audio {
    const auto outcome = engine.synthesize(std::move(request)).sync_wait_for(20s);
    REQUIRE(outcome.has_value());
    if (!outcome->ok()) FAIL("DECtalk failed: " << outcome->error().message);
    return outcome->value();
}

auto say(dectalk_engine& engine, std::string text) -> pcm_audio {
    return say(engine, speech_request{.text = std::move(text)});
}

auto peak(const pcm_audio& audio) -> int {
    int loudest = 0;
    for (const auto sample : audio.samples) {
        loudest = std::max(loudest, std::abs(static_cast<int>(sample)));
    }
    return loudest;
}

} // namespace

TEST_CASE("DECtalk speaks a phrase at 11025 Hz mono", "[audio][coro][threads]") {
    dectalk_engine engine;

    const pcm_audio audio = say(engine, "Hello there.");

    CHECK(audio.sample_rate == 11025);
    CHECK(audio.channels == 1);
    CHECK_FALSE(audio.truncated);
    CHECK(audio.duration() > 500ms);
    CHECK(audio.duration() < 3s);
    CHECK(peak(audio) > 1000);
}

TEST_CASE("the same request gives the same audio every time", "[audio][coro][threads]") {
    // Only true because each utterance gets a fresh engine: one engine kept
    // across requests carries its intonation from one to the next.
    dectalk_engine engine;

    const pcm_audio first = say(engine, "This is a test of the speech engine.");
    const pcm_audio second = say(engine, "This is a test of the speech engine.");

    CHECK(first.samples == second.samples);
}

TEST_CASE("one request's inline settings do not reach the next", "[audio][coro][threads]") {
    // With a single long-lived engine, [:rate] and [:dv] outlive the request
    // that set them (plan §2.2).
    dectalk_engine engine;

    const pcm_audio plain = say(engine, "Hello there.");
    const pcm_audio altered = say(engine, "[:rate 75][:dv ap 300 pr 250]Hello there.");
    const pcm_audio after = say(engine, "Hello there.");

    CHECK(altered.samples.size() > plain.samples.size());
    CHECK(after.samples == plain.samples);
}

TEST_CASE("the voice and rate settings change the audio", "[audio][coro][threads]") {
    dectalk_engine engine;

    const pcm_audio paul = say(engine, "Hello there.");
    const pcm_audio betty = say(engine, speech_request{.text = "Hello there.", .voice = {.voice = "betty"}});
    const pcm_audio fast = say(engine, speech_request{.text = "Hello there.", .voice = {.rate = 400}});

    CHECK(betty.samples != paul.samples);
    CHECK(fast.samples.size() < paul.samples.size());
}

TEST_CASE("volume scales the samples", "[audio][coro][threads]") {
    dectalk_engine engine;

    const pcm_audio full = say(engine, "Hello there.");
    const pcm_audio half = say(engine, speech_request{.text = "Hello there.", .voice = {.volume = 50}});

    REQUIRE(half.samples.size() == full.samples.size());
    CHECK(peak(half) == peak(full) * 50 / 100);
}

TEST_CASE("an utterance stops at its maximum duration", "[audio][coro][threads]") {
    dectalk_engine engine;

    // A one-minute tone, cut at two seconds.
    const pcm_audio audio = say(engine, speech_request{.text = "[:tone 440 60000]", .max_duration = 2s});

    CHECK(audio.truncated);
    CHECK(audio.samples.size() == std::size_t{2} * 11025);

    // The engine that was cut off is gone; the next one starts clean.
    CHECK_FALSE(say(engine, "Hello there.").samples.empty());
}

TEST_CASE("an utterance that takes too long is abandoned", "[audio][coro][threads]") {
    // [:pause] waits on the clock rather than producing silence (plan
    // §21.16), so this would hold the engine for a minute.
    dectalk_engine engine(dectalk_engine::default_dictionary(), 300ms);

    const auto started = std::chrono::steady_clock::now();
    const auto outcome = engine.synthesize({.text = "[:pause 60000]hello"}).sync_wait_for(20s);
    const auto took = std::chrono::steady_clock::now() - started;

    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(outcome->ok());
    CHECK(outcome->error().message == "that took too long to say");
    CHECK(took < 5s);

    CHECK_FALSE(say(engine, "Hello there.").samples.empty());
}

TEST_CASE("stop abandons the utterance being spoken and the queue", "[audio][coro][threads]") {
    dectalk_engine engine;

    // The first request holds the engine long enough for the rest to queue
    // behind it and for stop to arrive while it runs.
    auto slow = engine.synthesize({.text = "[:pause 60000]hello"});
    auto queued = engine.synthesize({.text = "Hello there."});
    std::this_thread::sleep_for(100ms);
    engine.stop();

    const auto slow_outcome = slow.sync_wait_for(5s);
    const auto queued_outcome = queued.sync_wait_for(5s);
    REQUIRE(slow_outcome.has_value());
    REQUIRE(queued_outcome.has_value());
    CHECK(slow_outcome->error().message == "stopped");
    CHECK(queued_outcome->error().message == "stopped");

    // Stopping is not permanent: the next request is spoken.
    CHECK_FALSE(say(engine, "Hello there.").samples.empty());
}

TEST_CASE("a missing dictionary fails the request instead of the process", "[audio][coro][threads]") {
    {
        dectalk_engine engine("no/such/dictionary.dic");

        const auto outcome = engine.synthesize({.text = "Hello there."}).sync_wait_for(20s);

        REQUIRE(outcome.has_value());
        REQUIRE_FALSE(outcome->ok());
        CHECK(outcome->error().message.starts_with("the DECtalk dictionary is missing"));
    }

    // DECtalk failing to load a dictionary breaks every later start in the
    // process, so the engine must not have let it try.
    dectalk_engine engine;
    CHECK_FALSE(say(engine, "Hello there.").samples.empty());
}
