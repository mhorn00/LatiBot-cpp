#include "core/util/text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <string_view>
#include <tuple>

using latibot::util::count_occurrences;
using latibot::util::is_inside_spoiler;

TEST_CASE("count_occurrences counts non-overlapping matches", "[util]") {
    CHECK(count_occurrences("", "||") == 0);
    CHECK(count_occurrences("no markers here", "||") == 0);
    CHECK(count_occurrences("||spoiler||", "||") == 2);
    CHECK(count_occurrences("|||", "||") == 1);   // non-overlapping
    CHECK(count_occurrences("||||", "||") == 2);
    CHECK(count_occurrences("anything", "") == 0);
}

TEST_CASE("is_inside_spoiler follows an odd count of markers", "[util]") {
    auto [before, spoilered] = GENERATE(table<std::string, bool>({
        {"", false},
        {"||", true},
        {"||closed|| ", false},
        {"||open ", true},
        {"a || b || c ||", true},
        {"single | pipe ", false},
    }));

    CAPTURE(before);
    CHECK(is_inside_spoiler(before) == spoilered);
}
