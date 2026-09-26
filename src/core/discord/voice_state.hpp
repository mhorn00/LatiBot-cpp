#pragma once

#include <dpp/snowflake.h>

namespace dpp {
class cluster;
class discord_client;
class discord_voice_client;
} // namespace dpp

namespace latibot::discord {

// Where people and the bot are in voice, read from DPP's cache. Ids are 0
// for "not in a voice channel", as DPP reports an absent id.

/// The voice channel a member is in.
[[nodiscard]] auto voice_channel_of(dpp::snowflake guild_id, dpp::snowflake user_id) -> dpp::snowflake;

/// The voice channel the bot is connected to in this guild, as `shard` knows
/// it.
[[nodiscard]] auto bot_voice_channel(dpp::discord_client* shard, dpp::snowflake guild_id) -> dpp::snowflake;

/// The same, asking whichever shard holds the guild.
[[nodiscard]] auto bot_voice_channel(dpp::cluster& cluster, dpp::snowflake guild_id) -> dpp::snowflake;

/// The shard a guild's events arrive on, or nullptr.
[[nodiscard]] auto shard_for(dpp::cluster& cluster, dpp::snowflake guild_id) -> dpp::discord_client*;

/// The guild's voice client, or nullptr when there is none or it is not
/// ready for audio yet.
[[nodiscard]] auto ready_voice_client(dpp::cluster& cluster, dpp::snowflake guild_id) -> dpp::discord_voice_client*;

/// People in a voice channel other than `self` and other bots. Someone the
/// cache cannot say is a bot counts as a person, so the bot stays rather than
/// leaving someone mid-sentence.
[[nodiscard]] auto humans_in(dpp::snowflake guild_id, dpp::snowflake channel_id, dpp::snowflake self) -> int;

} // namespace latibot::discord
