// Links as a module: what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, src/modules/links/docs/Url_Replacement.md).

#include "links/module.hpp"

#include "core/capabilities/link_replacements.hpp"
#include "core/commands/registry.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"
#include "links/replacements.hpp"

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
    // stays (src/core/docs/Operations.md §6).
    test_host bot;
    const module_list modules = start_links(bot);

    std::uint64_t asked = 0;
    for (const auto& [bits, purpose] : bot.wanted_permissions) {
        asked |= bits;
    }
    CHECK(asked == (dpp::p_embed_links | dpp::p_manage_messages));
    CHECK(bot.wanted_intents == 0);
}

TEST_CASE("links tells the language model which messages are replacements", "[links]") {
    // A reply to one comments on the post, and is not for the model
    // (src/modules/llm/docs/Language_Model.md §2.1).
    test_host bot;
    const module_list modules = start_links(bot);
    const auto* replacements = bot.offered.find<latibot::capabilities::link_replacements>();
    REQUIRE(replacements != nullptr);

    latibot::events::replacement_store store(bot.database());
    store.record({.message_id = dpp::snowflake{700},
                  .guild_id = dpp::snowflake{1},
                  .channel_id = dpp::snowflake{2},
                  .original_message_id = dpp::snowflake{699},
                  .original_author_id = dpp::snowflake{3},
                  .state = latibot::events::replacement_state::ok,
                  .created_at = std::chrono::sys_seconds{std::chrono::days{20000}},
                  .retried_at = std::nullopt,
                  .links = {}});
    CHECK(replacements->is_replacement(dpp::snowflake{700}));
    CHECK_FALSE(replacements->is_replacement(dpp::snowflake{699}));
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
