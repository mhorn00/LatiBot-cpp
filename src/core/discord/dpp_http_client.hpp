#pragma once

#include "core/ports/http_client.hpp"

namespace dpp {
class cluster;
}

namespace latibot::discord {

/// `http_client` backed by DPP's own HTTP queue.
///
/// Requests go through the cluster's raw REST queue, separate from the
/// Discord API queue, so a slow LLM call does not hold up Discord traffic.
/// The timeout is the cluster-wide `request_timeout` (60 s by default),
/// which is plenty for a capped completion (plan §14.7).
class dpp_http_client final : public ports::http_client {
public:
    explicit dpp_http_client(dpp::cluster& cluster) : cluster_(&cluster) {}

    auto send(ports::http_request request) -> dpp::task<ports::result<ports::http_response>> override;

private:
    dpp::cluster* cluster_;
};

} // namespace latibot::discord
