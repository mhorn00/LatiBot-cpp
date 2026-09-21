#include "core/ports/clock.hpp"

namespace latibot::ports {

std::chrono::system_clock::time_point system_clock::now() const {
    return std::chrono::system_clock::now();
}

std::chrono::steady_clock::time_point system_clock::steady_now() const {
    return std::chrono::steady_clock::now();
}

} // namespace latibot::ports
