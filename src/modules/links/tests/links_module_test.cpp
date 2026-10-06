// Links as a module: what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, docs/features/Url_Replacement.md).

#include "links/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"

#include "support/module_readme.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/dpp.h>

#include <filesystem>
#include <set>
#include <string>

using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

auto start_links(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::links::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("links asks for Embed Links and Manage Messages in every server", "[links]") {
    // Without Embed Links a replacement posts with no preview, which looks
    // like a broken mirror; without Manage Messages the original's preview
    // stays (docs/features/Operations.md §6).
    test_host bot;
    const module_list modules = start_links(bot);

    std::uint64_t asked = 0;
    for (const auto& [bits, purpose] : bot.wanted_permissions) {
        asked |= bits;
    }
    CHECK(asked == (dpp::p_embed_links | dpp::p_manage_messages));
    CHECK(bot.wanted_intents == 0);
}

TEST_CASE("the links README lists what the module registers", "[links]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_links(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == latibot::testing::command_names(bot.registry));
    CHECK(latibot::testing::readme_row(readme(), "Tables") == latibot::testing::table_names(bot.data, before));

    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") == std::set<std::string>(views.begin(), views.end()));
    const auto stages = bot.stages.stage_names();
    CHECK(latibot::testing::readme_row(readme(), "Message stages") == std::set<std::string>(stages.begin(), stages.end()));
    CHECK(latibot::testing::readme_row(readme(), "Discord events") == std::set<std::string>(bot.listeners.begin(), bot.listeners.end()));

    std::set<std::string> timers;
    for (const auto& each : bot.timers) {
        timers.insert(each.name);
    }
    CHECK(latibot::testing::readme_row(readme(), "Timers") == timers);
    CHECK(latibot::testing::readme_row(readme(), "Config section").empty());
    CHECK(bot.asked_sections.empty());
}
