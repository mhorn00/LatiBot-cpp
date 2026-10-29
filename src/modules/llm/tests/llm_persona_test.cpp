// Personas: someone else the model answers as, under its own name and
// picture, through a webhook (src/modules/llm/docs/Language_Model.md §2.11,
// §3.9).

#include "aliases.hpp"
#include "conversation.hpp"
#include "core/capabilities/model_replies.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "documents.hpp"
#include "guards.hpp"
#include "llm_command.hpp"
#include "llm_config.hpp"
#include "memory.hpp"
#include "memory_tools.hpp"
#include "persona_webhooks.hpp"
#include "personas.hpp"
#include "responder.hpp"
#include "settings.hpp"
#include "spend.hpp"
#include "stage.hpp"
#include "tools.hpp"

#include "llm_module.hpp"
#include "mock_llm.hpp"
#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "mocks/mock_speech.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

using latibot::events::incoming_message;
using latibot::llm::approach;
using latibot::llm::ask_llm;
using latibot::llm::document_kind;
using latibot::llm::document_ref;
using latibot::llm::persona;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake bot_id{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake other_bot{99};

/// The webhook the personas post through.
constexpr dpp::snowflake persona_hook{8500};

/// Noon on 2026-03-15, UTC.
constexpr std::chrono::sys_seconds noon{std::chrono::sys_days{std::chrono::year{2026} / 3 / 15} + 12h};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::llm::llm_config config;
    latibot::llm::blacklist_store blacklist{db};
    latibot::llm::usage_store usage{db};
    latibot::llm::document_store documents{db};
    latibot::llm::memory_store memories{db};
    latibot::llm::alias_store aliases{db};
    latibot::llm::persona_store personas{db};
    latibot::llm::tool_registry tools;

    latibot::testing::mock_clock clock{noon};
    latibot::testing::mock_discord discord;
    latibot::testing::mock_llm model;
    latibot::testing::mock_speech speech;
    std::vector<std::string> secrets;

    /// Which providers have a key.
    bool anthropic_key = true;
    bool openai_key = true;

    latibot::llm::persona_webhooks webhooks{discord, [] { return bot_id; }, [this](std::string token) { secrets.push_back(token); }};
    latibot::llm::conversation_windows windows{clock};

    latibot::llm::llm_stage stage{
        {.settings = &settings,
         .section = &config,
         .blacklist = &blacklist,
         .usage = &usage,
         .personas = &personas,
         .speech = &speech,
         .replacements = nullptr,
         .windows = &windows,
         .activity = nullptr,
         .has_provider =
             [this](latibot::llm::provider_kind kind) { return kind == latibot::llm::provider_kind::openai ? openai_key : anthropic_key; },
         .me = [] { return latibot::llm::bot_identity{.id = bot_id, .name = "LatiBot"}; },
         .persona_webhook = [](dpp::snowflake id) { return id == persona_hook; }},
        clock};

    latibot::llm::responder responder{{.discord = &discord,
                                       .clock = &clock,
                                       .settings = &settings,
                                       .section = &config,
                                       .documents = &documents,
                                       .memories = &memories,
                                       .usage = &usage,
                                       .tools = &tools,
                                       .aliases = &aliases,
                                       .provider_for = [this](latibot::llm::provider_kind) -> latibot::llm::provider* { return &model; },
                                       .speech = &speech,
                                       .personas = &personas,
                                       .webhooks = &webhooks},
                                      [] { return latibot::llm::bot_identity{.id = bot_id, .name = "LatiBot"}; }};

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
        settings.set_bool(guild, latibot::llm::enabled_key, true);
        latibot::llm::add_memory_tools(tools, memories);
        discord.webhook_creator = bot_id;
    }

    auto make(std::string name, std::string model_id = {}) -> persona {
        persona made{.id = 0,
                     .guild_id = guild,
                     .name = std::move(name),
                     .avatar_url = "https://example.com/claude.png",
                     .model = std::move(model_id),
                     .enabled = true,
                     .created_by = alice,
                     .created_at = noon};
        made.id = personas.add(made);
        return made;
    }

    auto answer(ask_llm ask) -> latibot::llm::answer_report {
        auto report = responder.answer(std::move(ask)).sync_wait_for(2s);
        REQUIRE(report.has_value());
        return *report;
    }
};

auto from_alice(std::string content) -> incoming_message {
    incoming_message message;
    message.guild_id = guild;
    message.channel_id = channel;
    message.message_id = dpp::snowflake{5000};
    message.author_id = alice;
    message.author_name = "Alice";
    message.content = std::move(content);
    return message;
}

/// LatiBot's own message, which the pipeline gives the stage as it gives it
/// any other.
auto from_latibot(std::string content) -> incoming_message {
    incoming_message message = from_alice(std::move(content));
    message.author_id = bot_id;
    message.author_name = "LatiBot";
    message.from_self = true;
    message.from_bot = true;
    return message;
}

