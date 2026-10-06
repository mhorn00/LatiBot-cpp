#include "core/util/env.hpp"

#include "core/util/text.hpp"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

namespace latibot::util {

namespace {

auto unquote(std::string_view value) -> std::string_view {
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

} // namespace

auto env_var(const char* name) -> std::optional<std::string> {
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC because the returned pointer is
    // invalidated by a later _putenv; _dupenv_s hands back an owned copy.
    char* value = nullptr;
    std::size_t size = 0;

    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return std::nullopt;

    const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
    return std::string(owned.get());
#else
    const char* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string(value);
#endif
}

auto set_env_var(const std::string& name, const std::string& value) -> void {
#ifdef _MSC_VER
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), /*overwrite=*/1);
#endif
}

auto parse_dotenv(std::string_view text) -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> entries;

    for (const std::string_view raw : lines(text)) {
        // Blank lines and comments carry nothing.
        std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#') continue;

        if (line.starts_with("export ")) line = trim(line.substr(std::string_view("export ").size()));

        // KEY=VALUE, split on the first '=' so a value may contain more.
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;

        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = unquote(trim(line.substr(equals + 1)));
        if (key.empty()) continue;

        entries.emplace_back(std::string(key), std::string(value));
    }

    return entries;
}

auto load_dotenv(const std::filesystem::path& path) -> void {
    const std::ifstream file(path, std::ios::binary);
    if (!file) return;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    for (const auto& [key, value] : parse_dotenv(buffer.str())) {
        if (env_var(key.c_str())) continue; // a real environment variable always wins
        set_env_var(key, value);
    }
}

} // namespace latibot::util
