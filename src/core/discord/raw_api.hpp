#pragma once

#include "core/ports/http_client.hpp"
#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/json_fwd.h>
#include <dpp/message.h>

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dpp {
class cluster;
}

namespace latibot::discord {

/// Turns a path into the endpoint DPP expects, prefixing the API version and
/// trimming a trailing slash. Exposed for testing.
[[nodiscard]] std::string build_endpoint(std::string_view path);

/// Calls Discord endpoints DPP does not wrap (plan v4 §5.5).
///
/// It is thin on purpose: `post_rest` already goes through DPP's REST queue,
/// so bot authentication, the correct user agent and Discord's rate limits
/// all come for free. What this adds is a coroutine interface and parsed JSON
/// or a typed error.
class raw_api {
public:
    explicit raw_api(dpp::cluster& cluster) : cluster_(&cluster) {}

    raw_api(const raw_api&) = delete;
    raw_api& operator=(const raw_api&) = delete;

    /// `path` may be a full endpoint ("/api/v10/channels/1/messages") or just
    /// the part after the version ("/channels/1/messages").
    ///
    /// A 4xx or 5xx reply becomes an `api_error` carrying Discord's message,
    /// since callers of a raw endpoint have no other way to see it.
    ///
    /// `audit_reason` sets the `X-Audit-Log-Reason` header. It goes through
    /// DPP's cluster-wide audit-reason slot, which another thread's REST call
    /// can consume in between, so use it only for rare administrative actions
    /// and never rely on it for attribution (plan v4 §8.1 stores that in the
    /// database instead).
    dpp::task<result<nlohmann::json>> request(ports::http_method method, std::string path,
                                              std::string body = {},
                                              std::string audit_reason = {});

    /// Multipart form upload, for endpoints that take `payload_json` plus
    /// files: voice messages, attachments (plan v4 §12.8).
    dpp::task<result<nlohmann::json>> multipart(ports::http_method method, std::string path,
                                                std::string payload_json,
                                                std::vector<dpp::message_file_data> files);

private:
    dpp::cluster* cluster_;

    /// Serialises our own use of the shared audit-reason slot. It cannot
    /// protect against DPP's other callers, hence the warning above.
    std::mutex audit_mutex_;
};

} // namespace latibot::discord
