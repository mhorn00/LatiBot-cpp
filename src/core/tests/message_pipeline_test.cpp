#include "core/events/message_pipeline.hpp"
#include "core/events/stage_order.hpp"

#include "support/capture_log.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using latibot::events::action;
using latibot::events::incoming_message;
using latibot::events::pipeline;
using latibot::events::send_message;
using latibot::events::stage_result;
namespace stage_order = latibot::events::stage_order;

namespace {

auto from_human(std::string content = "hello") -> incoming_message {
    return {.guild_id = dpp::snowflake{1},
            .channel_id = dpp::snowflake{2},
            .author_id = dpp::snowflake{3},
            .from_self = false,
            .from_bot = false,
            .author_is_administrator = false,
            .content = std::move(content)};
}

/// A stage that records that it ran and optionally answers.
auto recorder(std::vector<std::string>& ran, std::string name, bool consumes = false, bool answers = false) -> pipeline::stage_fn {
    return [&ran, name = std::move(name), consumes, answers](const incoming_message& message) {
        ran.push_back(name);
        stage_result result;
        result.consumed = consumes;
        if (answers) result.actions.emplace_back(send_message{.channel_id = message.channel_id, .content = name});
        return result;
    };
}

auto sent(const std::vector<action>& actions) -> std::vector<std::string> {
    std::vector<std::string> contents;
    for (const action& one : actions) {
        if (const auto* post = std::get_if<send_message>(&one)) contents.push_back(post->content);
    }
    return contents;
}

} // namespace

TEST_CASE("stages run by position, whatever order they were added in", "[events]") {
    // Modules start in dependency order, which says nothing about where
    // their stages belong (docs/modules/Module_Plan_Final.md §4.4).
    std::vector<std::string> ran;
    pipeline stages;
    stages.add(stage_order::model, "model", recorder(ran, "model"));
    stages.add(stage_order::stop, "stop", recorder(ran, "stop"));
    stages.add(stage_order::reply, "reply", recorder(ran, "reply"));
    stages.add(stage_order::rewrite, "rewrite", recorder(ran, "rewrite"));

    const auto actions = stages.run(from_human());

    CHECK(ran == std::vector<std::string>{"stop", "rewrite", "reply", "model"});
    CHECK(stages.stage_names() == std::vector<std::string_view>{"stop", "rewrite", "reply", "model"});
    CHECK(actions.empty());
}

TEST_CASE("two stages at one position stop startup", "[events]") {
    std::vector<std::string> ran;
    pipeline stages;
    stages.add(stage_order::reply, "triggers", recorder(ran, "triggers"));

    CHECK_THROWS_WITH(stages.add(stage_order::reply, "echo", recorder(ran, "echo")),
                      Catch::Matchers::ContainsSubstring("\"triggers\" and \"echo\" both claim position 300"));
    CHECK(stages.size() == 1);
}

TEST_CASE("a stage that consumes the message stops the ones after it", "[events]") {
    // This is the difference between a trigger reply, which lets a URL
    // replacement follow, and a goodbye, which ends the conversation.
    std::vector<std::string> ran;
    pipeline stages;
    stages.add(1, "first", recorder(ran, "first", /*consumes=*/false, /*answers=*/true));
    stages.add(2, "second", recorder(ran, "second", /*consumes=*/true, /*answers=*/true));
    stages.add(3, "third", recorder(ran, "third", /*consumes=*/false, /*answers=*/true));

    const auto actions = stages.run(from_human());

    CHECK(ran == std::vector<std::string>{"first", "second"});
    CHECK(sent(actions) == std::vector<std::string>{"first", "second"});
}

