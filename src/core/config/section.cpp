#include "core/config/section.hpp"

#include <cmath>

namespace latibot::config::detail {
namespace {

[[noreturn]] auto wrong_type(std::string_view key, std::string_view expected) -> void {
    throw config_error(std::format(R"(config key "{}" must be {})", key, expected));
}

/// Whole numbers print as such, so a range reads "1 to 65535", not
/// "1.000000 to 65535.000000".
auto shown(double value) -> std::string {
    return value == std::floor(value) ? std::format("{}", static_cast<std::int64_t>(value)) : std::format("{}", value);
}

} // namespace

auto read_value(const nlohmann::json& value, std::string_view key, bool& into) -> void {
    if (!value.is_boolean()) wrong_type(key, "true or false");
    into = value.get<bool>();
}

auto read_value(const nlohmann::json& value, std::string_view key, int& into) -> void {
    if (!value.is_number_integer()) wrong_type(key, "a whole number");
    into = value.get<int>();
}

auto read_value(const nlohmann::json& value, std::string_view key, std::int64_t& into) -> void {
    if (!value.is_number_integer()) wrong_type(key, "a whole number");
    into = value.get<std::int64_t>();
}

auto read_value(const nlohmann::json& value, std::string_view key, double& into) -> void {
    if (!value.is_number()) wrong_type(key, "a number");
    into = value.get<double>();
}

auto read_value(const nlohmann::json& value, std::string_view key, std::string& into) -> void {
    if (!value.is_string()) wrong_type(key, "a string");
    into = value.get<std::string>();
}

auto read_value(const nlohmann::json& value, std::string_view key, std::filesystem::path& into) -> void {
    if (!value.is_string()) wrong_type(key, "a string");
    into = value.get<std::string>();
}

auto check_range(double value, std::string_view key, const range& limits) -> void {
    if (value >= limits.lowest && value <= limits.highest) return;
    if (limits.highest == std::numeric_limits<double>::max()) {
        throw config_error(std::format(R"(config key "{}" must be at least {})", key, shown(limits.lowest)));
    }
    throw config_error(std::format(R"(config key "{}" must be {} to {})", key, shown(limits.lowest), shown(limits.highest)));
}

} // namespace latibot::config::detail
