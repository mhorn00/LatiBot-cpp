#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::util {

// Running other programs: yt-dlp and ffmpeg, for music
// (docs/features/Music.md §4.6). Arguments are always a list, never a shell
// line, so nothing in them is ever interpreted by a shell.

/// A program and its arguments.
struct program {
    std::filesystem::path path;
    std::vector<std::string> arguments;
};

/// A program could not be started.
class process_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// One argument, quoted so that Windows' `CommandLineToArgvW`, and the C
/// runtime's own parsing, read back exactly `argument`: spaces, quotes and
/// the backslashes before them included.
[[nodiscard]] auto quote_argument(std::string_view argument) -> std::string;

/// The whole command line: the program's path, then each argument, quoted.
[[nodiscard]] auto command_line(const program& to_run) -> std::string;

/// Where a program is: `configured` when it is set, which must exist;
/// otherwise `name.exe` beside the bot's own executable, then on `PATH`.
/// Nothing when it cannot be found.
[[nodiscard]] auto locate_program(std::string_view name, const std::filesystem::path& configured) -> std::optional<std::filesystem::path>;

/// What running a program to its end gave.
struct run_result {
    int exit_code = -1;
    std::string output;
    std::string errors;

    /// It ran past its time and was killed.
    bool timed_out = false;
};

/// Runs a program to its end, with nothing on its stdin, and collects what
/// it writes. It is killed, with anything it started, after `timeout`, or
/// once it has written more than `output_limit` bytes to stdout.
///
/// Throws `process_error` when it cannot be started.
[[nodiscard]] auto run(const program& to_run, std::chrono::milliseconds timeout, std::size_t output_limit = std::size_t{64} << 20U)
    -> run_result;

/// Programs whose output feeds the next one's input: `yt-dlp | ffmpeg`,
/// without a shell. The first reads nothing; the caller reads the last
/// one's output.
///
/// Every program runs in one Windows job object, so `kill`, or destroying
/// the pipeline, ends them and anything they started. Each program's stderr
/// is read on a thread of its own and handed over line by line, since a
/// full stderr pipe would stop the program.
class pipeline {
public:
    /// Called with a program's index in the pipeline and one line it wrote
    /// to stderr. Runs on the pipeline's own threads.
    using error_line = std::function<void(std::size_t, std::string_view)>;

    /// Starts every program. Throws `process_error` if any of them cannot be
    /// started, having ended the ones that were.
    explicit pipeline(std::span<const program> programs, const error_line& on_error = {});

    /// Kills whatever is still running, and waits for the stderr readers.
    ~pipeline();

    pipeline(const pipeline&) = delete;
    auto operator=(const pipeline&) -> pipeline& = delete;
    pipeline(pipeline&&) = delete;
    auto operator=(pipeline&&) -> pipeline& = delete;

    /// Reads what the last program has written, waiting until something
    /// arrives. 0 once its output has ended, or the pipeline was killed.
    [[nodiscard]] auto read(std::span<std::byte> into) -> std::size_t;

    /// Ends every program in it. Safe from any thread, and more than once.
    auto kill() -> void;

    /// Waits for every program to end, and says how each did, in order.
    [[nodiscard]] auto wait() -> std::vector<int>;

private:
    struct state;
    std::unique_ptr<state> state_;
};

} // namespace latibot::util
