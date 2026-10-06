#pragma once

#include "core/config/section.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

// The config.json sections of the features still inside the core
// (docs/modules/Module_Plan_Final.md §8.1). Each moves into its module when
// the module moves out, and is then read through `modules::host::section`.

namespace latibot::config {

/// "llm": which model answers, and what it is allowed to cost
/// (docs/features/Language_Model.md §3.2). Which providers and models are
/// allowed is the language model's to say (`llm::check_config`).
struct llm_config {
    std::string provider{"anthropic"};
    std::string model{"claude-haiku-4-5"};
    double spend_cap_daily_usd = 2.0;
    double spend_cap_monthly_usd = 20.0;
    int tool_rounds = 4;
};

/// "music": where its programs are (docs/features/Music.md §5, §4.10).
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

[[nodiscard]] auto llm_section() -> const section<llm_config>&;
[[nodiscard]] auto music_section() -> const section<music_config>&;

} // namespace latibot::config
