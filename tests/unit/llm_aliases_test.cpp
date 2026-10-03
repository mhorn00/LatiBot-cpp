// Who the model is told about, without being told who they are
// (docs/features/Language_Model.md §3.8): aliases in place of Discord ids,
// markers in place of names, and names put back only in what is posted.
// Every id and name here is made up.

#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/llm/aliases.hpp"

#include "mocks/mock_discord.hpp"

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <set>
#include <string>

using latibot::llm::alias_store;
using latibot::llm::aliases_as_mentions;
using latibot::llm::is_alias;
using latibot::llm::people;
using latibot::llm::random_alias;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake bot_id{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

struct fixture {
    latibot::db::database db{":memory:"};
    alias_store aliases{db};
    latibot::testing::mock_discord discord;
    people cast{aliases, discord, guild, bot_id, "LatiBot"};

    fixture() { latibot::db::migrate(db); }
};

} // namespace

TEST_CASE("an alias is u and six letters that spell nothing", "[llm]") {
    std::set<std::string> made;
    for (int index = 0; index < 1000; ++index) {
        const std::string alias = random_alias();
        CHECK(is_alias(alias));
        CHECK(alias.find_first_of("aeiouyl01", 1) == std::string::npos);
        made.insert(alias);
    }
    CHECK(made.size() > 990);
    CHECK_FALSE(is_alias("u7kx3q"));
    CHECK_FALSE(is_alias("uncle12"));
    CHECK_FALSE(is_alias("x7kx3qz"));
}

TEST_CASE("each person keeps one alias per server", "[llm]") {
    fixture test;
    const std::string first = test.aliases.alias_for(guild, alice);
    CHECK(test.aliases.alias_for(guild, alice) == first);
    CHECK(test.aliases.alias_for(guild, bob) != first);
    CHECK(test.aliases.user_for(guild, first) == alice);
    CHECK_FALSE(test.aliases.user_for(other_guild, first).has_value());
    CHECK_FALSE(test.aliases.user_for(guild, random_alias()).has_value());

    test.aliases.note_names(guild, alice, "Alice", "alice_1");
    test.aliases.note_names(guild, alice, "", "");
    const auto noted = test.aliases.noted_names(guild, alice);
    REQUIRE(noted.has_value());
    CHECK(noted->display_name == "Alice");
    CHECK(noted->username == "alice_1");
}

TEST_CASE("mentions become aliases, and of the bot its name", "[llm]") {
    fixture test;
    const std::string alice_alias = test.cast.meet(alice, "Alice");
    CHECK(test.cast.sanitize("<@11> and <@!11>, ask <@42>") == std::format("@{0} and @{0}, ask @LatiBot", alice_alias));

    // Someone only mentioned is met, and keeps the alias they were given.
    const std::string sanitized = test.cast.sanitize("hi <@12>");
    CHECK(sanitized == "hi @" + test.aliases.alias_for(guild, bob));
}

TEST_CASE("roles, channels, emoji, timestamps and commands lose their ids", "[llm]") {
    fixture test;
    test.discord.roles[dpp::snowflake{500}] = "moderators";
    test.discord.channels[dpp::snowflake{600}] = "general";
    CHECK(test.cast.sanitize("<@&500> <@&501> <#600> <#601>") == "@moderators @a role #general #a channel");
    CHECK(test.cast.sanitize("<:pog:123> <a:dance:456>") == ":pog: :dance:");
    CHECK(test.cast.sanitize("at <t:1773576000> or <t:1773576000:R>") == "at 2026-03-15 12:00 UTC or 2026-03-15 12:00 UTC");
    CHECK(test.cast.sanitize("try </music play:789>") == "try /music play");
    // Anything else in angle brackets is text.
    CHECK(test.cast.sanitize("a < b and <not a tag>") == "a < b and <not a tag>");
}

