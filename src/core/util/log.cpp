#include "core/util/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <iostream>

namespace latibot::util {
namespace {

constexpr std::array<std::string_view, 6> level_names{"trace", "debug", "info", "warn", "error", "off"};

void write_to_stderr(log_level level, std::string_view message) {
    const auto stamp = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    // One line per message, timestamp first, so logs stay greppable and sort
    // chronologically.
    std::clog << std::format("{:%Y-%m-%dT%H:%M:%SZ} [{}] {}\n", stamp, to_string(level), message);
}

} // namespace

std::string_view to_string(log_level level) noexcept {
    const auto index = static_cast<std::size_t>(level);
    return index < level_names.size() ? level_names[index] : "unknown";
}

std::optional<log_level> log_level_from_string(std::string_view name) {
    std::string lowered(name);
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const auto found = std::ranges::find(level_names, lowered);
    if (found == level_names.end()) {
        return std::nullopt;
    }
    return static_cast<log_level>(std::distance(level_names.begin(), found));
}

logger::logger(log_level minimum) : level_(minimum), sink_(&write_to_stderr) {}

void logger::set_level(log_level minimum) {
    const std::scoped_lock guard(mutex_);
    level_ = minimum;
}

log_level logger::level() const {
    const std::scoped_lock guard(mutex_);
    return level_;
}

void logger::set_sink(sink_fn sink) {
    const std::scoped_lock guard(mutex_);
    sink_ = sink ? std::move(sink) : sink_fn(&write_to_stderr);
}

bool logger::enabled(log_level level) const {
    const std::scoped_lock guard(mutex_);
    return level != log_level::off && level >= level_;
}

void logger::write(log_level level, std::string_view message) {
    // The lock covers the sink call as well, so lines from different threads
    // do not interleave mid-message.
    const std::scoped_lock guard(mutex_);
    if (level == log_level::off || level < level_) {
        return;
    }
    sink_(level, message);
}

logger& log() {
    static logger instance;
    return instance;
}

} // namespace latibot::util
