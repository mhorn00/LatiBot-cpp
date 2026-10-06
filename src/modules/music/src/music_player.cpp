#include "core/music/music_player.hpp"

#include "core/audio/pcm.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <utility>

namespace latibot::music {
namespace {

/// Samples in a second of 48 kHz stereo.
constexpr std::size_t samples_per_second = 96000;

} // namespace

auto music_marker(std::uint64_t playback) -> std::string {
    return std::format("{}{}", audio::voice_mixer::music_marker_prefix, playback);
}

auto playback_in(std::string_view marker) -> std::optional<std::uint64_t> {
    if (!marker.starts_with(audio::voice_mixer::music_marker_prefix)) return std::nullopt;
    marker.remove_prefix(audio::voice_mixer::music_marker_prefix.size());
    std::uint64_t playback = 0;
    const auto [end, error] = std::from_chars(marker.data(), marker.data() + marker.size(), playback);
    if (error != std::errc{} || end != marker.data() + marker.size()) return std::nullopt;
    return playback;
}

music_player::music_player(ports::stream_opener& opener, audio::voice_mixer& mixer, player_options options)
    : opener_(&opener), mixer_(&mixer), options_(std::move(options)) {}

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------

auto music_player::add(dpp::snowflake guild, std::vector<track> tracks, queue_position where) -> add_result {
    add_result result;
    bool started = false;
    {
        const std::scoped_lock lock(mutex_);
        guild_player& player = guilds_[guild];
        for (track& entry : tracks) {
            entry.id = next_track_id_++;
        }

        result = music::add(player.queue, std::move(tracks), where);
        if (result.replaces_current) {
            // The interrupted track's stream goes; it starts over when its
            // turn comes again. The mixer drops what it had queued below.
            player.switching = true;
        } else if (!player.queue.current && result.added > 0) {
            start_next(player.queue);
            start_playback(guild, player);
            started = true;
        } else if (player.end_sent && result.added > 0) {
            // The last track is finishing, and there is now something after
            // it to fetch ahead.
            prefetch(player);
        }
    }

    if (result.replaces_current) {
        mixer_->drop_music(guild);
        const std::scoped_lock lock(mutex_);
        guild_player& player = guilds_[guild];
        start_next(player.queue);
        start_playback(guild, player);
        player.switching = false;
        player.paused = false;
        started = true;
    }
    if (started) mixer_->wake(guild);
    return result;
}

auto music_player::skip(dpp::snowflake guild) -> std::optional<track> {
    std::optional<track> skipped;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = guilds_.find(guild);
        if (found == guilds_.end() || !found->second.queue.current) return std::nullopt;
        skipped = found->second.queue.current;
        // Nothing is read while the mixer drops what it had queued, or it
        // would drop the start of the next track with it.
        found->second.switching = true;
    }

    mixer_->drop_music(guild);
    {
        const std::scoped_lock lock(mutex_);
        guild_player& player = guilds_[guild];
        advance(player.queue, player.failed ? track_end::failed : track_end::skipped);
        start_playback(guild, player);
        player.switching = false;
        player.paused = false;
    }
    mixer_->wake(guild);
    return skipped;
}

auto music_player::toggle_pause(dpp::snowflake guild) -> std::optional<bool> {
    bool paused = false;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = guilds_.find(guild);
        if (found == guilds_.end() || !found->second.queue.current) return std::nullopt;
        found->second.paused = !found->second.paused;
        paused = found->second.paused;
    }
    if (paused) {
        mixer_->hold_music(guild);
    } else {
        mixer_->release_music(guild);
    }
    return paused;
}

auto music_player::stop(dpp::snowflake guild) -> std::size_t {
    std::size_t removed = 0;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = guilds_.find(guild);
        if (found == guilds_.end()) return 0;
        guild_player& player = found->second;
        removed = player.queue.upcoming.size() + (player.queue.current ? 1 : 0);
        player.queue.upcoming.clear();
        player.queue.current.reset();
        start_playback(guild, player);
        player.paused = false;
    }
    mixer_->drop_music(guild);
    return removed;
}

auto music_player::clear(dpp::snowflake guild) -> std::size_t {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return 0;
    found->second.prefetched.reset();
    return music::clear(found->second.queue);
}

auto music_player::shuffle(dpp::snowflake guild) -> std::size_t {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return 0;
    const std::size_t shuffled = music::shuffle(
        found->second.queue, (static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())) ^ ++shuffles_);
    if (found->second.end_sent) prefetch(found->second);
    return shuffled;
}

auto music_player::remove(dpp::snowflake guild, std::size_t position) -> std::optional<track> {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return std::nullopt;
    auto removed = music::remove(found->second.queue, position);
    if (removed && found->second.end_sent) prefetch(found->second);
    return removed;
}

auto music_player::set_repeat(dpp::snowflake guild, std::optional<repeat_mode> mode) -> repeat_mode {
    const std::scoped_lock lock(mutex_);
    guild_player& player = guilds_[guild];
    player.queue.repeat = mode.value_or(next_repeat_mode(player.queue.repeat));
    if (player.end_sent) prefetch(player);
    return player.queue.repeat;
}

auto music_player::volume_changed(dpp::snowflake guild) -> void {
    const int volume = options_.volume_percent ? options_.volume_percent(guild) : 100;
    const std::scoped_lock lock(mutex_);
    if (const auto found = guilds_.find(guild); found != guilds_.end()) found->second.volume = volume;
}

