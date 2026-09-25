#include "core/config/bootstrap.hpp"

#include "core/util/env.hpp"
#include "core/util/text.hpp"

#include <dpp/json.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace latibot::config {
namespace {

using json = nlohmann::json;

[[noreturn]] void wrong_type(std::string_view key, std::string_view expected) {
    throw config_error("config key \"" + std::string(key) + "\" must be " + std::string(expected));
}

std::string require_string(const json& object, std::string_view key) {
    const auto& value = object.at(std::string(key));
    if (!value.is_string()) wrong_type(key, "a string");
    return value.get<std::string>();
}

double require_number(const json& object, std::string_view key) {
    const auto& value = object.at(std::string(key));
    if (!value.is_number()) wrong_type(key, "a number");
    return value.get<double>();
}

int require_int(const json& object, std::string_view key) {
    const auto& value = object.at(std::string(key));
    if (!value.is_number_integer()) wrong_type(key, "a whole number");
    return value.get<int>();
}

/// Snowflakes are 64-bit and JSON numbers are doubles, which silently lose
/// precision past 2^53, so IDs are written as strings everywhere they cross a
/// JSON boundary (plan §5.2).
std::vector<dpp::snowflake> require_snowflakes(const json& object, std::string_view key) {
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
        try {
            ids.emplace_back(std::stoull(text));
        } catch (const std::exception&) {
            throw config_error("config key \"" + std::string(key) + "\" has \"" + text + "\", which is not a Discord ID");
        }
    }
    return ids;
}

bool require_bool(const json& object, std::string_view key) {
    const auto& value = object.at(std::string(key));
    if (!value.is_boolean()) wrong_type(key, "true or false");
    return value.get<bool>();
}

/// Rejects anything we do not recognise, so a typo in a hand-edited file is
/// reported instead of silently doing nothing.
void reject_unknown_keys(const json& parsed) {
    static constexpr std::array<std::string_view, 13> known_keys{
        "log_level",       "database_path",  "backup_directory", "backups_to_keep",     "backup_interval_minutes",
        "track_nicknames", "llm_provider",   "llm_model",        "spend_cap_daily_usd", "spend_cap_monthly_usd",
        "llm_tool_rounds", "trusted_guilds", "trusted_users",
    };

    for (const auto& [key, unused] : parsed.items()) {
        if (std::ranges::find(known_keys, key) == known_keys.end()) {
            throw config_error("unknown config key \"" + key + "\"");
        }
    }
}

/// Where the database lives and how often it is copied.
void read_storage_keys(const json& parsed, bootstrap& config) {
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

/// Which model answers, and what it is allowed to cost (plan §14).
void read_llm_keys(const json& parsed, bootstrap& config) {
    if (parsed.contains("llm_provider")) config.llm_provider = require_string(parsed, "llm_provider");
    if (parsed.contains("llm_model")) config.llm_model = require_string(parsed, "llm_model");
    if (parsed.contains("spend_cap_daily_usd")) config.spend_cap_daily_usd = require_number(parsed, "spend_cap_daily_usd");
    if (parsed.contains("spend_cap_monthly_usd")) config.spend_cap_monthly_usd = require_number(parsed, "spend_cap_monthly_usd");

    if (parsed.contains("llm_tool_rounds")) {
        config.llm_tool_rounds = require_int(parsed, "llm_tool_rounds");
        if (config.llm_tool_rounds < 1) throw config_error("config key \"llm_tool_rounds\" must be at least 1");
    }
}

} // namespace

bootstrap bootstrap::from_json(std::string_view text) {
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

    if (parsed.contains("trusted_guilds")) config.trusted_guilds = require_snowflakes(parsed, "trusted_guilds");
    if (parsed.contains("trusted_users")) config.trusted_users = require_snowflakes(parsed, "trusted_users");

    return config;
}

bootstrap bootstrap::load(const std::filesystem::path& path) {
    bootstrap config;

    const std::ifstream file(path);
    if (file) {
        std::ostringstream contents;
        contents << file.rdbuf();
        config = from_json(contents.str());
        util::log().debug("read configuration from {}", std::filesystem::absolute(path).generic_string());
    } else {
        // A missing config file is fine: every value has a default, and the
        // only thing the bot truly needs is the token from the environment.
        // Worth a line all the same, since "my setting did nothing" is usually
        // a file the bot never found.
        util::log().debug("no configuration file at {}; using defaults", std::filesystem::absolute(path).generic_string());
    }

    // Last word goes to the environment, so the level can be raised for one
    // run without editing a file the bot is about to read again.
    if (const auto wanted = log_level_from_environment()) {
        config.log_level = *wanted;
    }

    config.recompute_bot_id = recompute_bot_id_from_environment();

    return config;
}

std::optional<util::log_level> log_level_from_environment() {
    const auto wanted = util::env_var("LATIBOT_LOG_LEVEL");
    if (!wanted || wanted->empty()) {
        return std::nullopt;
    }

    const auto level = util::log_level_from_string(*wanted);
    if (!level) {
        throw config_error("LATIBOT_LOG_LEVEL is \"" + *wanted + "\", expected: trace, debug, info, warn, error, off");
    }
    return level;
}

std::optional<dpp::snowflake> parse_snowflake(std::string_view text) {
    text = util::trim(text);
    std::uint64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || stop != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return dpp::snowflake(value);
}

std::optional<dpp::snowflake> recompute_bot_id_from_environment(bool debug_build) {
    constexpr const char* name = "LATIBOT_DEBUG_RECOMPUTE_BOT_ID";
    const auto wanted = util::env_var(name);
    if (!wanted || wanted->empty()) {
        return std::nullopt;
    }

    if (!debug_build) {
        util::log().warn("{} is set and ignored: it only applies to debug builds", name);
        return std::nullopt;
    }

    const auto id = parse_snowflake(*wanted);
    if (!id) {
        throw config_error(std::string(name) + " is \"" + *wanted + "\", expected a Discord user ID");
    }
    return id;
}

bool bootstrap::is_trusted(dpp::snowflake guild_id, dpp::snowflake user_id, bool administrator) const {
    if (std::ranges::find(trusted_users, user_id) != trusted_users.end()) {
        return true;
    }
    // Administrator is per server, so it only counts in a server we trust
    // (plan §2.4).
    return administrator && std::ranges::find(trusted_guilds, guild_id) != trusted_guilds.end();
}

secrets secrets::from_environment() {
    secrets loaded;

    const auto token = util::env_var("DISCORD_BOT_TOKEN");
    if (!token || token->empty()) {
        throw config_error(
            "DISCORD_BOT_TOKEN is not set. Secrets come from the environment "
            "only; never put the token in config.json.");
    }
    loaded.discord_token = *token;

    if (const auto key = util::env_var("ANTHROPIC_API_KEY"); key && !key->empty()) {
        loaded.anthropic_key = *key;
    }
    if (const auto key = util::env_var("OPENAI_API_KEY"); key && !key->empty()) {
        loaded.openai_key = *key;
    }

    return loaded;
}

} // namespace latibot::config
