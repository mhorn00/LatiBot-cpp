#pragma once

#include "core/commands/registry.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>

namespace latibot::config {
class guild_settings;
}

namespace latibot::events {
class voice_sessions;
}

namespace latibot::commands {

/// The bounds `/voice grace` accepts.
inline constexpr std::int64_t max_voice_grace_seconds = 600;

/// How long the bot waits alone in a voice channel in this guild before
/// leaving, with anything out of range clamped.
[[nodiscard]] auto voice_grace_for(const config::guild_settings& settings, dpp::snowflake guild) -> std::chrono::seconds;

/// Starts and ends voice sessions (plan §13).
class voice_command final : public command {
public:
    voice_command(events::voice_sessions& sessions, config::guild_settings& settings);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto start(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto stop(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto grace(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::voice_sessions* sessions_;
    config::guild_settings* settings_;
};

} // namespace latibot::commands
