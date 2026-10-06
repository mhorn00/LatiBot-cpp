#include "core/config/bootstrap.hpp"

#include "core/util/env.hpp"
#include "core/util/text.hpp"

#include <dpp/json.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace latibot::config {
namespace {

using json = nlohmann::json;

[[noreturn]] auto wrong_type(std::string_view key, std::string_view expected) -> void {
    throw config_error("config key \"" + std::string(key) + "\" must be " + std::string(expected));
}

auto require_string(const json& object, std::string_view key) -> std::string {
    const auto& value = object.at(std::string(key));
    if (!value.is_string()) wrong_type(key, "a string");
    return value.get<std::string>();
}

auto require_number(const json& object, std::string_view key) -> double {
    const auto& value = object.at(std::string(key));
    if (!value.is_number()) wrong_type(key, "a number");
    return value.get<double>();
}

auto require_int(const json& object, std::string_view key) -> int {
    const auto& value = object.at(std::string(key));
    if (!value.is_number_integer()) wrong_type(key, "a whole number");
    return value.get<int>();
}

/// Snowflakes are 64-bit and JSON numbers are doubles, which silently lose
/// precision past 2^53, so IDs are written as strings everywhere they cross a
/// JSON boundary (docs/features/Operations.md §4).
auto require_snowflakes(const json& object, std::string_view key) -> std::vector<dpp::snowflake> {
    const auto& value = object.at(std::string(key));
    if (!value.is_array()) wrong_type(key, "an array of ID strings");

    std::vector<dpp::snowflake> ids;
    ids.reserve(value.size());
    for (const auto& entry : value) {
        if (!entry.is_string()) {
            throw config_error("config key \"" + std::string(key) +
                               "\" must hold IDs as strings: a JSON number cannot represent a Discord ID exactly");
        }

        const std::string text = entry.get<std::string>();
        const auto id = util::parse_snowflake(text);
        if (!id) throw config_error("config key \"" + std::string(key) + "\" has \"" + text + "\", which is not a Discord ID");
        ids.push_back(*id);
    }
    return ids;
}

auto require_bool(const json& object, std::string_view key) -> bool {
    const auto& value = object.at(std::string(key));
    if (!value.is_boolean()) wrong_type(key, "true or false");
    return value.get<bool>();
}

/// Rejects anything we do not recognise, so a typo in a hand-edited file is
/// reported instead of silently doing nothing. A key added here belongs in
/// `bootstrap::default_json` too, at its default.
auto reject_unknown_keys(const json& parsed) -> void {
    static constexpr std::array<std::string_view, 19> known_keys{
        "log_level",       "database_path",  "backup_directory",  "backups_to_keep",     "backup_interval_minutes",
        "track_nicknames", "llm_provider",   "llm_model",         "spend_cap_daily_usd", "spend_cap_monthly_usd",
        "llm_tool_rounds", "trusted_guilds", "trusted_users",     "emoji_copy_min_uses", "ytdlp_path",
        "ffmpeg_path",     "deno_path",      "pot_provider_path", "pot_provider_port",
    };

    for (const auto& [key, unused] : parsed.items()) {
        if (std::ranges::find(known_keys, key) == known_keys.end()) throw config_error("unknown config key \"" + key + "\"");
    }
}

/// Where music's programs are (docs/features/Music.md §5, §4.10).
auto read_music_keys(const json& parsed, bootstrap& config) -> void {
    if (parsed.contains("ytdlp_path")) config.ytdlp_path = require_string(parsed, "ytdlp_path");
    if (parsed.contains("ffmpeg_path")) config.ffmpeg_path = require_string(parsed, "ffmpeg_path");
    if (parsed.contains("deno_path")) config.deno_path = require_string(parsed, "deno_path");
    if (parsed.contains("pot_provider_path")) config.pot_provider_path = require_string(parsed, "pot_provider_path");
    if (parsed.contains("pot_provider_port")) {
        config.pot_provider_port = require_int(parsed, "pot_provider_port");
        if (config.pot_provider_port < 1 || config.pot_provider_port > 65535) {
            throw config_error(R"(config key "pot_provider_port" must be a port, 1 to 65535)");
        }
    }
}

/// Where the database lives and how often it is copied.
auto read_storage_keys(const json& parsed, bootstrap& config) -> void {
    if (parsed.contains("database_path")) config.database_path = require_string(parsed, "database_path");
    if (parsed.contains("backup_directory")) config.backup_directory = require_string(parsed, "backup_directory");

    if (parsed.contains("backups_to_keep")) {
        config.backups_to_keep = require_int(parsed, "backups_to_keep");
        if (config.backups_to_keep < 0) throw config_error("config key \"backups_to_keep\" cannot be negative");
    }

    if (parsed.contains("backup_interval_minutes")) {
        const int minutes = require_int(parsed, "backup_interval_minutes");
        if (minutes <= 0) throw config_error("config key \"backup_interval_minutes\" must be positive");
        config.backup_interval = std::chrono::minutes{minutes};
    }
}

/// Which model answers, and what it is allowed to cost
/// (docs/features/Language_Model.md §3.2).
auto read_llm_keys(const json& parsed, bootstrap& config) -> void {
    if (parsed.contains("llm_provider")) config.llm_provider = require_string(parsed, "llm_provider");
    if (parsed.contains("llm_model")) config.llm_model = require_string(parsed, "llm_model");
    if (parsed.contains("spend_cap_daily_usd")) config.spend_cap_daily_usd = require_number(parsed, "spend_cap_daily_usd");
    if (parsed.contains("spend_cap_monthly_usd")) config.spend_cap_monthly_usd = require_number(parsed, "spend_cap_monthly_usd");

    if (parsed.contains("llm_tool_rounds")) {
        config.llm_tool_rounds = require_int(parsed, "llm_tool_rounds");
        if (config.llm_tool_rounds < 1) throw config_error("config key \"llm_tool_rounds\" must be at least 1");
    }

    if (config.spend_cap_daily_usd < 0 || config.spend_cap_monthly_usd < 0) throw config_error("the spend caps cannot be negative");

    // Which provider and model are allowed is the language model's to say
    // (llm::check_config): the core reads the keys without knowing them.
}

/// Writes the defaults where the configuration was looked for. Never fatal:
/// the defaults are what the bot runs on either way, and a folder it cannot
/// write to is no reason not to start.
auto write_default_config(const std::filesystem::path& path) -> void {
    const std::string shown = std::filesystem::absolute(path).generic_string();

    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file;
    if (!error) file.open(path, std::ios::binary);
    if (file) file << bootstrap::default_json();
    if (file) file.close();
    if (error || !file) {
        util::log().warn("no configuration file at {}, and one could not be written there; using defaults", shown);
        return;
    }

    // Info rather than debug: "my setting did nothing" is usually a file the
    // bot never found, and this says where it looks.
    util::log().info("no configuration file at {}; wrote one with the defaults, to edit and restart", shown);
}

} // namespace

