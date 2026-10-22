// The sounds the bot can see: reading Discord's lists, and finding and
// suggesting them (src/modules/soundboard/docs/Soundboard.md §2).

#include "sounds.hpp"

#include <dpp/json.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using latibot::soundboard::parse_sounds;
using latibot::soundboard::sound;
using latibot::soundboard::sound_catalog;
using latibot::soundboard::sound_choice;

namespace {

constexpr dpp::snowflake here{100};
constexpr dpp::snowflake elsewhere{200};

auto make_sound(std::uint64_t id, std::string name, dpp::snowflake guild = {}, std::string emoji = {}) -> sound {
    return {.id = dpp::snowflake{id}, .name = std::move(name), .guild_id = guild, .emoji = std::move(emoji), .volume = 1.0};
}

auto labels(const std::vector<sound_choice>& choices) -> std::vector<std::string> {
    std::vector<std::string> named;
    named.reserve(choices.size());
    for (const sound_choice& each : choices) {
        named.push_back(each.label);
    }
    return named;
}

/// Discord's quack and airhorn, this server's quack and crickets, and
/// another server's quacks.
auto fill(sound_catalog& sounds) -> void {
    sounds.set_defaults({make_sound(1, "quack", {}, "🦆"), make_sound(2, "airhorn", {}, "🔊")});
    sounds.set_guild(here, "Home", {make_sound(10, "Quack", here), make_sound(11, "crickets", here)});
    sounds.set_guild(elsewhere, "The Pond", {make_sound(20, "quack", elsewhere), make_sound(21, "big quack", elsewhere)});
}

} // namespace

TEST_CASE("a server's sounds are read from Discord's items", "[soundboard]") {
    const auto listed = nlohmann::json::parse(R"({"items": [
        {"name": "quack", "sound_id": "1001", "volume": 0.5, "emoji_id": null, "emoji_name": "🦆", "guild_id": "7", "available": true},
        {"name": "no boosts", "sound_id": "1002", "volume": 1.0, "guild_id": "7", "available": false},
        {"name": "custom emoji", "sound_id": "1003", "volume": 3.0, "emoji_id": "55", "emoji_name": null, "guild_id": "7"},
        {"name": "", "sound_id": "1004"},
        {"name": "no id"}
    ]})");

    const std::vector<sound> sounds = parse_sounds(listed);

    REQUIRE(sounds.size() == 2);
    CHECK(sounds[0].id == dpp::snowflake{1001});
    CHECK(sounds[0].name == "quack");
    CHECK(sounds[0].guild_id == dpp::snowflake{7});
    CHECK(sounds[0].emoji == "🦆");
    CHECK(sounds[0].volume == 0.5);
    // A server's own emoji cannot be drawn in a suggestion, and a volume is
    // kept between 0 and 1.
    CHECK(sounds[1].emoji.empty());
    CHECK(sounds[1].volume == 1.0);
}

TEST_CASE("Discord's own sounds are read from a bare list, with no server", "[soundboard]") {
    const auto listed =
        nlohmann::json::parse(R"([{"name": "ducky", "sound_id": "1", "volume": 1.0, "emoji_name": "🦆", "available": true}])");

    const std::vector<sound> sounds = parse_sounds(listed);

    REQUIRE(sounds.size() == 1);
    CHECK(sounds[0].guild_id.empty());
    CHECK(parse_sounds(nlohmann::json::object()).empty());
    CHECK(parse_sounds(nlohmann::json("nonsense")).empty());
}

TEST_CASE("a sound's file is on Discord's CDN", "[soundboard]") {
    CHECK(latibot::soundboard::sound_url(dpp::snowflake{1234}) == "https://cdn.discordapp.com/soundboard-sounds/1234");
}

TEST_CASE("suggestions put names starting with what was typed first, this server's before Discord's before others'", "[soundboard]") {
    sound_catalog sounds;
    fill(sounds);

    CHECK(labels(sounds.suggest("qua", here)) ==
          std::vector<std::string>{"Quack", "🦆 quack (Discord)", "quack (The Pond)", "big quack (The Pond)"});
    // From the other server, its own come first and this one's is named.
    CHECK(labels(sounds.suggest("QUACK", elsewhere)) ==
          std::vector<std::string>{"quack", "🦆 quack (Discord)", "Quack (Home)", "big quack"});
}

TEST_CASE("a suggestion gives the sound's id", "[soundboard]") {
    sound_catalog sounds;
    fill(sounds);

    CHECK(sounds.suggest("cri", here) == std::vector<sound_choice>{{.label = "crickets", .value = "11"}});
}

TEST_CASE("nothing typed suggests everything, up to the limit", "[soundboard]") {
    sound_catalog sounds;
    fill(sounds);

    CHECK(sounds.suggest("", here).size() == 6);
    CHECK(labels(sounds.suggest("  ", here, 2)) == std::vector<std::string>{"crickets", "Quack"});
    CHECK(sounds.suggest("nothing like it", here).empty());
}

TEST_CASE("a suggestion's label fits Discord's 100 characters", "[soundboard]") {
    sound_catalog sounds;
    sounds.set_guild(elsewhere, std::string(120, 'x'), {make_sound(1, "quack", elsewhere)});

    const auto choices = sounds.suggest("quack", here);

    REQUIRE(choices.size() == 1);
    CHECK(choices[0].label.size() <= 100 + 2); // the "…" is three bytes for one character
    CHECK(choices[0].label.starts_with("quack (xxx"));
}

TEST_CASE("a sound is found by its id, or by its name preferring the nearest", "[soundboard]") {
    sound_catalog sounds;
    fill(sounds);

    CHECK(sounds.find("21", here)->name == "big quack");
    CHECK(sounds.find("quack", here)->id == dpp::snowflake{10});
    CHECK(sounds.find(" QUACK ", elsewhere)->id == dpp::snowflake{20});
    // A server with none of its own gets Discord's.
    CHECK(sounds.find("quack", dpp::snowflake{300})->id == dpp::snowflake{1});
    CHECK_FALSE(sounds.find("crickets!", here).has_value());
    CHECK_FALSE(sounds.find("", here).has_value());
}

TEST_CASE("a server the bot leaves takes its sounds with it", "[soundboard]") {
    sound_catalog sounds;
    fill(sounds);
    REQUIRE(sounds.size() == 6);
    CHECK(sounds.guilds().at(elsewhere) == "The Pond");

    sounds.forget_guild(elsewhere);

    CHECK(sounds.size() == 4);
    CHECK_FALSE(sounds.guilds().contains(elsewhere));
    CHECK_FALSE(sounds.find("big quack", here).has_value());
}
