// bgutil's PO token provider, which the bot keeps running for yt-dlp's
// plugin (docs/features/Music.md §4.10): how it is run, how the bot finds
// it set up, and that it is started again when it stops, and stopped with
// the bot. The stand-in program (tests/support/test_child.cpp) plays it.

#include "pot_provider.hpp"
#include "support/capture_log.hpp"
#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using latibot::music::pot_plugin_installed;
using latibot::music::pot_provider;
using latibot::music::pot_provider_address;
using latibot::music::pot_provider_program;
using latibot::music::pot_provider_ready;
using latibot::testing::capture_log;
using latibot::testing::temp_directory;
using latibot::util::program;
using namespace std::chrono_literals;

namespace {

auto child(std::vector<std::string> arguments) -> program {
    return {.path = LATIBOT_TEST_CHILD, .arguments = std::move(arguments)};
}

/// Waits for `done`, or `limit`.
template <typename Done>
auto wait_for(Done done, std::chrono::milliseconds limit = 10s) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return done();
}

auto touch(const std::filesystem::path& path) -> void {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "";
}

} // namespace

TEST_CASE("the provider runs with Deno from its packages, on this machine's port", "[music]") {
    const auto run = pot_provider_program(R"(C:\bot\deno.exe)", R"(C:\bot\bgutil-ytdlp-pot-provider\server)", 4416);
    CHECK(run.path == std::filesystem::path(R"(C:\bot\deno.exe)"));
    CHECK(run.working_directory == std::filesystem::path(R"(C:\bot\bgutil-ytdlp-pot-provider\server\node_modules)"));
    CHECK(run.arguments == std::vector<std::string>{"run", "--allow-env", "--allow-net", "--allow-ffi=.", "--allow-read=.",
                                                    "../src/main.ts", "--port", "4416"});
    CHECK(pot_provider_address(4416) == "http://127.0.0.1:4416");
    CHECK(pot_provider_address(8080) == "http://127.0.0.1:8080");
}

TEST_CASE("the provider is ready once its packages are installed", "[music]") {
    const temp_directory folder;
    const auto server = folder.file("server");
    CHECK_FALSE(pot_provider_ready(server));
    touch(server / "src" / "main.ts");
    CHECK_FALSE(pot_provider_ready(server));
    std::filesystem::create_directories(server / "node_modules");
    CHECK(pot_provider_ready(server));
}

TEST_CASE("the plugin is found in yt-dlp's own plugin folder, as a zip or a folder", "[music]") {
    const temp_directory folder;
    const auto ytdlp = folder.file("yt-dlp.exe");
    CHECK_FALSE(pot_plugin_installed(ytdlp));
    touch(folder.file("yt-dlp-plugins") / "another-plugin.zip");
    CHECK_FALSE(pot_plugin_installed(ytdlp));
    touch(folder.file("yt-dlp-plugins") / "bgutil-ytdlp-pot-provider.zip");
    CHECK(pot_plugin_installed(ytdlp));
}

TEST_CASE("a provider that stops is started again", "[music][threads]") {
    const pot_provider provider(child({"exit", "1"}), 10ms, 40ms);
    CHECK(wait_for([&provider] { return provider.starts() >= 3; }));
}

TEST_CASE("what the provider writes is logged", "[music][threads]") {
    const capture_log log;
    {
        const pot_provider provider(child({"stderr", "listening on 127.0.0.1:4416"}), 1h);
        CHECK(wait_for([&log] {
            const auto lines = log.lines();
            return std::ranges::any_of(lines,
                                       [](const auto& line) { return line.second == "PO token provider: listening on 127.0.0.1:4416"; });
        }));
    }
}

TEST_CASE("stopping the bot stops the provider at once", "[music][threads]") {
    const auto started = std::chrono::steady_clock::now();
    {
        const pot_provider provider(child({"hang"}));
        CHECK(wait_for([&provider] { return provider.starts() == 1; }));
    }
    CHECK(std::chrono::steady_clock::now() - started < 5s);
}

TEST_CASE("a provider that cannot start is tried again, and stopped with the bot", "[music][threads]") {
    const capture_log log;
    const auto started = std::chrono::steady_clock::now();
    {
        const pot_provider provider(program{.path = R"(C:\no\such\deno.exe)", .arguments = {}}, 10ms, 20ms);
        CHECK(wait_for([&log] {
            const auto lines = log.lines();
            return std::ranges::count_if(lines, [](const auto& line) { return line.second.contains("could not be started"); }) >= 2;
        }));
        CHECK(provider.starts() == 0);
    }
    CHECK(std::chrono::steady_clock::now() - started < 5s);
}
