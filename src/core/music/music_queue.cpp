#include "core/music/music_queue.hpp"

#include <algorithm>
#include <iterator>
#include <random>
#include <utility>

namespace latibot::music {

auto to_string(repeat_mode mode) noexcept -> std::string_view {
    switch (mode) {
    case repeat_mode::off:
        return "off";
    case repeat_mode::track:
        return "track";
    case repeat_mode::queue:
        return "queue";
    }
    return "off";
}

auto repeat_mode_from_string(std::string_view name) -> std::optional<repeat_mode> {
    for (const repeat_mode mode : {repeat_mode::off, repeat_mode::track, repeat_mode::queue}) {
        if (name == to_string(mode)) return mode;
    }
    return std::nullopt;
}

auto next_repeat_mode(repeat_mode mode) noexcept -> repeat_mode {
    switch (mode) {
    case repeat_mode::off:
        return repeat_mode::track;
    case repeat_mode::track:
        return repeat_mode::queue;
    case repeat_mode::queue:
        return repeat_mode::off;
    }
    return repeat_mode::off;
}

auto queue_position_from_string(std::string_view name) -> std::optional<queue_position> {
    if (name == "end") return queue_position::end;
    if (name == "next") return queue_position::next;
    if (name == "now") return queue_position::now;
    return std::nullopt;
}

auto add(guild_queue& queue, std::vector<track> tracks, queue_position where, std::size_t limit) -> add_result {
    const std::size_t held = queue.upcoming.size() + (queue.current ? 1 : 0);
    const std::size_t room = held >= limit ? 0 : limit - held;

    add_result result;
    if (tracks.size() > room) {
        result.over_limit = tracks.size() - room;
        tracks.resize(room);
    }
    result.added = tracks.size();
    if (tracks.empty()) return result;

    switch (where) {
    case queue_position::end:
        std::ranges::move(tracks, std::back_inserter(queue.upcoming));
        break;
    case queue_position::next:
        // All at the front, in their own order. The Java bot put each one at
        // the front in turn, which reversed a playlist.
        queue.upcoming.insert(queue.upcoming.begin(), std::make_move_iterator(tracks.begin()), std::make_move_iterator(tracks.end()));
        break;
    case queue_position::now:
        // The interrupted track comes back after them, from its start. The
        // Java bot dropped it.
        if (queue.current) {
            queue.upcoming.push_front(std::move(*queue.current));
            queue.current.reset();
            result.replaces_current = true;
        }
        queue.upcoming.insert(queue.upcoming.begin(), std::make_move_iterator(tracks.begin()), std::make_move_iterator(tracks.end()));
        break;
    }
    return result;
}

auto peek_next(const guild_queue& queue, track_end reason) -> const track* {
    if (queue.current && reason == track_end::finished && queue.repeat == repeat_mode::track) return &*queue.current;
    if (!queue.upcoming.empty()) return &queue.upcoming.front();
    // Repeating the queue with one track in it plays that track again.
    if (queue.current && reason != track_end::failed && queue.repeat == repeat_mode::queue) return &*queue.current;
    return nullptr;
}

auto advance(guild_queue& queue, track_end reason) -> const track* {
    if (!queue.current) return start_next(queue);

    // Repeating a track that finished plays it again. A skip is still a
    // skip, which the Java bot got wrong, and a failure never repeats,
    // which it got wrong as well, looping for ever.
    if (reason == track_end::finished && queue.repeat == repeat_mode::track) return &*queue.current;

    track ended = std::move(*queue.current);
    queue.current.reset();
    if (reason != track_end::failed && queue.repeat == repeat_mode::queue) queue.upcoming.push_back(std::move(ended));
    return start_next(queue);
}

auto start_next(guild_queue& queue) -> const track* {
    if (queue.upcoming.empty()) {
        queue.current.reset();
        return nullptr;
    }
    queue.current = std::move(queue.upcoming.front());
    queue.upcoming.pop_front();
    return &*queue.current;
}

auto shuffle(guild_queue& queue, std::uint64_t seed) -> std::size_t {
    std::mt19937_64 generator(seed);
    std::ranges::shuffle(queue.upcoming, generator);
    return queue.upcoming.size();
}

auto remove(guild_queue& queue, std::size_t position) -> std::optional<track> {
    if (position == 0 || position > queue.upcoming.size()) return std::nullopt;
    const auto at = queue.upcoming.begin() + static_cast<std::ptrdiff_t>(position - 1);
    track removed = std::move(*at);
    queue.upcoming.erase(at);
    return removed;
}

auto clear(guild_queue& queue) -> std::size_t {
    const std::size_t removed = queue.upcoming.size();
    queue.upcoming.clear();
    return removed;
}

auto total_time(const std::deque<track>& tracks) -> running_time {
    running_time total;
    for (const track& entry : tracks) {
        if (entry.media.duration) {
            total.known += *entry.media.duration;
        } else {
            ++total.unknown;
        }
    }
    return total;
}

} // namespace latibot::music