auto bootstrap::from_json(std::string_view text) -> bootstrap {
    const json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) throw config_error("config file is not valid JSON");
    if (!parsed.is_object()) throw config_error("config file must contain a JSON object");

    reject_unknown_keys(parsed);

    bootstrap config;

    if (parsed.contains("log_level")) {
        const std::string name = require_string(parsed, "log_level");
        const auto level = util::log_level_from_string(name);
        if (!level) throw config_error(R"(config key "log_level" has ")" + name + R"(", expected: trace, debug, info, warn, error, off)");
        config.log_level = *level;
    }

    read_storage_keys(parsed, config);
    read_llm_keys(parsed, config);

    if (parsed.contains("track_nicknames")) config.track_nicknames = require_bool(parsed, "track_nicknames");

    if (parsed.contains("emoji_copy_min_uses")) {
        config.emoji_copy_min_uses = require_int(parsed, "emoji_copy_min_uses");
        if (config.emoji_copy_min_uses < 0) throw config_error(R"(config key "emoji_copy_min_uses" cannot be negative)");
    }

    read_music_keys(parsed, config);

    if (parsed.contains("trusted_guilds")) config.trusted_guilds = require_snowflakes(parsed, "trusted_guilds");
    if (parsed.contains("trusted_users")) config.trusted_users = require_snowflakes(parsed, "trusted_users");

    return config;
}

