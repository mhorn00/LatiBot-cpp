// Triggers as a module: what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, src/modules/triggers/docs/Triggers.md).

#include "triggers/module.hpp"

#include "core/commands/registry.hpp"
#include "core/events/stage_order.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"

#include "support/module_readme.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <variant>

using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

auto start_triggers(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::triggers::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("a trigger added to the database is answered by the module's stage", "[triggers]") {
    // The stage the module adds is the one the pipeline runs: a message
    // matching a trigger gets its reply.
    test_host bot;
    const module_list modules = start_triggers(bot);
    bot.data.execute(
        "INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled) VALUES (1000, '420', 'whole_word', 30, 1);"
        "INSERT INTO trigger_responses (trigger_id, response, weight) VALUES (last_insert_rowid(), 'nice', 1);");

    latibot::events::incoming_message message;
    message.guild_id = dpp::snowflake{1000};
    message.channel_id = dpp::snowflake{3000};
    message.author_id = dpp::snowflake{11};
    message.content = "that's 420";

    const auto actions = bot.stages.run(message);
    REQUIRE(actions.size() == 1);
    const auto* reply = std::get_if<latibot::events::send_message>(&actions.front());
    REQUIRE(reply != nullptr);
    CHECK(reply->content == "nice");
}

TEST_CASE("the triggers README lists what the module registers", "[triggers]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_triggers(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == latibot::testing::command_names(bot.registry));
    CHECK(latibot::testing::readme_row(readme(), "Tables") == latibot::testing::table_names(bot.data, before));

    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") == std::set<std::string>(views.begin(), views.end()));
    const auto stages = bot.stages.stage_names();
    CHECK(latibot::testing::readme_row(readme(), "Message stages") == std::set<std::string>(stages.begin(), stages.end()));

    CHECK(latibot::testing::readme_row(readme(), "Discord events").empty());
    CHECK(bot.listeners.empty());
    CHECK(latibot::testing::readme_row(readme(), "Timers").empty());
    CHECK(bot.timers.empty());
    CHECK(latibot::testing::readme_row(readme(), "Config section").empty());
    CHECK(bot.asked_sections.empty());
}
