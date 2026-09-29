#pragma once

#include <dpp/coro/job.h>
#include <dpp/coro/task.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::ui {

// Answering panels' buttons, menus and forms, for the shell and the panels
// that route their own.

/// A submitted modal's text fields, by the id each was built with.
using form_values = std::map<std::string, std::string, std::less<>>;

/// What a modal sent back, read from however DPP laid it out.
///
/// DPP 10.1 sends each field wrapped in a Label, and hands back the fields
/// themselves as `components`, one per entry. Older DPP, and Discord's
/// older modals, had action rows with the fields inside them. Both are
/// read, so neither a DPP update nor a change back loses what was typed:
/// reading only the rows is what once made every form arrive empty.
[[nodiscard]] auto form_fields(const std::vector<dpp::component>& components) -> form_values;
[[nodiscard]] auto form_fields(const dpp::form_submit_t& event) -> form_values;

/// Replaces the message a panel's button or form belongs to.
///
/// The message keeps the flags it was sent with, which its command chose:
/// whether it is ephemeral cannot change after it is sent, but whether it
/// shows previews can, so an update without them would bring back previews
/// the command hid.
auto update_panel(const dpp::interaction_create_t& event, dpp::message message) -> void;

/// Answers a button, menu or form with a note only the person who used it
/// sees.
auto answer_privately(const dpp::interaction_create_t& event, std::string_view text) -> void;

/// Runs a coroutine to the end with nobody waiting on it.
///
/// `dpp::job` is DPP's fire-and-forget coroutine. The catch is the point of
/// this function: an exception leaving a job is rethrown on whichever DPP
/// thread resumed it, which would end the process.
auto detach(dpp::task<void> work, std::string what) -> dpp::job;

} // namespace latibot::ui
