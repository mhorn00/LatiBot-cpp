#include "core/events/message_pipeline.hpp"

#include "core/util/log.hpp"

#include <exception>
#include <utility>

namespace latibot::events {

void pipeline::add(std::string name, stage_fn handler) {
    stages_.push_back({.name = std::move(name), .handler = std::move(handler)});
}

std::vector<action> pipeline::run(const incoming_message& message) const {
    std::vector<action> actions;

    // Answering ourselves is a loop with no exit. Answering another bot is one
    // too, unless this guild has said it wants that (plan v4 §5.4).
    if (message.from_self) {
        return actions;
    }
    if (message.from_bot && !message.author_is_allowed_bot) {
        // Debug rather than trace: "why did the bot ignore the other bot" is a
        // question worth being able to answer without raising the level twice.
        util::log().debug("ignoring a message from bot {}, which guild {} has not allowed", message.author_id.str(),
                          message.guild_id.str());
        return actions;
    }

    util::log().trace("message {} from {} in channel {}: \"{}\"", message.from_bot ? "(bot)" : "", message.author_id.str(),
                      message.channel_id.str(), message.content);

    for (const stage& entry : stages_) {
        stage_result result;
        try {
            result = entry.handler(message);
        } catch (const std::exception& error) {
            // One broken stage should not silence the rest of the pipeline,
            // and it certainly should not escape into DPP's event thread.
            util::log().error("message stage \"{}\" threw: {}", entry.name, error.what());
            continue;
        } catch (...) {
            util::log().error("message stage \"{}\" threw an unknown exception", entry.name);
            continue;
        }

        // Silence is the common case and not worth a line; a stage that wants
        // something done, or stops the others, is exactly what you are looking
        // for when reading back why a message was answered the way it was.
        if (!result.actions.empty() || result.consumed) {
            util::log().debug("stage \"{}\" wants {} action(s){}", entry.name, result.actions.size(),
                              result.consumed ? " and consumed the message" : "");
        }

        actions.insert(actions.end(), std::make_move_iterator(result.actions.begin()), std::make_move_iterator(result.actions.end()));

        if (result.consumed) {
            break;
        }
    }

    return actions;
}

std::vector<std::string_view> pipeline::stage_names() const {
    std::vector<std::string_view> names;
    names.reserve(stages_.size());
    for (const stage& entry : stages_) {
        names.emplace_back(entry.name);
    }
    return names;
}

} // namespace latibot::events
