#include "core/commands/registry.hpp"

#include "support/capture_log.hpp"
#include "support/slash_event.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latibot::commands::command;
using latibot::commands::command_info;
using latibot::commands::registry;
using latibot::commands::registry_error;
using latibot::testing::capture_log;

namespace {

/// A command that records that it ran, and can be told to throw.
class spy_command final : public command {
public:
    explicit spy_command(command_info details) : info_(std::move(details)) {}

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }

    auto execute(const dpp::slashcommand_t& /*event*/) -> dpp::task<void> override {
        ++runs;
        if (should_throw) {
            throw std::runtime_error("command blew up");
        }
        co_return;
    }

    int runs = 0;
    bool should_throw = false;

private:
    command_info info_;
};

auto basic(std::string name, std::vector<std::string> aliases = {}, std::uint64_t permissions = 0) -> command_info {
    return command_info{.name = std::move(name),
                        .description = "a test command",
                        .aliases = std::move(aliases),
                        .required_bot_permissions = permissions,
                        .default_member_permissions = std::nullopt,
                        .guild_only = true};
}

} // namespace

TEST_CASE("commands are found by name and by alias", "[commands]") {
    registry commands;
    commands.add(std::make_unique<spy_command>(basic("nicknames", {"nicks", "names"})));

    REQUIRE(commands.size() == 1);
    CHECK(commands.find("nicknames") != nullptr);
    CHECK(commands.find("nicks") == commands.find("nicknames"));
    CHECK(commands.find("names") == commands.find("nicknames"));
    CHECK(commands.find("unknown") == nullptr);
}

TEST_CASE("a duplicate name or alias is refused", "[commands]") {
    registry commands;
    commands.add(std::make_unique<spy_command>(basic("ping", {"p"})));

    SECTION("same name") {
        REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("ping"))), registry_error);
    }

    SECTION("alias collides with an existing name") {
        REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("status", {"ping"}))), registry_error);
    }

    SECTION("alias collides with an existing alias") {
        REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("status", {"p"}))), registry_error);
    }

    SECTION("a refused command leaves the registry untouched") {
        // The clash is on the alias, so "status" must not be half-added.
        CHECK_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("status", {"p"}))), registry_error);
        CHECK(commands.size() == 1);
        CHECK(commands.find("status") == nullptr);
    }
}

TEST_CASE("an empty name is refused", "[commands]") {
    registry commands;
    REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic(""))), registry_error);
}

TEST_CASE("every name and alias gets its own registration payload", "[commands]") {
    registry commands;
    commands.add(std::make_unique<spy_command>(basic("nicknames", {"nicks"})));
    commands.add(std::make_unique<spy_command>(basic("ping")));

    const auto payloads = commands.build_all(dpp::snowflake{42});

    REQUIRE(payloads.size() == 3);
    for (const auto& payload : payloads) {
        CHECK(payload.application_id == dpp::snowflake{42});
        CHECK(payload.description == "a test command");
    }
}

TEST_CASE("required permissions are the union of every command's", "[commands]") {
    registry commands;
    commands.add(std::make_unique<spy_command>(basic("a", {}, dpp::p_send_messages)));
    commands.add(std::make_unique<spy_command>(basic("b", {}, dpp::p_manage_messages)));

    const auto required = commands.required_bot_permissions();

    CHECK((required & dpp::p_send_messages) != 0);
    CHECK((required & dpp::p_manage_messages) != 0);
    CHECK((required & dpp::p_administrator) == 0);
}

TEST_CASE("dispatch runs the command registered under the name", "[commands][coro]") {
    registry commands;
    auto owned = std::make_unique<spy_command>(basic("ping", {"p"}));
    const spy_command* spy = owned.get();
    commands.add(std::move(owned));

    const dpp::slashcommand_t event;

    REQUIRE(commands.dispatch("ping", event).sync_wait_for(2s));
    CHECK(spy->runs == 1);

    // An alias reaches the same command.
    REQUIRE(commands.dispatch("p", event).sync_wait_for(2s));
    CHECK(spy->runs == 2);
}

TEST_CASE("an unknown command name is logged, not thrown", "[commands][coro]") {
    // Discord can still deliver a command that was removed from the code but
    // not yet from the guild.
    const capture_log captured;
    const registry commands;
    const dpp::slashcommand_t event;

    REQUIRE(commands.dispatch("gone", event).sync_wait_for(2s));
    CHECK(captured.contains(latibot::util::log_level::warn, "gone"));
}

TEST_CASE("an exception from a handler is caught and logged", "[commands][coro]") {
    // Letting it escape would leave the coroutine unhandled and take the
    // process down.
    const capture_log captured;

    registry commands;
    auto owned = std::make_unique<spy_command>(basic("boom"));
    owned->should_throw = true;
    commands.add(std::move(owned));

    const dpp::slashcommand_t event;

    REQUIRE(commands.dispatch("boom", event).sync_wait_for(2s));
    CHECK(captured.contains(latibot::util::log_level::error, "command blew up"));
}

