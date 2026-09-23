#include "core/commands/bots.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using latibot::commands::render_allowed_bots;

namespace {

using listed_bot = std::pair<dpp::snowflake, std::string>;

} // namespace

TEST_CASE("an empty allowlist explains itself", "[commands]") {
    // "No bots" and "bots are off" look the same in a list, so the empty
    // state has to say which one it is.
    const std::string body = render_allowed_bots({});

    CHECK(body.find("No bots are allowed here") != std::string::npos);
    CHECK(body.find("/bots allow") != std::string::npos);
}

TEST_CASE("allowed bots are listed by name where one is known", "[commands]") {
    const std::vector<listed_bot> known{{dpp::snowflake{55}, "DiceBot"}, {dpp::snowflake{66}, "QuoteBot"}};

    const std::string body = render_allowed_bots(known);

    CHECK(body.find("DiceBot") != std::string::npos);
    CHECK(body.find("QuoteBot") != std::string::npos);
    CHECK(body.find("55") != std::string::npos);
}

TEST_CASE("a bot that has left is still listed, and says so", "[commands]") {
    // Removing it silently would leave an entry nobody can see to delete.
    const std::vector<listed_bot> known{{dpp::snowflake{55}, ""}};

    const std::string body = render_allowed_bots(known);

    CHECK(body.find("55") != std::string::npos);
    CHECK(body.find("not in this server any more") != std::string::npos);
}

TEST_CASE("the list says that hearing is not answering", "[commands]") {
    // The distinction people get wrong: allowing a bot does nothing on its
    // own until a trigger opts in too (plan v4 14.4).
    const std::vector<listed_bot> known{{dpp::snowflake{55}, "DiceBot"}};

    CHECK(render_allowed_bots(known).find("bots:true") != std::string::npos);
}
