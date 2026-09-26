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

private:
    dpp::cluster* cluster_;
};

} // namespace latibot::discord
