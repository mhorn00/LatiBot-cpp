// The soundboard as a module: what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, src/modules/soundboard/docs/Soundboard.md).

#include "soundboard/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "soundboard_config.hpp"

#include "mocks/mock_model_tools.hpp"
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

auto start_soundboard(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::soundboard::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("the soundboard module adds /soundboard and keeps its sounds up to date", "[soundboard]") {
    test_host bot;
    const module_list modules = start_soundboard(bot);

    REQUIRE(modules.size() == 1);
    CHECK(modules.front()->name() == "soundboard");
    CHECK(bot.registry.find("soundboard") != nullptr);
    REQUIRE(bot.timers.size() == 1);
    CHECK(bot.timers.front().name == "the soundboard refresh");
    CHECK(bot.timers.front().repeats);
    // Servers arrive whatever else the bot asks for, so it needs no intent.
    CHECK(bot.wanted_intents == 0);
    // Only for playing in voice: a voice message needs nothing of its own.
    REQUIRE(bot.wanted_permissions.size() == 1);
    CHECK(bot.wanted_permissions.front().first == dpp::p_use_soundboard);
}

TEST_CASE("the soundboard README lists what the module registers", "[soundboard]") {
    // The README is how a reviewer learns what a module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_soundboard(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == latibot::testing::command_names(bot.registry));
    CHECK(latibot::testing::readme_row(readme(), "Discord events") == std::set<std::string>(bot.listeners.begin(), bot.listeners.end()));

    std::set<std::string> timers;
    for (const auto& each : bot.timers) {
        timers.insert(each.name);
    }
    CHECK(latibot::testing::readme_row(readme(), "Timers") == timers);

    std::set<std::string> keys;
    for (const auto& each : latibot::soundboard::soundboard_section().keys()) {
        keys.insert("soundboard." + std::string(each.name()));
    }
    CHECK(latibot::testing::readme_row(readme(), "Config section") == keys);

    // The rows that say "none".
    CHECK(latibot::testing::readme_row(readme(), "Tables").empty());
    CHECK(latibot::testing::table_names(bot.data, before).empty());
    CHECK(latibot::testing::readme_row(readme(), "Panels").empty());
    CHECK(bot.routes.views().empty());
    CHECK(latibot::testing::readme_row(readme(), "Message stages").empty());
    CHECK(bot.stages.size() == 0);
}

TEST_CASE("the soundboard README lists the model's tools, made only when the language model is there", "[soundboard]") {
    test_host bot;
    latibot::testing::mock_model_tools tools;
    bot.offered.offer<latibot::capabilities::model_tools>(tools, "a test");
    const module_list modules = start_soundboard(bot);
    CHECK(latibot::testing::readme_row(readme(), "Model tools") == tools.names());
}
