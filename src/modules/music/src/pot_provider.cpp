#include "pot_provider.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace latibot::music {

auto pot_provider_address(int port) -> std::string {
    return std::format("http://127.0.0.1:{}", port);
}

auto pot_provider_program(const std::filesystem::path& deno, const std::filesystem::path& server, int port) -> util::program {
    return {
        .path = deno,
        .arguments = {"run", "--allow-env", "--allow-net", "--allow-ffi=.", "--allow-read=.", "../src/main.ts", "--port",
                      std::to_string(port)},
        .working_directory = server / "node_modules",
    };
}

auto pot_provider_ready(const std::filesystem::path& server) -> bool {
    std::error_code error;
    return std::filesystem::is_regular_file(server / "src" / "main.ts", error) &&
           std::filesystem::is_directory(server / "node_modules", error);
}

auto pot_plugin_installed(const std::filesystem::path& ytdlp) -> bool {
    std::error_code error;
    for (std::filesystem::directory_iterator entry(ytdlp.parent_path() / "yt-dlp-plugins", error);
         !error && entry != std::filesystem::directory_iterator{}; entry.increment(error)) {
        if (entry->path().filename().string().starts_with("bgutil-ytdlp-pot-provider")) return true;
    }
    return false;
}

pot_provider::pot_provider(util::program program, std::chrono::milliseconds first_wait, std::chrono::milliseconds longest_wait)
    : program_(std::move(program)),
      first_wait_(first_wait),
      longest_wait_(std::max(longest_wait, first_wait)),
      thread_([this](const std::stop_token& stopping) { keep_running(stopping); }) {}

pot_provider::~pot_provider() {
    thread_.request_stop();
    {
        const std::scoped_lock lock(mutex_);
        if (running_ != nullptr) running_->kill();
    }
    wake_.notify_all();
}

auto pot_provider::starts() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    return starts_;
}

// Only the log calls in the catch could still throw, and there is nowhere
// left to report that.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto pot_provider::keep_running(const std::stop_token& stopping) -> void {
    // Logged at debug: it says each token it makes, which is noise unless
    // something is wrong.
    const auto log_line = [](std::string_view line) {
        if (!util::trim(line).empty()) util::log().debug("PO token provider: {}", line);
    };

    std::chrono::milliseconds wait = first_wait_;
    while (!stopping.stop_requested()) {
        const auto started = std::chrono::steady_clock::now();
        // Outside the try, so `running_` is cleared before it goes, however
        // the try ends: the destructor may be about to kill it.
        std::unique_ptr<util::pipeline> running;
        std::string outcome;
        try {
            {
                const std::scoped_lock lock(mutex_);
                if (stopping.stop_requested()) return;
                running = std::make_unique<util::pipeline>(std::span(&program_, 1),
                                                           [&log_line](std::size_t, std::string_view line) { log_line(line); });
                running_ = running.get();
                ++starts_;
            }

            // Its output read as it comes, a line at a time: a full pipe
            // would stop it.
            std::array<std::byte, 4096> bytes{};
            std::string pending;
            while (const std::size_t got = running->read(bytes)) {
                pending.append(reinterpret_cast<const char*>(bytes.data()), got);
                for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
                    log_line(std::string_view(pending).substr(0, end));
                    pending.erase(0, end + 1);
                }
            }
            log_line(pending);
            outcome = std::format("stopped with code {}", running->wait().front());
        } catch (const std::exception& error) {
            outcome = std::format("could not be started: {}", error.what());
        }
        {
            const std::scoped_lock lock(mutex_);
            running_ = nullptr;
        }
        running.reset();
        if (stopping.stop_requested()) return;
        util::log().warn("the PO token provider {}; starting it again in {}", outcome,
                         std::chrono::duration_cast<std::chrono::seconds>(wait));

        // One that ran a good while is started again soon; one that keeps
        // stopping at once, as when its port is taken, less and less often.
        const bool ran_long = std::chrono::steady_clock::now() - started > std::chrono::minutes{1};
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, stopping, wait, [] { return false; });
        wait = ran_long ? first_wait_ : std::min(wait * 2, longest_wait_);
    }
}

} // namespace latibot::music
