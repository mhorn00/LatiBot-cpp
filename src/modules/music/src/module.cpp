#include "music/module.hpp"

#include "cookies.hpp"
#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/env.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "music_command.hpp"
#include "music_config.hpp"
#include "music_player.hpp"
#include "pot_provider.hpp"
#include "process.hpp"
#include "voice/services.hpp"
#include "voice/voice_mixer.hpp"
#include "yt_dlp.hpp"

#include <dpp/dpp.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace latibot::music {

auto sign_in_from_environment() -> sign_in {
    sign_in named;
    if (const auto profile = util::env_var("LATIBOT_YTDLP_FIREFOX_PROFILE"); profile && !profile->empty()) named.firefox_profile = *profile;
    if (const auto file = util::env_var("LATIBOT_YTDLP_COOKIES"); file && !file->empty()) named.cookies = *file;
    return named;
}

namespace {

constexpr std::string_view module_name = "music";

/// bgutil's PO token provider's `server` folder: where config.json says, or
/// beside the bot. Nothing when it is not there.
auto locate_pot_server(const music_config& section) -> std::optional<std::filesystem::path> {
    std::filesystem::path server = section.pot_provider_path;
    if (server.empty()) {
        const auto directory = util::executable_directory();
        if (!directory) return std::nullopt;
        server = *directory / "bgutil-ytdlp-pot-provider" / "server";
    }
    std::error_code error;
    if (!std::filesystem::is_directory(server, error)) return std::nullopt;
    std::filesystem::path found = std::filesystem::absolute(server, error);
    return error ? server : found;
}

/// What every run of yt-dlp is told: where Deno is, and the PO token
/// provider's address when its plugin is beside yt-dlp to ask it.
auto music_extras(const std::optional<std::filesystem::path>& deno, const std::optional<std::filesystem::path>& ytdlp, int pot_port)
    -> ytdlp_extras {
    ytdlp_extras extras{.deno = deno, .pot_provider = {}, .sign_in = {}};
    if (ytdlp && pot_plugin_installed(*ytdlp)) extras.pot_provider = pot_provider_address(pot_port);
    return extras;
}

/// The queue's pages.
class queue_panel {
public:
    explicit queue_panel(music_player*& player) : player_(&player) {}

    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& /*chosen*/) -> bool {
        return *player_ != nullptr && commands::on_music_component(**player_, event, state);
    }

private:
    music_player** player_;
};

class music_module final : public modules::module {
public:
    explicit music_module(modules::host& bot)
        : bot_(&bot),
          section_(music_section().read(bot.section(module_name))),
          sign_in_(sign_in_from_environment()),
          ytdlp_(util::locate_program("yt-dlp", section_.ytdlp_path)),
          ffmpeg_(util::locate_program("ffmpeg", section_.ffmpeg_path)),
          deno_(util::locate_program("deno", section_.deno_path)),
          pot_server_(locate_pot_server(section_)),
          cookies_(load_cookies(sign_in_.cookies, sign_in_.firefox_profile, bot.bootstrap().database_path.parent_path() / "yt-dlp-runs")),
          resolver_(ytdlp_.value_or("yt-dlp.exe"), std::chrono::seconds{30}, 2, cookies_.source,
                    music_extras(deno_, ytdlp_, section_.pot_provider_port)),
          opener_(ytdlp_.value_or("yt-dlp.exe"), ffmpeg_.value_or("ffmpeg.exe"), true, cookies_.source,
                  music_extras(deno_, ytdlp_, section_.pot_provider_port)),
          panel_(player_pointer_) {}

    music_module(const music_module&) = delete;
    music_module(music_module&&) = delete;
    auto operator=(const music_module&) -> music_module& = delete;
    auto operator=(music_module&&) -> music_module& = delete;

    /// The mixer, which voice keeps, stops reading from the player first.
    ~music_module() override {
        if (voice_ != nullptr) voice_->mixer().set_music(nullptr);
    }

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }

    auto start(modules::host& bot) -> void override {
        voice_ = &voice::required(bot.capabilities());
        player_ = std::make_unique<music_player>(
            opener_, voice_->mixer(),
            player_options{.volume_percent = [this](dpp::snowflake guild) { return commands::music_volume_for(bot_->settings(), guild); },
                           .track_limit = [this](dpp::snowflake guild) { return commands::track_limit_for(bot_->settings(), guild); },
                           .notify =
                               [this](dpp::snowflake channel, std::string text) {
                                   bot_->post(events::send_message{.channel_id = channel,
                                                                   .content = std::move(text),
                                                                   .flags = dpp::m_suppress_notifications,
                                                                   .what = "a note about a track"});
                               }});
        player_pointer_ = player_.get();

        // Music plays through the mixer, which reads it from the player, and
        // leaving takes the queue with it (docs/features/Music.md §3.4).
        voice_->mixer().set_music(player_.get());
        voice_->on_left([this](dpp::snowflake guild) { player_->forget(guild); });

        log_tools();
        start_pot_provider();

        bot.slash_commands().add(std::make_unique<commands::music_command>(commands::music_services{
            .player = player_.get(), .resolver = &resolver_, .settings = &bot.settings(), .unavailable = unavailable()}));
        bot.panels().add(panel_, {commands::music_queue_view}, module_name);
    }

