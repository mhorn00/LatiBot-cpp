#include "core/ui/interaction.hpp"

#include "core/discord/message_flags.hpp"
#include "core/util/log.hpp"

#include <dpp/cluster.h>

#include <exception>
#include <utility>

namespace latibot::ui {

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
