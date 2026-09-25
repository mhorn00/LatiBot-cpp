#include "core/events/goodbye.hpp"

#include "core/config/guild_settings.hpp"
#include "core/util/log.hpp"

#include <cctype>
#include <string>

namespace latibot::events {
namespace {

/// Lowercased words, separated by single spaces.
///
/// Everything that is not a letter or a digit separates words, so commas and
/// exclamation marks make no difference. Bytes above 127 are kept as part of
/// a word rather than dropped, so a message with an emoji in it is not the
/// phrase: this stops the bot, and near enough is not good enough.
std::string words_of(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    bool pending_space = false;
    for (const char letter : text) {
        const auto byte = static_cast<unsigned char>(letter);
        const bool part_of_word = byte > 127 || std::isalnum(byte) != 0;

        if (!part_of_word) {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space) {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(static_cast<char>(std::tolower(byte)));
    }

    return normalized;
}

} // namespace

bool is_goodbye(std::string_view content, std::string_view phrase) {
    const std::string wanted = words_of(phrase);
    if (wanted.empty()) {
        return false;
    }
    return words_of(content) == wanted;
}

pipeline::stage_fn goodbye_stage(const config::guild_settings& settings) {
    return [&settings](const incoming_message& message) -> stage_result {
        const std::string phrase = settings.get(message.guild_id, goodbye_phrase_key, default_goodbye_phrase);
        if (!is_goodbye(message.content, phrase)) {
            return {};
        }

        // The phrase matched, so whatever happens next is worth a line: either
        // the bot is about to stop, or somebody just found out they cannot
        // stop it, which is the question that otherwise gets asked out loud.
        if (!message.author_is_administrator) {
            util::log().debug("{} said the goodbye phrase in guild {} without Administrator; ignoring it", message.author_id,
                              message.guild_id);
            return {};
        }

        util::log().info("{} said the goodbye phrase in guild {}; stopping", message.author_id, message.guild_id);
        return {.actions = {send_message{.channel_id = message.channel_id,
                                         .content = std::string(goodbye_reply),
                                         .flags = dpp::m_suppress_notifications},
                            stop_bot{.after = goodbye_delay}},
                .consumed = true};
    };
}

} // namespace latibot::events