/// A persona's message, as it arrives from the webhook: under its name.
auto from_persona(std::string name, std::string content, dpp::snowflake hook = persona_hook) -> incoming_message {
    incoming_message message = from_alice(std::move(content));
    message.author_id = hook;
    message.author_name = std::move(name);
    message.from_bot = true;
    message.webhook_id = hook;
    return message;
}

/// A trigger that fired, as the triggers module hands it over, that
/// `persona_id` answers, 0 for LatiBot.
auto pizza_trigger(std::int64_t persona_id) -> latibot::capabilities::model_trigger {
    return {.trigger_id = 1, .prompt = "Have an opinion about pizza.", .persona_id = persona_id, .reply = true};
}

auto asked(const latibot::llm::stage_result& result) -> const ask_llm* {
    return result.actions.size() == 1 ? std::get_if<ask_llm>(&result.actions.front()) : nullptr;
}

/// The ask a trigger that fired comes to, as the stage decides it; nothing
/// when it refused, or only had a notice to post.
auto trigger_ask(latibot::llm::llm_stage& stage, const incoming_message& message, const latibot::capabilities::model_trigger& fired)
    -> std::optional<ask_llm> {
    const auto result = stage.trigger_ask(message, fired);
    for (const auto& step : result.actions) {
        if (const auto* ask = std::get_if<ask_llm>(&step)) return *ask;
    }
    return std::nullopt;
}

auto ask_as(std::int64_t persona_id, std::string content) -> ask_llm {
    return {.guild_id = guild,
            .channel_id = channel,
            .message_id = dpp::snowflake{5000},
            .author_id = alice,
            .author_name = "Alice",
            .content = std::move(content),
            .author_is_bot = false,
            .reply_to = {},
            .how = approach::addressed,
            .check_first = false,
            .trigger_id = 0,
            .context_prompt = {},
            .speak = false,
            .wait = 0s,
            .persona_id = persona_id};
}

auto history_message(std::uint64_t id, dpp::snowflake author, std::string name, const std::string& content) -> dpp::message {
    dpp::message message(channel, content);
    message.id = dpp::snowflake{id};
    message.author.id = author;
    message.author.username = std::move(name);
    return message;
}

} // namespace

// --------------------------------------------------------------------------
// Names, pictures and the store
// --------------------------------------------------------------------------

TEST_CASE("a persona's name must be one Discord shows, and not the bot's own", "[llm][persona]") {
    CHECK_FALSE(latibot::llm::persona_name_problem("Claude").has_value());
    CHECK_FALSE(latibot::llm::persona_name_problem("  Claude Opus  ").has_value());

    CHECK(latibot::llm::persona_name_problem("").has_value());
    CHECK(latibot::llm::persona_name_problem("   ").has_value());
    CHECK(latibot::llm::persona_name_problem(std::string(81, 'a')).has_value());
    CHECK(latibot::llm::persona_name_problem("latibot").has_value());
    // Discord refuses these in a webhook's name.
    CHECK(latibot::llm::persona_name_problem("Clyde Jr").has_value());
    CHECK(latibot::llm::persona_name_problem("my discord friend").has_value());
    CHECK(latibot::llm::persona_name_problem("everyone").has_value());
}

TEST_CASE("a persona's picture is an https link, or none", "[llm][persona]") {
    CHECK_FALSE(latibot::llm::avatar_problem("").has_value());
    CHECK_FALSE(latibot::llm::avatar_problem("https://example.com/a.png").has_value());
    CHECK(latibot::llm::avatar_problem("http://example.com/a.png").has_value());
    CHECK(latibot::llm::avatar_problem("example.com/a.png").has_value());
    CHECK(latibot::llm::avatar_problem("https://example.com/a b.png").has_value());
}

TEST_CASE("personas are kept per server, found by name whatever its case, and changed", "[llm][persona]") {
    fixture test;
    const persona claude = test.make("Claude");
    test.make("Zed", "gpt-6-luna");

    CHECK(test.personas.count(guild) == 2);
    CHECK(test.personas.count(dpp::snowflake{2000}) == 0);
    REQUIRE(test.personas.for_guild(guild).size() == 2);
    CHECK(test.personas.for_guild(guild).front().name == "Claude");
    CHECK(test.personas.for_guild(guild).back().model == "gpt-6-luna");

    const auto found = test.personas.find_named(guild, "cLaUdE");
    REQUIRE(found.has_value());
    CHECK(found->id == claude.id);
    CHECK_FALSE(test.personas.find_named(dpp::snowflake{2000}, "Claude").has_value());

    persona changed = *found;
    changed.name = "Claude Opus";
    changed.enabled = false;
    CHECK(test.personas.update(changed));
    CHECK_FALSE(test.personas.find(claude.id, guild)->enabled);
    CHECK(test.personas.find(claude.id, guild)->name == "Claude Opus");
    CHECK(test.personas.find(claude.id, guild)->model.empty());
}

