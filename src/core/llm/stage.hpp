#pragma once

#include "core/events/message_pipeline.hpp"
#include "core/llm/advanced_triggers.hpp"
#include "core/llm/guards.hpp"
#include "core/llm/models.hpp"
#include "core/llm/responder.hpp"
#include "core/llm/settings.hpp"
#include "core/llm/spend.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string_view>

namespace latibot::config {
class guild_settings;
struct bootstrap;
} // namespace latibot::config

namespace latibot::events {
class voice_sessions;
}

namespace latibot::ports {
class clock;
}

namespace latibot::llm {

/// Whether a message addresses the bot: an @mention, a reply to one of its
/// messages, or a message that starts with its name
/// (docs/features/Language_Model.md §2.1).
[[nodiscard]] auto addresses_bot(const events::incoming_message& message, std::string_view bot_name) -> bool;

/// What the bot says, once per guild per day or month, when a spend cap
/// stops it answering (docs/features/Language_Model.md §2.2).
[[nodiscard]] auto spend_cap_reply(const spend_status& status) -> std::string;

struct stage_services {
    const config::guild_settings* settings = nullptr;
    const config::bootstrap* bootstrap = nullptr;
    const blacklist_store* blacklist = nullptr;
    const advanced_trigger_store* triggers = nullptr;
    const usage_store* usage = nullptr;
    const events::voice_sessions* sessions = nullptr;

    /// Whether a key is set for this provider.
    std::function<bool(provider_kind)> has_provider;

    std::function<bot_identity()> me;
};

/// The pipeline's last stage: decides whether the model answers a message,
/// and if so hands the shell an `ask_llm`
/// (docs/features/Message_Pipeline.md §2.2,
/// docs/features/Language_Model.md §2.1).
///
/// Everything that can refuse runs here, before any money is spent, in this
/// order: the guild's switch; being addressed, or else an advanced trigger
/// firing on a message nothing else answered; the blacklist; the spend caps;
/// the rate limits; and for a bot, the pacing
/// (docs/features/Language_Model.md §2.2, §2.7). Consumes what it answers,
/// and what it refuses when addressed, so nothing after it sees a message
/// meant for the model.
///
/// Safe to call from several threads at once.
class llm_stage {
public:
    /// `roll` is the advanced triggers' dice; see `advanced_trigger_matcher`.
    llm_stage(stage_services services, ports::clock& clock, std::function<double()> roll = {});

    auto operator()(const events::incoming_message& message) -> events::stage_result;

private:
    /// The guards after the switch and the address, in order: the blacklist,
    /// the spend caps, the rate limits, and for a bot the pacing. The wait
    /// before answering when all of them let it through, nothing when one
    /// did not.
    auto admit(const events::incoming_message& message, const llm_settings& settings, bool addressed, events::stage_result& result)
        -> std::optional<std::chrono::seconds>;

    /// Whether a spend cap is reached, adding the notice to `result` the
    /// first time a guild asks after it was.
    auto over_spend_cap(const events::incoming_message& message, bool addressed, events::stage_result& result) -> bool;

    stage_services services_;
    ports::clock* clock_;

    advanced_trigger_matcher matcher_;
    rate_limiter users_;
    rate_limiter channels_;
    bot_pacing pacing_;
    spend_notices notices_;
};

} // namespace latibot::llm
