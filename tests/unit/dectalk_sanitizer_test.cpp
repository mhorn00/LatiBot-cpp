// What may reach DECtalk from whom (plan §12.5).

#include "core/audio/dectalk_sanitizer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using latibot::audio::sanitize_speech;
using latibot::audio::speech_trust;

namespace {

auto as_user(std::string_view text) -> std::string {
    return sanitize_speech(text, speech_trust::user).text;
}

auto as_trusted(std::string_view text) -> std::string {
    return sanitize_speech(text, speech_trust::trusted).text;
}

auto as_llm(std::string_view text) -> std::string {
    return sanitize_speech(text, speech_trust::llm).text;
}

} // namespace

TEST_CASE("plain text passes through untouched", "[audio]") {
    CHECK(as_user("Hello there, how are you?") == "Hello there, how are you?");
    CHECK(as_user("a ] stray bracket") == "a ] stray bracket");
    CHECK(as_user("").empty());
}

TEST_CASE("everyday commands are kept for everyone", "[audio]") {
    for (const auto trust : {speech_trust::user, speech_trust::trusted, speech_trust::llm}) {
        CHECK(sanitize_speech("[:rate 300]fast", trust).text == "[:rate 300]fast");
        CHECK(sanitize_speech("[:nb]hi", trust).text == "[:nb]hi");
        CHECK(sanitize_speech("[:dv ap 200 pr 150]hi", trust).text == "[:dv ap 200 pr 150]hi");
        CHECK(sanitize_speech("[:tone 440 500]", trust).text == "[:tone 440 500]");
        CHECK(sanitize_speech("[:dial 555#*]", trust).text == "[:dial 555#*]");
        CHECK(sanitize_speech("[:phoneme arpabet speak on]", trust).text == "[:phoneme arpabet speak on]");
    }
}

TEST_CASE("play, log, debug, loadv and setv are for trusted users only", "[audio]") {
    CHECK(as_user("[:play \"C:\\sound.wav\"]hi") == "hi");
    CHECK(as_user("[:log text on]hi") == "hi");
    CHECK(as_user("[:debug 1f]hi") == "hi");
    CHECK(as_user("[:loadv 1]hi") == "hi");
    CHECK(as_user("[:setv 1]hi") == "hi");

    CHECK(as_llm("[:play \"C:\\sound.wav\"]hi") == "hi");
    CHECK(as_llm("[:log text on]hi") == "hi");

    CHECK(as_trusted("[:play \"C:\\sound.wav\"]hi") == "[:play \"C:\\sound.wav\"]hi");
    CHECK(as_trusted("[:log text on]hi") == "[:log text on]hi");
    CHECK(as_trusted("[:debug 1f]hi") == "[:debug 1f]hi");
}

TEST_CASE("pause, resume and dv save are for nobody", "[audio]") {
    for (const auto trust : {speech_trust::user, speech_trust::trusted, speech_trust::llm}) {
        CHECK(sanitize_speech("[:pause 60000]hi", trust).text == "hi");
        CHECK(sanitize_speech("[:resume]hi", trust).text == "hi");
        CHECK(sanitize_speech("[:dv ap 200 save]hi", trust).text == "[:dv ap 200]hi");
        CHECK(sanitize_speech("[:dv SAVE]hi", trust).text == "[:dv]hi");
    }
}

TEST_CASE("removed commands are reported by their full names", "[audio]") {
    const auto result = sanitize_speech("[:pla \"x\"][:rate 200][:pau 5][:dv save]", speech_trust::user);

    CHECK(result.removed == std::vector<std::string>{"play", "pause", "dv save"});
    CHECK(result.text == "[:rate 200][:dv]");
}

