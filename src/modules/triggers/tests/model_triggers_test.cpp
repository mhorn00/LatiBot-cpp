// Triggers that ask the language model to answer, through the model_replies
// capability, and answering as a reply (src/modules/triggers/docs/Triggers.md
// §2.2, §2.6).

#include "core/capabilities/model_replies.hpp"
#include "core/db/database.hpp"
#include "fire_command.hpp"
#include "triggers.hpp"

#include "mocks/mock_clock.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using latibot::capabilities::model_trigger;
using latibot::capabilities::persona_name;
using latibot::events::background_task;
using latibot::events::incoming_message;
using latibot::events::match_mode;
using latibot::events::send_message;
using latibot::events::trigger;
using latibot::events::trigger_responder;
using latibot::events::trigger_store;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake other_channel{4000};
constexpr std::int64_t claude = 7;

/// The language model, as the triggers see it: what it says about a
/// message, and what it was asked to answer.
struct mock_model final : latibot::capabilities::model_replies {
    std::vector<persona_name> known{{.id = claude, .name = "Claude", .enabled = true}};

    /// Who of LatiBot's own wrote the message, when the test says it did.
    std::optional<std::int64_t> writer;
    bool addressed = false;
    std::optional<std::int64_t> replied;
    bool admits = true;
    bool handed_over = false;
    std::vector<model_trigger> asked;

    [[nodiscard]] auto personas(dpp::snowflake) const -> std::vector<persona_name> override { return known; }
    [[nodiscard]] auto own_writer(const incoming_message&) const -> std::optional<std::int64_t> override { return writer; }
    [[nodiscard]] auto addresses(const incoming_message&) const -> bool override { return addressed; }
    [[nodiscard]] auto replied_to(const incoming_message&) const -> std::optional<std::int64_t> override { return replied; }
    [[nodiscard]] auto answer(const incoming_message&, const model_trigger& fired) -> std::vector<latibot::events::action> override {
        asked.push_back(fired);
        std::vector<latibot::events::action> actions;
        if (admits) actions.emplace_back(background_task{.what = "answering", .run = []() -> dpp::task<void> { co_return; }});
        return actions;
    }
    auto take_over_answering() -> void override { handed_over = true; }
};

struct fixture {
    latibot::db::database db{":memory:"};
    trigger_store store{db};
    latibot::testing::mock_clock clock;
    mock_model model;
    std::uint64_t roll = 0;
    trigger_responder responder{store, clock, [this] { return roll; }, &model};

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::events::triggers_schema());
    }

    /// A trigger on "pizza" that asks the model, answered by `persona_id`, 0
    /// for LatiBot.
    auto add_pizza(std::int64_t persona_id = 0, bool respond_to_bots = false) -> std::int64_t {
        return store.add({.guild_id = guild,
                          .pattern = "pizza",
                          .mode = match_mode::whole_word,
                          .cooldown = 60s,
                          .enabled = true,
                          .respond_to_bots = respond_to_bots,
                          .reply = true,
                          .prompt = "Defend pineapple on it.",
                          .chance = 0.5,
                          .persona_id = persona_id});
    }
};

auto from_alice(std::string content, dpp::snowflake in = channel) -> incoming_message {
    return {.guild_id = guild,
            .channel_id = in,
            .message_id = dpp::snowflake{5555},
            .author_id = dpp::snowflake{9},
            .content = std::move(content)};
}

/// LatiBot's own message, or a persona's through its webhook: the stage
/// hears them only to let them set off each other's model triggers.
auto from_ours(std::string content) -> incoming_message {
    incoming_message message = from_alice(std::move(content));
    message.from_self = true;
    message.from_bot = true;
    return message;
}

/// A roll that comes to `fraction` of the way through [0, 1).
auto roll_of(double fraction) -> std::uint64_t {
    return static_cast<std::uint64_t>(fraction * 0x1.0p53) << 11U;
}

} // namespace

