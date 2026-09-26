#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace latibot::util {

enum class log_level : std::uint8_t { trace, debug, info, warn, error, off };

/// What a build logs when nothing says otherwise.
///
/// A debug build exists to be diagnosed, so it starts at `debug`; a release
/// build is running for other people, so it starts at `info`. `config.json`
/// and `LATIBOT_LOG_LEVEL` both override this (see `config::bootstrap::load`).
inline constexpr log_level default_log_level =
#ifdef NDEBUG
    log_level::info;
#else
    log_level::debug;
#endif

[[nodiscard]] auto to_string(log_level level) noexcept -> std::string_view;

/// Parses a level name from the config file. Case-insensitive; returns
/// nothing for an unknown name so the caller can report it.
[[nodiscard]] auto log_level_from_string(std::string_view name) -> std::optional<log_level>;

// --------------------------------------------------------------------------
// Colour
//
// Arguments are coloured by their type, automatically: `log().info("{} in
// {}", 7, guild_id)` shows the 7 as a number and the id as an id, and the
// caller writes nothing different. Text in the format string stays the
// terminal's own colour.
//
// Two places to change it. `palette` says what each colour is; `log_style`
// says which type gets which. Colouring a new type is one specialisation of
// `log_style`.
// --------------------------------------------------------------------------

/// An ANSI "select graphic rendition" code such as "92" or "1;91". Empty
/// means the terminal's own colour.
struct text_style {
    std::string_view sgr;

    [[nodiscard]] constexpr auto plain() const noexcept -> bool { return sgr.empty(); }
};

/// The sixteen standard colours, named for how they look on a dark terminal.
///
/// Sixteen rather than 256 so the log follows the terminal's theme instead
/// of fighting it.
namespace ansi {
inline constexpr text_style plain{};
inline constexpr text_style gray{"90"};
inline constexpr text_style dark_red{"31"};
inline constexpr text_style dark_green{"32"};
inline constexpr text_style dark_yellow{"33"};
inline constexpr text_style dark_blue{"34"};
inline constexpr text_style dark_purple{"35"};
inline constexpr text_style dark_cyan{"36"};
inline constexpr text_style light_red{"91"};
inline constexpr text_style light_green{"92"};
inline constexpr text_style light_yellow{"93"};
inline constexpr text_style light_blue{"94"};
inline constexpr text_style light_purple{"95"};
inline constexpr text_style light_cyan{"96"};
inline constexpr text_style bold_light_red{"1;91"};
} // namespace ansi

/// Every colour the log uses.
struct log_palette {
    text_style timestamp;

    text_style trace;
    text_style debug;
    text_style info;
    text_style warn;
    text_style error;

    /// A tag such as `[dpp]` naming where a forwarded line came from.
    text_style source;

    text_style number;
    text_style boolean_true;
    text_style boolean_false;
    text_style snowflake;
    text_style duration;
};

/// The colours. Change one here.
inline constexpr log_palette palette{
    .timestamp = ansi::gray,

    .trace = ansi::gray,
    .debug = ansi::dark_cyan,
    .info = ansi::light_blue,
    .warn = ansi::light_yellow,
    .error = ansi::bold_light_red,

    .source = ansi::dark_purple,

    .number = ansi::light_green,
    .boolean_true = ansi::dark_green,
    .boolean_false = ansi::light_red,
    .snowflake = ansi::light_purple,
    .duration = ansi::light_cyan,
};

[[nodiscard]] constexpr auto style_of(log_level level) noexcept -> text_style {
    switch (level) {
    case log_level::trace:
        return palette.trace;
    case log_level::debug:
        return palette.debug;
    case log_level::info:
        return palette.info;
    case log_level::warn:
        return palette.warn;
    case log_level::error:
        return palette.error;
    case log_level::off:
        break;
    }
    return ansi::plain;
}

/// Which colour a value of type `T` is logged in.
///
/// Unspecialised means plain, which is right for text and for anything not
/// listed below. To colour another type, specialise this with a static
/// `of(const T&)` returning a `text_style`; it takes the value, not just the
/// type, so a colour can depend on what is being shown.
template <typename T>
struct log_style {};

/// Whether `T` has a colour at all. Types that do not are passed to
/// `std::format` untouched, so plain text costs nothing extra.
template <typename T>
concept styled = requires(const T& value) {
    { log_style<T>::of(value) } -> std::convertible_to<text_style>;
};

namespace detail {

/// Characters are integers to the language and text to a reader.
template <typename T>
concept character =
    std::same_as<T, char> || std::same_as<T, wchar_t> || std::same_as<T, char8_t> || std::same_as<T, char16_t> || std::same_as<T, char32_t>;

} // namespace detail

/// Numbers, integer and floating point alike.
template <typename T>
    requires((std::integral<T> && !std::same_as<T, bool> && !detail::character<T>) || std::floating_point<T>)
