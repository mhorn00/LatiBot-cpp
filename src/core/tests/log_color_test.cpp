#include "core/util/log.hpp"

#include "support/capture_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

using latibot::util::color_mode;
using latibot::util::color_mode_from_string;
using latibot::util::log_level;
using latibot::util::log_source;
using latibot::util::palette;
using latibot::util::render_line;
using latibot::util::should_color;
using latibot::util::text_style;
using latibot::util::detail::format_message;
using namespace std::chrono_literals;

namespace {

/// `text` as it looks in `style`, built from the palette rather than written
/// out, so changing a colour does not break a test about the mechanism.
auto in(text_style style, std::string_view text) -> std::string {
    return std::format("\x1b[{}m{}\x1b[0m", style.sgr, text);
}

/// Turns the stderr sink's colours on for one test and off again after.
class colors_on {
public:
    colors_on() { latibot::util::log().set_colors(true); }
    ~colors_on() { latibot::util::log().set_colors(false); }

    colors_on(const colors_on&) = delete;
    auto operator=(const colors_on&) -> colors_on& = delete;
};

} // namespace

// --------------------------------------------------------------------------
// Arguments
// --------------------------------------------------------------------------

TEST_CASE("uncoloured output is exactly what std::format would give", "[log]") {
    // What every test, every sink other than stderr, and every redirected log
    // file sees, so it must not drift from the plain format by a byte.
    const dpp::snowflake guild{1234567890123456789ULL};

    CHECK(format_message(false, "{} in {} ({})", 7, guild, true) == "7 in 1234567890123456789 (true)");
}

TEST_CASE("text in the format string stays the terminal's own colour", "[log]") {
    CHECK(format_message(true, "connecting to Discord") == "connecting to Discord");
}

TEST_CASE("a number is coloured as a number", "[log]") {
    CHECK(format_message(true, "{} commands", 7) == in(palette.number, "7") + " commands");
    CHECK(format_message(true, "{}", std::size_t{12}) == in(palette.number, "12"));
    CHECK(format_message(true, "{}", std::int64_t{-3}) == in(palette.number, "-3"));
    CHECK(format_message(true, "{}", 2.5) == in(palette.number, "2.5"));
}

TEST_CASE("true and false are coloured differently", "[log]") {
    // The difference is usually the reason a boolean is in a log line at all.
    CHECK(format_message(true, "{}", true) == in(palette.boolean_true, "true"));
    CHECK(format_message(true, "{}", false) == in(palette.boolean_false, "false"));
}

TEST_CASE("a Discord id is coloured as an id", "[log]") {
    const dpp::snowflake guild{1234567890123456789ULL};

    CHECK(format_message(true, "guild {}", guild) == "guild " + in(palette.snowflake, "1234567890123456789"));
}

TEST_CASE("an id already turned into a string is only a string", "[log]") {
    // Why log calls pass the snowflake rather than `.str()`: the logger sees
    // types, and a string is text by the time it arrives.
    const dpp::snowflake guild{42};

    CHECK(format_message(true, "guild {}", guild.str()) == "guild 42");
}

TEST_CASE("strings and characters stay plain", "[log]") {
    const std::string name = "latios";
    const std::string_view view = "view";

    CHECK(format_message(true, "{} {} {} {}", name, view, "literal", 'c') == "latios view literal c");
}

TEST_CASE("a duration has a colour of its own", "[log]") {
    CHECK(format_message(true, "cooldown {}", 30s) == "cooldown " + in(palette.duration, "30s"));
}

TEST_CASE("format specs still apply inside the colour", "[log]") {
    // The escape codes wrap the padded value, so the padding is still the
    // width that was asked for rather than being eaten by invisible bytes.
    CHECK(format_message(true, "[{:>4}]", 42) == "[" + in(palette.number, "  42") + "]");
    CHECK(format_message(true, "{:x}", 255) == in(palette.number, "ff"));
    CHECK(format_message(true, "{:.1f}", 2.25) == in(palette.number, "2.2"));
}

TEST_CASE("a format colour cannot pass through falls back to a plain line", "[log]") {
    // A width taken from another argument has to be an integer, and arrives
    // wrapped instead. Logging must never be the thing that fails, so the
    // line is written without colour rather than not at all.
    CHECK(format_message(true, "[{:{}}]", 42, 5) == "[   42]");
}

TEST_CASE("a forwarded line's source tag is the coloured part", "[log]") {
    // DPP hands over finished text, so its message has no types left to
    // colour; the tag is what tells its lines apart from ours.
    const std::string message = "Shard 0 connected";

    CHECK(format_message(true, "{} {}", log_source{"dpp"}, message) == in(palette.source, "[dpp]") + " Shard 0 connected");
    CHECK(format_message(false, "{} {}", log_source{"dpp"}, message) == "[dpp] Shard 0 connected");
}