TEST_CASE("removing a persona takes its documents and posts with it, and nothing else", "[llm][persona]") {
    // Its triggers are the triggers module's, which keeps them, answered by
    // nobody until given to someone else.
    fixture test;
    const persona claude = test.make("Claude");
    const persona zed = test.make("Zed");

    test.documents.save(guild, document_ref(document_kind::system, claude.id), "You are Claude.", alice, noon);
    test.documents.save(guild, document_ref(document_kind::system, zed.id), "You are Zed.", alice, noon);
    test.documents.save(guild, document_kind::system, "Server rules.", alice, noon);
    test.personas.record_post(dpp::snowflake{7000}, claude.id, guild, channel, noon);
    test.personas.record_post(dpp::snowflake{7001}, zed.id, guild, channel, noon);

    CHECK(test.personas.poster(dpp::snowflake{7000}) == claude.id);

    REQUIRE(test.personas.remove(claude.id, guild));
    CHECK_FALSE(test.personas.find(claude.id, guild).has_value());
    CHECK(test.documents.history(guild, document_ref(document_kind::system, claude.id)).empty());
    CHECK_FALSE(test.personas.poster(dpp::snowflake{7000}).has_value());
    CHECK_FALSE(trigger_ask(test.stage, from_alice("pizza"), pizza_trigger(claude.id)).has_value());

    CHECK(test.documents.text(guild, document_ref(document_kind::system, zed.id)) == "You are Zed.");
    CHECK(test.documents.text(guild, document_kind::system) == "Server rules.");
}

TEST_CASE("a persona's documents are its own, empty until written, and named in forms", "[llm][persona]") {
    fixture test;
    const persona claude = test.make("Claude");
    const document_ref system(document_kind::system, claude.id);

    CHECK(latibot::llm::to_string(system) == std::format("persona.{}.system", claude.id));
    CHECK(latibot::llm::document_ref_from_string(latibot::llm::to_string(system)) == system);
    CHECK(latibot::llm::document_ref_from_string("personality") == document_ref(document_kind::personality));
    CHECK_FALSE(latibot::llm::document_ref_from_string("persona.7.trigger_style").has_value());
    CHECK_FALSE(latibot::llm::document_ref_from_string("persona.0.system").has_value());
    CHECK_FALSE(latibot::llm::document_ref_from_string("persona.7x.system").has_value());

    CHECK(test.documents.text(guild, document_ref(document_kind::personality, claude.id)).empty());
    test.documents.save(guild, system, "first", alice, noon);
    CHECK(test.documents.save(guild, system, "second", alice, noon) == 2);
    CHECK(test.documents.text(guild, document_kind::system).empty());

    const latibot::commands::document_target target(system, "Claude");
    CHECK(latibot::commands::document_label(target) == "Claude's system instructions");
    CHECK(latibot::commands::document_label(document_kind::personality) == "the personality");
    CHECK(latibot::commands::describe_saved(target, 2, "short").starts_with("saved Claude's system instructions as version 2"));
    CHECK(latibot::commands::render_history(target, {}).contains("Nobody has edited Claude's system instructions"));
}

// --------------------------------------------------------------------------
// When a persona answers
// --------------------------------------------------------------------------

TEST_CASE("a persona answers no name: only LatiBot does", "[llm][persona]") {
    fixture test;
    test.make("Claude");

    CHECK(asked(test.stage(from_alice("claude, what's 2+2"))) == nullptr);
    CHECK(asked(test.stage(from_alice("Claude what's 2+2"))) == nullptr);

    // LatiBot's own name still works, whatever persona is named beside it.
    incoming_message message = from_alice("claude says hi");
    message.mentions_bot = true;
    const auto result = test.stage(message);
    REQUIRE(asked(result) != nullptr);
    CHECK(asked(result)->persona_id == 0);
}

TEST_CASE("a reply to a persona's message is for that persona", "[llm][persona]") {
    fixture test;
    const persona claude = test.make("Claude");
    test.personas.record_post(dpp::snowflake{7000}, claude.id, guild, channel, noon);

    incoming_message message = from_alice("and then?");
    message.reply_to = dpp::snowflake{7000};
    const auto result = test.stage(message);
    REQUIRE(asked(result) != nullptr);
    CHECK(asked(result)->persona_id == claude.id);

    message.reply_to = dpp::snowflake{7001};
    CHECK(asked(test.stage(message)) == nullptr);
}

TEST_CASE("a persona never answers a bot's reply, and an off one never answers", "[llm][persona]") {
    fixture test;
    persona claude = test.make("Claude");
    test.personas.record_post(dpp::snowflake{7000}, claude.id, guild, channel, noon);

    incoming_message from_bot = from_alice("and then?");
    from_bot.reply_to = dpp::snowflake{7000};
    from_bot.author_id = other_bot;
    from_bot.from_bot = true;
    from_bot.author_is_allowed_bot = true;
    CHECK(asked(test.stage(from_bot)) == nullptr);

    incoming_message reply = from_alice("and then?");
    reply.reply_to = dpp::snowflake{7000};
    CHECK(asked(test.stage(reply)) != nullptr);
    claude.enabled = false;
    test.personas.update(claude);
    CHECK(asked(test.stage(reply)) == nullptr);
}

