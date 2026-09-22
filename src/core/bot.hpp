#pragma once

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/discord/dpp_gateway.hpp"
#include "core/discord/dpp_http_client.hpp"
#include "core/discord/raw_api.hpp"
#include "core/ports/clock.hpp"

#include <dpp/dpp.h>

namespace latibot {

/// Owns the Discord connection and the subsystems hanging off it.
///
/// This is the shell: it wires DPP events to core functions and holds the
/// ports features talk through. The logic itself lives outside, where it can
/// be tested without Discord (plan v4 §17.3).
class bot {
public:
    bot(config::bootstrap settings, const config::secrets& credentials);

    bot(const bot&) = delete;
    bot& operator=(const bot&) = delete;

    /// Connects and blocks until the bot shuts down.
    void run();

    [[nodiscard]] commands::registry& commands() noexcept { return commands_; }
    [[nodiscard]] db::database& database() noexcept { return database_; }
    [[nodiscard]] config::guild_settings& guild_settings() noexcept { return guild_settings_; }
    [[nodiscard]] ports::discord_gateway& gateway() noexcept { return gateway_; }
    [[nodiscard]] ports::http_client& http() noexcept { return http_; }
    [[nodiscard]] discord::raw_api& raw() noexcept { return raw_; }
    [[nodiscard]] ports::clock& clock() noexcept { return clock_; }
    [[nodiscard]] const config::bootstrap& settings() const noexcept { return settings_; }

private:
    void register_commands();
    void register_events();

    /// Warns about anything the bot cannot do in this guild. Never fatal: a
    /// missing permission disables one feature, not the bot (plan v4 §7).
    void check_permissions(const dpp::guild& guild) const;

    config::bootstrap settings_;
    db::database database_;
    config::guild_settings guild_settings_;

    dpp::cluster cluster_;
    commands::registry commands_;

    discord::dpp_gateway gateway_;
    discord::dpp_http_client http_;
    discord::raw_api raw_;
    ports::system_clock clock_;
};

} // namespace latibot
