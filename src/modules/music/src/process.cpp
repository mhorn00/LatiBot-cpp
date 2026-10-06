#include "core/util/process.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <format>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace latibot::util {
namespace {

// --------------------------------------------------------------------------
// Handles and text
// --------------------------------------------------------------------------

/// A Windows handle, closed when it goes.
class handle {
public:
    handle() = default;
    explicit handle(HANDLE value) : value_(value == INVALID_HANDLE_VALUE ? nullptr : value) {}
    ~handle() { reset(); }

    handle(const handle&) = delete;
    auto operator=(const handle&) -> handle& = delete;
    handle(handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    auto operator=(handle&& other) noexcept -> handle& {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] auto get() const noexcept -> HANDLE { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }

    auto reset() noexcept -> void {
        if (value_ != nullptr) CloseHandle(value_);
        value_ = nullptr;
    }

private:
    HANDLE value_ = nullptr;
};

auto widen(std::string_view text) -> std::wstring {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

/// What Windows says about a failure, for an exception's message.
auto failure(std::string_view what, DWORD code) -> process_error {
    return process_error(std::format("{} (Windows error {})", what, code));
}

/// The same, for the call that has just failed.
auto last_error(std::string_view what) -> process_error {
    return failure(what, GetLastError());
}

/// Both ends of a pipe. The child's end is inheritable and the parent's is
/// not, so only the process it is meant for ever holds the child's end.
struct pipe_ends {
    handle parent;
    handle child;
};

enum class child_end : std::uint8_t { reads, writes };

auto make_pipe(child_end side) -> pipe_ends {
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (CreatePipe(&read_end, &write_end, &inherit, 0) == 0) throw last_error("could not make a pipe");

    pipe_ends ends;
    ends.parent = handle(side == child_end::reads ? write_end : read_end);
    ends.child = handle(side == child_end::reads ? read_end : write_end);
    SetHandleInformation(ends.parent.get(), HANDLE_FLAG_INHERIT, 0);
    return ends;
}

/// `NUL`, opened to read, for a child that is given no input.
auto open_nul() -> handle {
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    handle nul(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));
    if (!nul) throw last_error("could not open NUL");
    return nul;
}

/// A job whose processes all end when it is closed or terminated, including
/// anything they start.
auto make_job() -> handle {
    handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) throw last_error("could not make a job object");

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0) {
        throw last_error("could not set up a job object");
    }
    return job;
}

/// Starts `to_run` in `job` with exactly these three handles as its
/// stdin, stdout and stderr.
///
/// The handles are passed in an explicit list rather than by inheriting
/// everything inheritable: another thread may be making pipes for another
/// program at the same moment, and a pipe end leaking into the wrong child
/// keeps that pipe open after its real owner has finished.
auto start(const program& to_run, HANDLE input, HANDLE output, HANDLE errors, HANDLE job) -> handle {
    // Each once: the list refuses a handle given twice.
    std::vector<HANDLE> inherited;
    for (HANDLE entry : {input, output, errors}) {
        if (std::ranges::find(inherited, entry) == inherited.end()) inherited.push_back(entry);
    }

    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    std::vector<std::byte> list_storage(list_size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_storage.data());
    if (InitializeProcThreadAttributeList(list, 1, 0, &list_size) == 0) throw last_error("could not set up a process");
    const auto cleanup = [list] { DeleteProcThreadAttributeList(list); };
    if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, static_cast<void*>(inherited.data()),
                                  inherited.size() * sizeof(HANDLE), nullptr, nullptr) == 0) {
        const DWORD code = GetLastError();
        cleanup();
        throw failure("could not set up a process", code);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = output;
    startup.StartupInfo.hStdError = errors;
    startup.lpAttributeList = list;

    const std::wstring application = to_run.path.wstring();
    std::wstring line = widen(command_line(to_run));
    const std::wstring directory = to_run.working_directory.wstring();

    PROCESS_INFORMATION started{};
    const BOOL created = CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                        directory.empty() ? nullptr : directory.c_str(), &startup.StartupInfo, &started);
    const DWORD code = GetLastError();
    cleanup();
    if (created == 0) throw failure(std::format("could not start {}", to_run.path.filename().string()), code);

    handle process(started.hProcess);
    const handle thread(started.hThread);

    // Into the job before it runs a single instruction, so nothing it starts
    // can escape it.
    if (AssignProcessToJobObject(job, process.get()) == 0) {
        const DWORD assign_code = GetLastError();
        TerminateProcess(process.get(), 1);
        throw failure(std::format("could not start {} in a job", to_run.path.filename().string()), assign_code);
    }
    ResumeThread(thread.get());
    return process;
}