TEST_CASE("the default palette is the one that was asked for", "[log]") {
    // Pinned on purpose: changing a colour is a one-line edit to `palette`,
    // and should be a deliberate one-line edit here too.
    CHECK(palette.number.sgr == latibot::util::ansi::light_green.sgr);
    CHECK(palette.boolean_true.sgr == latibot::util::ansi::dark_green.sgr);
    CHECK(palette.boolean_false.sgr == latibot::util::ansi::light_red.sgr);
    CHECK(palette.snowflake.sgr == latibot::util::ansi::light_purple.sgr);
    CHECK_FALSE(palette.timestamp.plain());
    CHECK_FALSE(palette.source.plain());
}

// --------------------------------------------------------------------------
// Types that colour their own parts
// --------------------------------------------------------------------------

TEST_CASE("paint_to colours only while a coloured line is being formatted", "[log]") {
    const dpp::snowflake id{42};

    std::string outside;
    latibot::util::paint_to(std::back_inserter(outside), id);
    CHECK(outside == "42");

    // And the flag that says "colouring now" does not outlive the line.
    CHECK(format_message(true, "{}", id) == in(palette.snowflake, "42"));
    CHECK_FALSE(latibot::util::detail::painting);
}

// --------------------------------------------------------------------------
// The line around the message
// --------------------------------------------------------------------------

namespace {

const auto sample_time =
    std::chrono::sys_seconds{std::chrono::sys_days{std::chrono::year{2026} / std::chrono::September / 23} + 18h + 1min + 34s};

} // namespace

TEST_CASE("an uncoloured line is the format the log has always had", "[log]") {
    // Anything that already parses these lines, and every log redirected to a
    // file, depends on this staying put.
    CHECK(render_line(sample_time, log_level::info, "hello", false) == "2026-09-23T18:01:34Z [info] hello\n");
}

TEST_CASE("a coloured line colours the timestamp and the level", "[log]") {
    const std::string line = render_line(sample_time, log_level::warn, "hello", true);

    CHECK(line == in(palette.timestamp, "2026-09-23T18:01:34Z") + " " + in(palette.warn, "[warn]") + " hello\n");
}

TEST_CASE("each level has its own colour", "[log]") {
    using latibot::util::style_of;

    CHECK(style_of(log_level::error).sgr != style_of(log_level::warn).sgr);
    CHECK(style_of(log_level::warn).sgr != style_of(log_level::info).sgr);
    CHECK(style_of(log_level::info).sgr != style_of(log_level::debug).sgr);
    CHECK(style_of(log_level::debug).sgr != style_of(log_level::trace).sgr);
}

// --------------------------------------------------------------------------
// When colour is used
// --------------------------------------------------------------------------

TEST_CASE("the colour setting accepts the obvious spellings", "[log]") {
    CHECK(color_mode_from_string("never") == color_mode::never);
    CHECK(color_mode_from_string("OFF") == color_mode::never);
    CHECK(color_mode_from_string("false") == color_mode::never);
    CHECK(color_mode_from_string("0") == color_mode::never);

    CHECK(color_mode_from_string("always") == color_mode::always);
    CHECK(color_mode_from_string("On") == color_mode::always);
    CHECK(color_mode_from_string("1") == color_mode::always);

    CHECK(color_mode_from_string("auto") == color_mode::automatic);
    CHECK(color_mode_from_string("") == color_mode::automatic);

    CHECK_FALSE(color_mode_from_string("purple").has_value());
}

TEST_CASE("colour follows the terminal unless told otherwise", "[log]") {
    // A log redirected to a file should never fill up with escape codes.
    CHECK(should_color(color_mode::automatic, /*no_color_set=*/false, /*stderr_is_terminal=*/true));
    CHECK_FALSE(should_color(color_mode::automatic, false, false));
}

TEST_CASE("NO_COLOR turns colour off, and an explicit always overrides it", "[log]") {
    // no-color.org: honoured by default, but a per-program setting wins.
    CHECK_FALSE(should_color(color_mode::automatic, /*no_color_set=*/true, /*stderr_is_terminal=*/true));
    CHECK(should_color(color_mode::always, true, true));
}

TEST_CASE("never and always mean exactly that", "[log]") {
    CHECK_FALSE(should_color(color_mode::never, false, true));
    CHECK(should_color(color_mode::always, false, false));
}

// --------------------------------------------------------------------------
// The logger
// --------------------------------------------------------------------------

TEST_CASE("a replacement sink gets plain text even with colours on", "[log]") {
    // Colour is for a person reading a terminal; a sink is usually a test
    // reading a string, and escape codes would break every `contains` in it.
    const colors_on colors;
    const latibot::testing::capture_log captured;

    CHECK_FALSE(latibot::util::log().colors());

    latibot::util::log().info("registered {} commands for guild {}", 7, dpp::snowflake{99});

    CHECK(captured.contains(log_level::info, "registered 7 commands for guild 99"));
}

TEST_CASE("colours are off until something turns them on", "[log]") {
    // So a test binary, or anything else that never calls
    // apply_log_colors_from_environment, writes plain text.
    const latibot::util::logger fresh(log_level::info);

    CHECK_FALSE(fresh.colors());
}
