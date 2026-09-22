#include "core/config/bootstrap.hpp"

#include "core/util/env.hpp"
#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

using latibot::config::bootstrap;
using latibot::config::config_error;
using latibot::config::secrets;
using latibot::testing::temp_directory;
using Catch::Matchers::ContainsSubstring;

namespace {

/// Sets an environment variable for the duration of a test.
class scoped_env {
public:
    scoped_env(const char* name, const char* value) : name_(name) {
        previous_ = latibot::util::env_var(name);
        set(value);
    }

    scoped_env(const scoped_env&) = delete;
    scoped_env& operator=(const scoped_env&) = delete;

    ~scoped_env() { set(previous_ ? previous_->c_str() : nullptr); }

private:
    void set(const char* value) const { _putenv_s(name_, value != nullptr ? value : ""); }

    const char* name_;
    std::optional<std::string> previous_;
};

} // namespace

TEST_CASE("an empty config object gives the documented defaults", "[config]") {
    const bootstrap config = bootstrap::from_json("{}");

    CHECK(config.database_path == std::filesystem::path("data/bot.db"));
    CHECK(config.backups_to_keep == 7);
    CHECK(config.llm_provider == "anthropic");
    CHECK(config.llm_model == "claude-haiku-4-5");
    CHECK(config.spend_cap_monthly_usd == 20.0);
    CHECK(config.spend_cap_daily_usd == 2.0);
    CHECK(config.trusted_guilds.empty());
}

TEST_CASE("a missing config file is not an error", "[config][fs]") {
    const temp_directory temp;
    const bootstrap config = bootstrap::load(temp.file("does-not-exist.json"));
    CHECK(config.llm_model == "claude-haiku-4-5");
}

TEST_CASE("values in the file replace the defaults", "[config][fs]") {
    const temp_directory temp;
    const auto path = temp.file("config.json");
    {
        std::ofstream file(path);
        file << R"json({
            "database_path": "D:/bot/state.db",
            "backups_to_keep": 3,
            "backup_interval_minutes": 60,
            "llm_model": "claude-sonnet-5",
            "spend_cap_monthly_usd": 5.5,
            "trusted_guilds": ["123456789012345678"],
            "trusted_users": ["987654321098765432"]
        })json";
    }

    const bootstrap config = bootstrap::load(path);

    CHECK(config.database_path == std::filesystem::path("D:/bot/state.db"));
    CHECK(config.backups_to_keep == 3);
    CHECK(config.backup_interval == std::chrono::minutes{60});
    CHECK(config.llm_model == "claude-sonnet-5");
    CHECK(config.spend_cap_monthly_usd == 5.5);
    REQUIRE(config.trusted_guilds.size() == 1);
    CHECK(config.trusted_guilds.front() == dpp::snowflake{123456789012345678ULL});
    REQUIRE(config.trusted_users.size() == 1);
    CHECK(config.trusted_users.front() == dpp::snowflake{987654321098765432ULL});
}

TEST_CASE("IDs written as JSON numbers are rejected", "[config]") {
    // JSON numbers are doubles and lose precision past 2^53, so a snowflake
    // written unquoted would silently come out wrong (plan v4 §5.2).
    REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"trusted_users": [987654321098765432]})"),
                           config_error, Catch::Matchers::MessageMatches(ContainsSubstring(
                                             "cannot represent a Discord ID exactly")));
}

TEST_CASE("bad config is reported with the key that caused it", "[config]") {
    SECTION("unknown key") {
        REQUIRE_THROWS_MATCHES(
            bootstrap::from_json(R"({"databse_path": "typo.db"})"), config_error,
            Catch::Matchers::MessageMatches(ContainsSubstring("databse_path")));
    }

    SECTION("wrong type") {
        REQUIRE_THROWS_MATCHES(
            bootstrap::from_json(R"({"llm_model": 5})"), config_error,
            Catch::Matchers::MessageMatches(ContainsSubstring("llm_model")));
    }

    SECTION("out of range") {
        REQUIRE_THROWS_AS(bootstrap::from_json(R"({"llm_tool_rounds": 0})"), config_error);
        REQUIRE_THROWS_AS(bootstrap::from_json(R"({"backups_to_keep": -1})"), config_error);
    }

    SECTION("not JSON at all") {
        REQUIRE_THROWS_AS(bootstrap::from_json("not json"), config_error);
    }

    SECTION("not an object") {
        REQUIRE_THROWS_AS(bootstrap::from_json("[1, 2, 3]"), config_error);
    }

    SECTION("unknown log level names the valid ones") {
        REQUIRE_THROWS_MATCHES(
            bootstrap::from_json(R"({"log_level": "verbose"})"), config_error,
            Catch::Matchers::MessageMatches(ContainsSubstring("trace, debug, info")));
    }
}

TEST_CASE("the log level is read from the config", "[config]") {
    CHECK(bootstrap::from_json("{}").log_level == latibot::util::log_level::info);
    CHECK(bootstrap::from_json(R"({"log_level": "debug"})").log_level ==
          latibot::util::log_level::debug);
    CHECK(bootstrap::from_json(R"({"log_level": "WARN"})").log_level ==
          latibot::util::log_level::warn);
}

TEST_CASE("trust needs a listed user, or an admin in a listed server", "[config]") {
    bootstrap config;
    config.trusted_guilds = {dpp::snowflake{111}};
    config.trusted_users = {dpp::snowflake{222}};

    const dpp::snowflake trusted_guild{111};
    const dpp::snowflake other_guild{999};
    const dpp::snowflake listed_user{222};
    const dpp::snowflake stranger{333};

    SECTION("a listed user is trusted anywhere, admin or not") {
        CHECK(config.is_trusted(other_guild, listed_user, /*administrator=*/false));
    }

    SECTION("an admin in a listed server is trusted") {
        CHECK(config.is_trusted(trusted_guild, stranger, /*administrator=*/true));
    }

    SECTION("an admin in some other server is not") {
        // The point of the list: being administrator somewhere else must not
        // grant access to this host (plan v4 §2.8).
        CHECK_FALSE(config.is_trusted(other_guild, stranger, /*administrator=*/true));
    }

    SECTION("a non-admin in a listed server is not") {
        CHECK_FALSE(config.is_trusted(trusted_guild, stranger, /*administrator=*/false));
    }

    SECTION("nothing is trusted when no lists are configured") {
        const bootstrap empty;
        CHECK_FALSE(empty.is_trusted(trusted_guild, listed_user, /*administrator=*/true));
    }
}

TEST_CASE("secrets come from the environment", "[config]") {
    SECTION("the token is required") {
        const scoped_env token("DISCORD_BOT_TOKEN", nullptr);
        REQUIRE_THROWS_AS(secrets::from_environment(), config_error);
    }

    SECTION("optional keys stay unset when absent") {
        const scoped_env token("DISCORD_BOT_TOKEN", "test-token");
        const scoped_env anthropic("ANTHROPIC_API_KEY", nullptr);
        const scoped_env openai("OPENAI_API_KEY", nullptr);

        const secrets loaded = secrets::from_environment();
        CHECK(loaded.discord_token == "test-token");
        CHECK_FALSE(loaded.anthropic_key.has_value());
        CHECK_FALSE(loaded.openai_key.has_value());
    }

    SECTION("keys are picked up when present") {
        const scoped_env token("DISCORD_BOT_TOKEN", "test-token");
        const scoped_env anthropic("ANTHROPIC_API_KEY", "test-key");

        const secrets loaded = secrets::from_environment();
        REQUIRE(loaded.anthropic_key.has_value());
        CHECK(*loaded.anthropic_key == "test-key");
    }
}