/// Reads a program's stderr until it ends, a line at a time.
auto drain_errors(HANDLE pipe, std::size_t index, const pipeline::error_line& on_error) -> void {
    std::string pending;
    std::array<char, 4096> buffer{};
    const auto emit = [&](std::string_view line) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (on_error && !line.empty()) on_error(index, line);
    };

    while (true) {
        DWORD got = 0;
        if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) == 0 || got == 0) break;
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

} // namespace

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

auto executable_directory() -> std::optional<std::filesystem::path> {
    std::wstring self(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (length == 0 || length >= self.size()) return std::nullopt;
    self.resize(length);
    return std::filesystem::path(self).parent_path();
}

auto locate_program(std::string_view name, const std::filesystem::path& configured) -> std::optional<std::filesystem::path> {
    std::error_code error;
    if (!configured.empty()) {
        if (std::filesystem::is_regular_file(configured, error)) return std::filesystem::absolute(configured, error);
        return std::nullopt;
    }

    const std::wstring file = widen(name) + L".exe";

    if (const auto directory = executable_directory()) {
        std::filesystem::path beside = *directory / file;
        if (std::filesystem::is_regular_file(beside, error)) return beside;
    }

    std::wstring found(MAX_PATH, L'\0');
    const DWORD found_length = SearchPathW(nullptr, file.c_str(), nullptr, static_cast<DWORD>(found.size()), found.data(), nullptr);
    if (found_length == 0 || found_length >= found.size()) return std::nullopt;
    found.resize(found_length);
    return std::filesystem::path(found);
}

// --------------------------------------------------------------------------
// Pipelines
// --------------------------------------------------------------------------

struct pipeline::state {
    handle job;
    std::vector<handle> processes;
    handle output;
    std::vector<std::jthread> error_readers;

    std::mutex mutex;
    bool killed = false;
};

pipeline::pipeline(std::span<const program> programs, const error_line& on_error) : state_(std::make_unique<state>()) {
    if (programs.empty()) throw process_error("a pipeline needs at least one program");
    state_->job = make_job();

    // The pipe the next program reads from: NUL for the first.
    handle next_input = open_nul();

    try {
        for (std::size_t index = 0; index < programs.size(); ++index) {
            pipe_ends output = make_pipe(child_end::writes);
            pipe_ends errors = make_pipe(child_end::writes);

            state_->processes.push_back(
                start(programs[index], next_input.get(), output.child.get(), errors.child.get(), state_->job.get()));

            // The child has its own copies now. Holding ours would keep each
            // pipe open after the child had finished with it.
            output.child.reset();
            errors.child.reset();

            state_->error_readers.emplace_back(
                [pipe = std::move(errors.parent), index, on_error] { drain_errors(pipe.get(), index, on_error); });

            // This program's output is the next one's input, which must be
            // inheritable for that one alone; the last is ours to read.
            if (index + 1 < programs.size()) {
                SetHandleInformation(output.parent.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                next_input = std::move(output.parent);
            } else {
                next_input.reset();
                state_->output = std::move(output.parent);
            }
        }
    } catch (...) {
        TerminateJobObject(state_->job.get(), 1);
        throw;
    }
}

pipeline::~pipeline() {
    kill();
    state_->error_readers.clear(); // joins: their pipes close as the programs die
}

auto pipeline::read(std::span<std::byte> into) -> std::size_t {
    if (into.empty()) return 0;
    DWORD got = 0;
    const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(into.size(), 1U << 20U));
    if (ReadFile(state_->output.get(), into.data(), wanted, &got, nullptr) == 0) return 0;
    return got;
}

auto pipeline::kill() -> void {
    const std::scoped_lock lock(state_->mutex);
    if (state_->killed) return;
    state_->killed = true;
    TerminateJobObject(state_->job.get(), 1);
}

auto pipeline::wait() -> std::vector<int> {
    std::vector<int> codes;
    for (const handle& process : state_->processes) {
        WaitForSingleObject(process.get(), INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process.get(), &code);
        codes.push_back(static_cast<int>(code));
    }
    return codes;
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
