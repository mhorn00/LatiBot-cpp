#pragma once

#include "core/config/config_error.hpp"
#include "core/config/feature_sections.hpp"
#include "core/util/log.hpp"

#include <dpp/json.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::config {

/// Global settings from `config.json` (docs/features/Operations.md §4).
///
/// Everything guild-specific lives in the database instead, because it is
/// edited at runtime through commands and panels.
struct bootstrap {
    /// Debug builds start at `debug`, release builds at `info`. `config.json`
    /// overrides that, and `LATIBOT_LOG_LEVEL` overrides both (see `load`).
    util::log_level log_level = util::default_log_level;

    std::filesystem::path database_path{"data/bot.db"};
    std::filesystem::path backup_directory{"data/backups"};
    int backups_to_keep = 7;
    std::chrono::minutes backup_interval{360};

    /// Servers whose administrators may use the DECtalk commands that touch
    /// the host filesystem, and users who may regardless of server
    /// (docs/features/Speech.md §2.2).
    std::vector<dpp::snowflake> trusted_guilds;
    std::vector<dpp::snowflake> trusted_users;

    // The sections of the features still inside the core
    // (core/config/feature_sections.hpp). Each moves to its module.
    linkstats_config linkstats;
    llm_config llm;
    music_config music;

    /// Every other object in config.json, as one object by name: a module's
    /// section, which the module reads with its own table
    /// (`modules::host::section`). One no module reads is warned about and
    /// ignored. JSON rather than a std::map, whose move can throw.
    nlohmann::json sections = nlohmann::json::object();

    /// The account whose messages `/linkstats recompute` reads as the bot's
    /// replacements, in place of the bot's own.
    ///
    /// For testing the statistics with a second bot while the production one
    /// is still running: the history worth reading was written by the other
    /// account. Set from `LATIBOT_DEBUG_RECOMPUTE_BOT_ID` in debug builds only,
    /// never from the file (see `recompute_bot_id_from_environment`).
    std::optional<dpp::snowflake> recompute_bot_id;

    /// Parses config text. Throws `config_error` naming the offending key.
    ///
    /// Unknown keys are rejected rather than ignored, so a typo in a
    /// hand-edited file is reported instead of silently doing nothing. An
    /// object is a module's section, kept for it in `sections`. A key from
    /// before sections, such as "llm_model", is read into its section with a
    /// warning naming its new place.
    [[nodiscard]] static auto from_json(std::string_view text) -> bootstrap;

    /// Reads the file. When there is nothing at `path`, writes `default_json`
    /// there, so a fresh install has a file to edit, and returns the
    /// defaults; a file that cannot be written is logged and the defaults
    /// used all the same. Throws `config_error` when something is there that
    /// cannot be read, rather than writing over it.
    ///
    /// `module_sections` are the built modules' sections at their defaults,
    /// `modules::enabled_config_defaults()`, for the file written.
    [[nodiscard]] static auto load(const std::filesystem::path& path,
                                   const nlohmann::ordered_json& module_sections = nlohmann::ordered_json::object()) -> bootstrap;

    /// The file `load` writes: every key at its default, except `log_level`,
    /// left out so each build keeps its own, then each section's.
    /// config.example.json in the repo is exactly this text, which a test
    /// checks.
    [[nodiscard]] static auto default_json(const nlohmann::ordered_json& module_sections = nlohmann::ordered_json::object()) -> std::string;

    /// Whether this user may use the host-touching DECtalk commands here.
    [[nodiscard]] auto is_trusted(dpp::snowflake guild_id, dpp::snowflake user_id, bool administrator) const -> bool;
};

/// The level `LATIBOT_LOG_LEVEL` asks for, or nothing when it is unset.
///
/// Separate from `bootstrap::load` so the level can be applied before the
/// configuration is read, which is the only way loading it is itself logged at
/// the level that was asked for. Throws `config_error` naming the valid levels.
[[nodiscard]] auto log_level_from_environment() -> std::optional<util::log_level>;

/// Whether this build reads the `LATIBOT_DEBUG_*` variables. Only a debug
/// build does, so a line left in `.env` cannot change what a release build
/// does.
inline constexpr bool reads_debug_overrides =
#ifdef NDEBUG
    false;
#else
    true;
#endif

/// The account `LATIBOT_DEBUG_RECOMPUTE_BOT_ID` names, when `debug_build`.
///
/// Nothing when it is unset or empty. In a release build it is not read at
/// all, only warned about if set, so that a forgotten override is noticed
/// rather than quietly obeyed. Throws `config_error` when a debug build finds
/// something that is not an ID: a typo in a testing aid should stop the run,
/// not fall back to reading the wrong history.
[[nodiscard]] auto recompute_bot_id_from_environment(bool debug_build = reads_debug_overrides) -> std::optional<dpp::snowflake>;

/// Credentials. These only ever come from the environment, never from a file
/// that could be committed (docs/features/Operations.md §4).
struct secrets {
    std::string discord_token;
    std::optional<std::string> anthropic_key;
    std::optional<std::string> openai_key;

    /// The account yt-dlp signs in as when it must, so music can play
    /// age-restricted videos (docs/features/Music.md §4.9): a Firefox
    /// profile's folder, from `LATIBOT_YTDLP_FIREFOX_PROFILE`, or a cookies
    /// file, from `LATIBOT_YTDLP_COOKIES`. Each is a sign-in, so it stays out
    /// of `config.json` like the keys.
    std::optional<std::filesystem::path> ytdlp_firefox_profile;
    std::optional<std::filesystem::path> ytdlp_cookies;

    /// Throws `config_error` when `DISCORD_BOT_TOKEN` is missing or empty.
    [[nodiscard]] static auto from_environment() -> secrets;
};

} // namespace latibot::config
