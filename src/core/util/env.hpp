#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace latibot::util {

/// Reads an environment variable.
///
/// Secrets (the bot token, API keys) only ever come from the environment
/// (plan v4 §5.1). Returns nothing when the variable is unset; an empty value
/// is returned as an empty string, which callers should treat as unset.
[[nodiscard]] std::optional<std::string> env_var(const char* name);

/// Parses `.env`-style text: one `KEY=VALUE` per line, an optional leading
/// `export `, and surrounding quotes stripped from the value. Blank lines,
/// lines starting with `#`, and lines with no `=` are skipped rather than
/// rejected, since this is a development convenience, not a format anyone
/// should have to get exactly right.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> parse_dotenv(std::string_view text);

/// Loads `path` into the process environment, if it exists.
///
/// Never overrides a variable that is already set, so a real environment
/// (CI, a container, a shell export) always wins over the file. A missing
/// file is not an error: `.env` is optional, for local runs only.
void load_dotenv(const std::filesystem::path& path);

} // namespace latibot::util