TEST_CASE("a model trigger is stored with its prompt, chance, persona and reply", "[triggers]") {
    fixture test;
    const std::int64_t id = test.add_pizza(claude);

    const auto found = test.store.find(id, guild);
    REQUIRE(found.has_value());
    CHECK(found->asks_model());
    CHECK(found->prompt == "Defend pineapple on it.");
    CHECK(found->chance == 0.5);
    CHECK(found->persona_id == claude);
    CHECK(found->reply);
    CHECK(found->responses.empty());
}

TEST_CASE("the model's old advanced triggers are copied in once, answering as they did", "[triggers]") {
    fixture test;

    // Nothing to copy without the old table, and nothing is remembered, so a
    // language model built in later is still copied from.
    CHECK(test.store.import_llm_triggers() == 0);

    // The table as the llm module's schema left it.
    test.db.execute(R"sql(
        CREATE TABLE llm_triggers (
            id INTEGER PRIMARY KEY, guild_id INTEGER NOT NULL, pattern TEXT NOT NULL, match_mode TEXT NOT NULL,
            context_prompt TEXT NOT NULL, probability REAL NOT NULL, cooldown_s INTEGER NOT NULL,
            enabled INTEGER NOT NULL DEFAULT 1, created_by INTEGER NOT NULL, persona_id INTEGER,
            respond_to_bots INTEGER NOT NULL DEFAULT 0);
        INSERT INTO llm_triggers (guild_id, pattern, match_mode, context_prompt, probability, cooldown_s, enabled, created_by, persona_id,
                                  respond_to_bots)
            VALUES (1000, 'pizza', 'substring', 'Defend it.', 0.25, 300, 1, 1, NULL, 0),
                   (1000, 'anthropic', 'whole_word', 'Say hi.', 1.0, 0, 0, 1, 7, 1);
    )sql");

    CHECK(test.store.import_llm_triggers() == 2);
    CHECK(test.store.import_llm_triggers() == 0);

    const auto all = test.store.for_guild(guild);
    REQUIRE(all.size() == 2);
    CHECK(all[0].pattern == "pizza");
    CHECK(all[0].mode == match_mode::substring);
    CHECK(all[0].prompt == "Defend it.");
    CHECK(all[0].chance == 0.25);
    CHECK(all[0].cooldown == 300s);
    CHECK(all[0].persona_id == 0);
    CHECK(all[0].reply);
    CHECK(all[0].message_flags == dpp::m_suppress_notifications);
    CHECK_FALSE(all[1].enabled);
    CHECK(all[1].persona_id == claude);
    CHECK(all[1].respond_to_bots);
}

TEST_CASE("a model trigger that wins its roll asks the model, once per cooldown", "[triggers]") {
    fixture test;
    const std::int64_t id = test.add_pizza();

    // 0.9 is not under 0.5: no answer, and no cooldown either.
    test.roll = roll_of(0.9);
    CHECK(test.responder(from_alice("who wants pizza")).actions.empty());

    test.roll = roll_of(0.1);
    const auto fired = test.responder(from_alice("who wants pizza"));
    REQUIRE(fired.actions.size() == 1);
    CHECK(std::holds_alternative<background_task>(fired.actions.front()));
    CHECK(fired.answered);
    CHECK_FALSE(fired.consumed);
    REQUIRE(test.model.asked.size() == 1);
    CHECK(test.model.asked[0].trigger_id == id);
    CHECK(test.model.asked[0].prompt == "Defend pineapple on it.");
    CHECK(test.model.asked[0].reply);

    CHECK(test.responder(from_alice("more pizza")).actions.empty());
    CHECK_FALSE(test.responder(from_alice("pizza?", other_channel)).actions.empty());
    test.clock.advance(60s);
    CHECK_FALSE(test.responder(from_alice("pizza again")).actions.empty());
}

