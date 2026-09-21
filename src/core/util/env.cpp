#include "core/util/env.hpp"

#include <cstdlib>
#include <memory>

namespace latibot::util {

std::optional<std::string> env_var(const char* name) {
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC because the returned pointer is
    // invalidated by a later _putenv; _dupenv_s hands back an owned copy.
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
    return std::string(owned.get());
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

} // namespace latibot::util
