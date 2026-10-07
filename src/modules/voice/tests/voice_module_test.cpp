// Voice as a module: what it offers the modules that require it, and what it
// brings to the bot when it starts (docs/modules/Module_Plan_Final.md §4,
// src/modules/voice/docs/Voice_Channels.md).

#include "voice/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/module.hpp"
#include "voice/services.hpp"

#include "support/module_readme.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>

using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

auto start_voice(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::voice::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("voice offers its services to the modules that require it", "[voice]") {
    test_host bot;
    const module_list modules = start_voice(bot);

    CHECK(bot.offered.offered_by<latibot::voice::services>() == "voice");
    CHECK_NOTHROW(latibot::voice::required(bot.offered));
}

TEST_CASE("a module that requires voice, started without it, is told so", "[voice]") {
    const latibot::modules::capability_registry nothing;
    CHECK_THROWS_AS(latibot::voice::required(nothing), std::logic_error);
}

TEST_CASE("the voice README lists what the module registers", "[voice]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_voice(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == latibot::testing::command_names(bot.registry));
    CHECK(latibot::testing::readme_row(readme(), "Discord events") == std::set<std::string>(bot.listeners.begin(), bot.listeners.end()));
    std::set<std::string> timers;
    for (const auto& each : bot.timers) {
        timers.insert(each.name);
    }
    CHECK(latibot::testing::readme_row(readme(), "Timers") == timers);

    CHECK(latibot::testing::readme_row(readme(), "Tables").empty());
    CHECK(latibot::testing::table_names(bot.data, before).empty());
    CHECK(latibot::testing::readme_row(readme(), "Panels").empty());
    CHECK(bot.routes.views().empty());
    CHECK(latibot::testing::readme_row(readme(), "Message stages").empty());
    CHECK(bot.stages.size() == 0);
    CHECK(latibot::testing::readme_row(readme(), "Config section").empty());
    CHECK(bot.asked_sections.empty());
}
