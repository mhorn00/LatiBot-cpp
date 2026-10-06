#pragma once

#include "core/config/config_error.hpp"

#include <dpp/json.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace latibot::config {

/// The values a number key may take, both ends included.
struct range {
    double lowest = std::numeric_limits<double>::lowest();
    double highest = std::numeric_limits<double>::max();
};

/// A number no smaller than `lowest`.
[[nodiscard]] constexpr auto at_least(double lowest) noexcept -> range {
    return {.lowest = lowest, .highest = std::numeric_limits<double>::max()};
}

namespace detail {

// Reads one value as a key's type, or throws config_error naming `key`, its
// whole name, such as "music.pot_provider_port".
auto read_value(const nlohmann::json& value, std::string_view key, bool& into) -> void;
auto read_value(const nlohmann::json& value, std::string_view key, int& into) -> void;
auto read_value(const nlohmann::json& value, std::string_view key, std::int64_t& into) -> void;
auto read_value(const nlohmann::json& value, std::string_view key, double& into) -> void;
auto read_value(const nlohmann::json& value, std::string_view key, std::string& into) -> void;
auto read_value(const nlohmann::json& value, std::string_view key, std::filesystem::path& into) -> void;

auto check_range(double value, std::string_view key, const range& limits) -> void;

} // namespace detail

/// One key of a config section: its name, the member it fills, what it is
/// for, and for a number, the values it may take
/// (docs/modules/Module_Plan_Final.md §8.2).
template <typename Config>
class key {
public:
    template <typename T>
    key(std::string_view name, T Config::* member, std::string_view description, std::optional<range> limits = std::nullopt)
        : name_(name),
          description_(description),
          read_([member, limits](const nlohmann::json& value, std::string_view full_name, Config& into) {
              T read{};
              detail::read_value(value, full_name, read);
              if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
                  if (limits) detail::check_range(static_cast<double>(read), full_name, *limits);
              } else {
                  static_cast<void>(limits); // only numbers have a range
              }
              into.*member = std::move(read);
          }),
          write_([member](const Config& from) -> nlohmann::ordered_json {
              if constexpr (std::is_same_v<T, std::filesystem::path>) {
                  return (from.*member).generic_string();
              } else {
                  return from.*member;
              }
          }) {}

    [[nodiscard]] auto name() const noexcept -> std::string_view { return name_; }
    [[nodiscard]] auto description() const noexcept -> std::string_view { return description_; }

    /// Reads `value` into `into`; `full_name` is for the message when it is
    /// the wrong type or out of range.
    auto read(const nlohmann::json& value, std::string_view full_name, Config& into) const -> void { read_(value, full_name, into); }

    /// `from`'s value, as config.json holds it.
    [[nodiscard]] auto write(const Config& from) const -> nlohmann::ordered_json { return write_(from); }

private:
    std::string_view name_;
    std::string_view description_;
    std::function<void(const nlohmann::json&, std::string_view, Config&)> read_;
    std::function<nlohmann::ordered_json(const Config&)> write_;
};

/// One object in config.json, such as "music": a module's settings, each key
/// declared once (docs/modules/Module_Plan_Final.md §8).
///
/// The table is the whole truth about the section: what it reads, what it
/// refuses, and what a fresh config.json says, which is a default-constructed
/// `Config`.
template <typename Config>
class section {
public:
    section(std::string_view name, std::vector<key<Config>> keys) : name_(name), keys_(std::move(keys)) {}

    [[nodiscard]] auto name() const noexcept -> std::string_view { return name_; }
    [[nodiscard]] auto keys() const noexcept -> const std::vector<key<Config>>& { return keys_; }

    /// The section's settings from its object in config.json. A key left out
    /// keeps its default; one the table does not have, of the wrong type, or
    /// out of range throws config_error naming it.
    [[nodiscard]] auto read(const nlohmann::json& object) const -> Config {
        if (!object.is_object()) throw config_error(std::format(R"(config key "{}" must be an object holding its settings)", name_));
        Config config{};
        for (const auto& [name, value] : object.items()) {
            const auto found = std::ranges::find(keys_, std::string_view{name}, &key<Config>::name);
            if (found == keys_.end()) throw config_error(std::format(R"(unknown config key "{}.{}")", name_, name));
            found->read(value, std::format("{}.{}", name_, name), config);
        }
        return config;
    }

    /// Every key at its default, in the table's order, as a fresh config.json
    /// holds the section.
    [[nodiscard]] auto defaults() const -> nlohmann::ordered_json {
        const Config config{};
        nlohmann::ordered_json written = nlohmann::ordered_json::object();
        for (const key<Config>& each : keys_) {
            written[std::string(each.name())] = each.write(config);
        }
        return written;
    }

private:
    std::string_view name_;
    std::vector<key<Config>> keys_;
};

} // namespace latibot::config
