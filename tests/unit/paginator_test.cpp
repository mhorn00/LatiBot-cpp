#include "core/ui/paginator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using latibot::ui::clamp_page;
using latibot::ui::controls;
using latibot::ui::custom_id_limit;
using latibot::ui::decode;
using latibot::ui::encode;
using latibot::ui::page_count;
using latibot::ui::page_label;
using latibot::ui::page_state;
using latibot::ui::range_for;

TEST_CASE("page state survives a round trip through a custom_id", "[ui]") {
    const page_state state{.view = "nicknames", .page = 3, .argument = "1234567890"};

    const auto id = encode(state);
    REQUIRE(id.has_value());

    const auto back = decode(*id);
    REQUIRE(back.has_value());
    CHECK(back->view == state.view);
    CHECK(back->page == state.page);
    CHECK(back->argument == state.argument);
}

TEST_CASE("an argument containing the separator still round trips", "[ui]") {
    // The argument is whatever is left after the second colon, so it may
    // contain colons; the view name may not, which encode enforces.
    const page_state state{.view = "urlrepl", .page = 0, .argument = "a:b:c"};

    const auto id = encode(state);
    REQUIRE(id.has_value());

    const auto back = decode(*id);
    REQUIRE(back.has_value());
    CHECK(back->argument == "a:b:c");
}

TEST_CASE("an id that would exceed Discord's limit is refused", "[ui]") {
    // Discord rejects an over-long custom_id when the message is sent, and a
    // truncated one would decode to the wrong page, so this has to fail here.
    const page_state state{.view = "view", .page = 0, .argument = std::string(custom_id_limit, 'x')};
    CHECK_FALSE(encode(state).has_value());
}

TEST_CASE("text that is not ours decodes to nothing", "[ui]") {
    CHECK_FALSE(decode("").has_value());
    CHECK_FALSE(decode("urlretry:12345").has_value()); // one separator, another feature's id
    CHECK_FALSE(decode("view::rest").has_value());     // no page number
    CHECK_FALSE(decode("view:x:rest").has_value());    // page is not a number
    CHECK_FALSE(decode("view:-1:rest").has_value());   // pages do not go backwards
    CHECK_FALSE(decode(":0:rest").has_value());        // no view name
}

TEST_CASE("an empty list is one page, not zero", "[ui]") {
    // Zero pages would make "Page 1 of 0" and a clamp range of [0, -1].
    CHECK(page_count(0, 10) == 1);
    CHECK(clamp_page(0, 0, 10) == 0);
    CHECK(page_label(0, 0, 10) == "Page 1 of 1");
    CHECK(range_for(0, 0, 10).empty());
}

TEST_CASE("pages are counted by rounding up", "[ui]") {
    CHECK(page_count(1, 10) == 1);
    CHECK(page_count(10, 10) == 1);
    CHECK(page_count(11, 10) == 2);
    CHECK(page_count(20, 10) == 2);
    CHECK(page_count(21, 10) == 3);
}

TEST_CASE("a stale page number is clamped rather than rejected", "[ui]") {
    // A button from a longer list is ordinary: somebody deleted rows while
    // the message was still on screen.
    CHECK(clamp_page(99, 25, 10) == 2);
    CHECK(clamp_page(-5, 25, 10) == 0);

    const auto window = range_for(99, 25, 10);
    CHECK(window.begin == 20);
    CHECK(window.end == 25);
}

TEST_CASE("the last page holds the remainder", "[ui]") {
    CHECK(range_for(0, 25, 10).size() == 10);
    CHECK(range_for(1, 25, 10).size() == 10);
    CHECK(range_for(2, 25, 10).size() == 5);
}

TEST_CASE("there is no paging row for a single page", "[ui]") {
    // Two permanently disabled buttons are worse than no buttons.
    CHECK_FALSE(controls({.view = "v", .page = 0, .argument = {}}, 5, 10).has_value());
    CHECK(controls({.view = "v", .page = 0, .argument = {}}, 15, 10).has_value());
}

TEST_CASE("the paging row disables the direction it cannot go", "[ui]") {
    const auto first = controls({.view = "v", .page = 0, .argument = {}}, 25, 10);
    REQUIRE(first.has_value());
    REQUIRE(first->components.size() == 2);
    CHECK(first->components[0].disabled);
    CHECK_FALSE(first->components[1].disabled);

    const auto last = controls({.view = "v", .page = 2, .argument = {}}, 25, 10);
    REQUIRE(last.has_value());
    CHECK_FALSE(last->components[0].disabled);
    CHECK(last->components[1].disabled);
}

TEST_CASE("the paging buttons carry the neighbouring pages", "[ui]") {
    const auto row = controls({.view = "v", .page = 1, .argument = "arg"}, 25, 10);
    REQUIRE(row.has_value());

    const auto previous = decode(row->components[0].custom_id);
    const auto next = decode(row->components[1].custom_id);
    REQUIRE(previous.has_value());
    REQUIRE(next.has_value());
    CHECK(previous->page == 0);
    CHECK(next->page == 2);
    CHECK(next->argument == "arg");
}
