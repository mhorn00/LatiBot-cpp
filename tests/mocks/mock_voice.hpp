#pragma once

#include "core/ports/voice_output.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace latibot::testing {

/// Stands in for a guild's voice connection: records what was played, and
/// keeps each guild's queue of audio and markers in order, as DPP does, so a
/// test can play it forward and see which markers pass.
class mock_voice final : public ports::voice_output {
public:
    /// 48 kHz stereo: samples in one millisecond.
    static constexpr std::size_t samples_per_ms = 96;

    struct played {
        dpp::snowflake guild;
        std::size_t samples = 0;
        std::string marker;
    };

    /// One entry in a connection's queue: audio, or a marker after it.
    struct queued_entry {
        std::vector<std::int16_t> audio;
        std::string marker;
    };

    /// Guilds with a connection ready for audio.
    std::map<dpp::snowflake, bool> connected;

    std::vector<played> plays;
    int skips = 0;
    int stops = 0;

    /// What each guild's connection still has queued, as markers in order.
    std::map<dpp::snowflake, std::vector<std::string>> queued;

    /// What each guild's connection still has queued, audio included.
    std::map<dpp::snowflake, std::deque<queued_entry>> buffer;

    /// Every sample each guild has heard, in the order it was played.
    std::map<dpp::snowflake, std::vector<std::int16_t>> heard;

    [[nodiscard]] auto ready(dpp::snowflake guild) -> bool override {
        const auto found = connected.find(guild);
        return found != connected.end() && found->second;
    }

    auto play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool override {
        if (!ready(guild)) return false;
        plays.push_back({.guild = guild, .samples = audio.size(), .marker = marker});
        if (!audio.empty()) buffer[guild].push_back({.audio = {audio.begin(), audio.end()}, .marker = {}});
        if (!marker.empty()) {
            buffer[guild].push_back({.audio = {}, .marker = marker});
            queued[guild].push_back(marker);
        }
        return true;
    }

    auto skip(dpp::snowflake guild) -> void override {
        ++skips;
        auto& markers = queued[guild];
        if (!markers.empty()) markers.erase(markers.begin());
        auto& entries = buffer[guild];
        while (!entries.empty()) {
            const bool was_marker = !entries.front().marker.empty();
            entries.pop_front();
            if (was_marker) break;
        }
    }

    auto stop(dpp::snowflake guild) -> void override {
        ++stops;
        queued[guild].clear();
        buffer[guild].clear();
    }

    [[nodiscard]] auto remaining(dpp::snowflake guild) -> std::chrono::milliseconds override {
        std::size_t samples = 0;
        for (const queued_entry& entry : buffer[guild]) {
            samples += entry.audio.size();
        }
        return std::chrono::milliseconds{samples / samples_per_ms};
    }

    /// Plays `span` of a guild's queue, as DPP's send loop would: what is
    /// heard is recorded, and the markers passed are returned in order. A
    /// marker at the front is passed once the audio before it has played.
    auto advance(dpp::snowflake guild, std::chrono::milliseconds span) -> std::vector<std::string> {
        std::vector<std::string> passed;
        std::size_t budget = static_cast<std::size_t>(span.count()) * samples_per_ms;
        auto& entries = buffer[guild];
        while (!entries.empty()) {
            queued_entry& front = entries.front();
            if (!front.marker.empty()) {
                passed.push_back(front.marker);
                auto& markers = queued[guild];
                if (!markers.empty() && markers.front() == front.marker) markers.erase(markers.begin());
                entries.pop_front();
                continue;
            }
            if (budget == 0) break;
            const std::size_t take = std::min(budget, front.audio.size());
            auto& ear = heard[guild];
            ear.insert(ear.end(), front.audio.begin(), front.audio.begin() + static_cast<std::ptrdiff_t>(take));
            front.audio.erase(front.audio.begin(), front.audio.begin() + static_cast<std::ptrdiff_t>(take));
            budget -= take;
            if (front.audio.empty()) entries.pop_front();
        }
        return passed;
    }
};

} // namespace latibot::testing
