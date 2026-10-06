// A server's music queue, as plain data (docs/features/Music.md §3.1, §3.2).
// The Java bot's queue bugs (§2) are each a test here.

#include "music_queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <string>
#include <vector>

using namespace latibot::music;
using namespace std::chrono_literals;

namespace {

auto song(std::uint64_t id, std::optional<std::chrono::seconds> length = 60s) -> track {
    track entry;
    entry.id = id;
    entry.media.title = "song " + std::to_string(id);
    entry.media.url = "https://x.com/" + std::to_string(id);
    entry.media.duration = length;
    return entry;
}

auto ids(const guild_queue& queue) -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> order;
    std::ranges::transform(queue.upcoming, std::back_inserter(order), &track::id);
    return order;
}

/// A queue playing track 1 with 2 and 3 after it.
auto playing_one_two_three() -> guild_queue {
    guild_queue queue;
    queue.current = song(1);
    queue.upcoming = {song(2), song(3)};
    return queue;
}

} // namespace

TEST_CASE("end adds at the back", "[music]") {
    guild_queue queue = playing_one_two_three();
    const auto result = add(queue, {song(4), song(5)}, queue_position::end);
    CHECK(result.added == 2);
    CHECK_FALSE(result.replaces_current);
    CHECK(ids(queue) == std::vector<std::uint64_t>{2, 3, 4, 5});
    CHECK(queue.current->id == 1);
}

TEST_CASE("next keeps a playlist in its own order", "[music]") {
    // The Java bot put each track at the front in turn, reversing it.
    guild_queue queue = playing_one_two_three();
    add(queue, {song(4), song(5), song(6)}, queue_position::next);
    CHECK(ids(queue) == std::vector<std::uint64_t>{4, 5, 6, 2, 3});
    CHECK(queue.current->id == 1);
}

TEST_CASE("now keeps the interrupted track, to play again from the start", "[music]") {
    // The Java bot skipped it, and it never came back.
    guild_queue queue = playing_one_two_three();
    const auto result = add(queue, {song(4)}, queue_position::now);
    CHECK(result.replaces_current);
    CHECK_FALSE(queue.current.has_value());
    CHECK(ids(queue) == std::vector<std::uint64_t>{4, 1, 2, 3});

    REQUIRE(start_next(queue) != nullptr);
    CHECK(queue.current->id == 4);
}

TEST_CASE("now with nothing playing just plays", "[music]") {
    guild_queue queue;
    const auto result = add(queue, {song(4), song(5)}, queue_position::now);
    CHECK_FALSE(result.replaces_current);
    CHECK(ids(queue) == std::vector<std::uint64_t>{4, 5});
}

TEST_CASE("a full queue takes what fits, and says how many did not", "[music]") {
    guild_queue queue = playing_one_two_three();
    const auto result = add(queue, {song(4), song(5), song(6)}, queue_position::end, 5);
    CHECK(result.added == 2);
    CHECK(result.over_limit == 1);
    CHECK(ids(queue) == std::vector<std::uint64_t>{2, 3, 4, 5});

    CHECK(add(queue, {song(7)}, queue_position::end, 5).added == 0);
}

TEST_CASE("the queue limit is 500, and a playlist adds at most 100", "[music]") {
    CHECK(max_queue == 500);
    CHECK(max_playlist == 100);
}

TEST_CASE("moving on with repeat off plays the queue in order, then stops", "[music]") {
    guild_queue queue = playing_one_two_three();
    CHECK(advance(queue, track_end::finished)->id == 2);
    CHECK(advance(queue, track_end::finished)->id == 3);
    CHECK(advance(queue, track_end::finished) == nullptr);
    CHECK_FALSE(queue.current.has_value());
}

TEST_CASE("repeating a track plays it again when it finishes", "[music]") {
    guild_queue queue = playing_one_two_three();
    queue.repeat = repeat_mode::track;
    CHECK(advance(queue, track_end::finished)->id == 1);
    CHECK(advance(queue, track_end::finished)->id == 1);
    CHECK(ids(queue) == std::vector<std::uint64_t>{2, 3});
}

TEST_CASE("skip moves on even when the track repeats", "[music]") {
    // The Java bot replayed the track on a skip with repeat on.
    guild_queue queue = playing_one_two_three();
    queue.repeat = repeat_mode::track;
    CHECK(advance(queue, track_end::skipped)->id == 2);
}

