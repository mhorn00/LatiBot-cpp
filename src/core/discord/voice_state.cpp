#include "core/discord/voice_state.hpp"

#include <dpp/cache.h>
#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/discordvoiceclient.h>
#include <dpp/guild.h>
#include <dpp/user.h>

namespace latibot::discord {

auto voice_channel_of(dpp::snowflake guild_id, dpp::snowflake user_id) -> dpp::snowflake {
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) return {};

    const auto found = guild->voice_members.find(user_id);
    return found == guild->voice_members.end() ? dpp::snowflake{} : found->second.channel_id;
}

auto bot_voice_channel(dpp::discord_client* shard, dpp::snowflake guild_id) -> dpp::snowflake {
    if (shard == nullptr) return {};

    const dpp::voiceconn* connection = shard->get_voice(guild_id);
    return connection == nullptr ? dpp::snowflake{} : connection->channel_id;
}

auto shard_for(dpp::cluster& cluster, dpp::snowflake guild_id) -> dpp::discord_client* {
    const auto& shards = cluster.get_shards();
    if (shards.empty()) return nullptr;
    if (shards.size() == 1) return shards.begin()->second.get();

    // Discord's own rule for which shard a guild's events arrive on.
    const auto id = static_cast<std::uint32_t>((static_cast<std::uint64_t>(guild_id) >> 22U) % shards.size());
    const auto found = shards.find(id);
    return found == shards.end() ? nullptr : found->second.get();
}

auto bot_voice_channel(dpp::cluster& cluster, dpp::snowflake guild_id) -> dpp::snowflake {
    return bot_voice_channel(shard_for(cluster, guild_id), guild_id);
}

auto ready_voice_client(dpp::cluster& cluster, dpp::snowflake guild_id) -> dpp::discord_voice_client* {
    dpp::discord_client* shard = shard_for(cluster, guild_id);
    if (shard == nullptr) return nullptr;

    const dpp::voiceconn* connection = shard->get_voice(guild_id);
    if (connection == nullptr || !connection->voiceclient || !connection->voiceclient->is_ready()) return nullptr;
    return connection->voiceclient.get();
}

auto humans_in(dpp::snowflake guild_id, dpp::snowflake channel_id, dpp::snowflake self) -> int {
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr || channel_id.empty()) return 0;

    int humans = 0;
    for (const auto& [user_id, state] : guild->voice_members) {
        if (state.channel_id != channel_id || user_id == self) continue;
        const dpp::user* user = dpp::find_user(user_id);
        if (user == nullptr || !user->is_bot()) ++humans;
    }
    return humans;
}

} // namespace latibot::discord
