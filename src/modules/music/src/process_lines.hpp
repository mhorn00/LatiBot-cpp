#pragma once

#include "process.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace latibot::util::detail {

/// The stderr readers still running, so `pipeline::wait` can let them hand
/// over a program's last lines before it says the program has ended: a
/// caller reads why it failed straight after.
class readers_running {
public:
    auto started() -> void {
        const std::scoped_lock lock(mutex_);
        ++running_;
    }

    auto finished() -> void {
        {
            const std::scoped_lock lock(mutex_);
            --running_;
        }
        done_.notify_all();
    }

    /// Waits for every one to finish, for at most `longest`: something a
    /// program started can hold its stderr open after it has gone.
    auto wait_for(std::chrono::milliseconds longest) -> void {
        std::unique_lock lock(mutex_);
        done_.wait_for(lock, longest, [this] { return running_ == 0; });
    }

private:
    std::mutex mutex_;
    std::condition_variable done_;
    int running_ = 0;
};

/// How long `pipeline::wait` gives the stderr readers once the programs have
/// ended.
inline constexpr std::chrono::milliseconds last_lines_wait{2000};

/// Reads a program's stderr until it ends, handing `on_error` a line at a
/// time. `read_some` fills the buffer it is given and says how much it
/// filled, 0 at the end (process_windows.cpp, process_posix.cpp).
template <typename Read>
auto drain_lines(Read read_some, std::size_t index, const pipeline::error_line& on_error) -> void {
    std::string pending;
    std::array<char, 4096> buffer{};
    const auto emit = [&](std::string_view line) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (on_error && !line.empty()) on_error(index, line);
    };

    while (const std::size_t got = read_some(std::span<char>(buffer))) {
        pending.append(buffer.data(), got);

        std::size_t start = 0;
        for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', start)) {
            emit(std::string_view(pending).substr(start, end - start));
            start = end + 1;
        }
        pending.erase(0, start);

        // A program that writes a very long line with no end still gets
        // heard, in pieces, rather than growing this without bound.
        if (pending.size() > 16384) {
            emit(pending);
            pending.clear();
        }
    }
    emit(pending);
}

} // namespace latibot::util::detail
