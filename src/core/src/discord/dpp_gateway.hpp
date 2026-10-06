#pragma once

#include "core/ports/discord_gateway.hpp"

namespace dpp {
class cluster;
}

namespace latibot::discord {

/// `discord_gateway` backed by a real DPP cluster.
class dpp_gateway final : public ports::discord_gateway {
public:
    explicit dpp_gateway(dpp::cluster& cluster) : cluster_(&cluster) {}

    auto send_message(dpp::message message) -> dpp::task<ports::result<dpp::message>> override;
    auto edit_message(dpp::message message) -> dpp::task<ports::result<dpp::message>> override;
    auto delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<ports::result<void>> override;
    auto set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed)
        -> dpp::task<ports::result<void>> override;
    auto get_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<ports::result<dpp::message>> override;
    auto get_messages(dpp::snowflake channel_id, dpp::snowflake before, std::uint64_t limit)
        -> dpp::task<ports::result<std::vector<dpp::message>>> override;
    auto get_reaction_users(dpp::snowflake channel_id, dpp::snowflake message_id, std::string emoji, dpp::snowflake after,
                            std::uint64_t limit) -> dpp::task<ports::result<std::vector<dpp::snowflake>>> override;
    auto start_typing(dpp::snowflake channel_id) -> dpp::task<ports::result<void>> override;
    auto create_application_emoji(std::string name, std::string image, bool animated) -> dpp::task<ports::result<dpp::snowflake>> override;
    auto delete_application_emoji(dpp::snowflake emoji_id) -> dpp::task<ports::result<void>> override;
    [[nodiscard]] auto member_names(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<ports::member_names> override;
    [[nodiscard]] auto role_name(dpp::snowflake role_id) const -> std::optional<std::string> override;
    [[nodiscard]] auto channel_name(dpp::snowflake channel_id) const -> std::optional<std::string> override;

private:
    dpp::cluster* cluster_;
};

} // namespace latibot::discord
