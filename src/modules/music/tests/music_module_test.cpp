// Music as a module: its config section, the sign-in it reads, and what it
// brings to the bot when it starts (docs/modules/Module_Plan_Final.md §4,
// docs/features/Music.md).

#include "music/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/module.hpp"
#include "core/ui/panel_routes.hpp"
#include "music_config.hpp"
#include "voice/module.hpp"

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

/// Voice first: music requires it, and plays through its mixer.
auto start_music(test_host& bot) -> module_list {
    return latibot::modules::start_modules(
        [](host& with) {
            module_list made;
            made.push_back(latibot::voice::make_module(with));
            made.push_back(latibot::music::make_module(with));
            return made;
        },
        bot, bot.offered);
}

auto readme() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_MODULE_DIR) / "README.md";
}

auto read(std::string_view text) -> latibot::music::music_config {
    return latibot::music::music_section().read(nlohmann::json::parse(text));
}

} // namespace

TEST_CASE("music's programs are looked for unless the config names them", "[music]") {
    const auto defaults = read("{}");
    CHECK(defaults.deno_path.empty());
    CHECK(read(R"({"deno_path": "C:/tools/deno.exe"})").deno_path == std::filesystem::path("C:/tools/deno.exe"));
    CHECK_THROWS_WITH(read(R"({"deno_path": 5})"), ContainsSubstring("music.deno_path"));
    CHECK_THROWS_WITH(read(R"({"ytdlp": "x"})"), ContainsSubstring(R"("music.ytdlp")"));

    CHECK(defaults.pot_provider_path.empty());
    CHECK(defaults.pot_provider_port == 4416);
    const auto provider = read(R"({"pot_provider_path": "C:/bgutil/server", "pot_provider_port": 8080})");
    CHECK(provider.pot_provider_path == std::filesystem::path("C:/bgutil/server"));
    CHECK(provider.pot_provider_port == 8080);
    CHECK_THROWS_WITH(read(R"({"pot_provider_port": 0})"), ContainsSubstring("must be 1 to 65535"));
    CHECK_THROWS_AS(read(R"({"pot_provider_port": 70000})"), latibot::config::config_error);
}

TEST_CASE("the account yt-dlp signs in as comes from the environment", "[music]") {
    SECTION("nothing named") {
        const scoped_env cookies("LATIBOT_YTDLP_COOKIES", nullptr);
        const scoped_env profile("LATIBOT_YTDLP_FIREFOX_PROFILE", "");
        const auto named = latibot::music::sign_in_from_environment();
        CHECK_FALSE(named.cookies.has_value());
        CHECK_FALSE(named.firefox_profile.has_value());
    }
    SECTION("a cookies file") {
        const scoped_env cookies("LATIBOT_YTDLP_COOKIES", "data/youtube-cookies.txt");
        const scoped_env profile("LATIBOT_YTDLP_FIREFOX_PROFILE", nullptr);
        CHECK(latibot::music::sign_in_from_environment().cookies == std::filesystem::path("data/youtube-cookies.txt"));
    }
    SECTION("a Firefox profile") {
        const scoped_env cookies("LATIBOT_YTDLP_COOKIES", nullptr);
        const scoped_env profile("LATIBOT_YTDLP_FIREFOX_PROFILE", "data/firefox-profile");
        CHECK(latibot::music::sign_in_from_environment().firefox_profile == std::filesystem::path("data/firefox-profile"));
    }
}

TEST_CASE("the music README lists what the module registers", "[music]") {
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
    const module_list modules = start_music(bot);

    std::set<std::string> commands;
    for (const std::string& each : latibot::testing::command_names(bot.registry)) {
        if (!voice_commands.contains(each)) commands.insert(each);
    }
    CHECK(latibot::testing::readme_row(readme(), "Commands") == commands);
    const auto views = bot.routes.views();
    CHECK(latibot::testing::readme_row(readme(), "Panels") == std::set<std::string>(views.begin(), views.end()));

    std::set<std::string> keys;
    for (const auto& each : latibot::music::music_section().keys()) {
        keys.insert("music." + std::string(each.name()));
    }
    CHECK(latibot::testing::readme_row(readme(), "Config section") == keys);
    CHECK(bot.asked_sections.contains("music"));

    CHECK(latibot::testing::readme_row(readme(), "Tables").empty());
    CHECK(latibot::testing::table_names(bot.data, before).empty());
    CHECK(bot.listeners.size() == voice_only.listeners.size());
    CHECK(bot.timers.size() == voice_only.timers.size());
    CHECK(latibot::testing::readme_row(readme(), "Discord events").empty());
    CHECK(latibot::testing::readme_row(readme(), "Timers").empty());
    CHECK(latibot::testing::readme_row(readme(), "Message stages").empty());
}
