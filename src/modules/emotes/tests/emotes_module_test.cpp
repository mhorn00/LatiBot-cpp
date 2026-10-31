// Emotes as a module: what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, src/modules/emotes/docs/Steal_Emote.md).

#include "emotes/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"

#include "support/module_readme.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>

using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

TEST_CASE("the emotes README lists what the module registers", "[emotes]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    const std::filesystem::path readme = std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::emotes::make_module(with));
            return made;
        },
        bot, bot.offered);

    CHECK(latibot::testing::readme_row(readme, "Commands") == latibot::testing::command_names(bot.registry));
    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme, "Panels") == std::set<std::string>(views.begin(), views.end()));

    // The rows that say "none".
    CHECK(latibot::testing::readme_row(readme, "Message stages").empty());
    CHECK(bot.stages.size() == 0);
    CHECK(latibot::testing::readme_row(readme, "Discord events").empty());
    CHECK(bot.listeners.empty());
    CHECK(latibot::testing::readme_row(readme, "Timers").empty());
    CHECK(bot.timers.empty());
    CHECK(latibot::testing::readme_row(readme, "Tables").empty());
    CHECK(latibot::testing::table_names(bot.data, before).empty());
    CHECK(latibot::testing::readme_row(readme, "Config section").empty());
    CHECK(bot.asked_sections.empty());
    CHECK(bot.wanted_intents == 0);
}
