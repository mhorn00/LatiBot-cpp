#pragma once

#include "core/events/message_pipeline.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace latibot::config {
class guild_settings;
}

namespace latibot::events {

/// Per-guild setting holding the phrase, and what it is when unset.
inline constexpr std::string_view goodbye_phrase_key = "goodbye_phrase";
inline constexpr std::string_view default_goodbye_phrase = "say goodbye latibot";

/// What the bot says on its way out.
inline constexpr std::string_view goodbye_reply = "ok bye bye!";

/// Long enough for the reply to reach Discord before the process stops.
inline constexpr std::chrono::milliseconds goodbye_delay{1500};

/// Whether this message is the goodbye phrase and essentially nothing else
/// (plan §6).
///
/// Case and surrounding punctuation are ignored, and runs of whitespace count
/// as one space, so "Say goodbye, LatiBot!" matches. Anything with words
/// around it does not: the phrase stops the bot, and it should not fire
/// because somebody quoted it mid-sentence.
///
/// An empty phrase never matches, so clearing the setting turns the feature
/// off rather than making every message a match.
[[nodiscard]] auto is_goodbye(std::string_view content, std::string_view phrase) -> bool;

/// The pipeline stage.
///
/// Administrator only, checked by the caller and passed through
/// `incoming_message`. Consumes the message: nothing after this matters.
[[nodiscard]] auto goodbye_stage(const config::guild_settings& settings) -> pipeline::stage_fn;

} // namespace latibot::events
