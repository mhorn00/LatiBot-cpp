#include "core/config/feature_sections.hpp"

namespace latibot::config {

auto music_section() -> const section<music_config>& {
    static const section<music_config> table{
        "music",
        {
            key("ytdlp_path", &music_config::ytdlp_path, "Where yt-dlp is. Empty: beside the bot, then PATH."),
            key("ffmpeg_path", &music_config::ffmpeg_path, "Where ffmpeg is. Empty: beside the bot, then PATH."),
            key("deno_path", &music_config::deno_path, "Where Deno is, for YouTube's challenges. Empty: beside the bot, then PATH."),
            key("pot_provider_path", &music_config::pot_provider_path,
                "bgutil's PO token provider's server folder. Empty: bgutil-ytdlp-pot-provider/server beside the bot."),
            key("pot_provider_port", &music_config::pot_provider_port, "The port the PO token provider listens on.",
                range{.lowest = 1, .highest = 65535}),
        }};
    return table;
}

} // namespace latibot::config