auto bootstrap::default_json() -> std::string {
    // Ordered, so the file reads in the order the keys are documented.
    const bootstrap defaults;
    nlohmann::ordered_json file;
    file["database_path"] = defaults.database_path.generic_string();
    file["backup_directory"] = defaults.backup_directory.generic_string();
    file["backups_to_keep"] = defaults.backups_to_keep;
    file["backup_interval_minutes"] = defaults.backup_interval.count();
    file["track_nicknames"] = defaults.track_nicknames;
    file["trusted_guilds"] = nlohmann::ordered_json::array();
    file["trusted_users"] = nlohmann::ordered_json::array();
    file["llm_provider"] = defaults.llm_provider;
    file["llm_model"] = defaults.llm_model;
    file["spend_cap_daily_usd"] = defaults.spend_cap_daily_usd;
    file["spend_cap_monthly_usd"] = defaults.spend_cap_monthly_usd;
    file["llm_tool_rounds"] = defaults.llm_tool_rounds;
    file["emoji_copy_min_uses"] = defaults.emoji_copy_min_uses;
    file["ytdlp_path"] = defaults.ytdlp_path.generic_string();
    file["ffmpeg_path"] = defaults.ffmpeg_path.generic_string();
    file["deno_path"] = defaults.deno_path.generic_string();
    file["pot_provider_path"] = defaults.pot_provider_path.generic_string();
    file["pot_provider_port"] = defaults.pot_provider_port;
    return file.dump(2) + "\n";
}

auto bootstrap::load(const std::filesystem::path& path) -> bootstrap {
    bootstrap config;

    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) {
        // A missing file is the ordinary case on a fresh install: every value
        // has a default, and the only thing the bot truly needs is the token
        // from the environment.
        write_default_config(path);
    } else {
        // Something is there, so it is somebody's configuration: one that
        // cannot be read stops startup rather than being replaced.
        const std::ifstream file(path);
        if (!file) throw config_error("could not read the configuration file at " + std::filesystem::absolute(path).generic_string());

        std::ostringstream contents;
        contents << file.rdbuf();
        config = from_json(contents.str());
        util::log().debug("read configuration from {}", std::filesystem::absolute(path).generic_string());
    }

    // Last word goes to the environment, so the level can be raised for one
    // run without editing a file the bot is about to read again.
    if (const auto wanted = log_level_from_environment()) config.log_level = *wanted;

    config.recompute_bot_id = recompute_bot_id_from_environment();

    return config;
}

auto log_level_from_environment() -> std::optional<util::log_level> {
    const auto wanted = util::env_var("LATIBOT_LOG_LEVEL");
    if (!wanted || wanted->empty()) return std::nullopt;

    const auto level = util::log_level_from_string(*wanted);
    if (!level) throw config_error("LATIBOT_LOG_LEVEL is \"" + *wanted + "\", expected: trace, debug, info, warn, error, off");
    return level;
}

auto recompute_bot_id_from_environment(bool debug_build) -> std::optional<dpp::snowflake> {
    constexpr const char* name = "LATIBOT_DEBUG_RECOMPUTE_BOT_ID";
    const auto wanted = util::env_var(name);
    if (!wanted || wanted->empty()) return std::nullopt;

    if (!debug_build) {
        util::log().warn("{} is set and ignored: it only applies to debug builds", name);
        return std::nullopt;
    }

    const auto id = util::parse_snowflake(*wanted);
    if (!id) throw config_error(std::string(name) + " is \"" + *wanted + "\", expected a Discord user ID");
    return id;
}

auto bootstrap::is_trusted(dpp::snowflake guild_id, dpp::snowflake user_id, bool administrator) const -> bool {
    if (std::ranges::find(trusted_users, user_id) != trusted_users.end()) return true;
    // Administrator is per server, so it only counts in a server we trust
    // (docs/features/Speech.md §2.2).
    return administrator && std::ranges::find(trusted_guilds, guild_id) != trusted_guilds.end();
}

auto secrets::from_environment() -> secrets {
    secrets loaded;

    const auto token = util::env_var("DISCORD_BOT_TOKEN");
    if (!token || token->empty()) {
        // Says where .env goes, since a release is only the executable, with
        // no .env.example beside it to show the way.
        throw config_error(
            std::format("DISCORD_BOT_TOKEN is not set. Set it in the environment, or put the line "
                        "DISCORD_BOT_TOKEN=<your token> in {}. Never put the token in config.json.",
                        std::filesystem::absolute(".env").generic_string()));
    }
    loaded.discord_token = *token;

    if (const auto key = util::env_var("ANTHROPIC_API_KEY"); key && !key->empty()) loaded.anthropic_key = *key;
    if (const auto key = util::env_var("OPENAI_API_KEY"); key && !key->empty()) loaded.openai_key = *key;
    if (const auto profile = util::env_var("LATIBOT_YTDLP_FIREFOX_PROFILE"); profile && !profile->empty()) {
        loaded.ytdlp_firefox_profile = std::filesystem::path(*profile);
    }
    if (const auto file = util::env_var("LATIBOT_YTDLP_COOKIES"); file && !file->empty()) {
        loaded.ytdlp_cookies = std::filesystem::path(*file);
    }

    return loaded;
}

} // namespace latibot::config
