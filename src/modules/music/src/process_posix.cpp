// Running other programs on Linux and other POSIX systems: posix_spawn, pipes,
// and a process group per pipeline (src/modules/music/docs/Music.md §4.6).
// What is the same on every system is in process.cpp.

#include "process.hpp"
#include "process_lines.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ; // NOLINT(readability-redundant-declaration): not every unistd.h declares it

namespace latibot::util {
namespace {

// --------------------------------------------------------------------------
// Descriptors
// --------------------------------------------------------------------------

/// A file descriptor, closed when it goes.
class descriptor {
public:
    descriptor() = default;
    explicit descriptor(int value) : value_(value) {}
    ~descriptor() { reset(); }

    descriptor(const descriptor&) = delete;
    auto operator=(const descriptor&) -> descriptor& = delete;
    descriptor(descriptor&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
    auto operator=(descriptor&& other) noexcept -> descriptor& {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }

    [[nodiscard]] auto get() const noexcept -> int { return value_; }
    explicit operator bool() const noexcept { return value_ >= 0; }

    auto reset() noexcept -> void {
        if (value_ >= 0) ::close(value_);
        value_ = -1;
    }

private:
    int value_ = -1;
};

/// What the system says about a failure, for an exception's message.
auto failure(std::string_view what, int code) -> process_error {
    return process_error(std::format("{} ({})", what, std::strerror(code)));
}

/// Both ends of a pipe, neither inherited by anything started: a child gets
/// its end as stdin, stdout or stderr alone, so a pipe end never leaks into
/// a program another thread starts at the same moment.
struct pipe_ends {
    descriptor read;
    descriptor write;
};

auto make_pipe() -> pipe_ends {
    int ends[2] = {-1, -1}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): pipe2 takes one
    if (::pipe2(ends, O_CLOEXEC) != 0) throw failure("could not make a pipe", errno);
    return {.read = descriptor(ends[0]), .write = descriptor(ends[1])};
}

/// /dev/null, opened to read, for a child that is given no input.
auto open_null() -> descriptor {
    descriptor null(::open("/dev/null", O_RDONLY | O_CLOEXEC)); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (!null) throw failure("could not open /dev/null", errno);
    return null;
}

/// Reads what is there, retrying when a signal interrupts; 0 at the end, or
/// on an error.
auto read_some(int from, std::span<char> into) -> std::size_t {
    while (true) {
        const ssize_t got = ::read(from, into.data(), into.size());
        if (got < 0 && errno == EINTR) continue;
        return got < 0 ? 0 : static_cast<std::size_t>(got);
    }
}

/// Starts `to_run` with exactly these three descriptors as its stdin, stdout
/// and stderr, in process group `group`: a new one, led by this program,
/// when `group` is 0.
auto start(const program& to_run, int input, int output, int errors, pid_t group) -> pid_t {
    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    const auto cleanup = [&] {
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
    };

    posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errors, STDERR_FILENO);
    const std::string directory = to_run.working_directory.string();
    if (!directory.empty()) posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());

    // Its own group, so killing the group ends it and whatever it starts;
    // signals as a freshly started program expects them, whatever the bot
    // blocks or ignores (a broken pipe has to end ffmpeg, not be ignored).
    sigset_t none{};
    sigemptyset(&none);
    sigset_t defaults{};
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setpgroup(&attributes, group);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    const std::string path = to_run.path.string();
    std::vector<std::string> arguments{path};
    arguments.insert(arguments.end(), to_run.arguments.begin(), to_run.arguments.end());
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    pid_t started = 0;
    const int code = posix_spawn(&started, path.c_str(), &actions, &attributes, argv.data(), environ);
    cleanup();
    if (code != 0) throw failure(std::format("could not start {}", to_run.path.filename().string()), code);
    return started;
}

/// Waits for a program to end, and says how it did: its exit code, or 128
/// plus the signal that ended it, as a shell reports it. With `keep`, it is
/// left a zombie, so its id cannot go to another process yet.
auto wait_for(pid_t process, bool keep) -> int {
    siginfo_t info{};
    const int options = WEXITED | (keep ? WNOWAIT : 0);
    while (::waitid(P_PID, static_cast<id_t>(process), &info, options) != 0) {
        if (errno != EINTR) return 1;
    }
    return info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
}

/// Whether `path` is a file this process could run.
auto runnable(const std::filesystem::path& path) -> bool {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && ::access(path.c_str(), X_OK) == 0;
}

} // namespace