TEST_CASE("a persona's trigger is answered by the persona, and only while it is on", "[llm][persona]") {
    fixture test;
    persona claude = test.make("Claude");

    const auto fired = trigger_ask(test.stage, from_alice("i read about pizza today"), pizza_trigger(claude.id));
    REQUIRE(fired.has_value());
    CHECK(fired->how == approach::trigger);
    CHECK(fired->persona_id == claude.id);

    claude.enabled = false;
    test.personas.update(claude);
    CHECK_FALSE(trigger_ask(test.stage, from_alice("i read about pizza today"), pizza_trigger(claude.id)).has_value());
}

TEST_CASE("LatiBot and the personas are told apart, and named, when one sets off another's trigger", "[llm][persona]") {
    // Which triggers they may set off is the triggers module's rule; who
    // wrote the message, and how the model is shown them, is the stage's.
    fixture test;
    const persona claude = test.make("Claude");
    const persona gemini = test.make("Gemini");

    const auto latibot = test.stage.own_writer_of(from_latibot("pizza is a vegetable"));
    REQUIRE(latibot.has_value());
    CHECK(latibot->persona_id == 0);
    CHECK(latibot->name == "LatiBot");

    // Whatever case its name is shown in.
    const auto persona_writer = test.stage.own_writer_of(from_persona("claude", "pizza is a vegetable"));
    REQUIRE(persona_writer.has_value());
    CHECK(persona_writer->persona_id == claude.id);

    const auto for_claude = trigger_ask(test.stage, from_latibot("pizza is a vegetable"), pizza_trigger(claude.id));
    REQUIRE(for_claude.has_value());
    CHECK(for_claude->persona_id == claude.id);
    CHECK(for_claude->author_is_bot);
    CHECK(for_claude->speaker == "LatiBot");

    const auto for_latibot = trigger_ask(test.stage, from_persona("Claude", "pizza is a vegetable"), pizza_trigger(0));
    REQUIRE(for_latibot.has_value());
    CHECK(for_latibot->persona_id == 0);
    CHECK(for_latibot->speaker == "Claude");

    const auto for_gemini = trigger_ask(test.stage, from_persona("Claude", "pizza"), pizza_trigger(gemini.id));
    REQUIRE(for_gemini.has_value());
    CHECK(for_gemini->persona_id == gemini.id);
}

TEST_CASE("an allowed bot's message is a bot's to the trigger it sets off, with no name of ours", "[llm][persona]") {
    fixture test;
    const persona claude = test.make("Claude");
    incoming_message message = from_alice("pizza");
    message.author_id = other_bot;
    message.from_bot = true;
    message.author_is_allowed_bot = true;

    CHECK_FALSE(test.stage.own_writer_of(message).has_value());
    const auto result = trigger_ask(test.stage, message, pizza_trigger(claude.id));
    REQUIRE(result.has_value());
    CHECK(result->persona_id == claude.id);
    CHECK(result->author_is_bot);
    CHECK(result->speaker.empty());
}

TEST_CASE("a command's response is not LatiBot talking, and sets off no trigger", "[llm][persona]") {
    // `/trigger edit` answers with the trigger's own pattern, which would set
    // the trigger off.
    fixture test;
    const persona claude = test.make("Claude");

    incoming_message result = from_latibot("updated `1` **pizza** (as Claude, answers bots)");
    result.command_output = true;
    CHECK_FALSE(test.stage.own_writer_of(result).has_value());
    CHECK_FALSE(trigger_ask(test.stage, result, pizza_trigger(claude.id)).has_value());
    CHECK(trigger_ask(test.stage, from_latibot("pizza is a vegetable"), pizza_trigger(claude.id)).has_value());
}

TEST_CASE("only LatiBot's own webhook speaks for a persona", "[llm][persona]") {
    fixture test;
    test.make("Claude");

    // Someone else's webhook is a bot nobody allowed, and a name no persona
    // has could be anyone's.
    CHECK_FALSE(test.stage.own_writer_of(from_persona("Claude", "pizza", dpp::snowflake{8501})).has_value());
    CHECK_FALSE(test.stage.own_writer_of(from_persona("Mallory", "pizza")).has_value());
    CHECK(test.stage.own_writer_of(from_persona("Claude", "pizza")).has_value());
    CHECK_FALSE(trigger_ask(test.stage, from_persona("Mallory", "pizza"), pizza_trigger(0)).has_value());
}

