#pragma once

#include "core/events/embed_watch.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/events/replacements.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/coro/task.h>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace latibot::ports {
class clock;
class discord_gateway;
} // namespace latibot::ports

namespace latibot::events {

/// Discord's limit on a message's text. A replacement that would pass it
/// loses links from the end until it fits, rather than failing outright.
inline constexpr std::size_t message_length_limit = 2000;

/// The URL replacement stage (plan v4 §5.4, §9).
///
/// Does nothing in a guild that has not turned replacement on. Does not
/// consume the message: "420" and a link in one message get the joke and the
/// preview both, which the Java bot's early returns got wrong.
class url_replacer {
public:
    explicit url_replacer(const url_rule_store& rules) : rules_(&rules) {}

    stage_result operator()(const incoming_message& message) const;

private:
    const url_rule_store* rules_;
};

/// Posts a replacement, turns the original's previews off, and starts
/// watching for ours (plan v4 §9.2, §9.3).
///
/// Posting comes first: if it fails, the original keeps its preview, which is
/// better than a message with none. The replacement is a plain message rather
/// than a reply, with notifications suppressed and nobody mentioned.
dpp::task<void> post_replacement(ports::discord_gateway& discord, replacement_store& replacements, embed_tracker& tracker,
                                 ports::clock& clock, replace_links request);

/// Does what the tracker decided. Failures are logged and otherwise
/// ignored: a preview that could not be switched is not worth more than that.
dpp::task<void> carry_out_embed_actions(ports::discord_gateway& discord, std::vector<embed_action> actions);

/// What pressing Retry does, decided.
struct retry_plan {
    /// For the tracker, once the first attempt is posted.
    watch_request request;

    /// The first attempt: the replacement back in place of the note, its
    /// previews on, the button gone.
    edit_replacement first;
};

/// Works out a Retry, or why there is nothing to retry.
///
/// Mirrors come from the rule as it is now, not as it was: a rule fixed
/// since the failure is exactly why somebody presses Retry. A link whose rule
/// has since been removed is dropped, and nothing is retried in a guild that
/// has since turned replacement off.
[[nodiscard]] std::variant<retry_plan, std::string> plan_retry(const replacement_store& replacements, const url_rule_store& rules,
                                                               dpp::snowflake message_id, dpp::snowflake guild_id);

} // namespace latibot::events
