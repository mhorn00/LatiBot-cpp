#pragma once

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/discord/dpp_gateway.hpp"
#include "core/discord/dpp_http_client.hpp"
#include "core/discord/raw_api.hpp"
#include "core/events/bot_allowlist.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/events/triggers.hpp"
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
    void register_stages();
    void register_events();

    /// Warns about anything the bot cannot do in this guild. Never fatal: a
    /// missing permission disables one feature, not the bot (plan v4 §7).
    void check_permissions(const dpp::guild& guild) const;

    /// Turns a DPP message into the plain struct the stages work on, which is
    /// where the Administrator check happens.
    [[nodiscard]] events::incoming_message describe(const dpp::message& message) const;

    /// Performs what the stages decided.
    void carry_out(const std::vector<events::action>& actions);

    /// Buttons and select menus. `chosen` is the select menu's value, empty
    /// for a button. Both arrive here because a panel mixes the two and the
    /// custom_id says what to do either way; the id is passed separately
    /// because DPP puts it on each event type rather than on their base.
    void on_component(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen);

    /// Modal submissions.
    void on_form(const dpp::form_submit_t& event);

    /// Applies one change to a trigger from the panel and logs what happened.
    /// `change` returns the past-tense verb for the log, so the two toggles
    /// differ only in the field they flip.
    void toggle_trigger(std::int64_t id, dpp::snowflake guild, std::string_view who,
                        const std::function<std::string_view(events::trigger&)>& change);

    config::bootstrap settings_;
    db::database database_;
    config::guild_settings guild_settings_;

    dpp::cluster cluster_;
    commands::registry commands_;

    discord::dpp_gateway gateway_;
    discord::dpp_http_client http_;
    discord::raw_api raw_;
    ports::system_clock clock_;

    events::bot_allowlist bot_allowlist_;
    events::trigger_store triggers_;
    events::trigger_responder trigger_responder_;
    events::pipeline pipeline_;
};

} // namespace latibot
