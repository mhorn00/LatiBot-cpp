#include "core/config/bootstrap.hpp"

#include "core/util/env.hpp"
#include "core/util/text.hpp"

#include <dpp/json.h>

#include <algorithm>
#include <array>
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

auto require_int(const json& object, std::string_view key) -> int {
    const auto& value = object.at(std::string(key));
    if (!value.is_number_integer()) wrong_type(key, "a whole number");
    return value.get<int>();
}

/// Snowflakes are 64-bit and JSON numbers are doubles, which silently lose
/// precision past 2^53, so IDs are written as strings everywhere they cross a
/// JSON boundary (src/core/docs/Operations.md §4).
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

/// The core's own keys, at the top of config.json. A key added here belongs
/// in `bootstrap::default_json` too, at its default.
constexpr std::array<std::string_view, 7> core_keys{
    "log_level", "database_path", "backup_directory", "backups_to_keep", "backup_interval_minutes", "trusted_guilds", "trusted_users",
};

/// A key from before config.json had sections, and where it lives now
/// (docs/modules/Module_Plan_Final.md §8.1). Still read, with a warning.
/// Remove after: you say so.
struct moved_key {
    std::string_view old_name;
    std::string_view section;
    std::string_view name;
};

constexpr std::array<moved_key, 12> moved_keys{{
    {.old_name = "track_nicknames", .section = "nicknames", .name = "track_changes"},
    {.old_name = "emoji_copy_min_uses", .section = "linkstats", .name = "emoji_copy_min_uses"},
    {.old_name = "llm_provider", .section = "llm", .name = "provider"},
    {.old_name = "llm_model", .section = "llm", .name = "model"},
    {.old_name = "llm_tool_rounds", .section = "llm", .name = "tool_rounds"},
    {.old_name = "spend_cap_daily_usd", .section = "llm", .name = "spend_cap_daily_usd"},
    {.old_name = "spend_cap_monthly_usd", .section = "llm", .name = "spend_cap_monthly_usd"},
    {.old_name = "ytdlp_path", .section = "music", .name = "ytdlp_path"},
    {.old_name = "ffmpeg_path", .section = "music", .name = "ffmpeg_path"},
    {.old_name = "deno_path", .section = "music", .name = "deno_path"},
    {.old_name = "pot_provider_path", .section = "music", .name = "pot_provider_path"},
    {.old_name = "pot_provider_port", .section = "music", .name = "pot_provider_port"},
}};

/// Every section in config.json, by name, with any old flat key moved into
/// its own: each a module's, which the module reads with its own table. A
/// top-level key that is neither the core's, a section, nor an old key is a
/// typo, and stops startup.
auto gather_sections(const json& parsed) -> json {
    json gathered = json::object();
    for (const auto& [key, value] : parsed.items()) {
        if (std::ranges::contains(core_keys, key)) continue;
        if (std::ranges::contains(moved_keys, std::string_view{key}, &moved_key::old_name)) continue;
        if (value.is_object()) {
            gathered[key] = value;
            continue;
        }
        throw config_error("unknown config key \"" + key + "\"");
    }

    for (const moved_key& moved : moved_keys) {
        const auto found = parsed.find(std::string(moved.old_name));
        if (found == parsed.end()) continue;
        json& into = gathered[std::string(moved.section)];
        if (into.is_null()) into = json::object();
        if (!into.is_object()) continue; // the section's own read says what is wrong
        if (into.contains(std::string(moved.name))) {
            throw config_error(
                std::format(R"(config.json sets both "{}" and "{}.{}"; keep the second)", moved.old_name, moved.section, moved.name));
        }
        into[std::string(moved.name)] = *found;
        util::log().warn(R"(config key "{}" is now "{}" inside "{}"; it still works, but move it there)", moved.old_name, moved.name,
                         moved.section);
    }
    return gathered;
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

/// Writes the defaults where the configuration was looked for. Never fatal:
/// the defaults are what the bot runs on either way, and a folder it cannot
/// write to is no reason not to start.
auto write_default_config(const std::filesystem::path& path, const nlohmann::ordered_json& module_sections) -> void {
    const std::string shown = std::filesystem::absolute(path).generic_string();

    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file;
    if (!error) file.open(path, std::ios::binary);
    if (file) file << bootstrap::default_json(module_sections);
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

    bootstrap config;
    json gathered = gather_sections(parsed);

    if (parsed.contains("log_level")) {
        const std::string name = require_string(parsed, "log_level");
        const auto level = util::log_level_from_string(name);
        if (!level) throw config_error(R"(config key "log_level" has ")" + name + R"(", expected: trace, debug, info, warn, error, off)");
        config.log_level = *level;
    }

    read_storage_keys(parsed, config);

    // Every section is a module's.
    config.sections = std::move(gathered);

    if (parsed.contains("trusted_guilds")) config.trusted_guilds = require_snowflakes(parsed, "trusted_guilds");
    if (parsed.contains("trusted_users")) config.trusted_users = require_snowflakes(parsed, "trusted_users");

    return config;
}

auto bootstrap::default_json(const nlohmann::ordered_json& module_sections) -> std::string {
    // Ordered, so the file reads in the order the keys are documented.
    const bootstrap defaults;
    nlohmann::ordered_json file;
    file["database_path"] = defaults.database_path.generic_string();
    file["backup_directory"] = defaults.backup_directory.generic_string();
    file["backups_to_keep"] = defaults.backups_to_keep;
    file["backup_interval_minutes"] = defaults.backup_interval.count();
    file["trusted_guilds"] = nlohmann::ordered_json::array();
    file["trusted_users"] = nlohmann::ordered_json::array();
    for (const auto& [name, section] : module_sections.items()) {
        file[name] = section;
    }
    return file.dump(2) + "\n";
}

auto bootstrap::load(const std::filesystem::path& path, const nlohmann::ordered_json& module_sections) -> bootstrap {
    bootstrap config;

    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) {
        // A missing file is the ordinary case on a fresh install: every value
        // has a default, and the only thing the bot truly needs is the token
        // from the environment.
        write_default_config(path, module_sections);
    } else {
        // Something is there, so it is somebody's configuration: one that
        // cannot be read stops startup rather than being replaced.
        // A folder opens on Linux, and only fails once read, so it is
        // refused by name.
        const std::ifstream file(path);
        if (!file || std::filesystem::is_directory(path, error)) {
            throw config_error("could not read the configuration file at " + std::filesystem::absolute(path).generic_string());
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        config = from_json(contents.str());
        util::log().debug("read configuration from {}", std::filesystem::absolute(path).generic_string());
    }

    // Last word goes to the environment, so the level can be raised for one
    // run without editing a file the bot is about to read again.
    if (const auto wanted = log_level_from_environment()) config.log_level = *wanted;

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
    // (src/modules/dectalk/docs/Speech.md §2.2).
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

    return loaded;
}

} // namespace latibot::config
