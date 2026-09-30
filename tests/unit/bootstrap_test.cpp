#include "core/config/bootstrap.hpp"

#include "core/util/env.hpp"
#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

using Catch::Matchers::ContainsSubstring;
using latibot::config::bootstrap;
using latibot::config::config_error;
using latibot::config::secrets;
using latibot::testing::temp_directory;

namespace {

/// A whole file as text, or empty when it cannot be read.
auto read_file(const std::filesystem::path& path) -> std::string {
    const std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

/// Sets an environment variable for the duration of a test.
class scoped_env {
public:
    scoped_env(const char* name, const char* value) : name_(name) {
        previous_ = latibot::util::env_var(name);
        set(value);
    }

    scoped_env(const scoped_env&) = delete;
    auto operator=(const scoped_env&) -> scoped_env& = delete;

    ~scoped_env() { set(previous_ ? previous_->c_str() : nullptr); }

private:
    auto set(const char* value) const -> void { _putenv_s(name_, value != nullptr ? value : ""); }

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

TEST_CASE("a missing config file is written with the defaults", "[config][fs]") {
    // A release is one executable, with no example beside it to copy, so the
    // first run leaves a file to edit.
    const temp_directory temp;
    const auto path = temp.file("config.json");

    const bootstrap config = bootstrap::load(path);

    CHECK(config.llm_model == "claude-haiku-4-5");
    CHECK(read_file(path) == bootstrap::default_json());
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
    REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"trusted_users": [987654321098765432]})"), config_error,
                           Catch::Matchers::MessageMatches(ContainsSubstring("cannot represent a Discord ID exactly")));
}

TEST_CASE("a trusted ID that is not exactly an ID stops startup", "[config]") {
    // These lists gate what the bot lets people do to its host (plan §12.5),
    // so a mistyped ID must not load as some other number: "12345abc" as
    // 12345, or "-1" wrapped round to the largest 64-bit number.
    for (const char* bad : {"12345abc", "-1", "0", ""}) {
        INFO(bad);
        const std::string guilds = std::string(R"({"trusted_guilds": [")") + bad + R"("]})";
        REQUIRE_THROWS_MATCHES(bootstrap::from_json(guilds), config_error,
                               Catch::Matchers::MessageMatches(ContainsSubstring("which is not a Discord ID")));
        const std::string users = std::string(R"({"trusted_users": [")") + bad + R"("]})";
        REQUIRE_THROWS_AS(bootstrap::from_json(users), config_error);
    }
}

TEST_CASE("bad config is reported with the key that caused it", "[config]") {
    SECTION("unknown key") {
        REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"databse_path": "typo.db"})"), config_error,
                               Catch::Matchers::MessageMatches(ContainsSubstring("databse_path")));
    }

    SECTION("wrong type") {
        REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"llm_model": 5})"), config_error,
                               Catch::Matchers::MessageMatches(ContainsSubstring("llm_model")));
    }

    SECTION("out of range") {
        REQUIRE_THROWS_AS(bootstrap::from_json(R"({"llm_tool_rounds": 0})"), config_error);
        REQUIRE_THROWS_AS(bootstrap::from_json(R"({"backups_to_keep": -1})"), config_error);
        REQUIRE_THROWS_AS(bootstrap::from_json(R"({"emoji_copy_min_uses": -1})"), config_error);
    }

    SECTION("not JSON at all") {
        REQUIRE_THROWS_AS(bootstrap::from_json("not json"), config_error);
    }

    SECTION("not an object") {
        REQUIRE_THROWS_AS(bootstrap::from_json("[1, 2, 3]"), config_error);
    }

    SECTION("unknown log level names the valid ones") {
        REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"log_level": "verbose"})"), config_error,
                               Catch::Matchers::MessageMatches(ContainsSubstring("trace, debug, info")));
    }
}

