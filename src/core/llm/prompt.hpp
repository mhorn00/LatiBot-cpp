#pragma once

#include "core/llm/memory.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::llm {

/// A message in the channel, as the model is shown it.
struct context_message {
    dpp::snowflake id;
    dpp::snowflake author_id;
    std::string author_name;

    /// Written by LatiBot itself.
    bool from_me = false;

    /// Written by some other bot.
    bool from_bot = false;

    std::string content;
};

/// What goes into the cached part of the instructions
/// (docs/features/Language_Model.md §3.3).
struct instruction_parts {
    /// The guild's `system` document: admins' rules.
    std::string system_document;

    /// The guild's `personality` document, which anyone may be allowed to
    /// edit, and so is placed last and labelled as style only.
    std::string personality;

    /// The `trigger_style` document, when an advanced trigger fired.
    std::string trigger_style;

    /// The reply will be spoken as well as posted
    /// (docs/features/Language_Model.md §2.5).
    bool speaking = false;
};

/// The instructions every request starts with, in the fixed order of
/// docs/features/Language_Model.md §3.3: the rules in code, then `system`,
/// then `personality`. Nothing in here changes from one message to the next,
/// which is what makes it worth caching.
[[nodiscard]] auto stable_instructions(const instruction_parts& parts) -> std::string;

/// The part that changes per message: the memories that matched, and the
/// time.
[[nodiscard]] auto varying_instructions(std::span<const memory> memories, std::chrono::sys_seconds now) -> std::string;

/// One message as a line of the transcript: "Name (id): text", with
/// mentions of the bot written as its name, and anything longer than a
/// message's worth cut short.
[[nodiscard]] auto transcript_line(const context_message& message, dpp::snowflake bot_id, std::string_view bot_name) -> std::string;

/// What the model is asked: the recent conversation, oldest first, then the
/// message to answer, or for an advanced trigger what caught its attention.
///
/// `history` is oldest first, and is cut from the oldest end to fit
/// `token_budget`, so the messages closest to the one being answered are the
/// ones kept (docs/features/Language_Model.md §2.3). The whole conversation
/// is one turn rather than a turn per message: several people talk in a
/// channel, and the providers expect two sides taking turns.
[[nodiscard]] auto question_for(std::span<const context_message> history, const context_message& latest, std::string_view context_prompt,
                                std::size_t token_budget, dpp::snowflake bot_id, std::string_view bot_name) -> std::string;

/// A reply cut into Discord messages of at most `limit` characters, on line
/// breaks where it can, and at most `most` of them; anything after that is
/// cut, and the last message says so.
[[nodiscard]] auto split_for_discord(std::string_view text, std::size_t limit = 2000, std::size_t most = 3) -> std::vector<std::string>;

} // namespace latibot::llm