TEST_CASE("LatiBot answers its name from a persona that talks, and nothing else answers a name between them", "[llm][persona]") {
    fixture test;
    persona claude = test.make("Claude");
    const persona gemini = test.make("Gemini");

    SECTION("a persona that does not talk is not answered by name") {
        CHECK(asked(test.stage(from_persona("Claude", "LatiBot, what do you think?"))) == nullptr);
        incoming_message mention = from_persona("Claude", "what do you think?");
        mention.mentions_bot = true;
        CHECK(asked(test.stage(mention)) == nullptr);
    }

    claude.talks = true;
    test.personas.update(claude);

    SECTION("one that talks is answered, by its name first or a mention, at once") {
        const auto to_latibot = test.stage(from_persona("Claude", "LatiBot, what do you think?"));
        REQUIRE(asked(to_latibot) != nullptr);
        CHECK(asked(to_latibot)->how == approach::addressed);
        CHECK(asked(to_latibot)->persona_id == 0);
        CHECK(asked(to_latibot)->speaker == "Claude");
        CHECK_FALSE(asked(to_latibot)->check_first);

        incoming_message mention = from_persona("Claude", "what do you think?");
        mention.mentions_bot = true;
        CHECK(asked(test.stage(mention))->persona_id == 0);
    }

    SECTION("named in passing, it goes to the check, with conversation mode on") {
        CHECK(asked(test.stage(from_persona("Claude", "hey LatiBot, you there?"))) == nullptr);
        test.settings.set_bool(guild, latibot::llm::conversation_key, true);
        const auto checked = test.stage(from_persona("Claude", "hey LatiBot, you there?"));
        REQUIRE(asked(checked) != nullptr);
        CHECK(asked(checked)->how == approach::named);
        CHECK(asked(checked)->check_first);
    }

    SECTION("no persona answers its name, from LatiBot or another, and never a reply") {
        CHECK(asked(test.stage(from_latibot("claude, what do you think?"))) == nullptr);
        CHECK(asked(test.stage(from_persona("Claude", "gemini, your turn"))) == nullptr);
        CHECK(asked(test.stage(from_latibot("LatiBot here, hello"))) == nullptr);

        test.personas.record_post(dpp::snowflake{7001}, gemini.id, guild, channel, noon);
        incoming_message reply = from_latibot("fair point");
        reply.reply_to = dpp::snowflake{7001};
        CHECK(asked(test.stage(reply)) == nullptr);
    }
}

TEST_CASE("in a channel the model ignores, nobody's message reaches it", "[llm][persona]") {
    fixture test;
    const persona claude = test.make("Claude");

    incoming_message person = from_alice("latibot, pizza?");
    person.channel_ignores.model = true;
    CHECK(test.stage(person).actions.empty());
    CHECK_FALSE(test.stage(person).consumed);

    incoming_message ours = from_latibot("pizza");
    ours.channel_ignores.model = true;
    CHECK_FALSE(trigger_ask(test.stage, ours, pizza_trigger(claude.id)).has_value());

    // Elsewhere, the same messages are answered.
    CHECK(asked(test.stage(from_alice("latibot, pizza?"))) != nullptr);
    CHECK(trigger_ask(test.stage, from_latibot("pizza"), pizza_trigger(claude.id)).has_value());
}

TEST_CASE("each persona has conversations of its own, and the one that replied last is joined", "[llm][persona][conversation]") {
    fixture test;
    const persona claude = test.make("Claude");
    persona gemini = test.make("Gemini");
    test.settings.set_bool(guild, latibot::llm::conversation_key, true);

    // Nobody is talking yet: an unnamed message is left alone.
    CHECK(asked(test.stage(from_alice("that's wild"))) == nullptr);

    test.windows.opened(channel, claude.id);
    const auto with_claude = test.stage(from_alice("that's wild"));
    REQUIRE(asked(with_claude) != nullptr);
    CHECK(asked(with_claude)->how == approach::joined_in);
    CHECK(asked(with_claude)->check_first);
    CHECK(asked(with_claude)->persona_id == claude.id);

    // LatiBot replied since: the message is more likely for it.
    test.clock.advance(1s);
    test.windows.opened(channel);
    CHECK(asked(test.stage(from_alice("that's wild")))->persona_id == 0);

    SECTION("a persona's name is just a word, even during its conversation; LatiBot's is not") {
        const auto named_claude = test.stage(from_alice("thanks claude"));
        REQUIRE(asked(named_claude) != nullptr);
        CHECK(asked(named_claude)->how == approach::joined_in);
        CHECK(asked(named_claude)->persona_id == 0);

        const auto named_latibot = test.stage(from_alice("what do latibot and claude think?"));
        REQUIRE(asked(named_latibot) != nullptr);
        CHECK(asked(named_latibot)->how == approach::named);
        CHECK(asked(named_latibot)->persona_id == 0);
    }

    SECTION("a persona that is off has no conversation") {
        test.clock.advance(1s);
        test.windows.opened(channel, gemini.id);
        gemini.enabled = false;
        test.personas.update(gemini);
        CHECK(asked(test.stage(from_alice("that's wild")))->persona_id == 0);
        CHECK(asked(test.stage(from_alice("thanks gemini")))->persona_id == 0);
    }

    SECTION("every window keeps its own limits") {
        test.clock.advance(590s);
        test.windows.opened(channel, claude.id);
        test.clock.advance(20s);
        // LatiBot's went quiet; Claude's is still open.
        CHECK(asked(test.stage(from_alice("that's wild")))->persona_id == claude.id);
    }
}

