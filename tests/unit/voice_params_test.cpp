// The built-in voices and the preamble that selects one (plan §12.6).

#include "core/audio/voice_params.hpp"

#include <catch2/catch_test_macros.hpp>

using latibot::audio::find_builtin_voice;
using latibot::audio::voice_preamble;

TEST_CASE("the built-in voices are found by name, in any case", "[audio]") {
    REQUIRE(find_builtin_voice("betty") != nullptr);
    CHECK(find_builtin_voice("betty")->command == "[:nb]");
    CHECK(find_builtin_voice("  WENDY ")->command == "[:nw]");
    CHECK(find_builtin_voice("chris") == nullptr); // not in this build
    CHECK(find_builtin_voice("") == nullptr);
}

TEST_CASE("the preamble selects the voice and says only what differs", "[audio]") {
    CHECK(voice_preamble({}) == "[:np]");
    CHECK(voice_preamble({.voice = "harry"}) == "[:nh]");
    CHECK(voice_preamble({.voice = "harry", .rate = 300}) == "[:nh][:rate 300]");
    CHECK(voice_preamble({.voice = "kit", .custom_params = "ap 300 pr 150"}) == "[:nk][:dv ap 300 pr 150]");
}

TEST_CASE("the preamble falls back to Paul and clamps the rate", "[audio]") {
    CHECK(voice_preamble({.voice = "nobody"}) == "[:np]");
    CHECK(voice_preamble({.rate = 10}) == "[:np][:rate 75]");
    CHECK(voice_preamble({.rate = 5000}) == "[:np][:rate 600]");
}