// --------------------------------------------------------------------------
// Response flags
// --------------------------------------------------------------------------

namespace {

/// A command with a subcommand and a group, for flags that name them.
class grouped_command final : public command {
public:
    explicit grouped_command(command_info details) : info_(std::move(details)) {}

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }

    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override {
        dpp::slashcommand payload = command::build(name, application_id);
        payload.add_option(dpp::command_option(dpp::co_sub_command, "list", "List them."));
        dpp::command_option alias(dpp::co_sub_command_group, "alias", "Aliases.");
        alias.add_option(dpp::command_option(dpp::co_sub_command, "add", "Add one."));
        payload.add_option(alias);
        return payload;
    }

    auto execute(const dpp::slashcommand_t& /*event*/) -> dpp::task<void> override { co_return; }

private:
    command_info info_;
};

/// Public results, private refusals, silent posts, and "alias add" answered
/// privately: the shape `/linkstats` has.
auto grouped_info() -> command_info {
    command_info details = basic("stats");
    details.responses = {.result = 0, .refusal = dpp::m_ephemeral, .post = dpp::m_suppress_notifications};
    details.subcommand_responses = {{"alias add", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}}};
    return details;
}

} // namespace

TEST_CASE("a subcommand's response flags override only what they name", "[commands]") {
    const command_info details = grouped_info();

    const auto alias_add = details.responses_for("alias add");
    CHECK(alias_add.result == dpp::m_ephemeral);
    CHECK(alias_add.refusal == dpp::m_ephemeral);
    CHECK(alias_add.post == dpp::m_suppress_notifications);

    CHECK(details.responses_for("list").result == 0);
    CHECK(details.responses_for("").result == 0);
}

TEST_CASE("the subcommand an interaction ran is read as a path", "[commands]") {
    using latibot::commands::subcommand_path;
    using latibot::testing::slash_event;

    CHECK(subcommand_path(slash_event("stats", "").command.get_command_interaction()).empty());
    CHECK(subcommand_path(slash_event("stats", "list").command.get_command_interaction()) == "list");
    CHECK(subcommand_path(slash_event("stats", "alias add").command.get_command_interaction()) == "alias add");

    // Plain options are arguments, not part of the path.
    const auto with_option = slash_event("say", "", {latibot::testing::bool_option("loud", true)});
    CHECK(subcommand_path(with_option.command.get_command_interaction()).empty());
}

TEST_CASE("every subcommand a payload offers is listed by path", "[commands]") {
    const grouped_command stats(grouped_info());
    CHECK(latibot::commands::subcommand_paths(stats.build("stats", dpp::snowflake{1})) == std::vector<std::string>{"list", "alias add"});
}

TEST_CASE("replies carry the flags configured for the subcommand that ran", "[commands]") {
    using latibot::testing::slash_event;
    const grouped_command stats(grouped_info());

    CHECK(stats.result(slash_event("stats", "list"), "a board").flags == 0);
    CHECK(stats.result(slash_event("stats", "alias add"), "ok").flags == dpp::m_ephemeral);
    CHECK(stats.refusal(slash_event("stats", "list"), "which emoji?").flags == dpp::m_ephemeral);
    CHECK(stats.post(slash_event("stats", "list"), dpp::message(dpp::snowflake{5}, "progress")).flags == dpp::m_suppress_notifications);

    // The configuration decides, not whatever built the message.
    dpp::message rendered("a board");
    rendered.flags = dpp::m_ephemeral | dpp::m_suppress_embeds;
    CHECK(stats.result(slash_event("stats", "list"), rendered).flags == 0);
}

TEST_CASE("response flags that could not work are refused at registration", "[commands]") {
    registry commands;

    SECTION("a flag no reply can carry") {
        command_info details = basic("odd");
        details.responses.result = dpp::m_urgent;
        CHECK_THROWS_AS(commands.add(std::make_unique<spy_command>(details)), registry_error);
    }

    SECTION("an ephemeral post, which only a reply can be") {
        command_info details = basic("odd");
        details.responses.post = dpp::m_ephemeral;
        CHECK_THROWS_AS(commands.add(std::make_unique<spy_command>(details)), registry_error);
    }

    SECTION("a subcommand that does not exist") {
        command_info details = grouped_info();
        details.subcommand_responses.emplace("alias_add", latibot::commands::response_overrides{});
        CHECK_THROWS_AS(commands.add(std::make_unique<grouped_command>(details)), registry_error);
    }

    SECTION("an override that could not work either") {
        command_info details = grouped_info();
        details.subcommand_responses["list"].post = dpp::m_ephemeral;
        CHECK_THROWS_AS(commands.add(std::make_unique<grouped_command>(details)), registry_error);
    }

    // Nothing half-registered is left behind.
    CHECK(commands.size() == 0);
    CHECK_NOTHROW(commands.add(std::make_unique<grouped_command>(grouped_info())));
}
