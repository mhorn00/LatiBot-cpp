#pragma once

#include "core/ui/paginator.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/json.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::testing {

/// Uses a panel as a person would in Discord, offline.
///
/// Each press, choice and submission is written as the JSON Discord sends,
/// read by DPP's own interaction handler, and answered through DPP's own
/// `reply` and `dialog`. On DPP's webhook path those hand the answer back
/// instead of sending it, so a test goes through the same reading and
/// writing the bot does. Events built by hand skip both, which is how every
/// form came to arrive empty without a test noticing: DPP 10.1 wraps a
/// modal's fields in labels and hands them back unwrapped, not in rows.
class panel_harness {
public:
    using json = nlohmann::json;
    using component_router = std::function<bool(const dpp::interaction_create_t&, const ui::page_state&, const std::string&)>;
    using form_router = std::function<bool(const dpp::form_submit_t&, const ui::page_state&)>;

    dpp::snowflake guild{1000};
    dpp::snowflake channel{3000};
    dpp::snowflake user{11};

    /// What the member may do, which Discord sends with every interaction.
    std::uint64_t permissions = 0;
    std::vector<dpp::snowflake> roles;

    panel_harness(component_router on_component, form_router on_form)
        : on_component_(std::move(on_component)), on_form_(std::move(on_form)) {
        // As the bot's own handlers do: a button has no chosen value, and a
        // menu's is the first of its values.
        cluster_.on_button_click([this](const dpp::button_click_t& event) { route(event, event.custom_id, {}); });
        cluster_.on_select_click([this](const dpp::select_click_t& event) {
            route(event, event.custom_id, event.values.empty() ? std::string{} : event.values.front());
        });
        cluster_.on_form_submit([this](const dpp::form_submit_t& event) {
            event.set_queued_response({});
            const auto state = ui::decode(event.custom_id);
            REQUIRE(state.has_value());
            CHECK(on_form_(event, *state));
        });
    }

    /// Shows `message` as the answer to the slash command that opens the
    /// panel, privately, as every panel is.
    auto open(const dpp::message& message) -> void {
        panel_ = json::parse(message.build_json());
        panel_["id"] = "7000";
        panel_["channel_id"] = std::to_string(static_cast<std::uint64_t>(channel));
        panel_["flags"] = static_cast<std::uint64_t>(dpp::m_ephemeral);
    }

    /// The panel as it stands, after every update the bot has made to it.
    [[nodiscard]] auto panel() const -> dpp::message {
        json copy = panel_;
        return dpp::message().fill_from_json(&copy);
    }

    [[nodiscard]] auto content() const -> std::string { return panel_.value("content", std::string{}); }

    /// Presses the button labelled `label`, and gives back the answer.
    auto press(std::string_view label) -> json {
        const json* found =
            find_component([&](const json& part) { return part.value("type", 0) == dpp::cot_button && part.value("label", "") == label; });
        INFO("no button labelled " << label << " on:\n" << panel_.dump(2));
        REQUIRE(found != nullptr);
        return interact(3, {{"custom_id", (*found)["custom_id"]}, {"component_type", dpp::cot_button}});
    }

    /// Picks the option `value` in whichever menu has it, or, when two menus
    /// offer the same values, in the one for the view `in_view`.
    auto choose(std::string_view value, std::string_view in_view = {}) -> json {
        const json* found = find_component([&](const json& part) {
            if (part.value("type", 0) != dpp::cot_selectmenu || !part.contains("options")) return false;
            if (!in_view.empty() && !part.value("custom_id", "").starts_with(std::string(in_view) + ":")) return false;
            return std::ranges::any_of(part["options"], [&](const json& option) { return option.value("value", "") == value; });
        });
        INFO("no menu" << (in_view.empty() ? "" : " for " + std::string(in_view)) << " offers " << value << " on:\n" << panel_.dump(2));
        REQUIRE(found != nullptr);
        return interact(3, {{"custom_id", (*found)["custom_id"]}, {"component_type", dpp::cot_selectmenu}, {"values", {value}}});
    }

    /// A modal as a slash command's answer, for a form no panel opens.
    [[nodiscard]] static auto answer_with(const dpp::interaction_modal_response& modal) -> json { return json::parse(modal.build_json()); }

