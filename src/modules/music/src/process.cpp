// Running other programs: what is the same on every system. Starting them
// is process_windows.cpp's, or process_posix.cpp's.

#include "process.hpp"

#include <array>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

namespace latibot::util {

// --------------------------------------------------------------------------
// Command lines
// --------------------------------------------------------------------------

auto quote_argument(std::string_view argument) -> std::string {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) return std::string(argument);

    // Backslashes are literal except before a quote, where each pair is one
    // backslash and an odd one escapes the quote. So the backslashes before
    // a quote, and before the closing quote, are doubled.
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char character : argument) {
        if (character == '\\') {
            ++backslashes;
            continue;
        }
        if (character == '"') {
            quoted.append((backslashes * 2) + 1, '\\');
        } else {
            quoted.append(backslashes, '\\');
        }
        backslashes = 0;
        quoted += character;
    }
    quoted.append(backslashes * 2, '\\');
    quoted += '"';
    return quoted;
}

auto executable_name(std::string_view name) -> std::string {
#ifdef _WIN32
    return std::format("{}.exe", name);
#else
    return std::string(name);
#endif
}

auto command_line(const program& to_run) -> std::string {
    // The program's own name is read by different rules: no escapes, and
    // quotes only group. A path cannot hold a quote, so plain quotes do.
    std::string line = std::format("\"{}\"", to_run.path.string());
    for (const std::string& argument : to_run.arguments) {
        line += ' ';
        line += quote_argument(argument);
    }
    return line;
}

// --------------------------------------------------------------------------
// Running one program
// --------------------------------------------------------------------------

auto run(const program& to_run, std::chrono::milliseconds timeout, std::size_t output_limit) -> run_result {
    constexpr std::size_t errors_limit = std::size_t{64} * 1024;

    run_result result;
    std::mutex errors_mutex;
    const auto collect = [&](std::size_t, std::string_view line) {
        const std::scoped_lock lock(errors_mutex);
        if (result.errors.size() >= errors_limit) return;
        result.errors.append(line);
        result.errors += '\n';
    };

    // Held by pointer so it can be ended, and its stderr reader finished
    // with `result.errors`, before the result is handed back.
    auto running = std::make_unique<pipeline>(std::span(&to_run, 1), collect);

    // A watchdog, since reading a pipe cannot itself time out: at the
    // deadline it kills the program, which ends the read below.
    std::mutex watch_mutex;
    std::condition_variable finished;
    bool done = false;
    bool expired = false;
    std::jthread watchdog([&] {
        std::unique_lock lock(watch_mutex);
        if (!finished.wait_for(lock, timeout, [&] { return done; })) {
            expired = true;
            lock.unlock();
            running->kill();
        }
    });

    std::array<std::byte, 16384> buffer{};
    while (const std::size_t got = running->read(buffer)) {
        if (result.output.size() + got > output_limit) {
            running->kill();
            break;
        }
        result.output.append(reinterpret_cast<const char*>(buffer.data()), got);
    }

    result.exit_code = running->wait().front();
    {
        const std::scoped_lock lock(watch_mutex);
        done = true;
    }
    finished.notify_all();
    watchdog.join();
    running.reset();

    result.timed_out = expired;
    return result;
}

} // namespace latibot::util
