#include "core/bot.hpp"

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

    register_events();
}

void bot::register_events() {
    // DPP's own logging goes through our logger, so there is one format and
    // one level to configure.
    cluster_.on_log([](const dpp::log_t& event) {
        util::log().write(from_dpp(event.severity), "[dpp] " + event.message);
    });

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
}

void bot::run() {
    cluster_.start(dpp::st_wait);
}

} // namespace latibot
