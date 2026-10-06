#pragma once

#include "core/events/message_pipeline.hpp"

#include <dpp/dpp.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace latibot::commands {
class registry;
}
namespace latibot::config {
struct bootstrap;
class guild_settings;
} // namespace latibot::config
namespace latibot::db {
class database;
}
namespace latibot::discord {
class raw_api;
}
namespace latibot::ports {
class clock;
class discord_gateway;
class http_client;
} // namespace latibot::ports
namespace latibot::ui {
class panel_routes;
}

namespace latibot::module {

class capability_registry;

/// Logs what a listener or timer threw, naming it. Out of line, so this
/// header needs no logger.
auto report_failure(std::string_view what, const std::exception* error) -> void;

/// What the core gives a module (docs/modules/Module_Plan_Final.md §4.2).
///
/// The bot is the host. A module keeps the services it needs as pointers,
/// which live as long as the bot does, and registers everything else from
/// `module::start`. Registering is only for startup: nothing here may be
/// called once the bot has connected.
class host {
public:
    virtual ~host() = default;

    host() = default;
    host(const host&) = delete;
    auto operator=(const host&) -> host& = delete;

    // ----------------------------------------------------------------------
    // Services
    // ----------------------------------------------------------------------

    [[nodiscard]] virtual auto database() -> db::database& = 0;
    /// Per-server settings.
    [[nodiscard]] virtual auto settings() -> latibot::config::guild_settings& = 0;
    /// config.json's core keys. Not `config()`: in the bot, which is the
    /// host, that name would hide the `config` namespace.
    [[nodiscard]] virtual auto bootstrap() const -> const latibot::config::bootstrap& = 0;
    [[nodiscard]] virtual auto gateway() -> ports::discord_gateway& = 0;
    [[nodiscard]] virtual auto http() -> ports::http_client& = 0;
    [[nodiscard]] virtual auto raw() -> discord::raw_api& = 0;
    [[nodiscard]] virtual auto clock() -> ports::clock& = 0;
    /// DPP itself, for what the ports do not cover. Not connected until
    /// every module has started.
    [[nodiscard]] virtual auto cluster() -> dpp::cluster& = 0;
    /// The bot's own user; empty until it connects.
    [[nodiscard]] virtual auto me() const -> const dpp::user& = 0;
    /// What the modules offer each other. Complete by the time any module
    /// starts.
    [[nodiscard]] virtual auto capabilities() const -> const capability_registry& = 0;

    // ----------------------------------------------------------------------
    // Registering
    // ----------------------------------------------------------------------

    /// Where a module adds its slash commands. Not `commands()`, for the same
    /// reason as `bootstrap()`.
    [[nodiscard]] virtual auto slash_commands() -> latibot::commands::registry& = 0;
    [[nodiscard]] virtual auto panels() -> ui::panel_routes& = 0;

    /// A message stage, at one of `events::stage_order`'s positions.
    virtual auto add_stage(int position, std::string name, events::pipeline::stage_fn stage) -> void = 0;

    /// Listens to a DPP event, such as `cluster().on_message_reaction_add`.
    /// What `listener` throws is logged under `name` rather than reaching
    /// DPP, and the startup log lists who listens to what.
    template <typename Event, typename Listener>
    auto listen(dpp::event_router_t<Event>& router, std::string name, Listener listener) -> void {
        note_listener(name);
        router([name = std::move(name), listener = std::move(listener)](const Event& event) {
            try {
                listener(event);
            } catch (const std::exception& error) {
                report_failure(name, &error);
            } catch (...) {
                report_failure(name, nullptr);
            }
        });
    }

    /// Runs `work` every `interval`, from when the bot starts. What it
    /// throws is logged under `name`, and the timer carries on.
    virtual auto every(std::chrono::seconds interval, std::string name, std::function<void()> work) -> void = 0;

    /// Runs `work` once, `delay` from now. May be called at any time, not
    /// only while starting.
    virtual auto after(std::chrono::seconds delay, std::string name, std::function<void()> work) -> void = 0;

    // ----------------------------------------------------------------------
    // Startup checks
    // ----------------------------------------------------------------------

    /// Gateway intents the module needs, added to the core's before the bot
    /// connects.
    virtual auto intents(std::uint32_t wanted) -> void = 0;

    /// Permissions the module needs in every server for something other than
    /// a command, which each server is checked for when it connects
    /// (docs/features/Operations.md §6).
    virtual auto permission(std::uint64_t bits, std::string purpose) -> void = 0;

    /// A value the log channel masks wherever it appears: an API key, a
    /// password.
    virtual auto secret(std::string value) -> void = 0;

    // ----------------------------------------------------------------------
    // Carrying out
    // ----------------------------------------------------------------------

    /// Posts a message, logging whether Discord took it.
    virtual auto post(events::send_message message) -> void = 0;

    /// Runs a coroutine to the end with nobody waiting on it, logging under
    /// `what` if it throws.
    virtual auto detach(dpp::task<void> work, std::string what) -> void = 0;

protected:
    /// Remembers that `name` listens to something, for the startup log.
    virtual auto note_listener(std::string_view name) -> void = 0;
};

} // namespace latibot::module
