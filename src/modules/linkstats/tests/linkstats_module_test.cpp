// Linkstats as a module: its config section, and what it brings to the bot
// when it starts (docs/modules/Module_Plan_Final.md §4, docs/features/Link_Stats.md).

#include "linkstats/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"
#include "links/module.hpp"
#include "linkstats_config.hpp"

#include "support/module_readme.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <set>
#include <string>

using Catch::Matchers::ContainsSubstring;
using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

/// Links first: linkstats requires it, and its tables refer to links'.
auto start_linkstats(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::links::make_module(with));
            made.push_back(latibot::linkstats::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

/// What only linkstats added: everything but the names in `links_had`.
auto without(const std::set<std::string>& all, const std::set<std::string>& links_had) -> std::set<std::string> {
    std::set<std::string> rest;
    for (const std::string& each : all) {
        if (!links_had.contains(each)) rest.insert(each);
    }
    return rest;
}

} // namespace

TEST_CASE("emoji copies are kept for every emote used, unless the config says otherwise", "[linkstats]") {
    const auto& section = latibot::linkstats::linkstats_section();
    CHECK(section.read(nlohmann::json::object()).emoji_copy_min_uses == 1);
    CHECK(section.read(nlohmann::json::parse(R"({"emoji_copy_min_uses": 5})")).emoji_copy_min_uses == 5);
    // Nought turns copying off.
    CHECK(section.read(nlohmann::json::parse(R"({"emoji_copy_min_uses": 0})")).emoji_copy_min_uses == 0);
    CHECK_THROWS_WITH(section.read(nlohmann::json::parse(R"({"emoji_copy_min_uses": -1})")),
                      ContainsSubstring("linkstats.emoji_copy_min_uses"));
    CHECK(latibot::linkstats::config_defaults().dump() == R"({"emoji_copy_min_uses":1})");
}

TEST_CASE("with copying off, there is no copying timer", "[linkstats]") {
    test_host bot;
    bot.bootstrap_settings.sections["linkstats"] = {{"emoji_copy_min_uses", 0}};
    const module_list modules = start_linkstats(bot);

    for (const auto& each : bot.timers) {
        CHECK(each.name != "copying emojis");
    }
}

TEST_CASE("the linkstats README lists what the module registers", "[linkstats]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    // Links is started with it, so what links added is set aside.
    test_host links_only;
    const auto core_tables = latibot::testing::table_names(links_only.data);
    const module_list links = latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::links::make_module(with));
            return made;
        },
        links_only, links_only.offered);
    const auto links_tables = latibot::testing::table_names(links_only.data, core_tables);
    const auto links_listeners = std::set<std::string>(links_only.listeners.begin(), links_only.listeners.end());
    const auto links_views = links_only.routes.views();
    const auto links_commands = latibot::testing::command_names(links_only.registry);

    test_host bot;
    const module_list modules = start_linkstats(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == without(latibot::testing::command_names(bot.registry), links_commands));
    std::set<std::string> before = core_tables;
    before.insert(links_tables.begin(), links_tables.end());
    CHECK(latibot::testing::readme_row(readme(), "Tables") == latibot::testing::table_names(bot.data, before));

    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") ==
          without(std::set<std::string>(views.begin(), views.end()), std::set<std::string>(links_views.begin(), links_views.end())));
    CHECK(latibot::testing::readme_row(readme(), "Discord events") ==
          without(std::set<std::string>(bot.listeners.begin(), bot.listeners.end()), links_listeners));

    std::set<std::string> timers;
    for (const auto& each : bot.timers) {
        timers.insert(each.name);
    }
    CHECK(latibot::testing::readme_row(readme(), "Timers") == without(timers, {"the preview tracker's tick"}));

    std::set<std::string> keys;
    for (const auto& each : latibot::linkstats::linkstats_section().keys()) {
        keys.insert("linkstats." + std::string(each.name()));
    }
    CHECK(latibot::testing::readme_row(readme(), "Config section") == keys);
    CHECK(latibot::testing::readme_row(readme(), "Message stages").empty());
}
