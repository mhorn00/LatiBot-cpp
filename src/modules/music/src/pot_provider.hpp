#pragma once

#include "process.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace latibot::music {

// bgutil's PO token provider (docs/features/Music.md §4.10): a small server
// on the bot's own machine that yt-dlp's bgutil plugin asks for the proof
// of origin tokens YouTube wants from its clients, so yt-dlp's requests look
// like the clients they claim to be. Install-Dependencies.ps1 puts it beside
// the bot; the bot runs it, with Deno, for as long as the bot runs.

/// The port the provider listens on, and its plugin asks, unless told.
inline constexpr int default_pot_port = 4416;

/// The address the plugin is given for a provider on `port`.
[[nodiscard]] auto pot_provider_address(int port) -> std::string;

/// The provider's server run with Deno, as bgutil's README gives it: from
/// `server/node_modules`, reading only there, listening on `port` on this
/// machine alone.
[[nodiscard]] auto pot_provider_program(const std::filesystem::path& deno, const std::filesystem::path& server, int port) -> util::program;

/// Whether `server`, bgutil's `server` folder, has been set up: its packages
/// installed into `node_modules`.
[[nodiscard]] auto pot_provider_ready(const std::filesystem::path& server) -> bool;

/// Whether bgutil's yt-dlp plugin is installed beside `ytdlp`, in the
/// `yt-dlp-plugins` folder yt-dlp searches beside its executable.
[[nodiscard]] auto pot_plugin_installed(const std::filesystem::path& ytdlp) -> bool;

/// Keeps the provider running: starts it, logs what it writes, and starts
/// it again when it stops, waiting `first_wait`, then twice as long each
/// time it stops again within a minute, up to `longest_wait`. Destroying it
/// stops the provider, and anything it started.
class pot_provider {
public:
    explicit pot_provider(util::program program, std::chrono::milliseconds first_wait = std::chrono::seconds{5},
                          std::chrono::milliseconds longest_wait = std::chrono::minutes{10});
    ~pot_provider();

    pot_provider(const pot_provider&) = delete;
    auto operator=(const pot_provider&) -> pot_provider& = delete;
    pot_provider(pot_provider&&) = delete;
    auto operator=(pot_provider&&) -> pot_provider& = delete;

    /// How many times it has been started, for tests.
    [[nodiscard]] auto starts() const -> std::size_t;

private:
    auto keep_running(const std::stop_token& stopping) -> void;

    util::program program_;
    std::chrono::milliseconds first_wait_;
    std::chrono::milliseconds longest_wait_;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    /// The running provider, to kill from the destructor. Owned by the
    /// thread, which only changes it with `mutex_` held.
    util::pipeline* running_ = nullptr;
    std::size_t starts_ = 0;

    /// Last, so it stops before anything it uses goes.
    std::jthread thread_;
};

} // namespace latibot::music