TEST_CASE("the check is asked about the persona whose conversation it is", "[llm][persona][conversation][coro]") {
    fixture test;
    const persona claude = test.make("Claude");
    test.personas.record_post(dpp::snowflake{4002}, claude.id, guild, channel, noon);
    dpp::message from_claude = history_message(4002, persona_hook, "Claude", "I'm Claude.");
    from_claude.webhook_id = persona_hook;
    from_claude.author.flags |= dpp::u_bot;
    test.discord.message_pages.emplace_back(std::vector<dpp::message>{from_claude});

    ask_llm ask = ask_as(claude.id, "and what do you think");
    ask.how = approach::joined_in;
    ask.check_first = true;
    test.model.answer("yes");
    const auto report = test.responder.check(ask).sync_wait_for(2s);
    REQUIRE(report.has_value());
    CHECK(report->yes);

    const latibot::llm::request& sent = test.model.requests.back();
    CHECK(sent.stable_system == latibot::llm::check_instructions("Claude"));
    CHECK(sent.stable_system.contains("You decide whether Claude, a bot"));
    CHECK_FALSE(sent.stable_system.contains("LatiBot"));
    CHECK(sent.conversation.at(0).text.contains("Claude (you): I'm Claude."));
    CHECK(sent.conversation.at(0).text.ends_with("Should Claude reply to it? Answer yes or no."));
    CHECK(test.usage.spent_by_persona(claude.id, noon - 1h, noon + 1h) > 0);
}

TEST_CASE("a persona on a model with no key is not answered", "[llm][persona]") {
    fixture test;
    const persona zed = test.make("Zed", "gpt-6-luna");
    test.personas.record_post(dpp::snowflake{7000}, zed.id, guild, channel, noon);
    incoming_message reply = from_alice("hi");
    reply.reply_to = dpp::snowflake{7000};
    test.openai_key = false;
    CHECK(asked(test.stage(reply)) == nullptr);
    test.openai_key = true;
    CHECK(asked(test.stage(reply)) != nullptr);
}

TEST_CASE("a persona answers as itself: through the webhook, with its own instructions, the tools and its memories",
          "[llm][persona][coro]") {
    fixture test;
    const persona claude = test.make("Claude", "claude-sonnet-5-5");
    test.memories.add(
        {.id = 0, .guild_id = guild, .subject = {}, .content = "the server has a cat", .created_by = alice, .created_at = noon});
    test.memories.add(
        {.id = 0, .guild_id = guild, .subject = {}, .content = "LatiBot's cat plan", .created_by = alice, .created_at = noon, .owner = 0});
    test.memories.add({.id = 0,
                       .guild_id = guild,
                       .subject = {},
                       .content = "Claude's cat opinion",
                       .created_by = alice,
                       .created_at = noon,
                       .owner = claude.id});
    test.documents.save(guild, document_ref(document_kind::system, claude.id), "You are Claude, made by Anthropic.", alice, noon);
    test.documents.save(guild, document_kind::personality, "Talk like a pirate.", alice, noon);
    test.model.answer("Four.");

    const auto report = test.answer(ask_as(claude.id, "claude, what's 2+2, and how is the cat"));
    CHECK(report.posted == std::vector<std::string>{"Four."});

    // The request: its rules, its documents, the tools, the shared memories
    // and its own, never LatiBot's own.
    REQUIRE(test.model.requests.size() == 1);
    const latibot::llm::request& sent = test.model.requests[0];
    CHECK(sent.model == "claude-sonnet-5-5");
    CHECK(sent.stable_system.starts_with("You are Claude, a character"));
    CHECK(sent.stable_system.contains("## Who you are\nYou are Claude, made by Anthropic."));
    CHECK_FALSE(sent.stable_system.contains("pirate"));
    CHECK(std::ranges::any_of(sent.tools, [](const auto& tool) { return tool.name == "remember"; }));
    CHECK(sent.varying_system.contains("the server has a cat"));
    CHECK(sent.varying_system.contains("(yours only): Claude's cat opinion"));
    CHECK_FALSE(sent.varying_system.contains("LatiBot's cat plan"));

    // Posted through the bot's webhook, made once, as Claude, and not as a
    // reply, which a webhook cannot send.
    CHECK(test.discord.sent.empty());
    CHECK(test.discord.typing.empty());
    REQUIRE(test.discord.webhooks_created.size() == 1);
    CHECK(test.discord.webhooks_created[0] == std::pair{channel, std::string(latibot::llm::persona_webhook_name)});
    REQUIRE(test.discord.webhook_posts.size() == 1);
    const auto& post = test.discord.webhook_posts[0];
    CHECK(post.username == "Claude");
    CHECK(post.avatar_url == "https://example.com/claude.png");
    CHECK(post.thread.empty());
    CHECK(post.message.message_reference.message_id.empty());
    CHECK(test.secrets == std::vector<std::string>{post.hook.token});

    // What it posted is noted as its, and what it spent as its.
    CHECK(test.personas.poster(dpp::snowflake{1002}) == claude.id);
    CHECK(test.usage.spent_by_persona(claude.id, noon - 1h, noon + 1h) > 0);
}