struct log_style<T> {
    static constexpr auto of(const T& /*value*/) noexcept -> text_style { return palette.number; }
};

/// true and false in different colours, since the difference is usually the
/// point of logging one.
template <>
struct log_style<bool> {
    static constexpr auto of(bool value) noexcept -> text_style { return value ? palette.boolean_true : palette.boolean_false; }
};

/// Discord ids. Only when passed as the snowflake itself: `id.str()` is a
/// string by the time the logger sees it, and strings are plain.
template <>
struct log_style<dpp::snowflake> {
    static constexpr auto of(const dpp::snowflake& /*value*/) noexcept -> text_style { return palette.snowflake; }
};

template <typename Rep, typename Period>
struct log_style<std::chrono::duration<Rep, Period>> {
    static constexpr auto of(const std::chrono::duration<Rep, Period>& /*value*/) noexcept -> text_style { return palette.duration; }
};

/// A tag naming where a line came from, logged as `[name]`.
///
/// For lines forwarded from something that formatted them itself, such as
/// DPP's own log. There are no typed arguments left to colour in those, so
/// the tag is the only thing that is.
struct log_source {
    std::string_view name;
};

template <>
struct log_style<log_source> {
    static constexpr auto of(const log_source& /*value*/) noexcept -> text_style { return palette.source; }
};

namespace detail {

/// True while a coloured line is being formatted on this thread.
///
/// Only `paint_to` reads it: it is how a type that formats itself in more
/// than one colour finds out whether to. Formatting is synchronous, so a
/// thread-local is exactly as wide as it needs to be.
inline thread_local bool painting = false;

/// A log argument on its way into a coloured line.
template <typename T>
struct painted {
    const T& value;
};

/// Wraps a value whose type has a colour, and passes everything else through
/// by reference, untouched.
template <typename T>
auto paint(const T& value) -> decltype(auto) {
    if constexpr (styled<T>) {
        return painted<T>{value};
    } else {
        return value;
    }
}

} // namespace detail

/// Writes `value` in its type's colour while a coloured line is being
/// formatted, and plainly otherwise.
///
/// For the formatter of a type whose parts want different colours, such as
/// a user shown as `name (id)`: the logger colours whole arguments, and this
/// is how one argument colours its own pieces.
template <typename OutputIt, typename T>
auto paint_to(OutputIt out, const T& value) -> OutputIt {
    if constexpr (styled<T>) {
        const text_style style = log_style<T>::of(value);
        if (detail::painting && !style.plain()) {
            return std::format_to(out, "\x1b[{}m{}\x1b[0m", style.sgr, value);
        }
    }
    return std::format_to(out, "{}", value);
}

namespace detail {

/// Restores `painting` however formatting ends, including by throwing.
class painting_scope {
public:
    painting_scope() noexcept : previous_(painting) { painting = true; }
    ~painting_scope() { painting = previous_; }

    painting_scope(const painting_scope&) = delete;
    auto operator=(const painting_scope&) -> painting_scope& = delete;

private:
    bool previous_;
};

/// Formats one log message, with its arguments coloured when `colored`.
///
/// Exposed for tests, which is the only reason it is not private to the
/// logger. The format string was checked against the real argument types at
/// the call site; colouring then only changes what those arguments write.
template <typename... Args>
[[nodiscard]] auto format_message(bool colored, std::format_string<Args...> fmt, Args&&... args) -> std::string {
    if (!colored) {
        return std::format(fmt, std::forward<Args>(args)...);
    }

    try {
        const painting_scope scope;
        const std::tuple<decltype(paint(args))...> parts{paint(args)...};
        return std::apply([&fmt](auto&... part) { return std::vformat(fmt.get(), std::make_format_args(part...)); }, parts);
    } catch (const std::format_error&) {
        // A format the wrapper cannot pass through, such as a width taken
        // from another argument ({:{}}), which then arrives wrapped instead
        // of as the integer it has to be. A plain line beats no line: logging
        // must never be the thing that fails.
        return std::format(fmt, std::forward<Args>(args)...);
    }
}

} // namespace detail

// --------------------------------------------------------------------------
// Where colour is used at all
// --------------------------------------------------------------------------

/// What `LATIBOT_LOG_COLOR` asks for.
enum class color_mode : std::uint8_t {
    /// Colour when stderr is a terminal and NO_COLOR is not set. A log
    /// redirected to a file stays free of escape codes.
    automatic,
    /// Always, even into a pipe or a file.
    always,
    /// Never.
    never,
};

/// Parses a `LATIBOT_LOG_COLOR` value. Case-insensitive, and forgiving:
/// `off`, `false`, `0` and `no` all mean never, since nobody should have to
/// look up which spelling of "no" is the right one. Nothing for a value that
/// means none of these, so the caller can say so.
[[nodiscard]] auto color_mode_from_string(std::string_view text) -> std::optional<color_mode>;

