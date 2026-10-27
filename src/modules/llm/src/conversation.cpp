#include "conversation.hpp"

#include "core/ports/clock.hpp"
#include "core/util/text.hpp"

#include <cctype>
#include <string>

namespace latibot::llm {
namespace {

auto is_word_character(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return byte == '_' || byte >= 128 || std::isalnum(byte) != 0;
}

} // namespace

auto names_bot(std::string_view text, std::string_view bot_name) -> bool {
    if (bot_name.empty()) return false;
    const std::string haystack = util::to_lower(text);
    const std::string needle = util::to_lower(bot_name);

    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        const std::size_t end = at + needle.size();
        const bool starts_word = at == 0 || !is_word_character(haystack[at - 1]);
        const bool ends_word = end == haystack.size() || !is_word_character(haystack[end]);
        if (starts_word && ends_word) return true;
    }
    return false;
}

auto conversation_windows::opened(dpp::snowflake channel, std::int64_t persona) -> void {
    const auto now = clock_->steady_now();
    const std::scoped_lock guard(mutex_);
    windows_[{channel, persona}] = {.started = now, .last_reply = now, .unprompted = 0};
}

auto conversation_windows::joined_in(dpp::snowflake channel, std::int64_t persona) -> void {
    const auto now = clock_->steady_now();
    const std::scoped_lock guard(mutex_);
    const auto [entry, added] = windows_.try_emplace({channel, persona}, window{.started = now, .last_reply = now, .unprompted = 0});
    entry->second.last_reply = now;
    ++entry->second.unprompted;
}

auto conversation_windows::still_open(const window& open, std::chrono::steady_clock::time_point now, const conversation_rules& rules)
    -> bool {
    return now - open.last_reply <= rules.quiet_after && now - open.started <= rules.longest && open.unprompted < rules.replies;
}

auto conversation_windows::open(dpp::snowflake channel, const conversation_rules& rules, std::int64_t persona) const -> bool {
    if (!rules.enabled) return false;
    const auto now = clock_->steady_now();
    const std::scoped_lock guard(mutex_);
    const auto found = windows_.find({channel, persona});
    return found != windows_.end() && still_open(found->second, now, rules);
}

auto conversation_windows::latest_open(dpp::snowflake channel, const conversation_rules& rules,
                                       std::span<const std::int64_t> speakers) const -> std::optional<std::int64_t> {
    if (!rules.enabled) return std::nullopt;
    const auto now = clock_->steady_now();
    const std::scoped_lock guard(mutex_);
    std::optional<std::int64_t> latest;
    std::chrono::steady_clock::time_point latest_reply;
    for (const std::int64_t speaker : speakers) {
        const auto found = windows_.find({channel, speaker});
        if (found == windows_.end() || !still_open(found->second, now, rules)) continue;
        if (!latest || found->second.last_reply > latest_reply) {
            latest = speaker;
            latest_reply = found->second.last_reply;
        }
    }
    return latest;
}

auto channel_activity::started_typing(dpp::snowflake channel, dpp::snowflake user) -> void {
    const auto now = clock_->steady_now();
    const std::scoped_lock guard(mutex_);
    typing_[{channel, user}] = now;
}

auto channel_activity::posted(dpp::snowflake channel, dpp::snowflake user, dpp::snowflake message) -> void {
    const std::scoped_lock guard(mutex_);
    typing_.erase({channel, user});
    latest_[{channel, user}] = message;
}

auto channel_activity::anyone_typing(dpp::snowflake channel) const -> bool {
    const auto now = clock_->steady_now();
    const std::scoped_lock guard(mutex_);
    for (auto entry = typing_.lower_bound({channel, dpp::snowflake{}}); entry != typing_.end() && entry->first.first == channel; ++entry) {
        if (now - entry->second < typing_shown_for) return true;
    }
    return false;
}

auto channel_activity::is_latest(dpp::snowflake channel, dpp::snowflake user, dpp::snowflake message) const -> bool {
    const std::scoped_lock guard(mutex_);
    const auto found = latest_.find({channel, user});
    return found == latest_.end() || found->second == message;
}

} // namespace latibot::llm
