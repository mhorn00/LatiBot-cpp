// A config section read through its table of keys
// (docs/modules/Module_Plan_Final.md §8.2).

#include "core/config/section.hpp"

#include "core/config/feature_sections.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

using Catch::Matchers::ContainsSubstring;
using latibot::config::config_error;
using latibot::config::key;
using latibot::config::section;

namespace {

struct weather_config {
    bool enabled = true;
    int interval_minutes = 30;
    std::int64_t station = 7;
    double threshold = 0.5;
    std::string units{"metric"};
    std::filesystem::path cache;
};

auto weather_section() -> const section<weather_config>& {
    static const section<weather_config> table{
        "weather",
        {
            key("enabled", &weather_config::enabled, "Whether to post the weather."),
            key("interval_minutes", &weather_config::interval_minutes, "How often.", latibot::config::range{.lowest = 5, .highest = 120}),
            key("station", &weather_config::station, "Which station.", latibot::config::at_least(1)),
            key("threshold", &weather_config::threshold, "How much rain is worth saying."),
            key("units", &weather_config::units, "metric or imperial."),
            key("cache", &weather_config::cache, "Where to keep forecasts. Empty: nowhere."),
        }};
    return table;
}

auto parsed(std::string_view text) -> nlohmann::json {
    return nlohmann::json::parse(text);
}

} // namespace

TEST_CASE("a section reads each key as its member's type, and keeps defaults for the rest", "[config]") {
    const weather_config read = weather_section().read(
        parsed(R"({"enabled": false, "interval_minutes": 60, "threshold": 2, "units": "imperial", "cache": "C:/forecasts"})"));

    CHECK_FALSE(read.enabled);
    CHECK(read.interval_minutes == 60);
    CHECK(read.station == 7);
    CHECK(read.threshold == 2.0);
    CHECK(read.units == "imperial");
    CHECK(read.cache == std::filesystem::path("C:/forecasts"));
}

TEST_CASE("a section refuses what its table does not say, naming the key in full", "[config]") {
    CHECK_THROWS_WITH(weather_section().read(parsed(R"({"colour": "blue"})")), ContainsSubstring(R"(unknown config key "weather.colour")"));
    CHECK_THROWS_WITH(weather_section().read(parsed(R"({"enabled": "yes"})")),
                      ContainsSubstring(R"("weather.enabled" must be true or false)"));
    CHECK_THROWS_WITH(weather_section().read(parsed(R"({"interval_minutes": 2.5})")),
                      ContainsSubstring(R"("weather.interval_minutes" must be a whole number)"));
    CHECK_THROWS_WITH(weather_section().read(parsed(R"({"interval_minutes": 1})")),
                      ContainsSubstring(R"("weather.interval_minutes" must be 5 to 120)"));
    CHECK_THROWS_WITH(weather_section().read(parsed(R"({"station": 0})")), ContainsSubstring(R"("weather.station" must be at least 1)"));
    CHECK_THROWS_WITH(weather_section().read(parsed(R"({"cache": 5})")), ContainsSubstring(R"("weather.cache" must be a string)"));
    CHECK_THROWS_AS(weather_section().read(parsed("[]")), config_error);
}

TEST_CASE("a section's defaults are every key, in the table's order", "[config]") {
    CHECK(weather_section().defaults().dump() ==
          R"({"enabled":true,"interval_minutes":30,"station":7,"threshold":0.5,"units":"metric","cache":""})");
    // And they read back as the defaults.
    const weather_config again = weather_section().read(nlohmann::json::parse(weather_section().defaults().dump()));
    CHECK(again.interval_minutes == 30);
    CHECK(again.units == "metric");
}

TEST_CASE("every key of every feature section says what it is for", "[config]") {
    // What a module's README lists, and later checks against.
    for (const auto& each : latibot::config::llm_section().keys()) {
        CHECK_FALSE(each.description().empty());
    }
    for (const auto& each : latibot::config::music_section().keys()) {
        CHECK_FALSE(each.description().empty());
    }
}