TEST_CASE("the model has to be one the bot can price, from the provider named", "[config]") {
    // The spend caps are worked out from each model's price, so a model the
    // bot has no price for would spend without being counted (plan §14.6).
    REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"llm_model": "claude-3-opus"})"), config_error,
                           Catch::Matchers::MessageMatches(ContainsSubstring("claude-haiku-4-5")));
    REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"llm_provider": "mistral"})"), config_error,
                           Catch::Matchers::MessageMatches(ContainsSubstring("anthropic, openai")));
    REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"llm_model": "gpt-6-luna"})"), config_error,
                           Catch::Matchers::MessageMatches(ContainsSubstring("from openai")));
    REQUIRE_THROWS_AS(bootstrap::from_json(R"({"spend_cap_daily_usd": -1})"), config_error);

    const bootstrap openai = bootstrap::from_json(R"({"llm_provider": "openai", "llm_model": "gpt-6-luna"})");
    CHECK(openai.llm_model == "gpt-6-luna");
}

TEST_CASE("emoji copies are kept for every emote used, unless the config says otherwise", "[config]") {
    CHECK(bootstrap::from_json("{}").emoji_copy_min_uses == 1);
    CHECK(bootstrap::from_json(R"({"emoji_copy_min_uses": 5})").emoji_copy_min_uses == 5);
    // Nought turns copying off.
    CHECK(bootstrap::from_json(R"({"emoji_copy_min_uses": 0})").emoji_copy_min_uses == 0);
}

TEST_CASE("nickname tracking is on unless the config turns it off", "[config]") {
    // This is the one setting that decides which gateway intents are asked
    // for, so a wrong value is the difference between connecting and being
    // turned away (plan v4 §8).
    CHECK(bootstrap::from_json("{}").track_nicknames);
    CHECK_FALSE(bootstrap::from_json(R"({"track_nicknames": false})").track_nicknames);

    REQUIRE_THROWS_MATCHES(bootstrap::from_json(R"({"track_nicknames": "yes"})"), config_error,
                           Catch::Matchers::MessageMatches(ContainsSubstring("track_nicknames")));
}

TEST_CASE("the log level is read from the config", "[config]") {
    // With nothing configured the build decides: a debug build is being
    // diagnosed, a release build is being used by other people.
    CHECK(bootstrap::from_json("{}").log_level == latibot::util::default_log_level);
    CHECK(bootstrap::from_json(R"({"log_level": "debug"})").log_level == latibot::util::log_level::debug);
    CHECK(bootstrap::from_json(R"({"log_level": "WARN"})").log_level == latibot::util::log_level::warn);
}

