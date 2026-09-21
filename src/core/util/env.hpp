#pragma once

#include <optional>
#include <string>

namespace latibot::util {

/// Reads an environment variable.
///
/// Secrets (the bot token, API keys) only ever come from the environment
/// (plan v4 §5.1). Returns nothing when the variable is unset; an empty value
/// is returned as an empty string, which callers should treat as unset.
[[nodiscard]] std::optional<std::string> env_var(const char* name);

} // namespace latibot::util
