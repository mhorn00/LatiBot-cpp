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

    dpp::task<result<dpp::message>> send_message(dpp::message message) override;
    dpp::task<result<dpp::message>> edit_message(dpp::message message) override;
    dpp::task<result<void>> delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) override;
    dpp::task<result<void>> set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed) override;
    dpp::task<result<dpp::message>> get_message(dpp::snowflake channel_id, dpp::snowflake message_id) override;
    dpp::task<result<std::vector<dpp::message>>> get_messages(dpp::snowflake channel_id, dpp::snowflake before,
                                                              std::uint64_t limit) override;
    dpp::task<result<std::vector<dpp::snowflake>>> get_reaction_users(dpp::snowflake channel_id, dpp::snowflake message_id,
                                                                      std::string emoji, dpp::snowflake after,
                                                                      std::uint64_t limit) override;

private:
    dpp::cluster* cluster_;
};

} // namespace latibot::discord
