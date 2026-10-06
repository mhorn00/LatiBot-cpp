#include "voice/voice_mixer.hpp"

#include <algorithm>
#include <utility>

namespace latibot::audio {
namespace {

/// Samples in a millisecond of 48 kHz stereo.
constexpr std::size_t samples_per_ms = 96;

/// Queued audio, as samples, to the nearest whole packet: DPP queues whole
/// 20 ms packets, and adds up their durations in floating point.
auto queued_samples(std::chrono::milliseconds queued) -> std::size_t {
    if (queued.count() <= 0) return 0;
    const auto packets = static_cast<std::size_t>((queued.count() + 10) / 20);
    return packets * voice_mixer::packet_samples;
}

} // namespace

voice_mixer::voice_mixer(ports::voice_output& connection, std::chrono::milliseconds lookahead)
    : connection_(&connection),
      lookahead_samples_(std::max<std::size_t>(static_cast<std::size_t>(lookahead.count()) * samples_per_ms / packet_samples, 1) *
                         packet_samples) {}

auto voice_mixer::set_music(music_source* source) -> void {
    const std::scoped_lock lock(mutex_);
    music_ = source;
}

// --------------------------------------------------------------------------
// Speech
// --------------------------------------------------------------------------

auto voice_mixer::ready(dpp::snowflake guild) -> bool {
    return connection_->ready(guild);
}

auto voice_mixer::play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool {
    const std::scoped_lock lock(mutex_);
    guild_mix& mix = guilds_[guild];

    // Speech already queued means the connection holds no music: the first
    // utterance took it back.
    if (mix.speech.empty()) rewind(guild, mix);

    if (!connection_->play(guild, audio, marker)) return false;
    if (!marker.empty()) mix.speech.push_back(marker);
    return true;
}

auto voice_mixer::skip(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end() || found->second.speech.empty()) return;

    // The connection holds only speech, so this drops exactly the utterance
    // playing, and its marker, which will therefore never be reported.
    connection_->skip(guild);
    found->second.speech.pop_front();
    if (found->second.speech.empty()) feed(guild, found->second);
}

auto voice_mixer::stop(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end() || found->second.speech.empty()) return;

    connection_->stop(guild);
    found->second.speech.clear();
    feed(guild, found->second);
}

auto voice_mixer::remaining(dpp::snowflake guild) -> std::chrono::milliseconds {
    return connection_->remaining(guild);
}

// --------------------------------------------------------------------------
// Music
// --------------------------------------------------------------------------

auto voice_mixer::wake(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guild_mix& mix = guilds_[guild];
    mix.active = true;
    feed(guild, mix);
}

auto voice_mixer::drop_music(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return;
    guild_mix& mix = found->second;

    // With speech queued, the music was already taken back: the connection
    // holds speech, which is not this call's to clear.
    if (mix.speech.empty() && !mix.history.empty()) connection_->stop(guild);
    mix.history.clear();
    mix.history_samples = 0;
    mix.replay.clear();
    mix.carry.clear();
    mix.pending_markers.clear();
    mix.held = false;
}

auto voice_mixer::hold_music(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guild_mix& mix = guilds_[guild];
    mix.held = true;
    if (mix.speech.empty()) rewind(guild, mix);
}

auto voice_mixer::release_music(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guild_mix& mix = guilds_[guild];
    mix.held = false;
    feed(guild, mix);
}

auto voice_mixer::music_unplayed(dpp::snowflake guild) -> std::size_t {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return 0;
    const guild_mix& mix = found->second;

    std::size_t unplayed = mix.carry.size();
    for (const entry& taken_back : mix.replay) {
        unplayed += taken_back.samples.size();
    }
    if (mix.speech.empty() && !mix.history.empty()) unplayed += queued_samples(connection_->remaining(guild));
    return unplayed;
}

// --------------------------------------------------------------------------
// Events
// --------------------------------------------------------------------------

auto voice_mixer::tick() -> void {
    const std::scoped_lock lock(mutex_);
    for (auto& [guild, mix] : guilds_) {
        if (mix.active) feed(guild, mix);
    }
}

auto voice_mixer::on_marker(dpp::snowflake guild, std::string_view marker) -> void {
    const bool is_music = marker.starts_with(music_marker_prefix);
    music_source* music = nullptr;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = guilds_.find(guild);
        if (found == guilds_.end()) return;
        guild_mix& mix = found->second;

        if (!is_music) {
            // Everything up to this utterance has been heard. An utterance
            // skipped before its marker came was taken off already.
            const auto passed = std::ranges::find(mix.speech, marker);
            if (passed == mix.speech.end()) return;
            mix.speech.erase(mix.speech.begin(), passed + 1);
            if (mix.speech.empty()) feed(guild, mix);
            return;
        }

        const auto pending = mix.pending_markers.find(marker);
        if (pending == mix.pending_markers.end()) return; // dropped by a skip
        mix.pending_markers.erase(pending);
        music = music_;
    }

    // Outside the lock: the source moves on to its next track, and may tell
    // the mixer so.
    if (music != nullptr) music->on_marker(guild, marker);

    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found != guilds_.end()) feed(guild, found->second);
}

auto voice_mixer::on_ready(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return;
    guild_mix& mix = found->second;

    // Whatever the old connection held is gone, and its markers will never
    // be reported. Speech waiting for this connection is queued again by the
    // speech queue. A track's end marker must still come, or the music
    // would never move on, so the ones still due are sent again; the few
    // seconds of music queued before them are lost.
    mix.speech.clear();
    for (auto it = mix.history.rbegin(); it != mix.history.rend(); ++it) {
        if (!it->marker.empty() && mix.pending_markers.contains(it->marker)) mix.replay.push_front({.samples = {}, .marker = it->marker});
    }
    mix.history.clear();
    mix.history_samples = 0;
    feed(guild, mix);
}