TEST_CASE("a persona reads itself as you, and LatiBot as a bot; LatiBot reads the persona by name", "[llm][persona][coro]") {
    fixture test;
    const persona claude = test.make("Claude");
    test.personas.record_post(dpp::snowflake{4002}, claude.id, guild, channel, noon);

    const auto page = [] {
        dpp::message from_claude = history_message(4002, dpp::snowflake{6000}, "Claude", "I'm Claude.");
        from_claude.webhook_id = dpp::snowflake{6000};
        from_claude.author.flags |= dpp::u_bot;
        dpp::message from_latibot = history_message(4001, bot_id, "LatiBot", "And I'm LatiBot.");
        from_latibot.author.flags |= dpp::u_bot;
        return std::vector<dpp::message>{from_claude, from_latibot};
    };

    test.discord.message_pages.emplace_back(page());
    test.model.answer("Hello.");
    test.answer(ask_as(claude.id, "claude, who's here?"));
    const std::string as_claude = test.model.requests.back().conversation.at(0).text;
    CHECK(as_claude.contains("Claude (you): I'm Claude."));
    CHECK(as_claude.contains("LatiBot (a bot): And I'm LatiBot."));
    // Its name, typed by a person, is not a person's name to hide.
    CHECK(as_claude.contains(": claude, who's here?"));

    test.discord.message_pages.emplace_back(page());
    test.model.answer("Hello.");
    test.answer(ask_as(0, "latibot, who's here?"));
    const std::string as_latibot = test.model.requests.back().conversation.at(0).text;
    CHECK(as_latibot.contains("Claude (a bot): I'm Claude."));
    CHECK(as_latibot.contains("LatiBot (you): And I'm LatiBot."));
}

TEST_CASE("the message a trigger answers shows LatiBot or a persona by name", "[llm][persona][coro]") {
    fixture test;
    const persona claude = test.make("Claude");

    ask_llm ask = ask_as(claude.id, "pizza is a vegetable");
    ask.author_id = bot_id;
    ask.author_name = "LatiBot";
    ask.author_is_bot = true;
    ask.how = approach::trigger;
    ask.context_prompt = "Have an opinion about pizza.";
    ask.speaker = "LatiBot";
    test.model.answer("It is not.");
    test.answer(ask);
    CHECK(test.model.requests.back().conversation.at(0).text.contains("LatiBot (a bot): pizza is a vegetable"));

    ask = ask_as(0, "pizza is a vegetable");
    ask.author_id = persona_hook;
    ask.author_name = "Claude";
    ask.author_is_bot = true;
    ask.how = approach::trigger;
    ask.speaker = "Claude";
    test.model.answer("It is not.");
    test.answer(ask);
    CHECK(test.model.requests.back().conversation.at(0).text.contains("Claude (a bot): pizza is a vegetable"));
}

TEST_CASE("the persona webhook is found, kept, and made again when it is gone", "[llm][persona][coro]") {
    fixture test;
    const persona claude = test.make("Claude");

    SECTION("one the bot made before is used, not another") {
        test.discord.webhooks[channel] = {
            {.id = dpp::snowflake{8000}, .channel_id = channel, .name = "someone else's", .token = "t1", .created_by = alice},
            {.id = dpp::snowflake{8001},
             .channel_id = channel,
             .name = std::string(latibot::llm::persona_webhook_name),
             .token = "t2",
             .created_by = bot_id}};
        test.model.answer("one");
        test.model.answer("two");
        test.answer(ask_as(claude.id, "claude, one"));
        test.answer(ask_as(claude.id, "claude, two"));
        CHECK(test.discord.webhooks_created.empty());
        REQUIRE(test.discord.webhook_posts.size() == 2);
        CHECK(test.discord.webhook_posts[0].hook.id == dpp::snowflake{8001});
        CHECK(test.discord.webhook_posts[1].hook.id == dpp::snowflake{8001});

        // Its messages are the personas'; the other webhook's are not.
        CHECK(test.webhooks.owns(dpp::snowflake{8001}));
        CHECK_FALSE(test.webhooks.owns(dpp::snowflake{8000}));
    }

    SECTION("one deleted since is made again, once") {
        test.model.answer("one");
        test.answer(ask_as(claude.id, "claude, one"));
        REQUIRE(test.discord.webhooks_created.size() == 1);

        // Someone deletes it: the kept one fails, and the channel has none.
        test.discord.webhooks[channel].clear();
        test.discord.webhook_results.emplace_back(
            std::unexpected(latibot::ports::api_error{.http_status = 404, .message = "Unknown Webhook"}));
        test.model.answer("two");
        const auto report = test.answer(ask_as(claude.id, "claude, two"));
        CHECK(report.posted == std::vector<std::string>{"two"});
        CHECK(test.discord.webhooks_created.size() == 2);
        REQUIRE(test.discord.webhook_posts.size() == 3);
        CHECK(test.discord.webhook_posts[2].hook.id != test.discord.webhook_posts[0].hook.id);
    }

    SECTION("in a thread, through its parent channel's") {
        constexpr dpp::snowflake thread{3001};
        test.discord.thread_parents[thread] = channel;
        test.model.answer("in a thread");
        ask_llm ask = ask_as(claude.id, "claude, hi");
        ask.channel_id = thread;
        test.answer(ask);
        REQUIRE(test.discord.webhooks_created.size() == 1);
        CHECK(test.discord.webhooks_created[0].first == channel);
        REQUIRE(test.discord.webhook_posts.size() == 1);
        CHECK(test.discord.webhook_posts[0].thread == thread);
    }
}

