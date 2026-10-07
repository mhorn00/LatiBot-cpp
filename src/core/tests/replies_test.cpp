#include "events/replies.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using latibot::events::reply_target_in;
using latibot::events::writes_mention;

namespace {

constexpr dpp::snowflake bot_id{42};

/// A MESSAGE_CREATE frame for a reply, with `referenced` as the message it
/// replies to, written as JSON.
auto reply_frame(const std::string& referenced) -> std::string {
    return R"({"op":0,"t":"MESSAGE_CREATE","d":{"id":"5000","type":19,"content":"hi","referenced_message":)" + referenced + "}}";
}

} // namespace

TEST_CASE("a reply's frame says who wrote the message it replies to", "[events]") {
    const auto target = reply_target_in(reply_frame(R"({"id":"4000","type":0,"author":{"id":"42"},"content":"earlier"})"));
    CHECK(target.author_id == bot_id);
    CHECK_FALSE(target.command_output);
}

TEST_CASE("a reply to a slash command's result or refusal is told apart from a reply to a post", "[events]") {
    // Discord marks a command's response with what it answered; a /say post
    // or a model's reply is an ordinary message.
    CHECK(reply_target_in(reply_frame(R"({"id":"4000","type":20,"author":{"id":"42"},"interaction_metadata":{"id":"7"}})")).command_output);
    CHECK(reply_target_in(reply_frame(R"({"id":"4000","type":0,"author":{"id":"42"},"interaction":{"id":"7"}})")).command_output);
    CHECK(reply_target_in(reply_frame(R"({"id":"4000","type":23,"author":{"id":"42"}})")).command_output);
    CHECK_FALSE(reply_target_in(reply_frame(R"({"id":"4000","type":0,"author":{"id":"42"},"interaction_metadata":null})")).command_output);
}

TEST_CASE("a frame that says nothing usable about the replied-to message reads as nothing known", "[events]") {
    // Discord sends null for a message deleted since.
    CHECK(reply_target_in(reply_frame("null")).author_id.empty());
    CHECK(reply_target_in(reply_frame(R"({"id":"4000"})")).author_id.empty());
    CHECK(reply_target_in(reply_frame(R"({"id":"4000","author":{"id":42}})")).author_id.empty());
    CHECK(reply_target_in(R"({"op":0,"d":{"id":"5000"}})").author_id.empty());
    CHECK(reply_target_in(R"({"op":0,"d":[]})").author_id.empty());
    CHECK(reply_target_in("not json").author_id.empty());
}

TEST_CASE("only a mention written in the message counts, not a reply's ping", "[events]") {
    CHECK(writes_mention("<@42> hi", bot_id));
    CHECK(writes_mention("hi <@!42>", bot_id));
    CHECK_FALSE(writes_mention("hi", bot_id));
    CHECK_FALSE(writes_mention("<@420> hi", bot_id));
    CHECK_FALSE(writes_mention("<@42> hi", dpp::snowflake{}));
}
