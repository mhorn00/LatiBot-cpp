// Nicknames as a module: its config section, and what it brings to the bot
// when it starts (docs/modules/Module_Plan_Final.md §4, docs/features/Nicknames.md).

#include "nicknames/module.hpp"

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"
#include "nickname_command.hpp"
#include "nicknames_config.hpp"

#include "support/module_readme.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <dpp/dpp.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>

using Catch::Matchers::ContainsSubstring;
using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

auto start_nicknames(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::nicknames::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("nickname tracking is on unless the config turns it off", "[nicknames]") {
    // This is the one setting that decides which gateway intents are asked
    // for, so a wrong value is the difference between connecting and being
    // turned away (docs/features/Operations.md §3).
    const auto& section = latibot::nicknames::nicknames_section();
    CHECK(section.read(nlohmann::json::object()).track_changes);
    CHECK_FALSE(section.read(nlohmann::json::parse(R"({"track_changes": false})")).track_changes);

    CHECK_THROWS_WITH(section.read(nlohmann::json::parse(R"({"track_changes": "yes"})")), ContainsSubstring("nicknames.track_changes"));
    CHECK_THROWS_WITH(section.read(nlohmann::json::parse(R"({"track": true})")), ContainsSubstring(R"("nicknames.track")"));
    CHECK(latibot::nicknames::config_defaults().dump() == R"({"track_changes":true})");
}

TEST_CASE("tracking asks for the Server Members intent and the audit log, and listens", "[nicknames]") {
    test_host bot;
    const module_list modules = start_nicknames(bot);

    CHECK(bot.asked_sections.contains("nicknames"));
    CHECK(bot.wanted_intents == dpp::i_guild_members);
    REQUIRE(bot.wanted_permissions.size() == 1);
    CHECK(bot.wanted_permissions.front().first == dpp::p_view_audit_log);
    CHECK(bot.listeners.size() == 4);
}

TEST_CASE("with tracking off, the module asks for nothing privileged and listens to nothing", "[nicknames]") {
    // A bot that asks for an intent it was not granted is refused the
    // gateway outright, so off means not asking.
    test_host bot;
    bot.bootstrap_settings.sections["nicknames"] = {{"track_changes", false}};
    const module_list modules = start_nicknames(bot);

    CHECK(bot.wanted_intents == 0);
    CHECK(bot.wanted_permissions.empty());
    CHECK(bot.listeners.empty());
    // The commands and the history still work.
    CHECK(bot.registry.find("nickname") != nullptr);
    CHECK(bot.routes.claimed(latibot::commands::nickname_history_view));
}

TEST_CASE("a key the nicknames section does not have stops the module from starting", "[nicknames]") {
    test_host bot;
    bot.bootstrap_settings.sections["nicknames"] = {{"track_chnages", false}};

    CHECK_THROWS_WITH(start_nicknames(bot), ContainsSubstring("nicknames.track_chnages"));
}

TEST_CASE("the nicknames README lists what the module registers", "[nicknames]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_nicknames(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == latibot::testing::command_names(bot.registry));
    CHECK(latibot::testing::readme_row(readme(), "Tables") == latibot::testing::table_names(bot.data, before));

    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") == std::set<std::string>(views.begin(), views.end()));
    CHECK(latibot::testing::readme_row(readme(), "Discord events") == std::set<std::string>(bot.listeners.begin(), bot.listeners.end()));

    std::set<std::string> keys;
    for (const auto& each : latibot::nicknames::nicknames_section().keys()) {
        keys.insert("nicknames." + std::string(each.name()));
    }
    CHECK(latibot::testing::readme_row(readme(), "Config section") == keys);

    // Started now; the fallback timer only appears after a change.
    CHECK(latibot::testing::readme_row(readme(), "Timers") == std::set<std::string>{"the audit log fallback"});
    CHECK(bot.timers.empty());
    CHECK(latibot::testing::readme_row(readme(), "Message stages").empty());
    CHECK(bot.stages.size() == 0);
}
