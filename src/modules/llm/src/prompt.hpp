#pragma once

#include "ask.hpp"
#include "memory.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::llm {

class people;

/// What the model is told it is called, in its fixed rules, the transcript
/// and the check, whatever the bot's Discord username: a test bot named
/// "LatiBot 2" must not read its own messages as somebody else's
/// (src/modules/llm/docs/Language_Model.md §3.3).
inline constexpr std::string_view model_name = "LatiBot";

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

    /// Who wrote it, by name, for a bot that is shown by name rather than an
    /// alias: a persona, wherever it is read, and LatiBot when a persona
    /// reads it (src/modules/llm/docs/Language_Model.md §3.9).
    std::string speaker;
};

/// What goes into the cached part of the instructions
/// (src/modules/llm/docs/Language_Model.md §3.3).
struct instruction_parts {
    /// The guild's `system` document: admins' rules.
    std::string system_document;

    /// The guild's `personality` document, which anyone may be allowed to
    /// edit, and so is placed last and labelled as style only.
    std::string personality;

    /// The `trigger_style` document, when an advanced trigger fired.
    std::string trigger_style;

    /// When the reply will be spoken as well as posted, what the speech
    /// capability tells the model about speaking; empty otherwise
    /// (src/modules/llm/docs/Language_Model.md §2.5).
    std::string speaking_guide;

    /// The persona answering, by name; empty for LatiBot. A persona has
    /// fixed rules of its own, and `system_document` and `personality` are
    /// its own documents (src/modules/llm/docs/Language_Model.md §2.11).
    std::string persona_name;
};

/// The instructions every request starts with, in the fixed order of
/// src/modules/llm/docs/Language_Model.md §3.3: the rules in code, then `system`,
/// then `personality`. Nothing in here changes from one message to the next,
/// which is what makes it worth caching.
[[nodiscard]] auto stable_instructions(const instruction_parts& parts) -> std::string;

/// The part that changes per message: the memories that matched, by the
/// alias of whom they are about, and the time. Everyone they are about must
/// have been met. Without `with_memories`, for a persona, which has no
/// memory, only the time.
[[nodiscard]] auto varying_instructions(std::span<const memory> memories, std::chrono::sys_seconds now, people& cast,
                                        bool with_memories = true) -> std::string;

/// One message as a line of the transcript: "alias: text", or "Name (you)"
/// for whoever is answering, "Name (a bot)" for a bot shown by name, its
/// content sanitized by `cast`, and anything longer than a message's worth
/// cut short (src/modules/llm/docs/Language_Model.md §3.8, §3.9).
[[nodiscard]] auto transcript_line(const context_message& message, people& cast) -> std::string;

/// What the model is asked: the recent conversation, oldest first, then the
/// message to answer, or for an advanced trigger what caught its attention.
/// When that message is a reply, `replied_to` is what it replies to, shown
/// just before it whether or not the transcript has it, since a reply can
/// be to something long gone from the channel's last few messages; null
/// otherwise (src/modules/llm/docs/Language_Model.md §2.3).
///
/// `history` is oldest first, and is cut from the oldest end to fit
/// `token_budget`, so the messages closest to the one being answered are the
/// ones kept (src/modules/llm/docs/Language_Model.md §2.3). The whole conversation
/// is one turn rather than a turn per message: several people talk in a
/// channel, and the providers expect two sides taking turns.
///
/// `how` says how the message came to the model: the message to answer for
/// someone talking to it, what caught its attention for an advanced trigger
/// (`context_prompt`), and for joining in, the choice of `silent_reply`.
[[nodiscard]] auto question_for(std::span<const context_message> history, const context_message& latest, const context_message* replied_to,
                                approach how, std::string_view context_prompt, std::size_t token_budget, people& cast) -> std::string;

/// What the model writes, joining in, to say nothing
/// (src/modules/llm/docs/Language_Model.md §2.10).
inline constexpr std::string_view silent_reply = "[silent]";

/// Whether the model chose to say nothing.
[[nodiscard]] auto is_silence(std::string_view answer) -> bool;

/// The conversation check's instructions: whether `answerer`, LatiBot or a
/// persona, should reply to the latest message, in one word
/// (src/modules/llm/docs/Language_Model.md §2.10). Fixed, and the same for
/// every server. With `question`, an analysis trigger's, the check answers
/// that about the latest message instead
/// (src/modules/triggers/docs/Triggers.md §2.6).
[[nodiscard]] auto check_instructions(std::string_view answerer = model_name, std::string_view question = {}) -> std::string;

/// What the check is asked: the recent messages, cut to `token_budget` from
/// the oldest end, what the latest replies to if anything, and the latest,
/// and whether `answerer` should reply to it, or `question` about it.
[[nodiscard]] auto check_question(std::span<const context_message> history, const context_message& latest,
                                  const context_message* replied_to, std::size_t token_budget, people& cast,
                                  std::string_view answerer = model_name, std::string_view question = {}) -> std::string;

/// Whether the check's answer is a yes. Anything else, a refusal included,
/// is a no.
[[nodiscard]] auto check_says_yes(std::string_view answer) -> bool;

/// The most messages one reply is posted as, and so with Discord's 2000
/// characters each, 8000 characters in all
/// (src/modules/llm/docs/Language_Model.md §2.3).
inline constexpr std::size_t reply_messages_limit = 4;

/// A reply cut into Discord messages of at most `limit` characters, on line
/// breaks where it can, and at most `most` of them; anything after that is
/// cut, and the last message says so.
[[nodiscard]] auto split_for_discord(std::string_view text, std::size_t limit = 2000, std::size_t most = reply_messages_limit)
    -> std::vector<std::string>;

} // namespace latibot::llm