TEST_CASE("a persona that fails says so as itself when asked, and one removed since says nothing", "[llm][persona][coro]") {
    fixture test;
    const persona claude = test.make("Claude");

    test.model.fail("overloaded", 529);
    test.answer(ask_as(claude.id, "claude, hi"));
    REQUIRE(test.discord.webhook_posts.size() == 1);
    CHECK(test.discord.webhook_posts[0].username == "Claude");
    CHECK(test.discord.webhook_posts[0].message.content.contains("overloaded"));
    CHECK(test.discord.sent.empty());

    test.personas.remove(claude.id, guild);
    const auto report = test.answer(ask_as(claude.id, "claude, hi"));
    CHECK(report.posted.empty());
    CHECK(test.model.requests.size() == 1);
}

// --------------------------------------------------------------------------
// /llm persona
// --------------------------------------------------------------------------

TEST_CASE("the persona list and view say what each is", "[llm][persona]") {
    CHECK(latibot::commands::render_personas({}).contains("/llm persona add"));

    const persona claude{.id = 3,
                         .guild_id = guild,
                         .name = "Claude",
                         .avatar_url = {},
                         .model = {},
                         .enabled = false,
                         .created_by = alice,
                         .created_at = noon};
    const std::vector<persona> all{claude};
    const std::string list = latibot::commands::render_personas(all);
    CHECK(list.contains("**Claude** (off)"));

    const std::string view = latibot::commands::render_persona(claude, "claude-haiku-4-5", 0.25);
    CHECK(view.contains("Claude Haiku 4.5, the server's model"));
    CHECK(view.contains("$0.25"));
    CHECK(view.contains("webhook's own"));
}

// --------------------------------------------------------------------------
// Speaking
// --------------------------------------------------------------------------

TEST_CASE("a persona answering in a voice session's channel is spoken", "[llm][persona]") {
    fixture test;
    const persona claude = test.make("Claude");
    test.personas.record_post(dpp::snowflake{7000}, claude.id, guild, channel, noon);
    incoming_message reply = from_alice("and you?");
    reply.reply_to = dpp::snowflake{7000};

    SECTION("in the session's channel") {
        test.speech.session_channels[guild] = channel;
        const auto result = test.stage(reply);
        REQUIRE(asked(result) != nullptr);
        CHECK(asked(result)->persona_id == claude.id);
        CHECK(asked(result)->speak);
    }
    SECTION("anywhere else") {
        const auto result = test.stage(reply);
        REQUIRE(asked(result) != nullptr);
        CHECK_FALSE(asked(result)->speak);
    }
}

TEST_CASE("a persona is spoken in its own voice, and LatiBot in the default", "[llm][persona][coro]") {
    fixture test;
    persona claude = test.make("Claude");
    claude.voice = "betty";
    REQUIRE(test.personas.update(claude));
    REQUIRE(test.personas.find(claude.id, guild)->voice == "betty");

    test.model.answer("ahoy there");
    ask_llm as_claude = ask_as(claude.id, "claude, say something");
    as_claude.speak = true;
    const auto report = test.answer(as_claude);
    REQUIRE(report.posted.size() == 1);
    CHECK(test.model.requests.at(0).stable_system.contains("## Speaking"));

    test.model.answer("and me");
    ask_llm as_latibot = ask_as(0, "latibot, say something");
    as_latibot.speak = true;
    test.answer(as_latibot);

    REQUIRE(test.speech.spoken.size() == 2);
    CHECK(test.speech.spoken[0].voice == "betty");
    CHECK(test.speech.spoken[0].text == report.posted[0]);
    CHECK(test.speech.spoken[1].voice.empty());
}

TEST_CASE("a persona's voice is shown when the bot speaks, and found ignoring case", "[llm][persona]") {
    persona claude{.id = 3,
                   .guild_id = guild,
                   .name = "Claude",
                   .avatar_url = {},
                   .model = {},
                   .enabled = true,
                   .created_by = alice,
                   .created_at = noon};
    CHECK_FALSE(latibot::commands::render_persona(claude, "claude-haiku-5-5", 0).contains("voice"));
    CHECK(latibot::commands::render_persona(claude, "claude-haiku-5-5", 0, true).contains("the default voice"));
    claude.voice = "betty";
    CHECK(latibot::commands::render_persona(claude, "claude-haiku-5-5", 0, true).contains("speaks as betty"));

    const std::vector<std::string> voices{"paul", "betty", "deep"};
    CHECK(latibot::commands::find_voice(voices, " Betty ") == "betty");
    CHECK(latibot::commands::find_voice(voices, "DEEP") == "deep");
    CHECK_FALSE(latibot::commands::find_voice(voices, "chris").has_value());
}
