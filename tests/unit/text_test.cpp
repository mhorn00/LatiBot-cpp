#include "core/util/text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using latibot::util::count_occurrences;

TEST_CASE("count_occurrences counts non-overlapping matches", "[util]") {
    CHECK(count_occurrences("", "||") == 0);
    CHECK(count_occurrences("no markers here", "||") == 0);
    CHECK(count_occurrences("||spoiler||", "||") == 2);
    CHECK(count_occurrences("|||", "||") == 1); // non-overlapping
    CHECK(count_occurrences("||||", "||") == 2);
    CHECK(count_occurrences("anything", "") == 0);
}

TEST_CASE("trim removes surrounding whitespace only", "[util]") {
    CHECK(latibot::util::trim("") == "");
    CHECK(latibot::util::trim("   ") == "");
    CHECK(latibot::util::trim("  hello  ") == "hello");
    CHECK(latibot::util::trim("\t\r\n hello world \v\f") == "hello world");

    SECTION("inner whitespace survives") {
        CHECK(latibot::util::trim(" a  b ") == "a  b");
    }
}

TEST_CASE("is_blank treats whitespace as empty", "[util]") {
    CHECK(latibot::util::is_blank(""));
    CHECK(latibot::util::is_blank(" \t\n"));
    CHECK_FALSE(latibot::util::is_blank("."));
    CHECK_FALSE(latibot::util::is_blank("  x  "));
}

TEST_CASE("character_count counts characters, not bytes", "[util]") {
    using latibot::util::character_count;
    CHECK(character_count("") == 0);
    CHECK(character_count("abc") == 3);
    CHECK(character_count("é") == 1);   // two bytes
    CHECK(character_count("◀▶") == 2);  // three bytes each
    CHECK(character_count("💀x") == 2); // four bytes, then one
}

TEST_CASE("truncate cuts to a character limit and marks the cut", "[util]") {
    using latibot::util::character_count;
    using latibot::util::truncate;

    SECTION("text that fits is left alone") {
        CHECK(truncate("", 5).empty());
        CHECK(truncate("hello", 5) == "hello");
        CHECK(truncate("héllo", 5) == "héllo"); // six bytes, five characters
    }

    SECTION("longer text ends in an ellipsis and stays within the limit") {
        CHECK(truncate("hello world", 6) == "hello…");
        CHECK(character_count(truncate(std::string(200, 'a'), 100)) == 100);
    }

    SECTION("a multi-byte character is never split") {
        CHECK(truncate("ééééé", 3) == "éé…");
        CHECK(truncate("💀💀💀", 2) == "💀…");
    }

    SECTION("a limit of zero leaves nothing") {
        CHECK(truncate("hello", 0).empty());
    }
}

TEST_CASE("a Discord ID is read as digits and nothing else", "[util]") {
    using latibot::util::parse_snowflake;

    CHECK(parse_snowflake("123456789012345678") == dpp::snowflake{123456789012345678});
    CHECK(parse_snowflake("  123456789012345678 ") == dpp::snowflake{123456789012345678});

    // std::stoull would take the leading digits of the first, and wrap the
    // second round to 18446744073709551615.
    CHECK_FALSE(parse_snowflake("123abc").has_value());
    CHECK_FALSE(parse_snowflake("-1").has_value());
    CHECK_FALSE(parse_snowflake("+123").has_value());
    CHECK_FALSE(parse_snowflake("<@123>").has_value());
    CHECK_FALSE(parse_snowflake("").has_value());
    CHECK_FALSE(parse_snowflake("0").has_value());
    CHECK_FALSE(parse_snowflake("99999999999999999999999").has_value());
}
