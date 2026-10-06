// Midnight as a module: what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, docs/features/Midnight.md).

#include "core/events/midnight_module.hpp"

#include "core/commands/registry.hpp"
#include "core/db/schema_versions.hpp"
#include "core/events/midnight.hpp"
#include "core/modules/module.hpp"

#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <utility>

using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

auto start_midnight(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::events::make_midnight_module(with));
            return made;
        },
        bot, bot.offered);
}

auto version_of(latibot::db::database& db, std::string_view module) -> int {
    for (const auto& [name, version] : latibot::db::recorded_versions(db)) {
        if (name == module) return version;
    }
    return 0;
}

} // namespace

TEST_CASE("the midnight module creates its own table when it is built", "[module]") {
    // The host creates the core's tables; a module's are its own.
    test_host bot;
    REQUIRE(version_of(bot.data, "midnight") == 0);

    const module_list modules = start_midnight(bot);

    REQUIRE(modules.size() == 1);
    CHECK(modules.front()->name() == "midnight");
    CHECK(version_of(bot.data, "midnight") == 1);
    CHECK(latibot::events::midnight_store(bot.data).enabled().empty());
}

TEST_CASE("the midnight module adds /midnight and checks the clock every thirty seconds", "[module]") {
    test_host bot;
    const module_list modules = start_midnight(bot);

    CHECK(bot.registry.find("midnight") != nullptr);
    REQUIRE(bot.timers.size() == 1);
    CHECK(bot.timers.front().name == "the midnight tick");
    CHECK(bot.timers.front().interval == latibot::events::midnight_tick);
    CHECK(bot.timers.front().repeats);
    // Nothing else: no stage, no listener, no intent.
    CHECK(bot.stages.size() == 0);
    CHECK(bot.listeners.empty());
    CHECK(bot.wanted_intents == 0);
}

TEST_CASE("the midnight tick posts what is due through the host", "[module]") {
    test_host bot;
    const module_list modules = start_midnight(bot);

    latibot::events::midnight_store store(bot.data);
    store.add({.id = 0,
               .guild_id = dpp::snowflake{1},
               .channel_id = dpp::snowflake{2},
               .timezone = "UTC",
               .message = "it is a new day",
               .enabled = true,
               .message_flags = dpp::m_suppress_notifications,
               .last_fired_date = "2026-09-22"});
    bot.fake_clock.set(std::chrono::sys_days{std::chrono::year{2026} / 9 / 23} + std::chrono::seconds{30});

    CHECK(bot.fire("the midnight tick") == 1);
    REQUIRE(bot.posted.size() == 1);
    CHECK(bot.posted.front().channel_id == dpp::snowflake{2});
    CHECK(bot.posted.front().content == "it is a new day");

    // Once a day.
    CHECK(bot.fire("the midnight tick") == 1);
    CHECK(bot.posted.size() == 1);
}