TEST_CASE("a fixed reply wins, and the model is not asked as well", "[triggers]") {
    fixture test;
    test.add_pizza();
    test.store.add({.guild_id = guild,
                    .pattern = "pizza",
                    .mode = match_mode::substring,
                    .cooldown = 0s,
                    .enabled = true,
                    .responses = {{.text = "nice", .weight = 1}}});

    const auto result = test.responder(from_alice("pizza"));
    REQUIRE(result.actions.size() == 1);
    CHECK(std::holds_alternative<send_message>(result.actions.front()));
    CHECK(test.model.asked.empty());
}

TEST_CASE("being addressed is answered as spoken to, ahead of any comment", "[triggers]") {
    // The built-in "LatiBot addressed" trigger, which every guild gets with
    // the model (src/modules/triggers/docs/Triggers.md §2.8).
    fixture test;
    test.add_pizza();
    test.model.addressed = true;

    REQUIRE(test.responder(from_alice("latibot, pizza?")).actions.size() == 1);
    REQUIRE(test.model.asked.size() == 1);
    CHECK(test.model.asked[0].how == latibot::capabilities::model_answer::spoken_to);
    CHECK(test.model.asked[0].persona_id == 0);
    CHECK(test.model.asked[0].reply);
    const auto fired = test.store.find(test.model.asked[0].trigger_id, guild);
    REQUIRE(fired.has_value());
    CHECK(fired->builtin == "latibot-addressed");
}

TEST_CASE("a reply to a persona's message is the persona's to answer, as spoken to", "[triggers]") {
    fixture test;
    test.model.replied = claude;
    REQUIRE(test.responder(from_alice("what do you mean")).actions.size() == 1);
    CHECK(test.model.asked.back().how == latibot::capabilities::model_answer::spoken_to);
    CHECK(test.model.asked.back().persona_id == claude);
    CHECK_FALSE(test.model.asked.back().reply);

    // And a reply to LatiBot's is LatiBot's.
    test.model.replied = 0;
    REQUIRE(test.responder(from_alice("what do you mean")).actions.size() == 1);
    CHECK(test.model.asked.back().persona_id == 0);
}

TEST_CASE("LatiBot's name in passing is an analysis: the check is asked first", "[triggers]") {
    fixture test;
    REQUIRE(test.responder(from_alice("is latibot around?")).actions.size() == 1);
    CHECK(test.model.asked.back().how == latibot::capabilities::model_answer::analysis);
    CHECK_FALSE(test.model.asked.back().question.empty());

    // Nothing without the word.
    test.model.asked.clear();
    CHECK(test.responder(from_alice("is the bot around?")).actions.empty());
    CHECK(test.model.asked.empty());
}

TEST_CASE("an analysis needs words to match, never every message", "[triggers]") {
    trigger analysis{.guild_id = guild, .pattern = "weather", .mode = match_mode::whole_word, .question = "Is it about the weather?"};
    CHECK_FALSE(latibot::events::trigger_problem(analysis).has_value());

    analysis.mode = match_mode::regex;
    CHECK(latibot::events::trigger_problem(analysis).has_value());
    analysis.mode = match_mode::substring;
    analysis.pattern = "!!";
    CHECK(latibot::events::trigger_problem(analysis).has_value());
    analysis.pattern = "rain";
    analysis.condition = latibot::events::trigger_condition::addressed;
    CHECK(latibot::events::trigger_problem(analysis).has_value());

    // A comment needs a prompt; being addressed does not.
    trigger comment{.guild_id = guild, .pattern = "pizza", .mode = match_mode::whole_word};
    comment.condition = latibot::events::trigger_condition::addressed;
    CHECK(comment.kind() == latibot::events::trigger_kind::model);
    CHECK_FALSE(latibot::events::trigger_problem(comment).has_value());
    comment.condition = latibot::events::trigger_condition::text;
    CHECK(latibot::events::trigger_problem(comment).has_value());
}

