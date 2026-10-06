// How a panel's form is read, however DPP lays its fields out: every panel
// relies on it, since that is where the panels once broke: each form arrived
// empty. Each module's panels are used end to end in its own tests, through
// support/panel_harness.hpp.

#include "core/ui/interaction.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/dpp.h>

#include <string>
#include <utility>
#include <vector>

// --------------------------------------------------------------------------
// How a form is read
// --------------------------------------------------------------------------

TEST_CASE("a form's fields are read however DPP lays them out", "[commands]") {
    auto input = [](const std::string& id, std::string value) {
        dpp::component field;
        field.set_type(dpp::cot_text).set_id(id);
        field.value = std::move(value);
        return field;
    };

    SECTION("each field on its own, as DPP 10.1 gives them") {
        const auto fields = latibot::ui::form_fields(std::vector{input("pattern", "hello"), input("mode", "")});
        CHECK(fields.size() == 2);
        CHECK(fields.at("pattern") == "hello");
        CHECK(fields.at("mode").empty());
    }

    SECTION("inside action rows, as older modals had them") {
        dpp::component row;
        row.add_component(input("pattern", "hello"));
        row.add_component(input("cooldown", "30"));
        const auto fields = latibot::ui::form_fields(std::vector{row});
        CHECK(fields.size() == 2);
        CHECK(fields.at("cooldown") == "30");
    }
}
