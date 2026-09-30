#include "core/discord/unregister_commands.hpp"

#include "core/commands/unregister.hpp"
#include "core/discord/dpp_log.hpp"
#include "core/ports/command_host.hpp"
#include "core/util/log.hpp"

#include <dpp/dpp.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>

namespace latibot::discord {
namespace {

/// How long signing in may take before the token is assumed to be wrong.
/// DPP reports a refused token only as a log line, so this is how the run
/// ends rather than waiting forever.
constexpr std::chrono::seconds sign_in_timeout{30};

auto to_error(const dpp::confirmation_callback_t& confirmation) -> ports::api_error {
    return ports::api_error{.http_status = confirmation.http_info.status, .message = confirmation.get_error().message};
}

/// The commands' names, sorted so the log reads the same every run.
auto names_of(const dpp::slashcommand_map& commands) -> std::vector<std::string> {
    std::vector<std::string> names;
    names.reserve(commands.size());
    for (const auto& [id, command] : commands) {
        names.push_back(command.name);
    }
    std::ranges::sort(names);
    return names;
}

/// `ports::command_host` over a DPP cluster. `global_*` use `cluster.me`,
/// which is filled in by the time `on_ready` fires.
class dpp_command_host final : public ports::command_host {
public:
    explicit dpp_command_host(dpp::cluster& cluster) : cluster_(&cluster) {}

    auto global_commands() -> dpp::task<ports::result<std::vector<std::string>>> override {
        const auto confirmation = co_await cluster_->co_global_commands_get();
        if (confirmation.is_error()) co_return to_error(confirmation);
        co_return names_of(std::get<dpp::slashcommand_map>(confirmation.value));
    }

    auto delete_global_commands() -> dpp::task<ports::result<void>> override {
        const auto confirmation = co_await cluster_->co_global_bulk_command_delete();
        if (confirmation.is_error()) co_return to_error(confirmation);
        co_return ports::result<void>{};
    }

    auto guilds() -> dpp::task<ports::result<std::vector<dpp::snowflake>>> override {
        const auto confirmation = co_await cluster_->co_current_user_get_guilds();
        if (confirmation.is_error()) co_return to_error(confirmation);

        std::vector<dpp::snowflake> ids;
        for (const auto& [id, guild] : std::get<dpp::guild_map>(confirmation.value)) {
            ids.push_back(id);
        }
        std::ranges::sort(ids);
        co_return ids;
    }

    auto guild_commands(dpp::snowflake guild_id) -> dpp::task<ports::result<std::vector<std::string>>> override {
        const auto confirmation = co_await cluster_->co_guild_commands_get(guild_id);
        if (confirmation.is_error()) co_return to_error(confirmation);
        co_return names_of(std::get<dpp::slashcommand_map>(confirmation.value));
    }

    auto delete_guild_commands(dpp::snowflake guild_id) -> dpp::task<ports::result<void>> override {
        const auto confirmation = co_await cluster_->co_guild_bulk_command_delete(guild_id);
        if (confirmation.is_error()) co_return to_error(confirmation);
        co_return ports::result<void>{};
    }

private:
    dpp::cluster* cluster_;
};

auto log_outcome(const commands::unregister_report& report) -> void {
    for (const std::string& failure : report.failures) {
        util::log().error("{}", failure);
    }
    if (!report.ok()) {
        util::log().error("unregistering finished with {} failure(s); run it again to retry what is left", report.failures.size());
        return;
    }
    if (report.global_deleted == 0 && report.guild_deleted == 0) {
        util::log().info("nothing to delete: no commands were registered globally or in any of {} server(s)", report.guilds_checked);
        return;
    }
    util::log().info(
        "done: deleted {} global command(s), and {} in {} of {} server(s). A Discord client still showing them drops "
        "them on reload (Ctrl+R)",
        report.global_deleted, report.guild_deleted, report.guilds_with_commands, report.guilds_checked);
}

} // namespace

auto unregister_commands(const std::string& token) -> bool {
    // Before the cluster, so they outlive anything it might still call.
    std::promise<void> signed_in;
    const std::future<void> ready = signed_in.get_future();
    std::once_flag once;

    // NO_SHARDS: the REST API only. DPP signs in by asking who the token
    // belongs to, fills in `me`, and fires on_ready itself.
    dpp::cluster cluster(token, 0, dpp::NO_SHARDS);
    cluster.on_log(
        [](const dpp::log_t& event) { util::log().log(log_level_of(event.severity), "{} {}", util::log_source{"dpp"}, event.message); });
    cluster.on_ready([&](const dpp::ready_t&) { std::call_once(once, [&] { signed_in.set_value(); }); });

    util::log().info("signing in to unregister the bot's commands");
    cluster.start(dpp::st_return);

    if (ready.wait_for(sign_in_timeout) != std::future_status::ready) {
        util::log().error("Discord did not answer within {} s; check DISCORD_BOT_TOKEN, and the lines from dpp above",
                          sign_in_timeout.count());
        return false;
    }

    // Worth reading before anything else: the token decides which bot this
    // is, and a real bot's commands are as easy to delete as a test one's.
    util::log().info("signed in as {} ({}); deleting its commands", cluster.me.username, cluster.me.id);

    dpp_command_host host(cluster);
    const commands::unregister_report report = commands::unregister_all(host).sync_wait();
    log_outcome(report);

    cluster.shutdown();
    return report.ok();
}

} // namespace latibot::discord
