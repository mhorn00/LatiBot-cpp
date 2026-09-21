#pragma once

#include "core/ports/clock.hpp"

namespace latibot::testing {

/// A clock the test drives by hand.
///
/// Wall-clock and monotonic time advance together, so a test can move past a
/// cooldown or across midnight without waiting.
class mock_clock final : public ports::clock {
public:
    explicit mock_clock(std::chrono::system_clock::time_point start = {}) : now_(start) {}

    [[nodiscard]] std::chrono::system_clock::time_point now() const override { return now_; }

    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const override {
        return steady_;
    }

    void advance(std::chrono::nanoseconds amount) {
        now_ += std::chrono::duration_cast<std::chrono::system_clock::duration>(amount);
        steady_ += std::chrono::duration_cast<std::chrono::steady_clock::duration>(amount);
    }

    void set(std::chrono::system_clock::time_point when) { now_ = when; }

private:
    std::chrono::system_clock::time_point now_{};
    std::chrono::steady_clock::time_point steady_{};
};

} // namespace latibot::testing
