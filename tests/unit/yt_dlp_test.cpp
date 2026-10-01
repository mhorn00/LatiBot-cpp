// Reading links with yt-dlp and decoding them with ffmpeg
// (docs/features/Music.md §4.4, §4.5). The argument lists and the JSON are
// checked directly; running them uses a stand-in program
// (tests/support/test_child.cpp), and the real programs are [live].

#include "core/music/yt_dlp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/coro/task.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

using latibot::music::decode_arguments;
using latibot::music::describe_failure;
using latibot::music::fetch_arguments;
using latibot::music::lookup_arguments;
using latibot::music::parse_lookup;
using latibot::music::process_stream;
using latibot::music::ytdlp_resolver;
using latibot::ports::stream_state;
using latibot::util::program;
using namespace std::chrono_literals;

namespace {

auto child(std::vector<std::string> arguments) -> program {
    return {.path = LATIBOT_TEST_CHILD, .arguments = std::move(arguments)};
}

/// Reads a stream until it is no longer running, or `limit` passes.
auto drain(process_stream& stream, std::chrono::milliseconds limit = 10s) -> std::vector<std::int16_t> {
    std::vector<std::int16_t> all;
    std::vector<std::int16_t> chunk(4096);
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::size_t got = stream.read(chunk);
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
        if (got == 0) {
            if (stream.state() != stream_state::running) break;
            std::this_thread::sleep_for(5ms);
        }
    }
    return all;
}

} // namespace

TEST_CASE("the link always follows --, and no config file is read", "[music]") {
    for (const auto& arguments :
         {lookup_arguments("--exec calc", 100), fetch_arguments("--exec calc", std::nullopt), fetch_arguments("--exec calc", "ff.exe")}) {
        REQUIRE(arguments.size() >= 2);
        CHECK(arguments.back() == "--exec calc");
        CHECK(arguments[arguments.size() - 2] == "--");
        CHECK(arguments.front() == "--ignore-config");
        CHECK(std::ranges::find(arguments, "--exec") == arguments.end());
    }
}

TEST_CASE("reading a link asks for JSON, a flat playlist, and at most so many entries", "[music]") {
    const auto arguments = lookup_arguments("https://x.com/a", 100);
    CHECK(std::ranges::find(arguments, "--dump-single-json") != arguments.end());
    CHECK(std::ranges::find(arguments, "--flat-playlist") != arguments.end());
    CHECK(std::ranges::find(arguments, "--no-playlist") != arguments.end());
    const auto end = std::ranges::find(arguments, "--playlist-end");
    REQUIRE(end != arguments.end());
    CHECK(*(end + 1) == "100");
}

TEST_CASE("fetching writes the best audio to stdout, and says where ffmpeg is", "[music]") {
    const auto arguments = fetch_arguments("https://x.com/a", "C:\\tools\\ffmpeg.exe");
    const auto output = std::ranges::find(arguments, "-o");
    REQUIRE(output != arguments.end());
    CHECK(*(output + 1) == "-");
    const auto location = std::ranges::find(arguments, "--ffmpeg-location");
    REQUIRE(location != arguments.end());
    CHECK(*(location + 1) == "C:\\tools\\ffmpeg.exe");
    const auto on_path = fetch_arguments("https://x.com/a", std::nullopt);
    CHECK(std::ranges::find(on_path, "--ffmpeg-location") == on_path.end());
}

TEST_CASE("decoding reads a pipe and writes 48 kHz stereo 16-bit samples", "[music]") {
    const auto plain = decode_arguments(false);
    CHECK(std::ranges::find(plain, "pipe:0") != plain.end());
    CHECK(plain.back() == "pipe:1");
    for (const auto& pair : {std::pair{"-f", "s16le"}, std::pair{"-ar", "48000"}, std::pair{"-ac", "2"}}) {
        const auto found = std::ranges::find(plain, pair.first);
        REQUIRE(found != plain.end());
        CHECK(*(found + 1) == pair.second);
    }
    CHECK(std::ranges::find(plain, "-af") == plain.end());
    const auto even = decode_arguments(true);
    const auto filter = std::ranges::find(even, "-af");
    REQUIRE(filter != even.end());
    CHECK((filter + 1)->starts_with("loudnorm"));
}

TEST_CASE("a single track's details", "[music]") {
    const auto lookup = parse_lookup(R"({"title": "Song", "webpage_url": "https://youtube.com/watch?v=1", "duration": 212.6,
                                        "uploader": "Artist", "live_status": "not_live"})",
                                     100);
    REQUIRE(lookup.ok());
    REQUIRE(lookup.value().items.size() == 1);
    const auto& item = lookup.value().items[0];
    CHECK(item.title == "Song");
    CHECK(item.url == "https://youtube.com/watch?v=1");
    CHECK(item.uploader == "Artist");
    CHECK(item.duration == 213s);
    CHECK_FALSE(item.live);
    CHECK_FALSE(lookup.value().is_playlist());
}

