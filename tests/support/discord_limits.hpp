#pragma once

// Discord's limits on what a message or a modal may carry, checked in one
// place. The API is the only thing that enforces them, and it does so by
// refusing the whole message (plan §21.4), so every renderer's test runs its
// output through these, the states that add rows included.
//
// Text is measured in characters, as Discord measures it, except custom IDs,
// which are held to 100 bytes: the paginator refuses anything longer.

#include "core/ui/paginator.hpp"
#include "core/util/text.hpp"

#include <dpp/appcommand.h>
#include <dpp/message.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>
#include <string>
#include <utility>

namespace latibot::testing {

namespace discord_limit {
inline constexpr std::size_t content = 2000;
inline constexpr std::size_t rows = 5;
inline constexpr std::size_t per_row = 5;
inline constexpr std::size_t button_label = 80;
inline constexpr std::size_t select_options = 25;
inline constexpr std::size_t select_option_text = 100;
inline constexpr std::size_t select_placeholder = 150;
inline constexpr std::size_t modal_title = 45;
inline constexpr std::size_t input_label = 45;
inline constexpr std::size_t input_placeholder = 100;
inline constexpr std::size_t input_length = 4000;
} // namespace discord_limit

/// Checks one message's content and components. A custom ID must be unique
/// within its message, which is the rule a panel breaks when two of its rows
/// happen to encode the same state.
inline void check_message_fits(const dpp::message& message) {
    CHECK(util::character_count(message.content) <= discord_limit::content);
    CHECK(message.components.size() <= discord_limit::rows);

    std::set<std::string> ids;
    for (const dpp::component& row : message.components) {
        CHECK(row.type == dpp::cot_action_row);
        CHECK(row.components.size() <= discord_limit::per_row);

        for (const dpp::component& part : row.components) {
            INFO("custom_id: " << part.custom_id << ", label: " << part.label);

            // A link button is the one component with a URL and no ID.
            if (part.type == dpp::cot_button && part.style == dpp::cos_link) {
                CHECK_FALSE(part.url.empty());
            } else {
                CHECK_FALSE(part.custom_id.empty());
                CHECK(part.custom_id.size() <= ui::custom_id_limit);
                CHECK(ids.insert(part.custom_id).second);
            }

            if (part.type == dpp::cot_button) {
                CHECK(util::character_count(part.label) <= discord_limit::button_label);
            }

            if (part.type == dpp::cot_selectmenu) {
                // A select menu fills its row on its own.
                CHECK(row.components.size() == 1);
                CHECK(util::character_count(part.placeholder) <= discord_limit::select_placeholder);
                CHECK_FALSE(part.options.empty());
                CHECK(part.options.size() <= discord_limit::select_options);

                std::set<std::string> values;
                for (const dpp::select_option& option : part.options) {
                    INFO("option: " << option.label);
                    CHECK_FALSE(option.label.empty());
                    CHECK(util::character_count(option.label) <= discord_limit::select_option_text);
                    CHECK(util::character_count(option.value) <= discord_limit::select_option_text);
                    CHECK(util::character_count(option.description) <= discord_limit::select_option_text);
                    CHECK(values.insert(option.value).second);
                }
            }
        }
    }
}

/// Checks a modal: its title, its ID, and each text input's.
inline void check_modal_fits(const dpp::interaction_modal_response& form) {
    CHECK_FALSE(form.title.empty());
    CHECK(util::character_count(form.title) <= discord_limit::modal_title);
    CHECK_FALSE(form.custom_id.empty());
    CHECK(form.custom_id.size() <= ui::custom_id_limit);
    CHECK(form.components.size() <= discord_limit::rows);

    std::set<std::string> ids;
    for (const auto& row : form.components) {
        // One text input per row is all a modal takes.
        CHECK(row.size() == 1);
        for (const dpp::component& input : row) {
            INFO("input: " << input.custom_id << ", label: " << input.label);
            CHECK_FALSE(input.label.empty());
            CHECK(util::character_count(input.label) <= discord_limit::input_label);
            CHECK(util::character_count(input.placeholder) <= discord_limit::input_placeholder);
            CHECK_FALSE(input.custom_id.empty());
            CHECK(input.custom_id.size() <= ui::custom_id_limit);
            CHECK(ids.insert(input.custom_id).second);
            CHECK(std::cmp_less_equal(input.max_length, discord_limit::input_length));
        }
    }
}

} // namespace latibot::testing
