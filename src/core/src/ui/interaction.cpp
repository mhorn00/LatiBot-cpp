#include "core/ui/interaction.hpp"

#include "core/discord/message_flags.hpp"
#include "core/util/log.hpp"

#include <dpp/cluster.h>

#include <exception>
#include <utility>

namespace latibot::ui {

auto form_fields(const std::vector<dpp::component>& components) -> form_values {
    form_values fields;
    for (const dpp::component& part : components) {
        if (part.type == dpp::cot_action_row) {
            for (const dpp::component& input : part.components) {
                if (const auto* text = std::get_if<std::string>(&input.value)) fields.insert_or_assign(input.custom_id, *text);
            }
        } else if (const auto* text = std::get_if<std::string>(&part.value); text != nullptr && !part.custom_id.empty()) {
            fields.insert_or_assign(part.custom_id, *text);
        }
    }
    return fields;
}

auto form_fields(const dpp::form_submit_t& event) -> form_values {
    return form_fields(event.components);
}

auto update_panel(const dpp::interaction_create_t& event, dpp::message message) -> void {
    const auto kept = static_cast<discord::message_flags>(event.command.msg.flags);
    event.reply(dpp::ir_update_message, discord::apply_flags(message, kept, discord::channel_message_flags));
}

auto answer_privately(const dpp::interaction_create_t& event, std::string_view text) -> void {
    dpp::message note{std::string(text)};
    note.set_flags(dpp::m_ephemeral);
    event.reply(note);
}

auto detach(dpp::task<void> work, std::string what) -> dpp::job {
    try {
        co_await std::move(work);
    } catch (const std::exception& error) {
        util::log().error("{} threw: {}", what, error.what());
    } catch (...) {
        util::log().error("{} threw an unknown exception", what);
    }
}

} // namespace latibot::ui
