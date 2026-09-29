#pragma once

#include <dpp/json.h>

#include <cstdint>
#include <string>

namespace latibot::llm {

// Reading fields out of a provider's reply. Each returns something empty
// rather than throwing when the field is missing or of another type: a reply
// shaped differently from what was expected should cost a field, not the
// reply.

[[nodiscard]] inline auto text_at(const nlohmann::json& object, const char* key) -> std::string {
    if (!object.is_object()) return {};
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] inline auto count_at(const nlohmann::json& object, const char* key) -> std::int64_t {
    if (!object.is_object()) return 0;
    const auto found = object.find(key);
    return found != object.end() && found->is_number_integer() ? found->get<std::int64_t>() : 0;
}

} // namespace latibot::llm
