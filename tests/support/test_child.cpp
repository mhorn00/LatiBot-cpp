// A program the process tests run in place of yt-dlp and ffmpeg
// (src/modules/music/docs/Music.md §7). What it does is its first argument:
//
//   echo-args ...     each further argument on a line of its own, as the
//                     system handed them over, in UTF-8
//   bytes N           N bytes of the pattern i % 251 on stdout
//   samples N         N 16-bit samples of the pattern i % 1000 on stdout
//   cat               stdin copied to stdout
//   stderr TEXT...    each further argument on a line of its own on stderr
//   exit CODE         nothing, then exit with CODE
//   fail CODE TEXT    TEXT on stderr, then exit with CODE
//   hang              nothing, for an hour
//   spawn-hang        starts itself with `hang`, then hangs as well
//   cwd               the folder it runs in, on a line of its own
//
// Started with `--ignore-config`, as yt-dlp is to read a link, it answers
// as yt-dlp would, going by the link, its last argument: one with "fail" in
// it fails, one with "hang" hangs, one with "list" is a playlist of three,
// and anything else is one track. Given `--cookies FILE` next, as yt-dlp
// signs in, the track is titled "signed in" when FILE holds a youtube.com
// cookie, and FILE is then written over as yt-dlp writes its cookies back.
// Given `--cookies-from-browser firefox:PROFILE`, it is signed in when
// PROFILE has a cookies.sqlite.
// One with "adult" in it is refused, as YouTube refuses an age-restricted
// video, unless it is signed in.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#else
#include <spawn.h>
#include <unistd.h>

#include <cerrno>

extern char** environ; // NOLINT(readability-redundant-declaration): not every unistd.h declares it
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

// --------------------------------------------------------------------------
// What differs by system: the arguments, the standard streams, and starting
// a copy of itself
// --------------------------------------------------------------------------

#ifdef _WIN32

using stream = HANDLE;

auto standard_input() -> stream {
    return GetStdHandle(STD_INPUT_HANDLE);
}
auto standard_output() -> stream {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}
auto standard_error() -> stream {
    return GetStdHandle(STD_ERROR_HANDLE);
}

auto narrow(const wchar_t* text) -> std::string {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    if (!out.empty()) out.pop_back(); // the terminator
    return out;
}

auto write_all(stream to, const void* data, std::size_t size) -> bool {
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        DWORD wrote = 0;
        if (WriteFile(to, bytes, static_cast<DWORD>(size), &wrote, nullptr) == 0) return false;
        bytes += wrote;
        size -= wrote;
    }
    return true;
}

auto read_some(stream from, std::span<char> into) -> std::size_t {
    DWORD got = 0;
    if (ReadFile(from, into.data(), static_cast<DWORD>(into.size()), &got, nullptr) == 0) return 0;
    return got;
}

/// The arguments as Windows parses the command line, rather than as the
/// C runtime's narrow `argv`, which is not UTF-8.
auto arguments(int /*count*/, char** /*values*/) -> std::vector<std::string> {
    int count = 0;
    wchar_t** raw = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        args.push_back(narrow(raw[i])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    LocalFree(static_cast<HLOCAL>(raw));
    return args;
}

/// Starts itself with `hang`; its process id, or nothing.
auto spawn_hanging_copy() -> std::optional<std::uint64_t> {
    std::wstring self(MAX_PATH, L'\0');
    self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    std::wstring line = L"\"" + self + L"\" hang";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION started{};
    if (CreateProcessW(self.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started) == 0) {
        return std::nullopt;
    }
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    return started.dwProcessId;
}

#else

using stream = int;

auto standard_input() -> stream {
    return STDIN_FILENO;
}
auto standard_output() -> stream {
    return STDOUT_FILENO;
}
auto standard_error() -> stream {
    return STDERR_FILENO;
}

auto write_all(stream to, const void* data, std::size_t size) -> bool {
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t wrote = ::write(to, bytes, size);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return false;
        bytes += wrote;
        size -= static_cast<std::size_t>(wrote);
    }
    return true;
}

auto read_some(stream from, std::span<char> into) -> std::size_t {
    while (true) {
        const ssize_t got = ::read(from, into.data(), into.size());
        if (got < 0 && errno == EINTR) continue;
        return got < 0 ? 0 : static_cast<std::size_t>(got);
    }
}