private:
    /// Why music cannot play, when yt-dlp or ffmpeg was not found; empty
    /// when both were.
    [[nodiscard]] auto unavailable() const -> std::string {
        if (ytdlp_ && ffmpeg_) return {};
        std::string missing = "yt-dlp and ffmpeg";
        if (ytdlp_) missing = "ffmpeg";
        if (ffmpeg_) missing = "yt-dlp";
        return std::format(
            "i can't play music: {} isn't installed where i can find it. Put it beside the bot or on PATH, or name it in "
            "config.json (music.ytdlp_path, music.ffmpeg_path)",
            missing);
    }

    /// Says at startup whether music can play, and asks yt-dlp and ffmpeg
    /// their versions, for the log.
    auto log_tools() -> void {
        if (!ytdlp_ || !ffmpeg_) {
            util::log().warn("music is off: {}", unavailable());
            return;
        }
        const std::filesystem::path ytdlp = *ytdlp_;
        const std::filesystem::path ffmpeg = *ffmpeg_;
        util::log().info("music uses yt-dlp at {} and ffmpeg at {}", ytdlp.string(), ffmpeg.string());
        // A warning, not a reason to turn music off: most sites need no
        // JavaScript, and yt-dlp gets some of YouTube without it.
        if (deno_) {
            util::log().info("yt-dlp solves YouTube's JavaScript challenges with Deno at {}", deno_->string());
        } else {
            util::log().warn(
                "Deno was not found, so yt-dlp cannot solve YouTube's JavaScript challenges, and some YouTube videos will fail, "
                "age-restricted ones above all. Install Deno 2.3 or newer (winget install DenoLand.Deno), beside the bot or on PATH, "
                "or name it in config.json (music.deno_path)");
        }
        log_account();
        // Which versions, off the startup path: an old yt-dlp is the usual
        // reason a site stops working, and asking takes a second or two.
        std::vector<std::pair<std::filesystem::path, const char*>> programs{{ytdlp, "--version"}, {ffmpeg, "-version"}};
        if (deno_) programs.emplace_back(*deno_, "--version");
        // Only the log call in the catch could still throw, as in main(), and
        // there is nowhere left to report that.
        // NOLINTNEXTLINE(bugprone-exception-escape)
        versions_ = std::jthread([programs = std::move(programs)] {
            // Everything inside the try: a thread must let nothing out.
            try {
                for (const auto& [program, flag] : programs) {
                    const auto ran = util::run({.path = program, .arguments = {flag}, .working_directory = {}}, std::chrono::seconds{20});
                    const auto first_line = util::lines(ran.output);
                    util::log().info("{}: {}", program.stem().string(), first_line.empty() ? "no version given" : first_line.front());
                }
            } catch (const std::exception& error) {
                util::log().warn("could not ask yt-dlp, ffmpeg or Deno its version: {}", error.what());
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        });
    }

    /// Says whether yt-dlp signs in, and with how many cookies, never what
    /// they are (docs/features/Music.md §4.9).
    auto log_account() const -> void {
        const cookie_status& cookies = cookies_;
        const std::string named = cookies.named().generic_string();
        if (named.empty()) {
            util::log().debug("music fetches signed out: neither LATIBOT_YTDLP_FIREFOX_PROFILE nor LATIBOT_YTDLP_COOKIES is set");
            return;
        }
        const bool profile = !cookies.profile.empty();
        if (cookies.both_named) {
            util::log().warn("LATIBOT_YTDLP_FIREFOX_PROFILE and LATIBOT_YTDLP_COOKIES are both set; music uses the Firefox profile, not {}",
                             cookies.file.generic_string());
        }
        // A warning: the owner set it to sign in, and it will not.
        if (!cookies.source) {
            util::log().warn("music fetches signed out: {} names {}, which {}",
                             profile ? "LATIBOT_YTDLP_FIREFOX_PROFILE" : "LATIBOT_YTDLP_COOKIES", named, cookies.problem);
            return;
        }
        // Counts only. The cookies are a sign-in, and never logged.
        util::log().info("music signs in when it must with the {} {}: {} cookie(s), {} of them for youtube.com",
                         profile ? "Firefox profile" : "cookies in", named, cookies.found.cookies, cookies.found.youtube);
        const std::string_view again =
            profile ? "open Firefox with it, sign in to YouTube, and close Firefox" : "export it again signed in";
        if (cookies.found.youtube == 0) {
            util::log().warn("{} has no youtube.com cookies, so YouTube will see music as signed out; {}", named, again);
        } else if (!cookies.found.youtube_sign_in) {
            util::log().warn(
                "{} has no youtube.com SAPISID or __Secure-3PAPISID cookie, which yt-dlp needs to sign in; {} "
                "(docs/features/Music.md §4.9)",
                named, again);
        }
        if (cookies.unsaved) {
            util::log().warn(
                "Firefox has cookies for {} that it has not yet saved where yt-dlp reads them; close Firefox, which saves "
                "them, and keep it closed while the bot runs",
                named);
        }
        if (cookies.found.malformed > 0) {
            util::log().warn("{} line(s) of {} are not cookies in the Netscape format, and yt-dlp will skip them", cookies.found.malformed,
                             named);
        }
    }

    /// Starts bgutil's PO token provider when it is set up, and says why not
    /// when it is not (docs/features/Music.md §4.10).
    auto start_pot_provider() -> void {
        if (!ytdlp_ || !ffmpeg_) return;
        const std::string address = pot_provider_address(section_.pot_provider_port);
        const bool plugin = pot_plugin_installed(*ytdlp_);
        const std::string plugins = (ytdlp_->parent_path() / "yt-dlp-plugins").string();

        if (!pot_server_) {
            if (!section_.pot_provider_path.empty()) {
                util::log().warn("music.pot_provider_path in config.json names {}, which is not a folder, so no PO token provider runs",
                                 section_.pot_provider_path.generic_string());
            } else if (plugin) {
                util::log().info(
                    "bgutil's PO token plugin is in {}, but its provider is not beside the bot; yt-dlp asks {} for tokens, "
                    "so run one there (docs/features/Music.md §4.10)",
                    plugins, address);
            } else {
                util::log().info(
                    "yt-dlp gets no PO tokens, so YouTube may refuse some of its requests; Install-Dependencies.ps1 sets "
                    "a provider up (docs/features/Music.md §4.10)");
            }
            return;
        }
        const std::string server = pot_server_->string();
        if (!plugin) {
            util::log().warn(
                "bgutil's PO token provider is at {}, but its yt-dlp plugin is not in {}, so yt-dlp would never ask it; "
                "run Install-Dependencies.ps1 again",
                server, plugins);
            return;
        }
        if (!deno_) {
            util::log().warn("bgutil's PO token provider at {} runs with Deno, which was not found", server);
            return;
        }
        if (!pot_provider_ready(*pot_server_)) {
            util::log().warn(
                "bgutil's PO token provider at {} is not set up: its packages are not installed; run "
                "Install-Dependencies.ps1 again",
                server);
            return;
        }
        pot_provider_ = std::make_unique<pot_provider>(pot_provider_program(*deno_, *pot_server_, section_.pot_provider_port));
        util::log().info("yt-dlp gets PO tokens from bgutil's provider, run with Deno from {}, at {}", server, address);
    }

    modules::host* bot_;
    music_config section_;
    sign_in sign_in_;
    // yt-dlp and ffmpeg are looked for once, at startup; without them the
    // music commands say so and nothing plays.
    std::optional<std::filesystem::path> ytdlp_;
    std::optional<std::filesystem::path> ffmpeg_;
    /// Optional: without it yt-dlp cannot solve YouTube's JavaScript
    /// challenges, and some of YouTube, age-restricted videos above all, fails.
    std::optional<std::filesystem::path> deno_;
    /// bgutil's PO token provider's `server` folder, when it is there.
    std::optional<std::filesystem::path> pot_server_;
    /// The account yt-dlp signs in as when it must, if the owner gave one.
    cookie_status cookies_;
    ytdlp_resolver resolver_;
    ytdlp_opener opener_;
    voice::services* voice_ = nullptr;
    // Made in start, once voice's mixer can be found.
    std::unique_ptr<music_player> player_;
    music_player* player_pointer_ = nullptr;
    queue_panel panel_;
    /// Runs the PO token provider for as long as the bot runs; null when it
    /// is not set up.
    std::unique_ptr<pot_provider> pot_provider_;
    /// Asks yt-dlp and ffmpeg their versions at startup, without holding
    /// startup up. Last, so it is joined first.
    std::jthread versions_;
};

} // namespace

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<music_module>(bot);
}

auto config_defaults() -> nlohmann::ordered_json {
    return music_section().defaults();
}

} // namespace latibot::music