TEST_CASE("the build's default log level matches the build", "[config]") {
#ifdef NDEBUG
    CHECK(latibot::util::default_log_level == latibot::util::log_level::info);
#else
    CHECK(latibot::util::default_log_level == latibot::util::log_level::debug);
#endif
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
        // Where to put it, for an install that is only the executable.
        CHECK_THROWS_WITH(secrets::from_environment(), ContainsSubstring(".env") && ContainsSubstring("DISCORD_BOT_TOKEN="));
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

TEST_CASE("the recompute bot override is read by debug builds only", "[config]") {
    using latibot::config::recompute_bot_id_from_environment;

    SECTION("unset or empty means the bot's own account") {
        const scoped_env unset("LATIBOT_DEBUG_RECOMPUTE_BOT_ID", nullptr);
        CHECK_FALSE(recompute_bot_id_from_environment(true).has_value());
    }

    SECTION("a debug build takes the account named") {
        const scoped_env set("LATIBOT_DEBUG_RECOMPUTE_BOT_ID", "123456789012345678");
        CHECK(recompute_bot_id_from_environment(true) == dpp::snowflake{123456789012345678});
    }

    SECTION("a release build ignores it") {
        const scoped_env set("LATIBOT_DEBUG_RECOMPUTE_BOT_ID", "123456789012345678");
        CHECK_FALSE(recompute_bot_id_from_environment(false).has_value());
    }

    SECTION("a debug build refuses something that is not an ID") {
        // Falling back to the bot's own account would read the wrong history
        // and report nothing found, which looks like a bug in the backfill.
        const scoped_env set("LATIBOT_DEBUG_RECOMPUTE_BOT_ID", "not-an-id");
        CHECK_THROWS_WITH(recompute_bot_id_from_environment(true), ContainsSubstring("LATIBOT_DEBUG_RECOMPUTE_BOT_ID"));
        CHECK_FALSE(recompute_bot_id_from_environment(false).has_value());
    }
}

TEST_CASE("loading the configuration applies the recompute bot override as the build allows", "[config][fs]") {
    const scoped_env set("LATIBOT_DEBUG_RECOMPUTE_BOT_ID", "123456789012345678");
    const temp_directory folder;
    const auto loaded = bootstrap::load(folder.path() / "missing.json");

    if (latibot::config::reads_debug_overrides) {
        CHECK(loaded.recompute_bot_id == dpp::snowflake{123456789012345678});
    } else {
        CHECK_FALSE(loaded.recompute_bot_id.has_value());
    }
}

TEST_CASE("the written defaults load as the defaults", "[config]") {
    // log_level is left out of the file on purpose: writing it would replace
    // the build's own default.
    const bootstrap written = bootstrap::from_json(bootstrap::default_json());
    const bootstrap defaults;

    CHECK(written.log_level == defaults.log_level);
    CHECK(written.database_path == defaults.database_path);
    CHECK(written.backup_directory == defaults.backup_directory);
    CHECK(written.backups_to_keep == defaults.backups_to_keep);
    CHECK(written.backup_interval == defaults.backup_interval);
    CHECK(written.track_nicknames == defaults.track_nicknames);
    CHECK(written.trusted_guilds == defaults.trusted_guilds);
    CHECK(written.trusted_users == defaults.trusted_users);
    CHECK(written.llm_provider == defaults.llm_provider);
    CHECK(written.llm_model == defaults.llm_model);
    CHECK(written.spend_cap_daily_usd == defaults.spend_cap_daily_usd);
    CHECK(written.spend_cap_monthly_usd == defaults.spend_cap_monthly_usd);
    CHECK(written.llm_tool_rounds == defaults.llm_tool_rounds);
    CHECK(written.emoji_copy_min_uses == defaults.emoji_copy_min_uses);
}

TEST_CASE("the example config is exactly what the bot writes", "[config][fs]") {
    // config.example.json is for reading on GitHub; the bot writes its own.
    // Line endings aside, since git may check the example out with CRLF.
    std::string example = read_file(std::filesystem::path(LATIBOT_TESTS_DIR).parent_path() / "config.example.json");
    std::erase(example, '\r');

    CHECK(example == bootstrap::default_json());
}

TEST_CASE("a config file in a folder that does not exist yet is written there", "[config][fs]") {
    const temp_directory temp;
    const auto path = temp.path() / "settings" / "config.json";

    static_cast<void>(bootstrap::load(path));

    CHECK(read_file(path) == bootstrap::default_json());
}

TEST_CASE("an existing config file is never written over", "[config][fs]") {
    const temp_directory temp;
    const auto path = temp.file("config.json");
    {
        std::ofstream out(path);
        out << R"({"backups_to_keep": 3})";
    }

    CHECK(bootstrap::load(path).backups_to_keep == 3);
    CHECK(read_file(path) == R"({"backups_to_keep": 3})");
}

TEST_CASE("a config file that cannot be written leaves the defaults", "[config][fs]") {
    // A file where the folder should be: nothing can be created under it.
    const temp_directory temp;
    {
        std::ofstream out(temp.file("in-the-way"));
        out << "not a folder";
    }

    const bootstrap config = bootstrap::load(temp.path() / "in-the-way" / "config.json");

    CHECK(config.backups_to_keep == 7);
}

TEST_CASE("something at the config path that cannot be read stops startup", "[config][fs]") {
    // A folder, say: somebody's configuration went wrong, and writing the
    // defaults over it, or running without it, would hide that.
    const temp_directory temp;
    std::filesystem::create_directories(temp.path() / "config.json");

    CHECK_THROWS_WITH(bootstrap::load(temp.path() / "config.json"), ContainsSubstring("could not read the configuration file"));
}
