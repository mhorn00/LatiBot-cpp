// The language model as a module: its section, its keys, the speech it
// finds or does not, and what it brings to the bot when it starts
// (docs/modules/Module_Plan_Final.md §4, src/modules/llm/docs/Language_Model.md).

#include "llm/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"
#include "llm_config.hpp"
#include "llm_module.hpp"

#include "mocks/mock_speech.hpp"
#include "support/module_readme.hpp"
#include "support/scoped_env.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <set>
#include <string>

using Catch::Matchers::ContainsSubstring;
using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::scoped_env;
using latibot::testing::test_host;

namespace {

auto start_llm(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::llm::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

} // namespace

TEST_CASE("the llm section reads its defaults, and the spend caps cannot be negative", "[llm]") {
    const auto& section = latibot::llm::llm_section();
    const auto defaults = section.read(nlohmann::json::object());
    CHECK(defaults.provider == "anthropic");
    CHECK(defaults.model == "claude-haiku-4-5");
    CHECK(defaults.spend_cap_daily_usd == 2.0);
    CHECK(defaults.spend_cap_monthly_usd == 20.0);
    CHECK(defaults.tool_rounds == 4);

    CHECK_THROWS_AS(section.read(nlohmann::json::parse(R"({"spend_cap_daily_usd": -1})")), latibot::config::config_error);
    CHECK_THROWS_AS(section.read(nlohmann::json::parse(R"({"spend_cap_monthly_usd": -1})")), latibot::config::config_error);
    CHECK_THROWS_WITH(section.read(nlohmann::json::parse(R"({"tool_rounds": 0})")),
                      ContainsSubstring("llm.tool_rounds\" must be at least 1"));
}

TEST_CASE("a model the bot cannot price stops the module from starting", "[llm]") {
    // Before anything connects, as a bad config.json key does
    // (src/modules/llm/docs/Language_Model.md §3.2).
    test_host bot;
    bot.bootstrap_settings.sections["llm"] = {{"model", "claude-3-opus"}};

    CHECK_THROWS_WITH(start_llm(bot), ContainsSubstring("claude-3-opus"));
}

TEST_CASE("the providers' keys come from the environment, and are masked in the log", "[llm]") {
    SECTION("absent, or empty, is no key") {
        const scoped_env anthropic("ANTHROPIC_API_KEY", nullptr);
        const scoped_env openai("OPENAI_API_KEY", "");
        const auto keys = latibot::llm::keys_from_environment();
        CHECK_FALSE(keys.anthropic.has_value());
        CHECK_FALSE(keys.openai.has_value());
    }

    SECTION("a key that is set is read, and handed to the log channel to mask") {
        const scoped_env anthropic("ANTHROPIC_API_KEY", "test-key-anthropic");
        const scoped_env openai("OPENAI_API_KEY", nullptr);
        REQUIRE(latibot::llm::keys_from_environment().anthropic == "test-key-anthropic");

        test_host bot;
        const module_list modules = start_llm(bot);
        CHECK(bot.secrets == std::vector<std::string>{"test-key-anthropic"});
    }
}

TEST_CASE("the model speaks only when someone offers speech", "[llm]") {
    // Without dectalk the capability is null and the model never speaks; the
    // module starts either way (docs/modules/Module_Plan_Final.md §5.3).
    SECTION("nobody offers it") {
        test_host bot;
        CHECK_NOTHROW(start_llm(bot));
    }
    SECTION("dectalk, or a stand-in, offers it") {
        test_host bot;
        latibot::testing::mock_speech speech;
        bot.offered.offer<latibot::capabilities::speech>(speech, "a test");
        CHECK_NOTHROW(start_llm(bot));
    }
}

TEST_CASE("the llm README lists what the module registers", "[llm]") {
    // The README is how a reviewer learns what the module adds
    // (docs/modules/Module_Plan_Final.md §11), so it cannot fall behind.
    test_host bot;
    const auto before = latibot::testing::table_names(bot.data);
    const module_list modules = start_llm(bot);

    CHECK(latibot::testing::readme_row(readme(), "Commands") == latibot::testing::command_names(bot.registry));

    // The full-text index's own tables are SQLite's, not the module's.
    std::set<std::string> tables;
    for (const std::string& each : latibot::testing::table_names(bot.data, before)) {
        if (!each.starts_with("llm_memory_search_")) tables.insert(each);
    }
    CHECK(latibot::testing::readme_row(readme(), "Tables") == tables);

    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") == std::set<std::string>(views.begin(), views.end()));
    const auto stages = bot.stages.stage_names();
    CHECK(latibot::testing::readme_row(readme(), "Message stages") == std::set<std::string>(stages.begin(), stages.end()));

    std::set<std::string> keys;
    for (const auto& each : latibot::llm::llm_section().keys()) {
        keys.insert("llm." + std::string(each.name()));
    }
    CHECK(latibot::testing::readme_row(readme(), "Config section") == keys);

    CHECK(latibot::testing::readme_row(readme(), "Discord events").empty());
    CHECK(bot.listeners.empty());
    CHECK(latibot::testing::readme_row(readme(), "Timers").empty());
    CHECK(bot.timers.empty());
}
