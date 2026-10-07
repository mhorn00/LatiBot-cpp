#pragma once

#include "advanced_triggers.hpp"
#include "ask.hpp"
#include "core/events/message_pipeline.hpp"
#include "guards.hpp"
#include "llm_config.hpp"
#include "models.hpp"
#include "responder.hpp"
#include "settings.hpp"
#include "spend.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string_view>

namespace latibot::config {
class guild_settings;
} // namespace latibot::config

namespace latibot::events {}

namespace latibot::ports {
class clock;
}

namespace latibot::capabilities {
class speech;
}

namespace latibot::llm {

/// What the stage decides: notes it posts itself, and the `ask_llm` the
/// pipeline hands to the responder (src/core/docs/Message_Pipeline.md §3).
using stage_result = events::own_stage_result<ask_llm>;

/// Whether a message addresses the bot: an @mention, a reply to one of its
/// messages, or a message that starts with its name
/// (src/modules/llm/docs/Language_Model.md §2.1).
[[nodiscard]] auto addresses_bot(const events::incoming_message& message, std::string_view bot_name) -> bool;

/// What the bot says, once per guild per day or month, when a spend cap
/// stops it answering (src/modules/llm/docs/Language_Model.md §2.2).
[[nodiscard]] auto spend_cap_reply(const spend_status& status) -> std::string;

struct stage_services {
    const config::guild_settings* settings = nullptr;
    /// config.json's llm section.
    const llm_config* section = nullptr;
    const blacklist_store* blacklist = nullptr;
    const advanced_trigger_store* triggers = nullptr;
    const usage_store* usage = nullptr;
    /// Whether a channel's replies are spoken, when DECtalk is built in;
    /// null otherwise, and then none is.
    const capabilities::speech* speech = nullptr;

    /// Whether a key is set for this provider.
    std::function<bool(provider_kind)> has_provider;

    std::function<bot_identity()> me;
};

/// The pipeline's last stage: decides whether the model answers a message,
/// and if so hands the shell an `ask_llm`
/// (src/core/docs/Message_Pipeline.md §2.2,
/// src/modules/llm/docs/Language_Model.md §2.1).
///
/// Everything that can refuse runs here, before any money is spent, in this
/// order: the guild's switch; being addressed, or else an advanced trigger
/// firing on a message nothing else answered; the blacklist; the spend caps;
/// the rate limits; and for a bot, the pacing
/// (src/modules/llm/docs/Language_Model.md §2.2, §2.7). Consumes what it answers,
/// and what it refuses when addressed, so nothing after it sees a message
/// meant for the model.
///
/// Safe to call from several threads at once.
class llm_stage {
public:
    /// `roll` is the advanced triggers' dice; see `advanced_trigger_matcher`.
    llm_stage(stage_services services, ports::clock& clock, std::function<double()> roll = {});

    auto operator()(const events::incoming_message& message) -> stage_result;

private:
    /// The guards after the switch and the address, in order: the blacklist,
    /// the spend caps, the rate limits, and for a bot the pacing. The wait
    /// before answering when all of them let it through, nothing when one
    /// did not.
    auto admit(const events::incoming_message& message, const llm_settings& settings, bool addressed, stage_result& result)
        -> std::optional<std::chrono::seconds>;

    /// Whether a spend cap is reached, adding the notice to `result` the
    /// first time a guild asks after it was.
    auto over_spend_cap(const events::incoming_message& message, bool addressed, stage_result& result) -> bool;

    stage_services services_;
    ports::clock* clock_;

    advanced_trigger_matcher matcher_;
    rate_limiter users_;
    rate_limiter channels_;
    bot_pacing pacing_;
    spend_notices notices_;
};

} // namespace latibot::llm
