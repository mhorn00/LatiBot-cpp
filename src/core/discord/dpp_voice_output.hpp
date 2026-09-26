#pragma once

#include "core/ports/voice_output.hpp"

namespace dpp {
class cluster;
}

namespace latibot::discord {

/// `voice_output` through DPP's voice client for the guild.
///
/// Looks the client up on every call rather than holding it: DPP owns it,
/// and replaces or deletes it whenever the connection moves or drops.
class dpp_voice_output final : public ports::voice_output {
public:
    explicit dpp_voice_output(dpp::cluster& cluster);

    [[nodiscard]] auto ready(dpp::snowflake guild) -> bool override;
    auto play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool override;
    auto skip(dpp::snowflake guild) -> void override;
    auto stop(dpp::snowflake guild) -> void override;

private:
    dpp::cluster* cluster_;
};

} // namespace latibot::discord