auto music_player::status(dpp::snowflake guild) -> music_status {
    // Asked before the lock: the mixer calls into the player with its own
    // lock held, so the player must never call the mixer with its own held.
    const std::size_t unplayed = mixer_->music_unplayed(guild);

    const std::scoped_lock lock(mutex_);
    music_status status;
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return status;
    const guild_player& player = found->second;

    status.current = player.queue.current;
    status.upcoming.assign(player.queue.upcoming.begin(), player.queue.upcoming.end());
    status.repeat = player.queue.repeat;
    status.paused = player.paused;
    const std::size_t heard = player.fed > unplayed ? player.fed - unplayed : 0;
    status.elapsed = std::chrono::seconds{heard / samples_per_second};
    return status;
}

auto music_player::forget(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guilds_.erase(guild);
}

// --------------------------------------------------------------------------
// Playing
// --------------------------------------------------------------------------

auto music_player::start_playback(dpp::snowflake guild, guild_player& player) -> void {
    player.fed = 0;
    player.end_sent = false;
    player.failed = false;

    if (!player.queue.current) {
        player.stream.reset();
        player.prefetched.reset();
        player.prefetched_track = 0;
        return;
    }

    const track& now = *player.queue.current;
    if (player.prefetched && player.prefetched_track == now.id) {
        player.stream = std::move(player.prefetched);
    } else {
        player.stream = opener_->open(now.media.url);
    }
    player.prefetched.reset();
    player.prefetched_track = 0;
    player.playback = next_playback_++;

    player.volume = options_.volume_percent ? options_.volume_percent(guild) : 100;
    const auto limit = options_.track_limit ? options_.track_limit(guild) : std::nullopt;
    player.limit_samples.reset();
    if (limit && !now.media.live) player.limit_samples = static_cast<std::size_t>(limit->count()) * samples_per_second;

    util::log().info("playing \"{}\" in guild {}, queued by {}", now.media.title, guild, now.requested_by);
}

auto music_player::prefetch(guild_player& player) -> void {
    const track* next = peek_next(player.queue, player.failed ? track_end::failed : track_end::finished);
    if (next == nullptr) {
        player.prefetched.reset();
        player.prefetched_track = 0;
        return;
    }
    if (player.prefetched && player.prefetched_track == next->id) return;
    player.prefetched = opener_->open(next->media.url);
    player.prefetched_track = next->id;
}

auto music_player::end_of_track(guild_player& player) -> std::string {
    player.end_sent = true;
    player.stream.reset();
    prefetch(player);
    return music_marker(player.playback);
}

auto music_player::read(dpp::snowflake guild, std::span<std::int16_t> into) -> chunk {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end() || (!found->second.queue.current && !found->second.switching)) {
        return {.samples = 0, .end_marker = {}, .idle = true};
    }
    guild_player& player = found->second;
    if (player.switching || player.paused || player.end_sent || !player.stream) return {};

    std::size_t count = player.stream->read(into);
    bool cut_off = false;
    if (player.limit_samples && player.fed + count >= *player.limit_samples) {
        count = *player.limit_samples - player.fed;
        cut_off = true;
    }
    if (count > 0) audio::apply_volume(into.subspan(0, count), player.volume);
    player.fed += count;

    const track& now = *player.queue.current;
    if (cut_off) {
        util::log().info("\"{}\" in guild {} reached the track limit", now.media.title, guild);
        if (options_.notify) {
            options_.notify(now.channel,
                            std::format("stopped **{}** at this server's {}-minute limit for a track", util::plain_text(now.media.title),
                                        std::chrono::duration_cast<std::chrono::minutes>(
                                            std::chrono::seconds{*player.limit_samples / samples_per_second})
                                            .count()));
        }
        return {.samples = count, .end_marker = end_of_track(player), .idle = false};
    }
    if (count > 0) return {.samples = count, .end_marker = {}, .idle = false};

    switch (player.stream->state()) {
    case ports::stream_state::running:
        return {};
    case ports::stream_state::finished:
        return {.samples = 0, .end_marker = end_of_track(player), .idle = false};
    case ports::stream_state::failed:
        break;
    }

    const std::string reason = player.stream->error();
    util::log().info("\"{}\" in guild {} failed: {}", now.media.title, guild, reason);
    // Both come from outside: a title, and what yt-dlp said. Posted as
    // plain text, so neither can format the message or ping anyone.
    if (options_.notify) {
        options_.notify(now.channel, std::format("couldn't play **{}**: {}", util::plain_text(now.media.title), util::plain_text(reason)));
    }
    player.failed = true;

    // Nothing of it was heard: move on at once, with no marker to wait for.
    if (player.fed == 0) {
        advance(player.queue, track_end::failed);
        start_playback(guild, player);
        return {.samples = 0, .end_marker = {}, .idle = !player.queue.current};
    }
    return {.samples = 0, .end_marker = end_of_track(player), .idle = false};
}

auto music_player::on_marker(dpp::snowflake guild, std::string_view marker) -> void {
    const auto playback = playback_in(marker);
    if (!playback) return;

    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return;
    guild_player& player = found->second;
    // A marker from a playback since skipped or restarted.
    if (player.playback != *playback || !player.end_sent) return;

    advance(player.queue, player.failed ? track_end::failed : track_end::finished);
    start_playback(guild, player);
    if (!player.queue.current) util::log().info("the music queue in guild {} has finished", guild);
}

} // namespace latibot::music