TEST_CASE("names written in text become markers, as whole words", "[llm]") {
    fixture test;
    test.discord.members[{guild, bob}] = {.nickname = "Big Bob", .display_name = "Bobby", .username = "bob_99"};
    const std::string alice_alias = test.cast.meet(alice, "Alice");
    const std::string bob_alias = test.cast.meet(bob);

    CHECK(test.cast.sanitize("ALICE told big bob that bob_99 is Bobby") ==
          std::format("<{0}:name> told <{1}:name> that <{1}:username> is <{1}:name>", alice_alias, bob_alias));
    // Not inside words, nor in an emoji's name.
    CHECK(test.cast.sanitize("Alicea and <:alice:5>") == "Alicea and :alice:");
    // Documents keep their names; only mentions change.
    CHECK(test.cast.sanitize("Alice is <@12>", false) == std::format("Alice is @{}", bob_alias));
}

TEST_CASE("a name too short to tell from a word is left", "[llm]") {
    fixture test;
    test.discord.members[{guild, alice}] = {.nickname = "Al", .display_name = {}, .username = "al"};
    (void)test.cast.meet(alice);
    CHECK(test.cast.sanitize("al is here") == "al is here");
}

TEST_CASE("a marker already written is kept as it is", "[llm]") {
    fixture test;
    const std::string alice_alias = test.cast.meet(alice, "Alice");
    const std::string memory = std::format("<{}:name> likes tea", alice_alias);
    CHECK(test.cast.sanitize(memory) == memory);
}

TEST_CASE("what the model writes gets its names back", "[llm]") {
    fixture test;
    test.discord.members[{guild, bob}] = {.nickname = "Big Bob", .display_name = "Bobby", .username = "bob_99"};
    const std::string alice_alias = test.cast.meet(alice, "Alice");
    const std::string bob_alias = test.cast.meet(bob);

    CHECK(test.cast.restore(std::format("hi <{}:name> and <{}:nickname>", alice_alias, bob_alias)) == "hi Alice and Big Bob");
    CHECK(test.cast.restore(std::format("<{}:username>", bob_alias)) == "bob_99");
    CHECK(test.cast.restore(std::format("<{}:mention>", bob_alias)) == "<@12>");
    CHECK(test.cast.restore(std::format("<{}:mention>", bob_alias), true) == "Big Bob");
    // An alias written bare, or as a mention, is a name too.
    CHECK(test.cast.restore(std::format("thanks {}, and @{}", alice_alias, bob_alias)) == "thanks Alice, and @Big Bob");
    // Someone the model made up is nobody.
    CHECK(test.cast.restore(std::format("<{}:name>", random_alias())) == "someone");
    CHECK(test.cast.restore("the uncle came") == "the uncle came");
}

TEST_CASE("someone known from before, not in this request, still gets their name", "[llm]") {
    fixture test;
    const std::string bob_alias = test.aliases.alias_for(guild, bob);
    test.aliases.note_names(guild, bob, "Bobby", "bob_99");
    CHECK(test.cast.restore(std::format("<{}:name>", bob_alias)) == "Bobby");
    CHECK(test.cast.user_for(bob_alias) == bob);
}

TEST_CASE("nothing sent names anyone, or gives an id", "[llm]") {
    fixture test;
    test.discord.members[{guild, alice}] = {.nickname = "Ally", .display_name = "Alice", .username = "alice_1"};
    (void)test.cast.meet(alice, "Ally");
    (void)test.cast.meet(bob, "Bob");
    const std::string sent = test.cast.sanitize("<@11> told Bob: Ally, alice_1 and Alice are all <@!11>");
    for (const char* leak : {"11", "12", "Ally", "Alice", "alice_1", "Bob"}) {
        CHECK_FALSE(sent.contains(leak));
    }
}

TEST_CASE("memories show the people they name as mentions in Discord", "[llm]") {
    fixture test;
    const std::string alice_alias = test.aliases.alias_for(guild, alice);
    CHECK(aliases_as_mentions(std::format("<{0}:name> likes tea, says {0}", alice_alias), guild, test.aliases) ==
          "<@11> likes tea, says <@11>");
    CHECK(aliases_as_mentions("nothing here", guild, test.aliases) == "nothing here");
}
