#pragma once

#include "core/config/section.hpp"

#include <filesystem>
#include <optional>

namespace latibot::music {

/// The "music" section of config.json: where its programs are
/// (src/modules/music/docs/Music.md §5, §4.10).
struct music_config {
    /// Empty: `yt-dlp.exe` and `ffmpeg.exe` beside the bot, then on PATH.
    std::filesystem::path ytdlp_path;
    std::filesystem::path ffmpeg_path;
    /// Where Deno is, which yt-dlp solves YouTube's JavaScript challenges
    /// with. Empty: `deno.exe` beside the bot, then on PATH.
    std::filesystem::path deno_path;
    /// bgutil's PO token provider, which the bot runs while it runs: its
    /// `server` folder, and the port it listens on, on this machine only.
    /// Empty: the folder `bgutil-ytdlp-pot-provider/server` beside the bot.
    std::filesystem::path pot_provider_path;
    int pot_provider_port = 4416;
};

/// Its keys (docs/modules/Module_Plan_Final.md §8.2).
[[nodiscard]] inline auto music_section() -> const config::section<music_config>& {
    static const config::section<music_config> table{
        "music",
        {
            config::key("ytdlp_path", &music_config::ytdlp_path, "Where yt-dlp is. Empty: beside the bot, then PATH."),
            config::key("ffmpeg_path", &music_config::ffmpeg_path, "Where ffmpeg is. Empty: beside the bot, then PATH."),
            config::key("deno_path", &music_config::deno_path,
                        "Where Deno is, for YouTube's challenges. Empty: beside the bot, then PATH."),
            config::key("pot_provider_path", &music_config::pot_provider_path,
                        "bgutil's PO token provider's server folder. Empty: bgutil-ytdlp-pot-provider/server beside the bot."),
            config::key("pot_provider_port", &music_config::pot_provider_port, "The port the PO token provider listens on.",
                        config::range{.lowest = 1, .highest = 65535}),
        }};
    return table;
}

/// The account yt-dlp signs in as when it must, so music can play
/// age-restricted videos (src/modules/music/docs/Music.md §4.9): a Firefox profile's
/// folder, from LATIBOT_YTDLP_FIREFOX_PROFILE, or a cookies file, from
/// LATIBOT_YTDLP_COOKIES. Each is a sign-in, so it stays out of config.json.
struct sign_in {
    std::optional<std::filesystem::path> firefox_profile;
    std::optional<std::filesystem::path> cookies;
};

/// Reads them; an empty variable names nothing.
[[nodiscard]] auto sign_in_from_environment() -> sign_in;

} // namespace latibot::music
