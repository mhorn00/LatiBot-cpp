#include "core/commands/unregister.hpp"

#include "core/util/log.hpp"

#include <format>
#include <functional>
#include <optional>
#include <utility>

namespace latibot::commands {
namespace {

/// "/a, /b, /c", for the log.
auto slash_names(const std::vector<std::string>& names) -> std::string {
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) joined += ", ";
        joined += "/" + name;
    }
    return joined;
}

/// How one set of commands went.
struct cleared {
    std::size_t deleted = 0;
    std::optional<std::string> failure;
};

/// Deletes one set of commands, if it has any. `where` finishes "the
/// commands ...", as "registered globally" or "in server 123".
auto clear(std::string where, ports::result<std::vector<std::string>> listed, std::function<dpp::task<ports::result<void>>()> remove)
    -> dpp::task<cleared> {
    if (!listed) co_return cleared{.failure = std::format("listing the commands {}: {}", where, listed.error().message)};

    const std::vector<std::string>& names = listed.value();
    if (names.empty()) co_return cleared{};

    if (const auto deleted = co_await remove(); !deleted) {
        co_return cleared{.failure = std::format("deleting the commands {}: {}", where, deleted.error().message)};
    }
    util::log().info("deleted {} command(s) {}: {}", names.size(), where, slash_names(names));
    co_return cleared{.deleted = names.size(), .failure = std::nullopt};
}

} // namespace

auto unregister_all(ports::command_host& host) -> dpp::task<unregister_report> {
    unregister_report report;

    const cleared global =
        co_await clear("registered globally", co_await host.global_commands(), [&host] { return host.delete_global_commands(); });
    report.global_deleted = global.deleted;
    if (global.failure) report.failures.push_back(*global.failure);

    const auto guilds = co_await host.guilds();
    if (!guilds) {
        report.failures.push_back(std::format("listing the bot's servers: {}", guilds.error().message));
        co_return report;
    }

    for (const dpp::snowflake guild : guilds.value()) {
        const cleared own = co_await clear(std::format("in server {}", guild), co_await host.guild_commands(guild),
                                           [&host, guild] { return host.delete_guild_commands(guild); });
        ++report.guilds_checked;
        report.guild_deleted += own.deleted;
        if (own.deleted > 0) ++report.guilds_with_commands;
        if (own.failure) report.failures.push_back(*own.failure);
    }
    co_return report;
}

} // namespace latibot::commands
