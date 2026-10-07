#pragma once

/// Where each message stage runs (src/core/docs/Message_Pipeline.md §2.2).
///
/// The core names the positions and a module picks one, so the order is
/// written down here once rather than decided by which module happened to
/// start first. Two stages at one position stop startup
/// (docs/modules/Module_Plan_Final.md §4.4). The gaps leave room for a stage
/// between two others without renumbering.
namespace latibot::events::stage_order {

/// The goodbye phrase (core): first, so nothing answers a message that shuts
/// the bot down.
inline constexpr int stop = 100;

/// Link replacement: rewrites what was posted, before anything replies to it.
inline constexpr int rewrite = 200;

/// Trigger replies.
inline constexpr int reply = 300;

/// The language model, last: it consumes what it answers, and a trigger's
/// reply before it keeps an advanced trigger quiet.
inline constexpr int model = 400;

} // namespace latibot::events::stage_order
