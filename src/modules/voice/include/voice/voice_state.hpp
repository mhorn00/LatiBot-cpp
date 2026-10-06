#pragma once

#include <dpp/snowflake.h>

#include <cstdint>

namespace dpp {
class cluster;
class discord_client;
class discord_voice_client;
} // namespace dpp

namespace latibot::discord {

// Where people and the bot are in voice, read from DPP's cache. Ids are 0
// for "not in a voice channel", as DPP reports an absent id.

/// Where something is played: speech, music or a voice lab preview.
enum class voice_route : std::uint8_t {
    /// Where the bot already is: a voice session's channel, or wherever
    /// `/join` put it.
    bot_channel,
    /// The bot is not in voice, so it joins whoever asked, as the Java bot
    /// did.
    join_caller,
    /// Neither of them is in voice.
    nowhere,
};

struct voice_plan {
    voice_route route = voice_route::nowhere;
    dpp::snowflake channel;
};

/// Where to play, given where the bot and whoever asked are.
[[nodiscard]] auto plan_voice(dpp::snowflake bot_channel, dpp::snowflake caller_channel) noexcept -> voice_plan;

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
