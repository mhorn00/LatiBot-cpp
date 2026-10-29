#pragma once

#include "ask.hpp"
#include "core/ports/result.hpp"
#include "tools.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::llm {

class persona_store;
struct persona;

/// The messages `talk_to` posted whose target is answering them already, so
/// the pipeline leaves them alone when Discord echoes them back
/// (src/modules/llm/docs/Language_Model.md §2.11). Noted before posting, since
/// the echo can arrive before the post returns. Thread-safe.
class handoffs {
public:
    /// How many are kept: an echo that never arrives is forgotten in time.
    static constexpr std::size_t kept = 100;

    auto expect(dpp::snowflake channel, std::string content) -> void;

    /// Whether `content` in `channel` is one, forgetting it if so.
    auto take(dpp::snowflake channel, std::string_view content) -> bool;

    /// Whether it is one, remembering it still: for the triggers, which see
    /// the message before the stage that takes it.
    [[nodiscard]] auto expects(dpp::snowflake channel, std::string_view content) const -> bool;

private:
    mutable std::mutex mutex_;
    std::deque<std::pair<dpp::snowflake, std::string>> expected_;
};

/// What `talk_to` works with.
struct talk_services {
    const persona_store* personas = nullptr;
    handoffs* expected = nullptr;

    /// A turn for one model answering another here, or why not.
    std::function<std::expected<std::chrono::seconds, std::string>(dpp::snowflake guild, dpp::snowflake channel)> claim_turn;

    /// Posts as LatiBot, `persona` 0, or a persona.
    std::function<dpp::task<ports::result<dpp::message>>(dpp::message message, std::int64_t persona, dpp::snowflake guild)> post;

    /// Has the target answer, in the background.
    std::function<void(ask_llm ask)> hand_off;
};

/// The tool's name.
inline constexpr std::string_view talk_tool_name = "talk_to";

/// Who `persona`, a persona or 0 for LatiBot, may reach with `talk_to`
/// among `personas`: LatiBot, for a persona that talks; and every enabled
/// persona that talks, but itself. By name.
[[nodiscard]] auto reachable_by(std::int64_t persona, const std::vector<llm::persona>& personas) -> std::vector<std::string>;

/// `talk_to`: posts a message in the channel as whoever is answering, to
/// LatiBot or a persona, which then answers it, paced as any bot turn is.
/// Only LatiBot and the personas that talk are given it, and only those
/// can be reached (src/modules/llm/docs/Language_Model.md §2.11).
auto add_talk_tool(tool_registry& tools, talk_services services) -> void;

} // namespace latibot::llm
