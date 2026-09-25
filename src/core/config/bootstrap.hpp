#pragma once

#include "core/util/log.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::config {

/// A malformed config file or a missing secret. Always fatal at startup: the
/// bot should say what is wrong and stop, not run half-configured.
class config_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Global settings from `config.json` (plan v4 §5.1).
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

    /// Whether to watch for nickname changes (plan v4 §8).
    ///
    /// This is the one setting that decides which intents the bot asks for:
    /// nickname changes only arrive with the privileged Server Members
    /// intent, which must also be switched on in the Discord developer
    /// portal. A bot that asks for an intent it was not granted is refused
    /// the gateway entirely, so this is the way to turn the request off.
    bool track_nicknames = true;

    std::string llm_provider{"anthropic"};
    std::string llm_model{"claude-haiku-4-5"};
    double spend_cap_daily_usd = 2.0;
    double spend_cap_monthly_usd = 20.0;
    int llm_tool_rounds = 4;

    /// Servers whose administrators may use the DECtalk commands that touch
    /// the host filesystem, and users who may regardless of server
    /// (plan v4 §12.5).
    std::vector<dpp::snowflake> trusted_guilds;
    std::vector<dpp::snowflake> trusted_users;

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
    /// hand-edited file is reported instead of silently doing nothing.
    [[nodiscard]] static bootstrap from_json(std::string_view text);

    /// Reads the file, or returns the defaults when it does not exist.
    [[nodiscard]] static bootstrap load(const std::filesystem::path& path);

    /// Whether this user may use the host-touching DECtalk commands here.
    [[nodiscard]] bool is_trusted(dpp::snowflake guild_id, dpp::snowflake user_id, bool administrator) const;
};

/// The level `LATIBOT_LOG_LEVEL` asks for, or nothing when it is unset.
///
/// Separate from `bootstrap::load` so the level can be applied before the
/// configuration is read, which is the only way loading it is itself logged at
/// the level that was asked for. Throws `config_error` naming the valid levels.
[[nodiscard]] std::optional<util::log_level> log_level_from_environment();

/// Whether this build reads the `LATIBOT_DEBUG_*` variables. Only a debug
/// build does, so a line left in `.env` cannot change what a release build
/// does.
inline constexpr bool reads_debug_overrides =
#ifdef NDEBUG
    false;
#else
    true;
#endif

/// A Discord ID written as text: digits only, surrounding whitespace allowed.
/// Nothing for anything else, including 0, rather than the leading digits of
/// "123abc".
[[nodiscard]] std::optional<dpp::snowflake> parse_snowflake(std::string_view text);

/// The account `LATIBOT_DEBUG_RECOMPUTE_BOT_ID` names, when `debug_build`.
///
/// Nothing when it is unset or empty. In a release build it is not read at
/// all, only warned about if set, so that a forgotten override is noticed
/// rather than quietly obeyed. Throws `config_error` when a debug build finds
/// something that is not an ID: a typo in a testing aid should stop the run,
/// not fall back to reading the wrong history.
[[nodiscard]] std::optional<dpp::snowflake> recompute_bot_id_from_environment(bool debug_build = reads_debug_overrides);

/// Credentials. These only ever come from the environment, never from a file
/// that could be committed (plan v4 §5.1).
struct secrets {
    std::string discord_token;
    std::optional<std::string> anthropic_key;
    std::optional<std::string> openai_key;

    /// Throws `config_error` when `DISCORD_BOT_TOKEN` is missing or empty.
    [[nodiscard]] static secrets from_environment();
};

} // namespace latibot::config