auto voice_mixer::forget(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guilds_.erase(guild);
}

// --------------------------------------------------------------------------
// Feeding and taking back
// --------------------------------------------------------------------------

auto voice_mixer::send(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t> samples, const std::string& marker) -> bool {
    if (samples.empty() && marker.empty()) return true;
    if (!connection_->play(guild, samples, marker)) return false;

    if (!samples.empty()) {
        mix.history_samples += samples.size();
        mix.history.push_back({.samples = std::move(samples), .marker = {}});
    }
    if (!marker.empty()) {
        mix.history.push_back({.samples = {}, .marker = marker});
        mix.pending_markers.insert(marker);
    }

    // What has certainly been heard is of no more use: the connection never
    // holds more than the lookahead, so twice that is plenty to keep.
    while (mix.history.size() > 1 && mix.history_samples - mix.history.front().samples.size() >= 2 * lookahead_samples_) {
        mix.history_samples -= mix.history.front().samples.size();
        mix.history.pop_front();
    }
    return true;
}

auto voice_mixer::flush(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t>& staged, const std::string& marker) -> void {
    if (marker.empty()) {
        // Whole packets only; the rest waits for more.
        const std::size_t whole = staged.size() - (staged.size() % packet_samples);
        std::vector<std::int16_t> rest(staged.begin() + static_cast<std::ptrdiff_t>(whole), staged.end());
        staged.resize(whole);
        send(guild, mix, std::move(staged), {});
        staged = std::move(rest);
        return;
    }
    // All of it, padded to a whole packet, since a marker has to follow: a
    // track's end is heard as a few milliseconds of silence at most.
    if (const std::size_t partial = staged.size() % packet_samples; partial != 0) {
        staged.resize(staged.size() + packet_samples - partial);
    }
    send(guild, mix, std::move(staged), marker);
    staged.clear();
}

auto voice_mixer::stage_replay(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t>& staged, std::size_t wanted) -> std::size_t {
    std::size_t taken = 0;
    while (!mix.replay.empty() && taken < wanted) {
        entry next = std::move(mix.replay.front());
        mix.replay.pop_front();
        if (!next.marker.empty()) {
            flush(guild, mix, staged, next.marker);
            continue;
        }
        const std::size_t take = std::min(wanted - taken, next.samples.size());
        staged.insert(staged.end(), next.samples.begin(), next.samples.begin() + static_cast<std::ptrdiff_t>(take));
        taken += take;
        if (take < next.samples.size()) {
            next.samples.erase(next.samples.begin(), next.samples.begin() + static_cast<std::ptrdiff_t>(take));
            mix.replay.push_front(std::move(next));
        }
    }
    return taken;
}

auto voice_mixer::stage_source(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t>& staged, std::size_t wanted) -> void {
    std::size_t taken = mix.carry.size();
    staged.insert(staged.end(), mix.carry.begin(), mix.carry.end());
    mix.carry.clear();

    std::vector<std::int16_t> buffer;
    while (taken < wanted) {
        buffer.resize(wanted - taken);
        const music_source::chunk chunk = music_->read(guild, buffer);
        staged.insert(staged.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(chunk.samples));
        taken += chunk.samples;
        if (!chunk.end_marker.empty()) {
            flush(guild, mix, staged, chunk.end_marker);
            continue;
        }
        if (chunk.samples == 0) {
            if (chunk.idle && staged.empty()) mix.active = false;
            return;
        }
    }
}

auto voice_mixer::feed(dpp::snowflake guild, guild_mix& mix) -> void {
    if (music_ == nullptr || mix.held || !mix.speech.empty() || !connection_->ready(guild)) return;

    const std::size_t queued = mix.history.empty() ? 0 : queued_samples(connection_->remaining(guild));
    if (queued >= lookahead_samples_) return;
    const std::size_t wanted = lookahead_samples_ - queued;

    // First, music taken back by speech or a pause: it is older than
    // anything still to be read. Then what was read last time but did not
    // fill a packet, then the source itself.
    std::vector<std::int16_t> staged;
    const std::size_t taken = stage_replay(guild, mix, staged, wanted);
    if (mix.replay.empty() && taken < wanted) stage_source(guild, mix, staged, wanted - taken);

    flush(guild, mix, staged, {});
    mix.carry.insert(mix.carry.begin(), staged.begin(), staged.end());
}

auto voice_mixer::rewind(dpp::snowflake guild, guild_mix& mix) -> void {
    if (mix.history.empty()) return;

    // The connection's queue is the newest end of what was sent. Walk back
    // from the end until as much audio as is still queued has been taken,
    // and keep every marker in it, and any at the very end not yet heard.
    std::size_t needed = queued_samples(connection_->remaining(guild));
    std::deque<entry> unheard;
    while (!mix.history.empty()) {
        entry& last = mix.history.back();
        if (!last.marker.empty()) {
            if (needed > 0 || mix.pending_markers.contains(last.marker)) unheard.push_front(std::move(last));
            mix.history.pop_back();
            continue;
        }
        if (needed == 0) break;
        const std::size_t take = std::min(needed, last.samples.size());
        unheard.push_front({.samples = {last.samples.end() - static_cast<std::ptrdiff_t>(take), last.samples.end()}, .marker = {}});
        needed -= take;
        mix.history.pop_back();
    }
    mix.history.clear();
    mix.history_samples = 0;

    for (auto it = unheard.rbegin(); it != unheard.rend(); ++it) {
        mix.replay.push_front(std::move(*it));
    }
    connection_->stop(guild);
}

} // namespace latibot::audio
