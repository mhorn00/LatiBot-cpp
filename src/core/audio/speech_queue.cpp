#include "core/audio/speech_queue.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace latibot::audio {
namespace {

constexpr std::string_view marker_prefix = "tts:";

/// The utterance id a marker names, or nothing for a marker that is not ours.
auto id_in(std::string_view marker) -> std::optional<std::uint64_t> {
    if (!marker.starts_with(marker_prefix)) return std::nullopt;
    marker.remove_prefix(marker_prefix.size());

    std::uint64_t id = 0;
    const auto [end, error] = std::from_chars(marker.data(), marker.data() + marker.size(), id);
    if (error != std::errc{} || end != marker.data() + marker.size()) return std::nullopt;
    return id;
}

} // namespace

speech_queue::speech_queue(ports::voice_output& output) : output_(&output) {}

auto speech_queue::marker_for(std::uint64_t id) -> std::string {
    return std::format("{}{}", marker_prefix, id);
}

auto speech_queue::ticket(dpp::snowflake guild) -> std::uint64_t {
    const std::scoped_lock lock(mutex_);
    return guilds_[guild].generation;
}

auto speech_queue::enqueue(dpp::snowflake guild, dpp::snowflake owner, std::uint64_t ticket, std::vector<std::int16_t> audio)
    -> speech_outcome {
    // Held while the connection encodes the audio, so a stop cannot land
    // between the check below and the audio being queued.
    const std::scoped_lock lock(mutex_);
    guild_speech& speech = guilds_[guild];
    if (ticket != speech.generation) return speech_outcome::stopped;

    const std::uint64_t id = next_id_++;

    // Behind anything already waiting, so the order asked is the order heard.
    if (speech.waiting.empty() && output_->ready(guild) && output_->play(guild, audio, marker_for(id))) {
        speech.playing.push_back({.id = id, .owner = owner});
        return speech_outcome::playing;
    }
    speech.waiting.push_back({.id = id, .owner = owner, .audio = std::move(audio)});
    return speech_outcome::waiting;
}

auto speech_queue::on_ready(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return;

    guild_speech& speech = found->second;
    while (!speech.waiting.empty()) {
        waiting_utterance& next = speech.waiting.front();
        if (!output_->play(guild, next.audio, marker_for(next.id))) return;
        speech.playing.push_back({.id = next.id, .owner = next.owner});
        speech.waiting.pop_front();
    }
}

auto speech_queue::on_marker(dpp::snowflake guild, std::string_view marker) -> void {
    const auto id = id_in(marker);
    if (!id) return;

    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end()) return;

    // Ids only grow, so everything up to this one has finished too. One that
    // is not there was skipped before its marker was reported.
    auto& playing = found->second.playing;
    const auto finished = std::ranges::find(playing, *id, &utterance::id);
    if (finished != playing.end()) playing.erase(playing.begin(), finished + 1);
}

auto speech_queue::skip(dpp::snowflake guild) -> bool {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end() || found->second.playing.empty()) return false;

    output_->skip(guild);
    found->second.playing.pop_front();
    return true;
}

auto speech_queue::stop(dpp::snowflake guild) -> std::size_t {
    const std::scoped_lock lock(mutex_);
    guild_speech& speech = guilds_[guild];
    const std::size_t dropped = speech.playing.size() + speech.waiting.size();

    output_->stop(guild);
    speech.playing.clear();
    speech.waiting.clear();
    ++speech.generation;
    return dropped;
}

auto speech_queue::forget(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guild_speech& speech = guilds_[guild];
    speech.playing.clear();
    speech.waiting.clear();
    ++speech.generation;
}

auto speech_queue::current_owner(dpp::snowflake guild) const -> std::optional<dpp::snowflake> {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    if (found == guilds_.end() || found->second.playing.empty()) return std::nullopt;
    return found->second.playing.front().owner;
}

auto speech_queue::size(dpp::snowflake guild) const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    const auto found = guilds_.find(guild);
    return found == guilds_.end() ? 0 : found->second.playing.size() + found->second.waiting.size();
}

} // namespace latibot::audio
