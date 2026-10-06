#include "commands/bots.hpp"

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

    CHECK(body.contains("No bots are allowed here"));
    CHECK(body.contains("/bots allow"));
}

TEST_CASE("allowed bots are listed by name where one is known", "[commands]") {
    const std::vector<listed_bot> known{{dpp::snowflake{55}, "DiceBot"}, {dpp::snowflake{66}, "QuoteBot"}};

    const std::string body = render_allowed_bots(known);

    CHECK(body.contains("DiceBot"));
    CHECK(body.contains("QuoteBot"));
    CHECK(body.contains("55"));
}

TEST_CASE("a bot that has left is still listed, and says so", "[commands]") {
    // Removing it silently would leave an entry nobody can see to delete.
    const std::vector<listed_bot> known{{dpp::snowflake{55}, ""}};

    const std::string body = render_allowed_bots(known);

    CHECK(body.contains("55"));
    CHECK(body.contains("not in this server any more"));
}

TEST_CASE("the list says that hearing is not answering", "[commands]") {
    // The distinction people get wrong: allowing a bot does nothing on its
    // own until a trigger opts in too
    // (docs/features/Message_Pipeline.md §2.1).
    const std::vector<listed_bot> known{{dpp::snowflake{55}, "DiceBot"}};

    CHECK(render_allowed_bots(known).contains("bots:true"));
}
