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
/// (plan §5.1). Returns nothing when the variable is unset; an empty value
/// is returned as an empty string, which callers should treat as unset.
[[nodiscard]] auto env_var(const char* name) -> std::optional<std::string>;

/// Sets an environment variable for this process, replacing any current
/// value. Only useful for variables read by a library we cannot configure
/// directly, which is why `SSL_CERT_FILE` exists (see ca_certificates.hpp).
auto set_env_var(const std::string& name, const std::string& value) -> void;

/// Parses `.env`-style text: one `KEY=VALUE` per line, an optional leading
/// `export `, and surrounding quotes stripped from the value. Blank lines,
/// lines starting with `#`, and lines with no `=` are skipped rather than
/// rejected, since this is a development convenience, not a format anyone
/// should have to get exactly right.
[[nodiscard]] auto parse_dotenv(std::string_view text) -> std::vector<std::pair<std::string, std::string>>;

/// Loads `path` into the process environment, if it exists.
///
/// Never overrides a variable that is already set, so a real environment
/// (CI, a container, a shell export) always wins over the file. A missing
/// file is not an error: `.env` is optional, for local runs only.
auto load_dotenv(const std::filesystem::path& path) -> void;

} // namespace latibot::util
