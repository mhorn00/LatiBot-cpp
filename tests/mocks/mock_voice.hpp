#pragma once

#include "core/ports/voice_output.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace latibot::testing {

/// Stands in for a guild's voice connection: records what was played and
/// keeps each guild's queue of markers, as DPP does.
class mock_voice final : public ports::voice_output {
public:
    struct played {
        dpp::snowflake guild;
        std::size_t samples = 0;
        std::string marker;
    };

    /// Guilds with a connection ready for audio.
    std::map<dpp::snowflake, bool> connected;

    std::vector<played> plays;
    int skips = 0;
    int stops = 0;

    /// What each guild's connection still has queued, as markers in order.
    std::map<dpp::snowflake, std::vector<std::string>> queued;

    [[nodiscard]] auto ready(dpp::snowflake guild) -> bool override {
        const auto found = connected.find(guild);
        return found != connected.end() && found->second;
    }

    auto play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool override {
        if (!ready(guild)) return false;
        plays.push_back({.guild = guild, .samples = audio.size(), .marker = marker});
        queued[guild].push_back(marker);
        return true;
    }

    auto skip(dpp::snowflake guild) -> void override {
        ++skips;
        auto& markers = queued[guild];
        if (!markers.empty()) markers.erase(markers.begin());
    }

    auto stop(dpp::snowflake guild) -> void override {
        ++stops;
        queued[guild].clear();
    }
};

} // namespace latibot::testing
