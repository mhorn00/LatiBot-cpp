#pragma once

#include "core/llm/memory.hpp"
#include "core/llm/tools.hpp"

namespace latibot::llm {

/// `remember`, `recall` and `forget`: the model's long-term memory, over
/// `store` (docs/features/Language_Model.md §3.4).
///
/// The model may forget only what is about the person it is answering, or
/// what that person had it remember. Everything else is an admin's to
/// remove with `/memory`, so somebody cannot talk the model into erasing
/// what it knows about somebody else.
auto add_memory_tools(tool_registry& tools, memory_store& store) -> void;

} // namespace latibot::llm
