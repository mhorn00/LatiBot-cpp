#pragma once

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/discord/raw_api.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "mocks/mock_http.hpp"
#include "support/schema.hpp"

#include <dpp/dpp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::testing {

/// The bot as a module sees it, with nothing connected
/// (docs/modules/Module_Plan_Final.md §4.2).
///
/// Everything a module registers is kept where a test can look at it or set
/// it off: its commands in `registry`, its stages in `stages`, its timers in
/// `timers` (fired with `fire`), what it posts in `posted`. The services are
/// the usual mocks, and an in-memory database with the schema applied.
class test_host final : public modules::host {
public:
    struct timer {
        std::chrono::seconds interval{};
        std::string name;
        std::function<void()> work;
        /// False for `after`'s one-shots, which `fire` removes.
        bool repeats = true;
    };

    struct detached_task {
        std::string what;
        dpp::task<void> work;
    };

    // Not named after their namespaces, which a member would hide here.
    db::database data{":memory:"};
    config::guild_settings guild_settings{data};
    config::bootstrap bootstrap_settings;
    mock_discord fake_discord;
    mock_http fake_http;
    mock_clock fake_clock;
    /// Never connected; for the few modules that reach past the ports.
    dpp::cluster bot_cluster{1U};
    discord::raw_api raw_calls{bot_cluster};
    dpp::user self;

    commands::registry registry;
    ui::panel_routes routes;
    events::pipeline stages;
    modules::capability_registry offered;

    std::vector<timer> timers;
    std::vector<std::string> listeners;
    std::uint32_t wanted_intents = 0;
    std::vector<std::pair<std::uint64_t, std::string>> wanted_permissions;
    std::vector<std::string> secrets;
    std::vector<events::send_message> posted;
    std::vector<detached_task> detached;

    /// The database as the bot leaves it for its modules: their own tables
    /// come from `modules::start_modules`.
    test_host() { create_builtin_schema(data); }

    [[nodiscard]] auto database() -> db::database& override { return data; }
    [[nodiscard]] auto settings() -> config::guild_settings& override { return guild_settings; }
    [[nodiscard]] auto bootstrap() const -> const config::bootstrap& override { return bootstrap_settings; }
    [[nodiscard]] auto section(std::string_view name) -> const nlohmann::json& override {
        static const nlohmann::json none = nlohmann::json::object();
        const auto found = bootstrap_settings.sections.find(std::string(name));
        return found != bootstrap_settings.sections.end() ? *found : none;
    }
    [[nodiscard]] auto gateway() -> ports::discord_gateway& override { return fake_discord; }
    [[nodiscard]] auto http() -> ports::http_client& override { return fake_http; }
    [[nodiscard]] auto raw() -> discord::raw_api& override { return raw_calls; }
    [[nodiscard]] auto clock() -> ports::clock& override { return fake_clock; }
    [[nodiscard]] auto cluster() -> dpp::cluster& override { return bot_cluster; }
    [[nodiscard]] auto me() const -> const dpp::user& override { return self; }
    [[nodiscard]] auto capabilities() const -> const modules::capability_registry& override { return offered; }
    [[nodiscard]] auto slash_commands() -> commands::registry& override { return registry; }
    [[nodiscard]] auto panels() -> ui::panel_routes& override { return routes; }

    auto add_stage(int position, std::string name, events::pipeline::stage_fn stage) -> void override {
        stages.add(position, std::move(name), std::move(stage));
    }
    auto every(std::chrono::seconds interval, std::string name, std::function<void()> work) -> void override {
        timers.push_back({.interval = interval, .name = std::move(name), .work = std::move(work), .repeats = true});
    }
    auto after(std::chrono::seconds delay, std::string name, std::function<void()> work) -> void override {
        timers.push_back({.interval = delay, .name = std::move(name), .work = std::move(work), .repeats = false});
    }
    auto intents(std::uint32_t wanted) -> void override { wanted_intents |= wanted; }
    auto permission(std::uint64_t bits, std::string purpose) -> void override { wanted_permissions.emplace_back(bits, std::move(purpose)); }
    auto secret(std::string value) -> void override { secrets.push_back(std::move(value)); }
    auto post(events::send_message message) -> void override { posted.push_back(std::move(message)); }
    auto detach(dpp::task<void> work, std::string what) -> void override {
        detached.push_back({.what = std::move(what), .work = std::move(work)});
    }

    /// Runs every timer named `name`, as if its time had come, and says how
    /// many there were. A one-shot runs once and is gone.
    auto fire(std::string_view name) -> int {
        // Copied out first: a timer's work may add another.
        std::vector<timer> due;
        for (const timer& each : timers) {
            if (each.name == name) due.push_back(each);
        }
        std::erase_if(timers, [name](const timer& each) { return each.name == name && !each.repeats; });
        for (const timer& each : due) {
            each.work();
        }
        return static_cast<int>(due.size());
    }

protected:
    auto note_listener(std::string_view name) -> void override { listeners.emplace_back(name); }
};

} // namespace latibot::testing