TEST_CASE("the built-in triggers are made once, follow the personas, and are never removed", "[triggers]") {
    fixture test;
    const std::vector<std::int64_t> personas{claude};
    CHECK(test.store.ensure_builtins(guild, personas));
    CHECK_FALSE(test.store.ensure_builtins(guild, personas));

    auto all = test.store.for_guild(guild);
    REQUIRE(all.size() == 4);
    CHECK(all[0].builtin == "latibot-addressed");
    CHECK(all[1].builtin == "latibot-reply");
    CHECK(all[2].builtin == "latibot-named");
    CHECK(all[3].builtin == "persona-reply:7");
    CHECK_FALSE(test.store.remove(all[0].id, guild));

    // Turned off, it stays off.
    all[0].enabled = false;
    REQUIRE(test.store.update(all[0]));
    CHECK_FALSE(test.store.ensure_builtins(guild, personas));
    CHECK_FALSE(test.store.find(all[0].id, guild)->enabled);

    // The persona's goes with the persona.
    CHECK(test.store.ensure_builtins(guild, std::vector<std::int64_t>{}));
    CHECK(test.store.for_guild(guild).size() == 3);
}

TEST_CASE("fired by hand, an analysis skips its check and is answered as spoken to", "[triggers]") {
    fixture test;
    test.store.ensure_builtins(guild, std::vector<std::int64_t>{});
    const auto named = test.store.for_guild(guild)[2];
    REQUIRE(named.builtin == "latibot-named");

    latibot::events::stage_result result;
    test.responder.fire(named, from_alice("nothing to do with it"), result, /*forced=*/true);
    REQUIRE(result.actions.size() == 1);
    CHECK(test.model.asked.back().how == latibot::capabilities::model_answer::spoken_to);
}

TEST_CASE("the model's refusal is nothing posted, and only one model trigger is tried", "[triggers]") {
    fixture test;
    test.add_pizza();
    test.add_pizza(claude);
    test.model.admits = false;

    const auto result = test.responder(from_alice("pizza"));
    CHECK(result.actions.empty());
    CHECK_FALSE(result.answered);
    CHECK(test.model.asked.size() == 1);
}

TEST_CASE("LatiBot and the personas set off each other's triggers that answer bots, never their own", "[triggers]") {
    fixture test;

    SECTION("LatiBot's message, for a persona's trigger") {
        test.add_pizza(0, true);
        test.add_pizza(claude, true);
        test.model.writer = 0;
        REQUIRE(test.responder(from_ours("pizza is a vegetable")).actions.size() == 1);
        CHECK(test.model.asked.back().persona_id == claude);
    }

    SECTION("a persona's message, for LatiBot's trigger") {
        test.add_pizza(claude, true);
        test.add_pizza(0, true);
        test.model.writer = claude;
        REQUIRE(test.responder(from_ours("pizza is a vegetable")).actions.size() == 1);
        CHECK(test.model.asked.back().persona_id == 0);
    }

    SECTION("a trigger that does not answer bots answers only people") {
        test.add_pizza(0, false);
        test.model.writer = claude;
        CHECK(test.responder(from_ours("pizza")).actions.empty());
        test.model.writer.reset();
        CHECK_FALSE(test.responder(from_alice("pizza")).actions.empty());
    }

    SECTION("a message that is not ours, or sets off nothing, sets off nothing") {
        test.add_pizza(claude, true);
        test.model.writer.reset();
        CHECK(test.responder(from_ours("pizza")).actions.empty());
        CHECK(test.model.asked.empty());
    }

    SECTION("fixed replies never answer the bot's own messages") {
        test.store.add({.guild_id = guild,
                        .pattern = "pizza",
                        .mode = match_mode::substring,
                        .cooldown = 0s,
                        .enabled = true,
                        .respond_to_bots = true,
                        .responses = {{.text = "nice", .weight = 1}}});
        test.model.writer = 0;
        CHECK(test.responder(from_ours("pizza")).actions.empty());
    }
}