auto arguments(int count, char** values) -> std::vector<std::string> {
    return {values, values + count}; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

/// Starts itself with `hang`; its process id, or nothing.
auto spawn_hanging_copy() -> std::optional<std::uint64_t> {
    std::string self = std::filesystem::read_symlink("/proc/self/exe").string();
    std::string mode = "hang";
    std::array<char*, 3> argv{self.data(), mode.data(), nullptr};
    pid_t started = 0;
    if (posix_spawn(&started, self.c_str(), nullptr, nullptr, argv.data(), environ) != 0) return std::nullopt;
    return static_cast<std::uint64_t>(started);
}

#endif

auto write_line(stream to, const std::string& text) -> bool {
    const std::string line = text + "\n";
    return write_all(to, line.data(), line.size());
}

auto hang() -> int {
    std::this_thread::sleep_for(std::chrono::hours{1});
    return 0;
}

// --------------------------------------------------------------------------
// What it does
// --------------------------------------------------------------------------

/// Answers as yt-dlp would to read `args.back()`, a link.
auto pretend_to_be_yt_dlp(const std::vector<std::string>& args) -> int {
    const stream out = standard_output();
    const stream err = standard_error();
    const std::string& link = args.back();
    if (link.contains("fail")) {
        write_line(err, "ERROR: [generic] Unable to download webpage: HTTP Error 404: Not Found");
        return 1;
    }
    if (link.contains("hang")) return hang();
    std::string title = "A song";
    const auto flag = std::ranges::find(args, std::string("--cookies"));
    if (flag != args.end() && flag + 1 != args.end()) {
        const std::string& file = *(flag + 1);
        std::ifstream cookies(file, std::ios::binary);
        const std::string held((std::istreambuf_iterator<char>(cookies)), std::istreambuf_iterator<char>());
        if (held.contains("youtube.com\t")) title = "signed in";
        cookies.close();
        std::ofstream(file, std::ios::binary | std::ios::trunc) << "# written back by the stand-in\n";
    }
    const auto browser = std::ranges::find(args, std::string("--cookies-from-browser"));
    if (browser != args.end() && browser + 1 != args.end() && (browser + 1)->starts_with("firefox:")) {
        const std::string profile = (browser + 1)->substr(std::string_view("firefox:").size());
        std::error_code error;
        if (std::filesystem::exists(std::filesystem::path(profile) / "cookies.sqlite", error)) title = "signed in";
    }
    if (link.contains("adult") && title != "signed in") {
        write_line(err,
                   "ERROR: [youtube] abc: Sign in to confirm your age. This video may be inappropriate for some users. "
                   "Use --cookies-from-browser or --cookies for the authentication.");
        return 1;
    }
    std::string json;
    if (link.contains("list")) {
        json = R"({"_type": "playlist", "title": "Three songs", "playlist_count": 3, "entries": [)"
               R"({"url": "https://example.com/1", "title": "One", "duration": 60},)"
               R"({"url": "https://example.com/2", "title": "Two", "duration": 120.4},)"
               R"({"url": "https://example.com/3", "title": "Three"}]})";
    } else {
        json = R"({"title": ")" + title + R"(", "webpage_url": ")" + link + R"(", "duration": 61, "uploader": "tester"})";
    }
    return write_line(out, json) ? 0 : 1;
}

auto child_main(const std::vector<std::string>& args) -> int {
    if (args.size() < 2) return 2;
    const std::string& mode = args[1];
    const stream out = standard_output();
    const stream err = standard_error();

    if (mode == "echo-args") {
        for (std::size_t i = 2; i < args.size(); ++i) {
            write_line(out, args[i]);
        }
        return 0;
    }
    if (mode == "bytes" && args.size() > 2) {
        const auto count = std::strtoull(args[2].c_str(), nullptr, 10);
        std::vector<unsigned char> data(count);
        for (std::size_t i = 0; i < count; ++i) {
            data[i] = static_cast<unsigned char>(i % 251);
        }
        return write_all(out, data.data(), data.size()) ? 0 : 1;
    }
    if (mode == "samples" && args.size() > 2) {
        const auto count = std::strtoull(args[2].c_str(), nullptr, 10);
        std::vector<std::int16_t> data(count);
        for (std::size_t i = 0; i < count; ++i) {
            data[i] = static_cast<std::int16_t>(i % 1000);
        }
        return write_all(out, data.data(), data.size() * sizeof(std::int16_t)) ? 0 : 1;
    }
    if (mode == "cat") {
        std::array<char, 8192> buffer{};
        while (const std::size_t got = read_some(standard_input(), buffer)) {
            if (!write_all(out, buffer.data(), got)) return 1;
        }
        return 0;
    }
    if (mode == "stderr") {
        for (std::size_t i = 2; i < args.size(); ++i) {
            write_line(err, args[i]);
        }
        return 0;
    }
    if (mode == "exit" && args.size() > 2) return static_cast<int>(std::strtol(args[2].c_str(), nullptr, 10));
    if (mode == "fail" && args.size() > 3) {
        write_line(err, args[3]);
        return static_cast<int>(std::strtol(args[2].c_str(), nullptr, 10));
    }
    if (mode == "--ignore-config") return pretend_to_be_yt_dlp(args);
    if (mode == "hang") return hang();
    if (mode == "cwd") {
        const std::u8string here = std::filesystem::current_path().u8string();
        return write_line(out, std::string(here.begin(), here.end())) ? 0 : 1;
    }
    if (mode == "spawn-hang") {
        // Its id, so the test can check it was killed too.
        const auto started = spawn_hanging_copy();
        if (!started) return 1;
        write_line(out, std::to_string(*started));
        return hang();
    }
    return 2;
}

} // namespace

auto main(int count, char** values) -> int {
    try {
        return child_main(arguments(count, values));
    } catch (...) {
        return 3;
    }
}
