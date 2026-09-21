#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace latibot::util {

enum class log_level : std::uint8_t { trace, debug, info, warn, error, off };

[[nodiscard]] std::string_view to_string(log_level level) noexcept;

/// Parses a level name from the config file. Case-insensitive; returns
/// nothing for an unknown name so the caller can report it.
[[nodiscard]] std::optional<log_level> log_level_from_string(std::string_view name);

/// Writes log lines.
///
/// The sink is replaceable so tests can capture output instead of printing
/// it, and so the bot can send DPP's own log events through the same path.
class logger {
public:
    using sink_fn = std::function<void(log_level, std::string_view)>;

    explicit logger(log_level minimum = log_level::info);

    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;

    void set_level(log_level minimum);
    [[nodiscard]] log_level level() const;

    /// Replaces the destination. Passing an empty function restores the
    /// default, which writes timestamped lines to stderr.
    void set_sink(sink_fn sink);

    [[nodiscard]] bool enabled(log_level level) const;

    /// Writes a message that is already formatted.
    void write(log_level level, std::string_view message);

    template <typename... Args>
    void log(log_level level, std::format_string<Args...> fmt, Args&&... args) {
        if (!enabled(level)) {
            return;
        }
        write(level, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) {
        log(log_level::trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(log_level::debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        log(log_level::info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        log(log_level::warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        log(log_level::error, fmt, std::forward<Args>(args)...);
    }

private:
    mutable std::mutex mutex_;
    log_level level_;
    sink_fn sink_;
};

/// The bot's logger. One instance, because logging is called from everywhere
/// including DPP's own threads.
[[nodiscard]] logger& log();

} // namespace latibot::util