TEST_CASE("a command is recognised by any unique prefix, in any case", "[audio]") {
    // DECtalk runs [:pla], [:PLAY] and [:pL] as play.
    CHECK(as_user("[:pla \"x\"]hi") == "hi");
    CHECK(as_user("[:PLAY \"x\"]hi") == "hi");
    CHECK(as_user("[:pL \"x\"]hi") == "hi");
    CHECK(as_user("[:Ra 300]hi") == "[:rate 300]hi");

    // Letters past a settled match are its parameters, as DECtalk reads them.
    CHECK(as_user("[:playfoo]hi") == "hi");
    CHECK(as_user("[:rate300]hi") == "[:rate 300]hi");
}

TEST_CASE("an ambiguous or unknown command is dropped", "[audio]") {
    CHECK(as_user("[:p 5]hi") == "hi"); // pause, play, phoneme, pitch...
    CHECK(as_user("[:zzz 5]hi") == "hi");
    CHECK(as_user("[:]hi") == "hi");
}

TEST_CASE("chained commands are judged one by one", "[audio]") {
    CHECK(as_user("[:rate 200 :play \"x\"]hi") == "[:rate 200]hi");
    CHECK(as_user("[:play \"x\" :rate 200]hi") == "[:rate 200]hi");
    CHECK(as_user("[:np :rate 250 :volume set 50]hi") == "[:np][:rate 250][:volume set 50]hi");
}

TEST_CASE("spaces and extra brackets before the colon do not hide a command", "[audio]") {
    // DECtalk skips both between '[' and ':'.
    CHECK(as_user("[  :play \"x\"]hi") == "hi");
    CHECK(as_user("[[:play \"x\"]hi") == "hi");
    CHECK(as_user("[\n:play \"x\"]hi") == "hi");
}

TEST_CASE("a quoted parameter can hold a closing bracket", "[audio]") {
    // To DECtalk the ']' inside the quotes is part of the path, so the
    // command runs to the second ']'.
    CHECK(as_user("[:play \"a]b\"]hi") == "hi");

    // Even trusted, a parameter holding ']' is not written back: it could
    // not be quoted safely.
    CHECK(as_trusted("[:play <a]b>]hi") == "[:play]hi");
}

TEST_CASE("an unterminated command swallows the rest, as it does in DECtalk", "[audio]") {
    CHECK(as_user("hi [:play \"x") == "hi ");
    CHECK(as_user("hi [:rate 200") == "hi [:rate 200]");
}

TEST_CASE("phoneme brackets are kept, and cannot hide a command", "[audio]") {
    CHECK(as_user("[:phoneme on][hxeh'low]") == "[:phoneme on][hxeh'low]");

    // A '[' inside one would start over, so the outer '[' is dropped.
    CHECK(as_user("[hx[:play \"x\"]") == "hx");
    // An unclosed one is dropped, and what follows is kept as text.
    CHECK(as_user("[hello") == "hello");
}

TEST_CASE("control characters are removed before anything else is read", "[audio]") {
    // Removed afterwards, the \x01 would have left "[:play x]" behind.
    CHECK(as_user("[\x01:play \"x\"]hi") == "hi");
    CHECK(as_user("a\x1b[:play \"x\"]b") == "ab");
    CHECK(as_user("tab\tand\nnewline") == "tab\tand\nnewline");
}

TEST_CASE("parameters that could open or close anything are dropped", "[audio]") {
    // A quote opens only at the start of a parameter, so 200" is one
    // parameter, and it goes whole.
    CHECK(as_user("[:rate 200\"]hi") == "[:rate]hi");
    CHECK(as_user("[:mode \"spell\" on]hi") == "[:mode spell on]hi");
    CHECK(as_user("[:rate 2[0]hi") == "[:rate]hi");
}

TEST_CASE("sanitizing twice changes nothing more", "[audio]") {
    for (const std::string_view text : {"[:rate 200 :play \"x\"]hi", "[:pla \"a]b\"] [x] [:dv ap 1 save]", "[[:np]] [ :nb]]"}) {
        const std::string once = as_user(text);
        CHECK(as_user(once) == once);
    }
}