TEST_CASE("a live stream has no length", "[music]") {
    const auto lookup = parse_lookup(R"({"title": "Radio", "webpage_url": "https://x.com/live", "is_live": true, "duration": 5})", 100);
    REQUIRE(lookup.ok());
    CHECK(lookup.value().items[0].live);
    CHECK_FALSE(lookup.value().items[0].duration.has_value());
}

TEST_CASE("a playlist's entries, in order, up to the limit", "[music]") {
    const std::string json = R"({"_type": "playlist", "title": "Mix", "playlist_count": 250, "entries": [
        {"url": "https://x.com/1", "title": "One", "duration": 60},
        {"url": "not-a-link", "title": "Unusable"},
        {"url": "https://x.com/soon", "title": "Upcoming", "live_status": "is_upcoming"},
        {"url": "https://x.com/2", "channel": "Somebody"},
        {"url": "https://x.com/3", "title": "Three"}]})";

    const auto all = parse_lookup(json, 100);
    REQUIRE(all.ok());
    CHECK(all.value().is_playlist());
    CHECK(all.value().playlist_title == "Mix");
    CHECK(all.value().playlist_size == 250);
    REQUIRE(all.value().items.size() == 3);
    CHECK(all.value().items[0].title == "One");
    CHECK(all.value().items[1].title == "https://x.com/2"); // no title: the link stands in
    CHECK(all.value().items[1].uploader == "Somebody");
    CHECK(all.value().items[2].title == "Three");

    const auto two = parse_lookup(json, 2);
    REQUIRE(two.ok());
    CHECK(two.value().items.size() == 2);
}

TEST_CASE("answers with nothing playable are errors", "[music]") {
    CHECK_FALSE(parse_lookup("not json", 100).ok());
    CHECK_FALSE(parse_lookup("[]", 100).ok());
    CHECK_FALSE(parse_lookup(R"({"_type": "playlist", "title": "Empty", "entries": []})", 100).ok());
    CHECK_FALSE(parse_lookup(R"({"title": "No link"})", 100).ok());
}

TEST_CASE("yt-dlp's error line is what is shown", "[music]") {
    CHECK(describe_failure("[youtube] abc: Downloading webpage\nERROR: [youtube] abc: Video unavailable\n") ==
          "[youtube] abc: Video unavailable");
    CHECK(describe_failure("something odd happened\n") == "something odd happened");
    CHECK(describe_failure("") == "yt-dlp couldn't read that link");
}

TEST_CASE("the resolver runs yt-dlp and reads what it says", "[music][threads][coro]") {
    ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s);

    // 203.0.113.0/24 is set aside for documentation: public, and never
    // looked up, so these need no network.
    SECTION("a track") {
        const auto lookup = resolver.lookup_now("https://203.0.113.5/song", 100);
        REQUIRE(lookup.ok());
        CHECK(lookup.value().items[0].title == "A song");
        CHECK(lookup.value().items[0].duration == 61s);
    }
    SECTION("a playlist") {
        const auto lookup = resolver.lookup_now("https://203.0.113.5/list", 100);
        REQUIRE(lookup.ok());
        CHECK(lookup.value().items.size() == 3);
    }
    SECTION("a failure, told as yt-dlp told it") {
        const auto lookup = resolver.lookup_now("https://203.0.113.5/fail", 100);
        REQUIRE_FALSE(lookup.ok());
        CHECK(lookup.error().message.find("HTTP Error 404") != std::string::npos);
    }
    SECTION("a private address, refused before yt-dlp runs") {
        const auto lookup = resolver.lookup_now("http://127.0.0.1/list", 100);
        REQUIRE_FALSE(lookup.ok());
        CHECK(lookup.error().message.find("private network") != std::string::npos);
    }
    SECTION("through the workers") {
        const auto lookup = resolver.lookup("https://203.0.113.5/song", 100).sync_wait_for(10s);
        REQUIRE(lookup.has_value());
        CHECK(lookup->ok());
    }
}

TEST_CASE("a link yt-dlp takes too long over is given up on", "[music][threads]") {
    const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 300ms);
    const auto lookup = resolver.lookup_now("https://203.0.113.5/hang", 100);
    REQUIRE_FALSE(lookup.ok());
    CHECK(lookup.error().message.find("took more than") != std::string::npos);
}

TEST_CASE("a stream delivers every sample, in order, then finishes", "[music][threads]") {
    process_stream stream({child({"samples", "96000"}), child({"cat"})}, 10s, 4096);
    const auto samples = drain(stream);
    REQUIRE(samples.size() == 96000);
    CHECK(samples[0] == 0);
    CHECK(samples[54321] == 54321 % 1000);
    CHECK(stream.state() == stream_state::finished);
    CHECK(stream.error().empty());
}

TEST_CASE("a stream fails with the first program's error", "[music][threads]") {
    process_stream stream({child({"fail", "1", "ERROR: [youtube] abc: Private video"}), child({"cat"})}, 10s);
    (void)drain(stream);
    CHECK(stream.state() == stream_state::failed);
    CHECK(stream.error() == "[youtube] abc: Private video");
}

TEST_CASE("a stream that produces nothing for too long has failed", "[music][threads]") {
    process_stream stream({child({"hang"})}, 200ms);
    std::this_thread::sleep_for(400ms);
    CHECK(stream.state() == stream_state::failed);
    CHECK(stream.error().find("no audio arrived") != std::string::npos);
}

TEST_CASE("a stream whose program cannot start has failed at once", "[music]") {
    process_stream stream({program{.path = R"(C:\no\such\ffmpeg.exe)", .arguments = {}}});
    CHECK(stream.state() == stream_state::failed);
    CHECK_FALSE(stream.error().empty());
}

TEST_CASE("dropping a stream mid-way ends its programs", "[music][threads]") {
    const auto started = std::chrono::steady_clock::now();
    {
        process_stream stream({child({"samples", "100000000"})}, 10s, 1024);
        std::vector<std::int16_t> some(512);
        while (stream.read(some) == 0) {
            std::this_thread::sleep_for(1ms);
        }
    }
    CHECK(std::chrono::steady_clock::now() - started < 10s);
}
