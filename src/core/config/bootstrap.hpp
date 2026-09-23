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