TEST_CASE("a persona's answer is never a reply, which a webhook cannot post", "[triggers]") {
    fixture test;
    test.add_pizza(claude);
    REQUIRE(test.responder(from_alice("pizza")).actions.size() == 1);
    CHECK_FALSE(test.model.asked.back().reply);
}

TEST_CASE("without the model, a model trigger is kept and never fires", "[triggers]") {
    fixture test;
    test.add_pizza();
    trigger_responder alone(test.store, test.clock, [] { return std::uint64_t{0}; });
    CHECK(alone(from_alice("pizza")).actions.empty());

    // And the bot's own messages are left alone entirely.
    CHECK(alone(from_ours("pizza")).actions.empty());
}

TEST_CASE("a fixed trigger can answer as a reply to the message", "[triggers]") {
    fixture test;
    const std::int64_t id = test.store.add({.guild_id = guild,
                                            .pattern = "420",
                                            .mode = match_mode::whole_word,
                                            .cooldown = 0s,
                                            .enabled = true,
                                            .responses = {{.text = "nice", .weight = 1}}});

    auto plain = test.responder(from_alice("420"));
    REQUIRE(plain.actions.size() == 1);
    CHECK(std::get<send_message>(plain.actions.front()).reply_to.empty());

    auto entry = test.store.find(id, guild);
    entry->reply = true;
    REQUIRE(test.store.update(*entry));
    auto replied = test.responder(from_alice("420"));
    REQUIRE(replied.actions.size() == 1);
    CHECK(std::get<send_message>(replied.actions.front()).reply_to == dpp::snowflake{5555});
}

TEST_CASE("Fire a trigger is a message's Apps menu command, for Manage Messages", "[triggers]") {
    fixture test;
    latibot::commands::fire_command fire(test.store, test.responder, &test.model, [] { return dpp::snowflake{1}; }, [](auto) {});
    const dpp::slashcommand payload = fire.build(std::string(latibot::commands::fire_command_name), dpp::snowflake{2});
    CHECK(payload.type == dpp::ctxm_message);
    CHECK(payload.name == "Fire a trigger");
    CHECK(fire.info().default_member_permissions == dpp::permission(dpp::p_manage_messages));
}

TEST_CASE("the fire menu pages its triggers 25 at a time, and says when there are none", "[triggers]") {
    std::vector<trigger> many;
    for (int index = 1; index <= 30; ++index) {
        many.push_back(
            {.id = index, .guild_id = guild, .pattern = std::format("word{}", index), .responses = {{.text = "x", .weight = 1}}});
    }
    const dpp::message first = latibot::commands::render_fire_menu(many, 0, 9);
    REQUIRE(first.components.size() == 2);
    CHECK(first.components[0].components[0].options.size() == 25);
    const dpp::message second = latibot::commands::render_fire_menu(many, 1, 9);
    CHECK(second.components[0].components[0].options.size() == 5);

    CHECK(latibot::commands::render_fire_menu({}, 0, 9).components.empty());
}

TEST_CASE("a message from a context menu reads as the stages read one", "[triggers]") {
    dpp::message from;
    from.id = dpp::snowflake{77};
    from.channel_id = channel;
    from.author.id = dpp::snowflake{9};
    from.author.username = "alice";
    from.content = "pizza";
    const auto incoming = latibot::commands::as_incoming(from, guild, dpp::snowflake{1});
    CHECK(incoming.message_id == dpp::snowflake{77});
    CHECK(incoming.content == "pizza");
    CHECK(incoming.author_name == "alice");
    CHECK_FALSE(incoming.from_self);

    from.author.id = dpp::snowflake{1};
    CHECK(latibot::commands::as_incoming(from, guild, dpp::snowflake{1}).from_self);
}
