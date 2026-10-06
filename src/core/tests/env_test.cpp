#include "core/util/env.hpp"

#include <catch2/catch_test_macros.hpp>

using latibot::util::parse_dotenv;

TEST_CASE("parse_dotenv reads simple key-value lines", "[util]") {
    const auto entries = parse_dotenv("DISCORD_BOT_TOKEN=abc123\nANTHROPIC_API_KEY=xyz\n");

    REQUIRE(entries.size() == 2);
    CHECK(entries[0] == std::pair<std::string, std::string>{"DISCORD_BOT_TOKEN", "abc123"});
    CHECK(entries[1] == std::pair<std::string, std::string>{"ANTHROPIC_API_KEY", "xyz"});
}

TEST_CASE("parse_dotenv skips blank lines and comments", "[util]") {
    const auto entries = parse_dotenv("\n# a comment\nKEY=value\n   \n# KEY2=ignored\n");

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].first == "KEY");
}

TEST_CASE("parse_dotenv trims whitespace around key and value", "[util]") {
    const auto entries = parse_dotenv("  KEY  =  value with spaces  \n");

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].first == "KEY");
    CHECK(entries[0].second == "value with spaces");
}

TEST_CASE("parse_dotenv strips a leading export", "[util]") {
    const auto entries = parse_dotenv("export KEY=value\n");

    REQUIRE(entries.size() == 1);
    CHECK(entries[0] == std::pair<std::string, std::string>{"KEY", "value"});
}

TEST_CASE("parse_dotenv strips matching surrounding quotes", "[util]") {
    const auto entries = parse_dotenv("A=\"double\"\nB='single'\nC=\"mismatched'\n");

    REQUIRE(entries.size() == 3);
    CHECK(entries[0].second == "double");
    CHECK(entries[1].second == "single");
    CHECK(entries[2].second == "\"mismatched'"); // not a matching pair: left alone
}

TEST_CASE("parse_dotenv skips a line with no '='", "[util]") {
    const auto entries = parse_dotenv("not a valid line\nKEY=value\n");

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].first == "KEY");
}

TEST_CASE("parse_dotenv allows an empty value", "[util]") {
    const auto entries = parse_dotenv("KEY=\n");

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].second.empty());
}

TEST_CASE("parse_dotenv handles a final line with no trailing newline", "[util]") {
    const auto entries = parse_dotenv("KEY=value");

    REQUIRE(entries.size() == 1);
    CHECK(entries[0] == std::pair<std::string, std::string>{"KEY", "value"});
}

TEST_CASE("parse_dotenv copes with CRLF line endings", "[util]") {
    const auto entries = parse_dotenv("KEY=value\r\nOTHER=thing\r\n");

    REQUIRE(entries.size() == 2);
    CHECK(entries[0] == std::pair<std::string, std::string>{"KEY", "value"});
    CHECK(entries[1] == std::pair<std::string, std::string>{"OTHER", "thing"});
}
