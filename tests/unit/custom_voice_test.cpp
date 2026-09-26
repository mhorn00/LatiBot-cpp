// Custom voices: the [:dv] parameters, reading and writing them, and names
// (plan §12.6).

#include "core/audio/voice_params.hpp"
#include "core/audio/voice_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using latibot::audio::custom_voice;
using latibot::audio::parse_custom_voice;
using latibot::audio::voice_name_refusal;

TEST_CASE("every parameter is in exactly one group of at most five", "[audio]") {
    std::set<std::string_view> grouped;
    for (const auto& group : latibot::audio::voice_parameter_groups()) {
        INFO(group.name);
        CHECK(group.name.size() <= 45); // a modal's title
        for (const std::string_view code : group.codes) {
            if (code.empty()) continue;
            CHECK(latibot::audio::find_voice_parameter(code) != nullptr);
            CHECK(grouped.insert(code).second);
        }
    }
    CHECK(grouped.size() == latibot::audio::voice_parameters().size());

    for (const auto& parameter : latibot::audio::voice_parameters()) {
        INFO(parameter.code);
        CHECK(parameter.label.size() <= 45); // a text input's label
        CHECK(parameter.min < parameter.max);
    }
}

TEST_CASE("edits are clamped to DECtalk's limits and written in table order", "[audio]") {
    custom_voice voice;
    CHECK(voice.set("pr", 150));
    CHECK(voice.set("AP", 9999));
    CHECK(voice.set("sx", -4));
    CHECK_FALSE(voice.set("zz", 1));
    CHECK_FALSE(voice.set("save", 1));

    CHECK(voice.dv_parameters() == "ap 350 pr 150 sx 0");
    CHECK(custom_voice{}.dv_parameters().empty());
}

TEST_CASE("a voice reads back from [:dv] text, with or without brackets", "[audio]") {
    const auto bare = parse_custom_voice("ap 200 pr 150", "harry");
    CHECK(bare.problems.empty());
    CHECK(bare.voice.base == "harry");
    CHECK(bare.voice.dv_parameters() == "ap 200 pr 150");

    const auto bracketed = parse_custom_voice("[:nk][:dv ap 200, pr 150]", "paul");
    CHECK(bracketed.problems.empty());
    CHECK(bracketed.voice.base == "kit");
    CHECK(bracketed.voice.dv_parameters() == "ap 200 pr 150");

    const auto named = parse_custom_voice("[:name wendy][:DV BR 30]", "paul");
    CHECK(named.voice.base == "wendy");
    CHECK(named.voice.dv_parameters() == "br 30");

    // What dv_parameters writes, parse_custom_voice reads back unchanged.
    CHECK(parse_custom_voice(bracketed.voice.dv_parameters(), "kit").voice == bracketed.voice);
}

TEST_CASE("what cannot be read is reported and skipped", "[audio]") {
    const auto parsed = parse_custom_voice("[:dv ap 999 zz 5 pr loud save hs]", "nobody");

    CHECK(parsed.voice.base == "paul"); // an unknown base falls back
    CHECK(parsed.voice.dv_parameters() == "ap 350");
    CHECK(parsed.problems == std::vector<std::string>{
                                 "ap goes from 50 to 350, so 999 became 350",
                                 "\"zz\" isn't a voice parameter",
                                 "\"5\" isn't a voice parameter",
                                 "pr needs a number after it",
                                 "\"loud\" isn't a voice parameter",
                                 "\"save\" isn't needed: saving the voice keeps it",
                                 "hs needs a number after it",
                             });
}

TEST_CASE("a custom voice's preamble is rebuilt, not pasted", "[audio]") {
    // custom_params comes from the database; whatever is in it, nothing but
    // parameters and numbers reaches the engine.
    const latibot::ports::voice_settings settings{.voice = "harry", .custom_params = "ap 200][:play \"x\"] pr 150"};
    CHECK(latibot::audio::voice_preamble(settings) == "[:nh][:dv ap 200 pr 150]");
}

TEST_CASE("custom voice names are short, plain and never a built-in's", "[audio]") {
    CHECK(voice_name_refusal("robo") == std::nullopt);
    CHECK(voice_name_refusal("  Robo_Voice-2 ") == std::nullopt);
    CHECK(latibot::audio::normalise_voice_name("  Robo_Voice-2 ") == "robo_voice-2");

    CHECK(voice_name_refusal("") == "a voice needs a name");
    CHECK(voice_name_refusal("harry") == "harry is already one of the built-in voices");
    CHECK(voice_name_refusal("HARRY") == "harry is already one of the built-in voices");
    CHECK(voice_name_refusal("robo voice") == "a voice's name can only have letters, digits, - and _");
    CHECK(voice_name_refusal(std::string(33, 'a')) == "a voice's name can be at most 32 characters");
}
