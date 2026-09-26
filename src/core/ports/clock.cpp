#include "core/ports/clock.hpp"

namespace latibot::ports {

auto system_clock::now() const -> std::chrono::system_clock::time_point {
    return std::chrono::system_clock::now();
}

auto system_clock::steady_now() const -> std::chrono::steady_clock::time_point {
    return std::chrono::steady_clock::now();
}

} // namespace latibot::ports
