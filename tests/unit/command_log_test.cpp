#include "core/commands/registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using latibot::commands::describe_invocation;
using latibot::commands::describe_user;

namespace {

dpp::command_data_option option(const std::string& name, dpp::command_value value, dpp::command_option_type type = dpp::co_string) {
    dpp::command_data_option built;
    built.name = name;
    built.type = type;
    built.value = std::move(value);
    return built;
}

dpp::command_interaction command_named(const std::string& name) {
    dpp::command_interaction interaction;
    interaction.name = name;
    return interaction;
}

} // namespace

TEST_CASE("a command with no options logs as its name", "[commands]") {
    CHECK(describe_invocation(command_named("ping")) == "/ping");
}

TEST_CASE("options are logged as name=value", "[commands]") {
    auto interaction = command_named("say");
    interaction.options.push_back(option("message", std::string("hello")));

    CHECK(describe_invocation(interaction) == "/say message=\"hello\"");
}

TEST_CASE("a subcommand reads as part of the command name", "[commands]") {
    // "/trigger add pattern=…" is how somebody typed it; "add=…" would suggest
    // add was an argument.
    auto interaction = command_named("trigger");
    auto add = option("add", {}, dpp::co_sub_command);
    add.options.push_back(option("pattern", std::string("420")));
    add.options.push_back(option("cooldown", std::int64_t{30}, dpp::co_integer));
    interaction.options.push_back(add);

    CHECK(describe_invocation(interaction) == "/trigger add pattern=\"420\" cooldown=30");
}

TEST_CASE("every option type has a readable form", "[commands]") {
    auto interaction = command_named("mixed");
    interaction.options.push_back(option("text", std::string("hi")));
    interaction.options.push_back(option("count", std::int64_t{7}, dpp::co_integer));
    interaction.options.push_back(option("flag", true, dpp::co_boolean));
    interaction.options.push_back(option("who", dpp::snowflake{1234567890123456789ULL}, dpp::co_user));

    const std::string line = describe_invocation(interaction);

    CHECK(line.find("text=\"hi\"") != std::string::npos);
    CHECK(line.find("count=7") != std::string::npos);
    CHECK(line.find("flag=true") != std::string::npos);
    CHECK(line.find("who=1234567890123456789") != std::string::npos);
}

TEST_CASE("an unfilled option says so rather than logging nothing", "[commands]") {
    auto interaction = command_named("edit");
    interaction.options.push_back(option("pattern", {}));

    CHECK(describe_invocation(interaction) == "/edit pattern=<unset>");
}

TEST_CASE("newlines in a value never break the line", "[commands]") {
    // The logger writes one line per message, and a pasted multi-line
    // response would otherwise split a single command across several.
    auto interaction = command_named("trigger");
    interaction.options.push_back(option("responses", std::string("nice\nvery nice\r\nbest")));

    const std::string line = describe_invocation(interaction);

    CHECK(line.find('\n') == std::string::npos);
    CHECK(line.find('\r') == std::string::npos);
    CHECK(line.find("nice\\nvery nice") != std::string::npos);
}

TEST_CASE("a quote in a value is escaped", "[commands]") {
    auto interaction = command_named("say");
    interaction.options.push_back(option("message", std::string("he said \"hi\"")));

    CHECK(describe_invocation(interaction) == "/say message=\"he said \\\"hi\\\"\"");
}

TEST_CASE("a long value is cut, and says how long it really was", "[commands]") {
    // A 2000-character /say is legal, and would otherwise push everything
    // around it out of a terminal.
    auto interaction = command_named("say");
    interaction.options.push_back(option("message", std::string(2000, 'a')));

    const std::string line = describe_invocation(interaction);

    CHECK(line.size() < 300);
    CHECK(line.find("(2000 chars)") != std::string::npos);
}

TEST_CASE("a value that just fits is not cut", "[commands]") {
    auto interaction = command_named("say");
    interaction.options.push_back(option("message", std::string(120, 'a')));

    CHECK(describe_invocation(interaction).find("chars)") == std::string::npos);
}

TEST_CASE("a user is logged by name and id", "[commands]") {
    // Names are not unique and change; ids are unreadable. The log keeps both
    // so a line is both searchable and recognisable.
    dpp::user who;
    who.username = "latios";
    who.id = dpp::snowflake{42};

    CHECK(describe_user(who) == "latios (42)");
}