    /// Fills in the modal `answer` opened: each field keeps what the bot
    /// filled it with unless `typed` names it, as a person leaves most of a
    /// form alone. Then submits it, as Discord would send it back.
    auto submit(const json& answer, const std::map<std::string, std::string>& typed = {}) -> json {
        INFO("expected a modal, got:\n" << answer.dump(2));
        REQUIRE(is_modal(answer));
        const json& modal = answer["data"];

        json fields = json::array();
        int next_id = 1;
        for (const json& label : modal["components"]) {
            // What DPP sends is a label around each field; what comes back is
            // the same, with the field's value. Nothing unwrapped is sent.
            REQUIRE(label.value("type", 0) == dpp::cot_label);
            const json& input = label["component"];
            const std::string id = input.value("custom_id", "");
            const auto changed = typed.find(id);
            const std::string value = changed != typed.end() ? changed->second : input.value("value", std::string{});
            fields.push_back({{"type", dpp::cot_label},
                              {"id", next_id++},
                              {"component", {{"type", input["type"]}, {"id", next_id++}, {"custom_id", id}, {"value", value}}}});
        }
        for (const auto& [id, value] : typed) {
            INFO("the form has no field " << id);
            CHECK(std::ranges::any_of(fields, [&](const json& field) { return field["component"]["custom_id"] == id; }));
        }
        return interact(5, {{"custom_id", modal["custom_id"]}, {"components", fields}});
    }

    [[nodiscard]] static auto is_modal(const json& answer) -> bool { return answer.value("type", 0) == dpp::ir_modal_dialog; }
    [[nodiscard]] static auto is_update(const json& answer) -> bool { return answer.value("type", 0) == dpp::ir_update_message; }

    /// A note only the person who pressed sees, rather than a change to the
    /// panel.
    [[nodiscard]] static auto is_private_note(const json& answer) -> bool {
        return answer.value("type", 0) == dpp::ir_channel_message_with_source &&
               (answer["data"].value("flags", std::uint64_t{0}) & dpp::m_ephemeral) != 0;
    }

    [[nodiscard]] static auto text_of(const json& answer) -> std::string {
        return answer.contains("data") ? answer["data"].value("content", std::string{}) : std::string{};
    }

private:
    auto route(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen) -> void {
        event.set_queued_response({});
        const auto state = ui::decode(custom_id);
        REQUIRE(state.has_value());
        CHECK(on_component_(event, *state, chosen));
    }

    template <typename Match>
    auto find_component(Match match) const -> const json* {
        if (!panel_.contains("components")) return nullptr;
        for (const json& row : panel_["components"]) {
            if (!row.contains("components")) continue;
            for (const json& part : row["components"]) {
                if (match(part)) return &part;
            }
        }
        return nullptr;
    }

    /// One interaction on the panel, through DPP, and what the bot answered.
    /// An answer that updates the panel is kept as the panel from then on.
    auto interact(int type, json data) -> json {
        json member{{"user", {{"id", std::to_string(static_cast<std::uint64_t>(user))}, {"username", "tester"}}},
                    {"roles", json::array()},
                    {"permissions", std::to_string(permissions)},
                    {"joined_at", "2026-01-01T00:00:00+00:00"}};
        for (const dpp::snowflake role : roles) {
            member["roles"].push_back(std::to_string(static_cast<std::uint64_t>(role)));
        }

        json interaction{{"id", std::to_string(++interaction_id_)},
                         {"application_id", "42"},
                         {"type", type},
                         {"token", "token"},
                         {"version", 1},
                         {"guild_id", std::to_string(static_cast<std::uint64_t>(guild))},
                         {"channel_id", std::to_string(static_cast<std::uint64_t>(channel))},
                         {"member", member},
                         {"data", std::move(data)}};
        // A modal a slash command opened belongs to no message.
        if (!panel_.empty()) interaction["message"] = panel_;
        const std::string raw = interaction.dump();
        INFO("the interaction: " << raw);
        const std::string reply = dpp::events::internal_handle_interaction(&cluster_, 0, interaction, raw, true);
        INFO("the answer: " << reply);
        REQUIRE_FALSE(reply.empty());

        json answer = json::parse(reply);
        if (is_update(answer)) {
            const std::string id = panel_.value("id", "7000");
            const auto flags = panel_.value("flags", std::uint64_t{0});
            panel_ = answer["data"];
            panel_["id"] = id;
            panel_["channel_id"] = std::to_string(static_cast<std::uint64_t>(channel));
            panel_["flags"] = flags;
        }
        return answer;
    }

    // Never started: no token, no connection. It only reads events and
    // writes answers.
    dpp::cluster cluster_{1U};

    component_router on_component_;
    form_router on_form_;
    json panel_ = json::object();
    std::uint64_t interaction_id_ = 9000;
};

} // namespace latibot::testing
