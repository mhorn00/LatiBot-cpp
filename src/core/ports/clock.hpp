#pragma once

#include <chrono>
#include <memory>

namespace latibot::ports {

/// Source of time.
///
/// Cooldowns, the midnight scheduler, spend-cap rollovers and voice-session
/// grace periods all depend on the clock, and none of them can be tested
/// against the real one (plan §17.3). Everything that asks "what time is
/// it" goes through here.
class clock {
public:
    virtual ~clock() = default;

    clock() = default;
    clock(const clock&) = delete;
    clock& operator=(const clock&) = delete;

    /// Wall-clock time. Use for anything stored or compared against a date.
    [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;

    /// Monotonic time. Use for durations and cooldowns: unlike `now()`, it
    /// does not jump when the machine's clock is corrected.
    [[nodiscard]] virtual std::chrono::steady_clock::time_point steady_now() const = 0;
};

/// The real clock.
class system_clock final : public clock {
public:
    [[nodiscard]] std::chrono::system_clock::time_point now() const override;
    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const override;
};

} // namespace latibot::ports
