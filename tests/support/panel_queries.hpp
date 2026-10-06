#pragma once

#include <dpp/dpp.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace latibot::testing {

// What a panel test asks of a panel it opened through `panel_harness`.

/// The option in the panel's menus whose value is `value`, if any. A copy,
/// since the panel it came from is usually a temporary.
inline auto option_in(const dpp::message& panel, std::string_view value) -> std::optional<dpp::select_option> {
    for (const dpp::component& row : panel.components) {
        for (const dpp::component& part : row.components) {
            for (const dpp::select_option& option : part.options) {
                if (option.value == value) return option;
            }
        }
    }
    return std::nullopt;
}

inline auto has_button(const dpp::message& panel, std::string_view label) -> bool {
    for (const dpp::component& row : panel.components) {
        for (const dpp::component& part : row.components) {
            if (part.type == dpp::cot_button && part.label == label) return true;
        }
    }
    return false;
}

/// What a modal's field was filled with.
inline auto field_value(const nlohmann::json& answer, std::string_view id) -> std::string {
    std::optional<std::string> found;
    for (const nlohmann::json& label : answer["data"]["components"]) {
        if (label["component"].value("custom_id", "") == id) found = label["component"].value("value", std::string{});
    }
    INFO("the form has no field " << id);
    REQUIRE(found.has_value());
    return *found;
}

} // namespace latibot::testing
