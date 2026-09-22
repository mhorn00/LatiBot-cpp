#include "core/commands/registry.hpp"

#include "support/capture_log.hpp"

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

    [[nodiscard]] const command_info& info() const override { return info_; }

    dpp::task<void> execute(const dpp::slashcommand_t& /*event*/) override {
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

command_info basic(std::string name, std::vector<std::string> aliases = {},
                   std::uint64_t permissions = 0) {
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
        REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("ping"))),
                          registry_error);
    }

    SECTION("alias collides with an existing name") {
        REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("status", {"ping"}))),
                          registry_error);
    }

    SECTION("alias collides with an existing alias") {
        REQUIRE_THROWS_AS(commands.add(std::make_unique<spy_command>(basic("status", {"p"}))),
                          registry_error);
    }

    SECTION("a refused command leaves the registry untouched") {
        try {
            commands.add(std::make_unique<spy_command>(basic("status", {"p"})));
        } catch (const registry_error&) {
            // The clash is on the alias, so "status" must not be half-added.
        }
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
    spy_command* spy = owned.get();
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
