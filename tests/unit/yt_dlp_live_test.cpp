// The real yt-dlp and ffmpeg (docs/features/Music.md §7). Hidden ([.]) and
// [live]: run with `latibot_tests.exe "[live]"`. Skipped when either
// program is not installed.
//
// The file is Wikimedia Commons' Example.ogg: freely licensed, small, and
// there for years.

#include "core/music/yt_dlp.hpp"
#include "core/util/process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr const char* example = "https://upload.wikimedia.org/wikipedia/commons/c/c8/Example.ogg";

} // namespace

TEST_CASE("yt-dlp reads a real link, and yt-dlp piped into ffmpeg plays it", "[music][live][.]") {
    const auto ytdlp = latibot::util::locate_program("yt-dlp", {});
    const auto ffmpeg = latibot::util::locate_program("ffmpeg", {});
    if (!ytdlp || !ffmpeg) SKIP("yt-dlp or ffmpeg is not installed");

    const latibot::music::ytdlp_resolver resolver(*ytdlp, 60s);
    const auto lookup = resolver.lookup_now(example, 100);
    REQUIRE(lookup.ok());
    REQUIRE(lookup.value().items.size() == 1);
    INFO(lookup.value().items[0].title);

    latibot::music::ytdlp_opener opener(*ytdlp, *ffmpeg);
    const auto stream = opener.open(lookup.value().items[0].url);

    std::vector<std::int16_t> all;
    std::vector<std::int16_t> chunk(48000);
    const auto deadline = std::chrono::steady_clock::now() + 120s;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::size_t got = stream->read(chunk);
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
        if (got == 0) {
            if (stream->state() != latibot::ports::stream_state::running) break;
            std::this_thread::sleep_for(20ms);
        }
    }
    INFO(stream->error());
    CHECK(stream->state() == latibot::ports::stream_state::finished);
    // Example.ogg is a few seconds long: more than one second of 48 kHz
    // stereo, and not all silence.
    CHECK(all.size() > 96000);
    CHECK(std::ranges::any_of(all, [](std::int16_t sample) { return sample > 1000 || sample < -1000; }));
}
