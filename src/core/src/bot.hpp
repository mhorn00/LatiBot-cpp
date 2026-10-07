#pragma once

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/discord/raw_api.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/modules/module.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/paginator.hpp"
#include "core/ui/panel_routes.hpp"
#include "discord/dpp_gateway.hpp"
#include "discord/dpp_http_client.hpp"
#include "events/bot_allowlist.hpp"
#include "events/log_channel.hpp"

#include <dpp/dpp.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace latibot {

/// Owns the Discord connection and the subsystems hanging off it.
///
/// This is the shell: it wires DPP events to core functions and holds the
/// ports features talk through. The logic itself lives outside, where it can
/// be tested without Discord.
///
/// It is also the modules' host (docs/modules/Module_Plan_Final.md §4): it
/// builds them with `make_modules`, has each offer its capabilities and then
/// start, and only then connects. The features not yet moved into a module
/// are still its own members.
class bot final : public modules::host {
public:
    bot(config::bootstrap settings, const config::secrets& credentials, const modules::module_factory& make_modules);

    bot(const bot&) = delete;
    auto operator=(const bot&) -> bot& = delete;

    /// Connects and blocks until the bot shuts down.
    auto run() -> void;

    // modules::host
    [[nodiscard]] auto database() -> db::database& override { return database_; }
    [[nodiscard]] auto settings() -> config::guild_settings& override { return guild_settings_; }
    [[nodiscard]] auto bootstrap() const -> const config::bootstrap& override { return settings_; }
    [[nodiscard]] auto section(std::string_view name) -> const nlohmann::json& override;
    [[nodiscard]] auto gateway() -> ports::discord_gateway& override { return gateway_; }
    [[nodiscard]] auto http() -> ports::http_client& override { return http_; }
    [[nodiscard]] auto raw() -> discord::raw_api& override { return raw_; }
    [[nodiscard]] auto clock() -> ports::clock& override { return clock_; }
    [[nodiscard]] auto cluster() -> dpp::cluster& override { return cluster_; }
    [[nodiscard]] auto me() const -> const dpp::user& override { return cluster_.me; }
    [[nodiscard]] auto capabilities() const -> const modules::capability_registry& override { return capabilities_; }
    [[nodiscard]] auto slash_commands() -> commands::registry& override { return commands_; }
    [[nodiscard]] auto panels() -> ui::panel_routes& override { return panels_; }
    auto add_stage(int position, std::string name, events::pipeline::stage_fn stage) -> void override;
    auto every(std::chrono::seconds interval, std::string name, std::function<void()> work) -> void override;
    auto after(std::chrono::seconds delay, std::string name, std::function<void()> work) -> void override;
    auto intents(std::uint32_t wanted) -> void override { module_intents_ |= wanted; }
    auto permission(std::uint64_t bits, std::string purpose) -> void override;
    auto secret(std::string value) -> void override { log_channel_.add_secret(std::move(value)); }
    auto post(events::send_message message) -> void override;
    auto detach(dpp::task<void> work, std::string what) -> void override;

protected:
    auto note_listener(std::string_view name) -> void override { listeners_.emplace_back(name); }

private:
    /// Builds the modules, has each offer, then start, and logs what they
    /// added (docs/modules/Module_Plan_Final.md §4.3).
    auto start_modules(const modules::module_factory& make_modules) -> void;

    auto register_commands() -> void;
    auto register_stages() -> void;
    auto register_events() -> void;

    /// Connected, or reconnected: puts the last `/status` back, and registers
    /// the commands the first time.
    auto on_ready(const dpp::ready_t& event) -> void;

    /// Starts the core's timers: the log channel's and the database backups
    /// (src/core/docs/Operations.md §2). Modules start their own.
    auto register_timers() -> void;

    /// Warns about anything the bot cannot do in this guild. Never fatal: a
    /// missing permission disables one feature, not the bot
    /// (src/core/docs/Operations.md §6).
    auto check_permissions(const dpp::guild& guild) const -> void;

    /// Turns a DPP message into the plain struct the stages work on, which is
    /// where the Administrator check happens. `raw_event` is the gateway
    /// frame, the only place a reply says whose message it replies to.
    [[nodiscard]] auto describe(const dpp::message& message, const std::string& raw_event) const -> events::incoming_message;

    /// Performs what the stages decided.
    auto carry_out(std::vector<events::action> actions) -> void;

    /// Buttons and select menus. `chosen` is the select menu's value, empty
    /// for a button. Both arrive here because a panel mixes the two and the
    /// custom_id says what to do either way; the id is passed separately
    /// because DPP puts it on each event type rather than on their base.
    auto on_component(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen) -> void;

    /// Does what a decoded component asks. False when no panel claims it,
    /// which `on_component` answers.
    auto route_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// Modal submissions.
    auto on_form(const dpp::form_submit_t& event) -> void;

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
    events::pipeline pipeline_;

    /// Where the log is posted (`/logs`). Destroyed before everything above
    /// it, and unhooked from the logger as it goes, so a line logged while
    /// the rest shuts down reaches the console and nothing else.
    events::log_destination_store log_destinations_;
    events::log_channel log_channel_;

    // What the modules registered (docs/modules/Module_Plan_Final.md §4.2).
    // The routes and the capabilities point into the modules, which are
    // destroyed first.
    modules::capability_registry capabilities_;
    ui::panel_routes panels_;
    std::uint32_t module_intents_ = 0;
    /// Permissions modules asked for, checked with the commands' in each
    /// server.
    std::vector<std::pair<std::uint64_t, std::string>> module_permissions_;
    /// Who listens to which DPP event, for the startup log.
    std::vector<std::string> listeners_;
    /// The config.json sections modules asked for; any other is warned about.
    std::set<std::string, std::less<>> claimed_sections_;

    /// The modules, in dependency order. After everything they were given,
    /// so they are destroyed before any of it.
    modules::module_list modules_;

    /// The pause between the goodbye and shutting down. Last, so it is
    /// joined first when the bot is destroyed, while the cluster it shuts
    /// down still exists.
    std::jthread goodbye_;
};

} // namespace latibot
