#include "core/config/command_line.hpp"
#include "core/config/bootstrap.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string_view>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using latibot::config::command_line;
using latibot::config::config_error;

namespace {

auto parse(std::vector<std::string_view> arguments) -> command_line {
    return command_line::parse(arguments);
}

} // namespace

TEST_CASE("no arguments run the bot with config.json", "[config]") {
    const command_line parsed = parse({});

    CHECK(parsed.config_path == std::filesystem::path("config.json"));
    CHECK_FALSE(parsed.unregister_commands);
}

TEST_CASE("a lone argument is the config file", "[config]") {
    const command_line parsed = parse({"other.json"});

    CHECK(parsed.config_path == std::filesystem::path("other.json"));
    CHECK_FALSE(parsed.unregister_commands);
}

TEST_CASE("the unregister flag goes before or after the config file", "[config]") {
    const command_line first = parse({"--unregister-commands"});
    CHECK(first.unregister_commands);
    CHECK(first.config_path == std::filesystem::path("config.json"));

    const command_line before = parse({"--unregister-commands", "other.json"});
    CHECK(before.unregister_commands);
    CHECK(before.config_path == std::filesystem::path("other.json"));

    const command_line after = parse({"other.json", "--unregister-commands"});
    CHECK(after.unregister_commands);
    CHECK(after.config_path == std::filesystem::path("other.json"));
}

TEST_CASE("an unknown option is refused rather than read as a config file", "[config]") {
    CHECK_THROWS_MATCHES(parse({"--unregister"}), config_error, Catch::Matchers::MessageMatches(ContainsSubstring("--unregister")));
    CHECK_THROWS_MATCHES(parse({"-h"}), config_error, Catch::Matchers::MessageMatches(ContainsSubstring("usage")));
}

TEST_CASE("two config files are refused", "[config]") {
    CHECK_THROWS_MATCHES(parse({"a.json", "b.json"}), config_error,
                         Catch::Matchers::MessageMatches(ContainsSubstring("a.json") && ContainsSubstring("b.json")));
}