/// Whether the stderr sink should colour.
///
/// Pure, so each combination can be tested. `NO_COLOR` is the convention
/// other command-line tools already honour (no-color.org); an explicit
/// `always` wins over it, as that convention says it should.
[[nodiscard]] auto should_color(color_mode mode, bool no_color_set, bool stderr_is_terminal) noexcept -> bool;

/// Reads `LATIBOT_LOG_COLOR` and `NO_COLOR`, checks whether stderr is a
/// terminal that can show colour, and applies the answer to `log()`.
///
/// Called once from main after `.env` has been read, so a colour setting
/// there is honoured. On Windows this is also what switches the console into
/// processing escape sequences, which it does not do by default.
auto apply_log_colors_from_environment() -> void;

/// One line as the stderr sink writes it: timestamp, level, message.
///
/// Uncoloured, it is byte-for-byte what the log has always looked like.
[[nodiscard]] auto render_line(std::chrono::sys_seconds stamp, log_level level, std::string_view message, bool colored) -> std::string;

// --------------------------------------------------------------------------
// The logger
// --------------------------------------------------------------------------

/// Writes log lines.
///
/// The sink is replaceable so tests can capture output instead of printing
/// it, and so the bot can send DPP's own log events through the same path.
class logger {
public:
    using sink_fn = std::function<void(log_level, std::string_view)>;

    explicit logger(log_level minimum = default_log_level);

    logger(const logger&) = delete;
    auto operator=(const logger&) -> logger& = delete;

    auto set_level(log_level minimum) -> void;
    [[nodiscard]] auto level() const -> log_level;

    /// Replaces the destination. Passing an empty function restores the
    /// default, which writes timestamped lines to stderr.
    ///
    /// A replacement sink always receives plain text: colour is for a person
    /// reading a terminal, and a sink is usually a test reading a string.
    auto set_sink(sink_fn sink) -> void;

    /// Whether the stderr sink colours its output. Off until something turns
    /// it on, which in the bot is `apply_log_colors_from_environment`.
    auto set_colors(bool on) -> void;

    /// Whether the next line will be coloured: colours on, and the stderr
    /// sink in use.
    [[nodiscard]] auto colors() const -> bool;

    [[nodiscard]] auto enabled(log_level level) const -> bool;

    /// Writes a message that is already formatted.
    auto write(log_level level, std::string_view message) -> void;

    template <typename... Args>
    auto log(log_level level, std::format_string<Args...> fmt, Args&&... args) -> void {
        if (!enabled(level)) {
            return;
        }
        write(level, detail::format_message(colors(), fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    auto trace(std::format_string<Args...> fmt, Args&&... args) -> void {
        log(log_level::trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto debug(std::format_string<Args...> fmt, Args&&... args) -> void {
        log(log_level::debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto info(std::format_string<Args...> fmt, Args&&... args) -> void {
        log(log_level::info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto warn(std::format_string<Args...> fmt, Args&&... args) -> void {
        log(log_level::warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto error(std::format_string<Args...> fmt, Args&&... args) -> void {
        log(log_level::error, fmt, std::forward<Args>(args)...);
    }

private:
    mutable std::mutex mutex_;
    log_level level_;
    sink_fn sink_;

    /// Empty `sink_` means the stderr sink; these only apply to it.
    bool colors_ = false;
};

/// The bot's logger. One instance, because logging is called from everywhere
/// including DPP's own threads.
[[nodiscard]] auto log() -> logger&;

} // namespace latibot::util

// --------------------------------------------------------------------------
// Formatters
// --------------------------------------------------------------------------

/// A painted argument formats exactly as the value would, inside its colour.
///
/// Inheriting the value's own formatter is what keeps format specs working:
/// `{:>5}` on a coloured number pads the number, and the escape codes sit
/// outside the padding where they cannot throw the width off.
///
/// Specialising std::formatter for a program-defined type is what the
/// standard allows ([namespace.std]); the check cannot see that through the
/// template parameter, which is why it flags this and not the full
/// specialisations below.
template <typename T>
// NOLINTNEXTLINE(bugprone-std-namespace-modification)
struct std::formatter<latibot::util::detail::painted<T>, char> : std::formatter<T, char> {
    template <typename FormatContext>
    auto format(const latibot::util::detail::painted<T>& arg, FormatContext& ctx) const {
        const latibot::util::text_style style = latibot::util::log_style<T>::of(arg.value);
        if (style.plain()) {
            return std::formatter<T, char>::format(arg.value, ctx);
        }

        ctx.advance_to(std::format_to(ctx.out(), "\x1b[{}m", style.sgr));
        auto out = std::formatter<T, char>::format(arg.value, ctx);
        return std::format_to(out, "\x1b[0m");
    }
};

template <>
struct std::formatter<latibot::util::log_source, char> {
    static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const latibot::util::log_source& source, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "[{}]", source.name);
    }
};
