#pragma once

#include "core/ports/command_host.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace latibot::testing {

/// Discord's command registry as plain containers: deleting empties them.
class mock_command_host final : public ports::command_host {
public:
    std::vector<std::string> global;
    std::map<dpp::snowflake, std::vector<std::string>> per_guild;

    /// When set, that call fails with this message.
    std::optional<std::string> fail_global_list;
    std::optional<std::string> fail_global_delete;
    std::optional<std::string> fail_guild_list;
    std::map<dpp::snowflake, std::string> fail_guild_delete;

    /// How many bulk deletes were sent, which a run with nothing to delete
    /// should leave at zero.
    int deletes = 0;

    auto global_commands() -> dpp::task<ports::result<std::vector<std::string>>> override {
        if (fail_global_list) co_return failure(*fail_global_list);
        co_return global;
    }

    auto delete_global_commands() -> dpp::task<ports::result<void>> override {
        ++deletes;
        if (fail_global_delete) co_return failure(*fail_global_delete);
        global.clear();
        co_return ports::result<void>{};
    }

    auto guilds() -> dpp::task<ports::result<std::vector<dpp::snowflake>>> override {
        if (fail_guild_list) co_return failure(*fail_guild_list);
        std::vector<dpp::snowflake> ids;
        ids.reserve(per_guild.size());
        for (const auto& [id, commands] : per_guild) {
            ids.push_back(id);
        }
        co_return ids;
    }

    auto guild_commands(dpp::snowflake guild_id) -> dpp::task<ports::result<std::vector<std::string>>> override {
        co_return per_guild[guild_id];
    }

    auto delete_guild_commands(dpp::snowflake guild_id) -> dpp::task<ports::result<void>> override {
        ++deletes;
        if (const auto found = fail_guild_delete.find(guild_id); found != fail_guild_delete.end()) co_return failure(found->second);
        per_guild[guild_id].clear();
        co_return ports::result<void>{};
    }

private:
    static auto failure(std::string message) -> ports::api_error { return {.http_status = 500, .message = std::move(message)}; }
};

} // namespace latibot::testing
