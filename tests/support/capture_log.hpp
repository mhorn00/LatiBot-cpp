#pragma once

#include "core/util/log.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace latibot::testing {

/// Redirects the process logger into a buffer for the duration of a test, and
/// puts it back afterwards.
class capture_log {
public:
    explicit capture_log(util::log_level level = util::log_level::trace) : previous_level_(util::log().level()) {
        util::log().set_level(level);
        util::log().set_sink([this](util::log_level severity, std::string_view message) {
            const std::scoped_lock guard(mutex_);
            lines_.emplace_back(severity, std::string(message));
        });
    }

    capture_log(const capture_log&) = delete;
    capture_log& operator=(const capture_log&) = delete;

    ~capture_log() {
        util::log().set_sink({}); // restores the default stderr sink
        util::log().set_level(previous_level_);
    }

    [[nodiscard]] std::vector<std::pair<util::log_level, std::string>> lines() const {
        const std::scoped_lock guard(mutex_);
        return lines_;
    }

    [[nodiscard]] std::size_t count() const {
        const std::scoped_lock guard(mutex_);
        return lines_.size();
    }

    /// True when some line at `severity` contains `text`.
    [[nodiscard]] bool contains(util::log_level severity, std::string_view text) const {
        const std::scoped_lock guard(mutex_);
        return std::ranges::any_of(lines_,
                                   [&](const auto& line) { return line.first == severity && line.second.find(text) != std::string::npos; });
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::pair<util::log_level, std::string>> lines_;
    util::log_level previous_level_;
};

} // namespace latibot::testing
