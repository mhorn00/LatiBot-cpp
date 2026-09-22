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

    if (message.from_self || message.from_bot) {
        return actions;
    }

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

        actions.insert(actions.end(), std::make_move_iterator(result.actions.begin()),
                       std::make_move_iterator(result.actions.end()));

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
