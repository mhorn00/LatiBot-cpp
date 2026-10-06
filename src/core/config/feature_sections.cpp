#include "core/config/feature_sections.hpp"

namespace latibot::config {

auto llm_section() -> const section<llm_config>& {
    static const section<llm_config> table{
        "llm",
        {
            key("provider", &llm_config::provider, "Who answers by default: anthropic or openai."),
            key("model", &llm_config::model, "The model that answers by default; a server can choose another."),
            key("spend_cap_daily_usd", &llm_config::spend_cap_daily_usd, "The most the model may cost in a day, across every server.",
                at_least(0)),
            key("spend_cap_monthly_usd", &llm_config::spend_cap_monthly_usd, "The most it may cost in a month.", at_least(0)),
            key("tool_rounds", &llm_config::tool_rounds, "How many times one answer may call its tools.", at_least(1)),
        }};
    return table;
}

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
