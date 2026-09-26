#pragma once

#include <dpp/coro/job.h>
#include <dpp/coro/task.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>

#include <string>
#include <string_view>

namespace latibot::ui {

// Answering panels' buttons, menus and forms, for the shell and the panels
// that route their own.

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
