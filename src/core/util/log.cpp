#include "core/util/log.hpp"

#include "core/util/env.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <cstdio>
#endif

namespace latibot::util {
namespace {

constexpr std::array<std::string_view, 6> level_names{"trace", "debug", "info", "warn", "error", "off"};

auto write_to_stderr(log_level level, std::string_view message, bool colored) -> void {
    const auto stamp = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::clog << render_line(stamp, level, message, colored);
}

/// Whether stderr is a terminal that will show colour, switching Windows'
/// console into escape-sequence mode if that is what it takes.
///
/// A console on Windows prints escape codes literally until it is told not
/// to; Windows Terminal and VS Code's terminal handle them regardless, but
/// the classic console host needs the mode set. A handle that is not a
/// console at all (a file, a pipe) fails `GetConsoleMode`, which doubles as
/// the "is this a terminal" test.
auto stderr_can_show_color() -> bool {
#ifdef _WIN32
    auto* const handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr || GetConsoleMode(handle, &mode) == 0) {
        return false;
    }
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        return true;
    }
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

} // namespace

auto to_string(log_level level) noexcept -> std::string_view {
    const auto index = static_cast<std::size_t>(level);
    return index < level_names.size() ? level_names[index] : "unknown";
}

auto log_level_from_string(std::string_view name) -> std::optional<log_level> {
    const std::string lowered = to_lower(name);

    const auto found = std::ranges::find(level_names, lowered);
    if (found == level_names.end()) {
        return std::nullopt;
    }
    return static_cast<log_level>(std::distance(level_names.begin(), found));
}

// --------------------------------------------------------------------------

auto color_mode_from_string(std::string_view text) -> std::optional<color_mode> {
    const std::string wanted = to_lower(text);

    if (wanted.empty() || wanted == "auto" || wanted == "automatic") {
        return color_mode::automatic;
    }
    if (wanted == "always" || wanted == "on" || wanted == "true" || wanted == "1" || wanted == "yes" || wanted == "force") {
        return color_mode::always;
    }
    if (wanted == "never" || wanted == "off" || wanted == "false" || wanted == "0" || wanted == "no" || wanted == "none") {
        return color_mode::never;
    }
    return std::nullopt;
}

auto should_color(color_mode mode, bool no_color_set, bool stderr_is_terminal) noexcept -> bool {
    switch (mode) {
    case color_mode::always:
        return true;
    case color_mode::never:
        return false;
    case color_mode::automatic:
        break;
    }
    return !no_color_set && stderr_is_terminal;
}

auto apply_log_colors_from_environment() -> void {
    color_mode mode = color_mode::automatic;
    std::optional<std::string> unrecognised;

    if (const auto wanted = env_var("LATIBOT_LOG_COLOR")) {
        if (const auto parsed = color_mode_from_string(*wanted)) {
            mode = *parsed;
        } else {
            unrecognised = *wanted;
        }
    }

    // NO_COLOR counts when present and not empty, per the convention.
    const auto no_color = env_var("NO_COLOR");
    const bool no_color_set = no_color.has_value() && !no_color->empty();

    // Asked even when the answer is already decided, because asking is what
    // switches the Windows console into escape-sequence mode for `always`.
    const bool terminal = mode != color_mode::never && stderr_can_show_color();

    log().set_colors(should_color(mode, no_color_set, terminal));

    // After the decision, so the warning is itself shown the way the rest of
    // the log will be.
    if (unrecognised) {
        log().warn("LATIBOT_LOG_COLOR is \"{}\", expected auto, always or never; using auto", *unrecognised);
    }
}

auto render_line(std::chrono::sys_seconds stamp, log_level level, std::string_view message, bool colored) -> std::string {
    // One line per message, timestamp first, so logs stay greppable and sort
    // chronologically.
    if (!colored) {
        return std::format("{:%Y-%m-%dT%H:%M:%SZ} [{}] {}\n", stamp, to_string(level), message);
    }

    const text_style when = palette.timestamp;
    const text_style tag = style_of(level);
    return std::format("\x1b[{}m{:%Y-%m-%dT%H:%M:%SZ}\x1b[0m \x1b[{}m[{}]\x1b[0m {}\n", when.sgr, stamp, tag.sgr, to_string(level),
                       message);
}

// --------------------------------------------------------------------------

logger::logger(log_level minimum) : level_(minimum) {}

auto logger::set_level(log_level minimum) -> void {
    const std::scoped_lock guard(mutex_);
    level_ = minimum;
}

auto logger::level() const -> log_level {
    const std::scoped_lock guard(mutex_);
    return level_;
}

auto logger::set_sink(sink_fn sink) -> void {
    const std::scoped_lock guard(mutex_);
    sink_ = std::move(sink);
}

auto logger::set_colors(bool on) -> void {
    const std::scoped_lock guard(mutex_);
    colors_ = on;
}

auto logger::colors() const -> bool {
    const std::scoped_lock guard(mutex_);
    return colors_ && !sink_;
}

auto logger::enabled(log_level level) const -> bool {
    const std::scoped_lock guard(mutex_);
    return level != log_level::off && level >= level_;
}

auto logger::write(log_level level, std::string_view message) -> void {
    // The lock covers the sink call as well, so lines from different threads
    // do not interleave mid-message.
    const std::scoped_lock guard(mutex_);
    if (level == log_level::off || level < level_) {
        return;
    }

    if (sink_) {
        sink_(level, message);
    } else {
        write_to_stderr(level, message, colors_);
    }
}

auto log() -> logger& {
    static logger instance;
    return instance;
}

} // namespace latibot::util
