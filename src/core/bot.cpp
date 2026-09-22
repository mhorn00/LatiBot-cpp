#include "core/bot.hpp"

#include "core/commands/basic.hpp"
#include "core/commands/preflight.hpp"
#include "core/db/migrations.hpp"
#include "core/util/log.hpp"
#include "core/version.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace latibot {
namespace {

util::log_level from_dpp(dpp::loglevel level) {
    switch (level) {
    case dpp::ll_trace:
        return util::log_level::trace;
    case dpp::ll_debug:
        return util::log_level::debug;
    case dpp::ll_info:
        return util::log_level::info;
    case dpp::ll_warning:
        return util::log_level::warn;
    default:
        // Errors and criticals both matter enough to surface the same way.
        return util::log_level::error;
    }
}

/// Makes sure the folder holding the database exists, so a first run on a
/// clean machine works without setup. Returns by value: handing back a
/// reference to the parameter would dangle if a caller ever passed a
/// temporary.
std::filesystem::path prepare(const std::filesystem::path& database_path) {
    if (database_path.has_parent_path() && !database_path.parent_path().empty()) {
        std::filesystem::create_directories(database_path.parent_path());
    }
    return database_path;
}

} // namespace

bot::bot(config::bootstrap settings, const config::secrets& credentials)
    : settings_(std::move(settings)),
      database_(prepare(settings_.database_path)),
      guild_settings_(database_),
      cluster_(credentials.discord_token),
      gateway_(cluster_),
      http_(cluster_),
      raw_(cluster_) {
    util::log().set_level(settings_.log_level);

    const int version = db::migrate(database_);
    util::log().info("LatiBot {} starting; database {} at schema version {}", version_string(),
                     settings_.database_path.generic_string(), version);

    register_commands();
    register_events();
}

void bot::register_commands() {
    commands::add_basic_commands(commands_, cluster_, clock_, [this] { cluster_.shutdown(); });
}

void bot::register_events() {
    // DPP's own logging goes through our logger, so there is one format and
    // one level to configure.
    cluster_.on_log(
        [](const dpp::log_t& event) { util::log().write(from_dpp(event.severity), "[dpp] " + event.message); });

    cluster_.on_slashcommand([this](const dpp::slashcommand_t& event) -> dpp::task<void> {
        co_await commands_.dispatch(event.command.get_command_name(), event);
    });

    cluster_.on_ready([this](const dpp::ready_t&) {
        if (!dpp::run_once<struct register_bot_commands>()) {
            return;
        }

        if (commands_.size() == 0) {
            // A bulk create with an empty list deletes every registered
            // command, which is not what "no commands built yet" should mean.
            util::log().warn("no commands registered; skipping command registration");
            return;
        }

        cluster_.global_bulk_command_create(commands_.build_all(cluster_.me.id));
        util::log().info("registered {} commands", commands_.size());
    });

    // Guilds arrive as guild_create after the gateway connects, including the
    // ones the bot was already in, so this covers both cases plan v4 §7 asks
    // for without a separate sweep on ready.
    cluster_.on_guild_create([this](const dpp::guild_create_t& event) { check_permissions(event.created); });
}

void bot::check_permissions(const dpp::guild& guild) const {
    const auto self = guild.members.find(cluster_.me.id);
    if (self == guild.members.end()) {
        // Without GUILD_MEMBERS the bot's own member object may be absent.
        // Say so once rather than reporting every permission as missing.
        util::log().debug("no member record for the bot in {}; skipping the permission check", guild.name);
        return;
    }

    std::vector<commands::requirement> required;
    const auto passive = commands::passive_requirements();
    required.assign(passive.begin(), passive.end());
    required.push_back({.permissions = commands_.required_bot_permissions(), .purpose = "the registered commands"});

    const std::uint64_t granted = guild.base_permissions(self->second);
    for (const commands::gap& missing : commands::unmet(required, granted)) {
        util::log().warn("{} ({}): missing {} for {}", guild.name, guild.id.str(),
                         commands::describe_permissions(missing.permissions), missing.purpose);
    }
}

void bot::run() {
    cluster_.start(dpp::st_wait);
}

} // namespace latibot