TEST_CASE("the bot never answers itself, or a bot this guild has not allowed", "[events]") {
    // Answering our own message is a loop with no exit, and answering an
    // arbitrary bot is the same loop with two participants
    // (docs/features/Message_Pipeline.md §2.1).
    std::vector<std::string> ran;
    pipeline stages;
    stages.add(1, "only", recorder(ran, "only", false, true));

    SECTION("our own message") {
        auto message = from_human();
        message.from_self = true;
        CHECK(stages.run(message).empty());
    }

    SECTION("a bot nobody allowed") {
        auto message = from_human();
        message.from_bot = true;
        CHECK(stages.run(message).empty());
    }

    SECTION("our own message, even if we somehow allowed ourselves") {
        // from_self is checked first on purpose: an allowlist entry for our
        // own id must not talk us into answering ourselves.
        auto message = from_human();
        message.from_self = true;
        message.from_bot = true;
        message.author_is_allowed_bot = true;
        CHECK(stages.run(message).empty());
    }

    CHECK(ran.empty());
}

TEST_CASE("an allowed bot reaches the stages", "[events]") {
    // Being heard is the allowlist's decision; whether to answer is each
    // stage's own (docs/features/Message_Pipeline.md §2.1).
    std::vector<std::string> ran;
    pipeline stages;
    stages.add(1, "only", recorder(ran, "only", false, true));

    auto message = from_human();
    message.from_bot = true;
    message.author_is_allowed_bot = true;

    CHECK(sent(stages.run(message)) == std::vector<std::string>{"only"});
    CHECK(ran == std::vector<std::string>{"only"});
}

TEST_CASE("a stage that throws is logged and the rest still run", "[events]") {
    // A stage escaping into DPP's event thread would take the process down,
    // and one broken feature should not silence every other.
    const latibot::testing::capture_log captured(latibot::util::log_level::error);

    std::vector<std::string> ran;
    pipeline stages;
    stages.add(1, "broken", [](const incoming_message&) -> stage_result { throw std::runtime_error("nope"); });
    stages.add(2, "healthy", recorder(ran, "healthy", false, true));

    const auto actions = stages.run(from_human());

    CHECK(ran == std::vector<std::string>{"healthy"});
    CHECK(sent(actions) == std::vector<std::string>{"healthy"});
    CHECK(captured.contains(latibot::util::log_level::error, "broken"));
}

TEST_CASE("an empty pipeline decides nothing", "[events]") {
    const pipeline stages;
    CHECK(stages.size() == 0);
    CHECK(stages.run(from_human()).empty());
}

namespace {

/// A feature's own action, as the URL replacer's replace_links is.
struct own_step {
    int number = 0;
};

} // namespace

TEST_CASE("a stage's own actions become background tasks, and the rest pass through", "[events][coro]") {
    using latibot::events::background_task;
    using latibot::events::own_stage_result;

    std::vector<int> carried;
    pipeline stages;
    stages.add(1, "own",
               latibot::events::carried_out_by<own_step>(
                   [](const incoming_message& message) {
                       own_stage_result<own_step> decided;
                       decided.actions.emplace_back(own_step{.number = 7});
                       decided.actions.emplace_back(send_message{.channel_id = message.channel_id, .content = "noted"});
                       decided.answered = true;
                       return decided;
                   },
                   "doing the step",
                   [&carried](own_step step) -> dpp::task<void> {
                       carried.push_back(step.number);
                       co_return;
                   }));
    stages.add(2, "later", [](const incoming_message& message) {
        stage_result result;
        // The first stage answered, and this one sees it.
        if (message.answered) result.actions.emplace_back(send_message{.channel_id = message.channel_id, .content = "seen"});
        return result;
    });

    auto actions = stages.run(from_human());
    REQUIRE(actions.size() == 3);
    auto* task = std::get_if<background_task>(&actions.front());
    REQUIRE(task != nullptr);
    CHECK(task->what == "doing the step");
    CHECK(sent(actions) == std::vector<std::string>{"noted", "seen"});

    // Nothing is carried out until the task runs.
    CHECK(carried.empty());
    task->run().sync_wait_for(std::chrono::seconds{2});
    CHECK(carried == std::vector<int>{7});
}
