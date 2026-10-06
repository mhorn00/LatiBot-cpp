#include "commands/logs.hpp"
#include "core/commands/registry.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>

using latibot::commands::may_configure_logs;
using latibot::commands::render_log_channel;
using latibot::events::log_channel_status;
using latibot::events::log_destination;
using latibot::util::log_level;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake here{100};

auto pointing_at(dpp::snowflake guild, log_level level) -> log_channel_status {
    return {.destination = log_destination{.guild_id = guild, .channel_id = dpp::snowflake{200}, .level = level},
            .waiting = 0,
            .failure = std::nullopt,
            .retry_in = 0s};
}

} // namespace

TEST_CASE("only the trusted users can choose where the log goes", "[commands]") {
    latibot::config::bootstrap settings;
    settings.trusted_users = {dpp::snowflake{7}};
    settings.trusted_guilds = {here};

    CHECK(may_configure_logs(settings, dpp::snowflake{7}));
    // An administrator of a trusted server is trusted with DECtalk, not with
    // a log that covers every other server too.
    CHECK_FALSE(may_configure_logs(settings, dpp::snowflake{8}));

    settings.trusted_users.clear();
    CHECK_FALSE(may_configure_logs(settings, dpp::snowflake{7}));
}

TEST_CASE("the log channel's state says where, from which level, and how it is going", "[commands]") {
    CHECK(render_log_channel({}, here).contains("`/logs set`"));

    const std::string plain = render_log_channel(pointing_at(here, log_level::info), here);
    CHECK(plain.contains("<#200>"));
    CHECK(plain.contains("**info** and above"));
    CHECK_FALSE(plain.contains("another server"));
    CHECK_FALSE(plain.contains("failing"));

    CHECK(render_log_channel(pointing_at(here, log_level::trace), here).contains("everything"));
    CHECK(render_log_channel(pointing_at(dpp::snowflake{300}, log_level::info), here).contains("another server (`300`)"));

    log_channel_status failing = pointing_at(here, log_level::warn);
    failing.waiting = 12;
    failing.failure = "Missing Access";
    failing.retry_in = 60s;
    const std::string trouble = render_log_channel(failing, here);
    CHECK(trouble.contains("12 line(s) waiting"));
    CHECK(trouble.contains("Missing Access"));
    CHECK(trouble.contains("60s"));
}

TEST_CASE("the logs command registers, with a level for every choice but off", "[commands]") {
    latibot::db::database db(":memory:");
    latibot::testing::create_schema(db);
    latibot::config::guild_settings settings(db);
    latibot::events::log_destination_store store(settings);
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    latibot::events::log_channel channel(discord, clock);
    const latibot::config::bootstrap bootstrap;

    auto command = std::make_unique<latibot::commands::logs_command>(bootstrap, store, channel, discord);
    const dpp::slashcommand payload = command->build("logs", dpp::snowflake{1});

    REQUIRE(payload.options.size() == 4);
    const dpp::command_option& set = payload.options[0];
    CHECK(set.name == "set");
    REQUIRE(set.options.size() == 2);
    CHECK(set.options[0].channel_types == std::vector<dpp::channel_type>{dpp::CHANNEL_TEXT, dpp::CHANNEL_ANNOUNCEMENT});

    const dpp::command_option& level = set.options[1];
    REQUIRE(level.choices.size() == 5);
    for (const dpp::command_option_choice& choice : level.choices) {
        INFO(choice.name);
        CHECK(choice.name.size() <= 100);
        CHECK(latibot::util::log_level_from_string(std::get<std::string>(choice.value)).has_value());
    }

    latibot::commands::registry commands;
    CHECK_NOTHROW(commands.add(std::move(command)));
}
