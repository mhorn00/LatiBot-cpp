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
    auto operator=(const clock&) -> clock& = delete;

    /// Wall-clock time. Use for anything stored or compared against a date.
    [[nodiscard]] virtual auto now() const -> std::chrono::system_clock::time_point = 0;

    /// Monotonic time. Use for durations and cooldowns: unlike `now()`, it
    /// does not jump when the machine's clock is corrected.
    [[nodiscard]] virtual auto steady_now() const -> std::chrono::steady_clock::time_point = 0;
};

/// The real clock.
class system_clock final : public clock {
public:
    [[nodiscard]] auto now() const -> std::chrono::system_clock::time_point override;
    [[nodiscard]] auto steady_now() const -> std::chrono::steady_clock::time_point override;
};

} // namespace latibot::ports
