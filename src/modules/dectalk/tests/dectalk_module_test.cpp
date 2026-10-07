// DECtalk as a module: the speech it offers, and what it brings to the bot
// when it starts (docs/modules/Module_Plan_Final.md §4, §5.3,
// src/modules/dectalk/docs/Speech.md).

#include "dectalk/module.hpp"

#include "core/capabilities/speech.hpp"
#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"
#include "voice/module.hpp"

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

/// Voice first: dectalk requires it, and finds its mixer in its own offer.
auto start_dectalk(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::voice::make_module(with));
            made.push_back(latibot::dectalk::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("dectalk offers the speech the language model speaks through", "[dectalk]") {
    test_host bot;
    const module_list modules = start_dectalk(bot);

    CHECK(bot.offered.offered_by<latibot::capabilities::speech>() == "dectalk");
    REQUIRE(bot.offered.find<latibot::capabilities::speech>() != nullptr);
    // Nobody is in a voice session yet, so nothing is spoken.
    CHECK_FALSE(bot.offered.find<latibot::capabilities::speech>()->speaks_in(dpp::snowflake{1000}, dpp::snowflake{3000}));
}

TEST_CASE("the dectalk README lists what the module registers", "[dectalk]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    // Voice is started with it, so what voice added is set aside.
    test_host voice_only;
    const module_list voice = latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::voice::make_module(with));
            return made;
        },
        voice_only, voice_only.offered);
    const auto voice_commands = latibot::testing::command_names(voice_only.registry);

    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_dectalk(bot);

    std::set<std::string> commands;
    for (const std::string& each : latibot::testing::command_names(bot.registry)) {
        if (!voice_commands.contains(each)) commands.insert(each);
    }
    CHECK(latibot::testing::readme_row(readme(), "Commands") == commands);
    CHECK(latibot::testing::readme_row(readme(), "Tables") == latibot::testing::table_names(bot.data, before));
    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") == std::set<std::string>(views.begin(), views.end()));

    // Voice's listeners and timers, and none of its own.
    CHECK(bot.listeners.size() == voice_only.listeners.size());
    CHECK(bot.timers.size() == voice_only.timers.size());
    CHECK(latibot::testing::readme_row(readme(), "Discord events").empty());
    CHECK(latibot::testing::readme_row(readme(), "Timers").empty());
    CHECK(latibot::testing::readme_row(readme(), "Message stages").empty());
    CHECK(latibot::testing::readme_row(readme(), "Config section").empty());
}