TEST_CASE("a track that failed never repeats", "[music]") {
    // The Java bot retried a broken track with repeat on, for ever.
    guild_queue queue = playing_one_two_three();
    SECTION("repeating the track") {
        queue.repeat = repeat_mode::track;
        CHECK(advance(queue, track_end::failed)->id == 2);
    }
    SECTION("repeating the queue") {
        queue.repeat = repeat_mode::queue;
        CHECK(advance(queue, track_end::failed)->id == 2);
        CHECK(ids(queue) == std::vector<std::uint64_t>{3});
    }
}

TEST_CASE("repeating the queue sends each finished or skipped track to the back", "[music]") {
    guild_queue queue = playing_one_two_three();
    queue.repeat = repeat_mode::queue;
    CHECK(advance(queue, track_end::finished)->id == 2);
    CHECK(ids(queue) == std::vector<std::uint64_t>{3, 1});
    CHECK(advance(queue, track_end::skipped)->id == 3);
    CHECK(ids(queue) == std::vector<std::uint64_t>{1, 2});

    guild_queue alone;
    alone.current = song(9);
    alone.repeat = repeat_mode::queue;
    CHECK(advance(alone, track_end::finished)->id == 9);
}

TEST_CASE("peeking at what plays next agrees with moving on", "[music]") {
    for (const repeat_mode mode : {repeat_mode::off, repeat_mode::track, repeat_mode::queue}) {
        for (const track_end reason : {track_end::finished, track_end::skipped, track_end::failed}) {
            for (const bool alone : {false, true}) {
                guild_queue queue = playing_one_two_three();
                if (alone) queue.upcoming.clear();
                queue.repeat = mode;
                const track* peeked = peek_next(queue, reason);
                const std::optional<std::uint64_t> expected = peeked == nullptr ? std::nullopt : std::optional(peeked->id);
                const track* moved = advance(queue, reason);
                const std::optional<std::uint64_t> got = moved == nullptr ? std::nullopt : std::optional(moved->id);
                INFO(std::string(to_string(mode)) << " " << static_cast<int>(reason) << (alone ? " alone" : ""));
                CHECK(expected == got);
            }
        }
    }
}

TEST_CASE("remove counts from 1, as the queue is shown", "[music]") {
    guild_queue queue = playing_one_two_three();
    CHECK_FALSE(remove(queue, 0).has_value());
    CHECK_FALSE(remove(queue, 3).has_value());
    CHECK(remove(queue, 2)->id == 3);
    CHECK(ids(queue) == std::vector<std::uint64_t>{2});
    CHECK(queue.current->id == 1);
}

TEST_CASE("clear empties the queue and leaves the current track", "[music]") {
    guild_queue queue = playing_one_two_three();
    CHECK(clear(queue) == 2);
    CHECK(queue.upcoming.empty());
    CHECK(queue.current->id == 1);
}

TEST_CASE("shuffle keeps every track, and never the current one", "[music]") {
    guild_queue queue;
    queue.current = song(1);
    for (std::uint64_t id = 2; id <= 50; ++id) {
        queue.upcoming.push_back(song(id));
    }

    CHECK(shuffle(queue, 42) == 49);
    CHECK(queue.current->id == 1);
    std::vector<std::uint64_t> in_order(49);
    std::ranges::iota(in_order, std::uint64_t{2});
    auto order = ids(queue);
    CHECK(order != in_order);
    std::ranges::sort(order);
    CHECK(order.front() == 2);
    CHECK(order.back() == 50);
    CHECK(order.size() == 49);
}

TEST_CASE("repeat modes by name, and in turn", "[music]") {
    CHECK(repeat_mode_from_string("track") == repeat_mode::track);
    CHECK_FALSE(repeat_mode_from_string("all").has_value());
    CHECK(next_repeat_mode(repeat_mode::off) == repeat_mode::track);
    CHECK(next_repeat_mode(repeat_mode::track) == repeat_mode::queue);
    CHECK(next_repeat_mode(repeat_mode::queue) == repeat_mode::off);
    CHECK(queue_position_from_string("now") == queue_position::now);
    CHECK_FALSE(queue_position_from_string("later").has_value());
}

TEST_CASE("the running time adds what is known and counts what is not", "[music]") {
    const std::deque<track> tracks{song(1, 60s), song(2, std::nullopt), song(3, 90s)};
    const running_time total = total_time(tracks);
    CHECK(total.known == 150s);
    CHECK(total.unknown == 1);
}