// --------------------------------------------------------------------------
// Finding programs
// --------------------------------------------------------------------------

auto executable_directory() -> std::optional<std::filesystem::path> {
    std::error_code error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || self.empty()) return std::nullopt;
    return self.parent_path();
}

auto locate_program(std::string_view name, const std::filesystem::path& configured) -> std::optional<std::filesystem::path> {
    std::error_code error;
    if (!configured.empty()) {
        if (runnable(configured)) return std::filesystem::absolute(configured, error);
        return std::nullopt;
    }

    if (const auto directory = executable_directory()) {
        std::filesystem::path beside = *directory / executable_name(name);
        if (runnable(beside)) return beside;
    }

    const char* search = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe): read once, at startup
    if (search == nullptr) return std::nullopt;
    for (const auto entry : std::views::split(std::string_view(search), ':')) {
        const std::string_view folder(entry.begin(), entry.end());
        if (folder.empty()) continue;
        std::filesystem::path candidate = std::filesystem::path(folder) / executable_name(name);
        if (runnable(candidate)) return candidate;
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------
// Pipelines
// --------------------------------------------------------------------------

/// The first program leads the process group, and is only ever reaped when
/// the pipeline goes: until then its id, and so the group's, cannot be
/// given to anything else, and killing the group can only reach what the
/// pipeline started, however long after the programs ended.
struct pipeline::state {
    /// The process group every program is in: the first one's id.
    pid_t group = 0;
    std::vector<pid_t> processes;
    /// How each ended, once waited for; the leader's while it is a zombie.
    std::vector<std::optional<int>> codes;
    descriptor output;
    std::vector<std::jthread> error_readers;

    std::mutex mutex;
    bool killed = false;

    /// Kills the group and reaps every program not yet reaped.
    auto end_all() -> void {
        if (group != 0) ::kill(-group, SIGKILL);
        for (std::size_t index = 0; index < processes.size(); ++index) {
            if (index == 0 || !codes[index]) (void)wait_for(processes[index], false);
        }
        processes.clear();
    }
};

pipeline::pipeline(std::span<const program> programs, const error_line& on_error) : state_(std::make_unique<state>()) {
    if (programs.empty()) throw process_error("a pipeline needs at least one program");

    // The pipe the next program reads from: /dev/null for the first.
    descriptor next_input = open_null();

    try {
        for (std::size_t index = 0; index < programs.size(); ++index) {
            pipe_ends output = make_pipe();
            pipe_ends errors = make_pipe();

            const pid_t started = start(programs[index], next_input.get(), output.write.get(), errors.write.get(), state_->group);
            state_->processes.push_back(started);
            state_->codes.emplace_back();
            if (state_->group == 0) state_->group = started;

            // The child has its own copies now. Holding ours would keep each
            // pipe open after the child had finished with it.
            output.write.reset();
            errors.write.reset();

            state_->error_readers.emplace_back([pipe = std::move(errors.read), index, on_error] {
                detail::drain_lines([&pipe](std::span<char> into) { return read_some(pipe.get(), into); }, index, on_error);
            });

            // This program's output is the next one's input; the last is
            // ours to read.
            if (index + 1 < programs.size()) {
                next_input = std::move(output.read);
            } else {
                next_input.reset();
                state_->output = std::move(output.read);
            }
        }
    } catch (...) {
        state_->end_all();
        throw;
    }
}

pipeline::~pipeline() {
    kill();
    state_->error_readers.clear(); // joins: their pipes close as the programs die
    state_->end_all();
}

auto pipeline::read(std::span<std::byte> into) -> std::size_t {
    if (into.empty()) return 0;
    const std::size_t wanted = std::min<std::size_t>(into.size(), std::size_t{1} << 20U);
    return read_some(state_->output.get(), std::span(reinterpret_cast<char*>(into.data()), wanted));
}

auto pipeline::kill() -> void {
    const std::scoped_lock lock(state_->mutex);
    if (state_->killed) return;
    state_->killed = true;
    ::kill(-state_->group, SIGKILL);
}

auto pipeline::wait() -> std::vector<int> {
    std::vector<int> codes;
    for (std::size_t index = 0; index < state_->processes.size(); ++index) {
        std::optional<int>& code = state_->codes[index];
        if (!code) code = wait_for(state_->processes[index], index == 0);
        codes.push_back(*code);
    }
    return codes;
}

} // namespace latibot::util
