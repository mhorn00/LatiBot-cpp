#include "core/commands/nickname.hpp"
#include "core/ui/paginator.hpp"

#include "support/discord_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <string>
#include <vector>

using latibot::commands::nickname_history_view;
using latibot::commands::nicknames_per_page;
using latibot::commands::render_nickname_history;
using latibot::events::nickname_change;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake member{3000};

const auto noon = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::September / 23} + 12h;

std::vector<nickname_change> history_of(std::size_t entries) {
    std::vector<nickname_change> history;
    for (std::size_t index = 0; index < entries; ++index) {
        history.push_back({.user_id = member,
                           .nickname = std::format("name {}", index),
                           .changed_at = noon - (std::chrono::hours(1) * static_cast<int>(index))});
    }
    return history;
}

} // namespace

TEST_CASE("an empty history says so rather than showing an empty page", "[commands]") {
    const dpp::message reply = render_nickname_history({}, member, 0);

    CHECK(reply.content.find("Nothing recorded here yet.") != std::string::npos);

    // One empty page needs no buttons to page between.
    CHECK(reply.components.empty());
}

TEST_CASE("a history page shows its entries and where it is", "[commands]") {
    const auto history = history_of(3);
    const dpp::message reply = render_nickname_history(history, member, 0);

    CHECK(reply.content.find("**Nickname history for <@3000>**") != std::string::npos);
    CHECK(reply.content.find("name 0") != std::string::npos);
    CHECK(reply.content.find("name 2") != std::string::npos);
    CHECK(reply.content.find("Page 1 of 1") != std::string::npos);
}

TEST_CASE("a long history pages, and the buttons remember whose it is", "[commands]") {
    const auto history = history_of(nicknames_per_page * 2);
    const dpp::message reply = render_nickname_history(history, member, 1);

    CHECK(reply.content.find("Page 2 of 2") != std::string::npos);
    latibot::testing::check_message_fits(reply);
    latibot::testing::check_message_fits(render_nickname_history(history, member, 0));

    // The second page holds the older half.
    CHECK(reply.content.find(std::format("name {}", nicknames_per_page)) != std::string::npos);
    CHECK(reply.content.find("name 0\n") == std::string::npos);

    REQUIRE_FALSE(reply.components.empty());
    const auto& buttons = reply.components.front().components;
    REQUIRE_FALSE(buttons.empty());

    // Whose history a button pages through rides in the custom_id, so paging
    // still works after a restart (plan v4 §8.2).
    const auto state = latibot::ui::decode(buttons.front().custom_id);
    REQUIRE(state.has_value());
    CHECK(state->view == nickname_history_view);
    CHECK(state->argument == "3000");
}

TEST_CASE("a page number from a stale button is brought back in range", "[commands]") {
    const auto history = history_of(3);

    // The history was longer when that button was made. Clamping beats an
    // error, since somebody scrolling back through an old reply is ordinary.
    const dpp::message reply = render_nickname_history(history, member, 40);
    CHECK(reply.content.find("Page 1 of 1") != std::string::npos);
}

TEST_CASE("a history is posted for the room, not just for whoever asked", "[commands]") {
    // The one list this bot posts publicly: half the point of a nickname
    // history is showing it to the person it is about.
    CHECK((render_nickname_history(history_of(3), member, 0).flags & dpp::m_ephemeral) == 0);
    CHECK((render_nickname_history({}, member, 0).flags & dpp::m_ephemeral) == 0);
}

TEST_CASE("a history reply cannot ping the people it names", "[commands]") {
    auto history = history_of(1);
    history.front().changed_by = dpp::snowflake{4000};

    const dpp::message reply = render_nickname_history(history, member, 0);

    // Mentions are how somebody who has left the server still gets a name.
    // The message is public, so without this every person in the history
    // would be pinged by somebody else looking it up.
    CHECK(reply.content.find("<@4000>") != std::string::npos);
    CHECK_FALSE(reply.allowed_mentions.parse_users);
    CHECK_FALSE(reply.allowed_mentions.parse_everyone);
    CHECK_FALSE(reply.allowed_mentions.parse_roles);
}
