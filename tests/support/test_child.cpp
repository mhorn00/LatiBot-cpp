// A program the process tests run in place of yt-dlp and ffmpeg
// (docs/features/Music.md §7). What it does is its first argument:
//
//   echo-args ...     each further argument on a line of its own, as Windows
//                     parsed the command line, in UTF-8
//   bytes N           N bytes of the pattern i % 251 on stdout
//   samples N         N 16-bit samples of the pattern i % 1000 on stdout
//   cat               stdin copied to stdout
//   stderr TEXT...    each further argument on a line of its own on stderr
//   exit CODE         nothing, then exit with CODE
//   fail CODE TEXT    TEXT on stderr, then exit with CODE
//   hang              nothing, for an hour
//   spawn-hang        starts itself with `hang`, then hangs as well
//
// Started with `--ignore-config`, as yt-dlp is to read a link, it answers
// as yt-dlp would, going by the link, its last argument: one with "fail" in
// it fails, one with "hang" hangs, one with "list" is a playlist of three,
// and anything else is one track.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

auto narrow(const wchar_t* text) -> std::string {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    if (!out.empty()) out.pop_back(); // the terminator
    return out;
}

auto write_all(HANDLE to, const void* data, std::size_t size) -> bool {
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        DWORD wrote = 0;
        if (WriteFile(to, bytes, static_cast<DWORD>(size), &wrote, nullptr) == 0) return false;
        bytes += wrote;
        size -= wrote;
    }
    return true;
}

auto arguments() -> std::vector<std::string> {
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

auto hang() -> int {
    std::this_thread::sleep_for(std::chrono::hours{1});
    return 0;
}

auto child_main() -> int {
    const std::vector<std::string> args = arguments();
    if (args.size() < 2) return 2;
    const std::string& mode = args[1];
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);

    if (mode == "echo-args") {
        for (std::size_t i = 2; i < args.size(); ++i) {
            const std::string line = args[i] + "\n";
            write_all(out, line.data(), line.size());
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
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        std::array<char, 8192> buffer{};
        DWORD got = 0;
        while (ReadFile(in, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) != 0 && got > 0) {
            if (!write_all(out, buffer.data(), got)) return 1;
        }
        return 0;
    }
    if (mode == "stderr") {
        for (std::size_t i = 2; i < args.size(); ++i) {
            const std::string line = args[i] + "\n";
            write_all(err, line.data(), line.size());
        }
        return 0;
    }
    if (mode == "exit" && args.size() > 2) return static_cast<int>(std::strtol(args[2].c_str(), nullptr, 10));
    if (mode == "fail" && args.size() > 3) {
        const std::string line = args[3] + "\n";
        write_all(err, line.data(), line.size());
        return static_cast<int>(std::strtol(args[2].c_str(), nullptr, 10));
    }
    if (mode == "--ignore-config") {
        const std::string& link = args.back();
        if (link.find("fail") != std::string::npos) {
            const std::string line = "ERROR: [generic] Unable to download webpage: HTTP Error 404: Not Found\n";
            write_all(err, line.data(), line.size());
            return 1;
        }
        if (link.find("hang") != std::string::npos) return hang();
        std::string json;
        if (link.find("list") != std::string::npos) {
            json = R"({"_type": "playlist", "title": "Three songs", "playlist_count": 3, "entries": [)"
                   R"({"url": "https://example.com/1", "title": "One", "duration": 60},)"
                   R"({"url": "https://example.com/2", "title": "Two", "duration": 120.4},)"
                   R"({"url": "https://example.com/3", "title": "Three"}]})";
        } else {
            json = R"({"title": "A song", "webpage_url": ")" + link + R"(", "duration": 61, "uploader": "tester"})";
        }
        json += "\n";
        write_all(out, json.data(), json.size());
        return 0;
    }
    if (mode == "hang") return hang();
    if (mode == "spawn-hang") {
        std::wstring self(MAX_PATH, L'\0');
        self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
        std::wstring line = L"\"" + self + L"\" hang";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION started{};
        if (CreateProcessW(self.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started) ==
            0) {
            return 1;
        }
        // Its id, so the test can check it was killed too.
        const std::string id = std::to_string(started.dwProcessId) + "\n";
        write_all(out, id.data(), id.size());
        CloseHandle(started.hThread);
        CloseHandle(started.hProcess);
        return hang();
    }
    return 2;
}

} // namespace

auto main() -> int {
    try {
        return child_main();
    } catch (...) {
        return 3;
    }
}
