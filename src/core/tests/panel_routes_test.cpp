// Panels claiming their views (docs/modules/Module_Plan_Final.md §4.6).

#include "core/ui/panel_routes.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <dpp/dpp.h>

#include <string>
#include <string_view>
#include <vector>

using latibot::ui::page_state;
using latibot::ui::panel_routes;

namespace {

/// A panel with forms, recording what reached it.
struct recording_panel {
    std::vector<std::string> seen;
    bool handles = true;

    auto on_component(const dpp::interaction_create_t& /*event*/, const page_state& state, const std::string& chosen) -> bool {
        seen.push_back(state.view + ":" + chosen);
        return handles;
    }
    auto on_form(const dpp::form_submit_t& /*event*/, const page_state& state) -> bool {
        seen.push_back("form " + state.view);
        return handles;
    }
};

/// A panel without forms, as the music queue's is.
struct buttons_only {
    int pressed = 0;

    auto on_component(const dpp::interaction_create_t& /*event*/, const page_state& /*state*/, const std::string& /*chosen*/) -> bool {
        ++pressed;
        return true;
    }
};

auto view(std::string name) -> page_state {
    page_state state;
    state.view = std::move(name);
    return state;
}

} // namespace

TEST_CASE("a component goes to the panel that claimed its view", "[ui]") {
    panel_routes routes;
    recording_panel triggers;
    recording_panel links;
    routes.add(triggers, {"trigpanel", "trigedit"}, "triggers");
    routes.add(links, {"urlpanel"}, "links");

    const dpp::button_click_t click;
    CHECK(routes.on_component(click, view("trigedit"), "7"));
    CHECK(routes.on_component(click, view("urlpanel"), ""));

    CHECK(triggers.seen == std::vector<std::string>{"trigedit:7"});
    CHECK(links.seen == std::vector<std::string>{"urlpanel:"});
    CHECK(routes.views() == std::vector<std::string_view>{"trigedit", "trigpanel", "urlpanel"});
}

TEST_CASE("a view nobody claimed, or one its panel declines, is not handled", "[ui]") {
    // Either way the shell answers that the button is from an older build.
    panel_routes routes;
    recording_panel declining;
    declining.handles = false;
    routes.add(declining, {"nicks"}, "nicknames");

    const dpp::button_click_t click;
    const dpp::form_submit_t form;
    CHECK_FALSE(routes.claimed("linkboard"));
    CHECK_FALSE(routes.on_component(click, view("linkboard"), ""));
    CHECK_FALSE(routes.on_form(form, view("linkboard")));
    CHECK_FALSE(routes.on_component(click, view("nicks"), ""));
    CHECK(declining.seen == std::vector<std::string>{"nicks:"});
}

TEST_CASE("a form goes to its panel, and a panel without forms handles none", "[ui]") {
    panel_routes routes;
    recording_panel triggers;
    buttons_only music;
    routes.add(triggers, {"trigform"}, "triggers");
    routes.add(music, {"musicq"}, "music");

    const dpp::form_submit_t form;
    CHECK(routes.on_form(form, view("trigform")));
    CHECK_FALSE(routes.on_form(form, view("musicq")));
    CHECK(triggers.seen == std::vector<std::string>{"form trigform"});

    const dpp::button_click_t click;
    CHECK(routes.on_component(click, view("musicq"), ""));
    CHECK(music.pressed == 1);
}

TEST_CASE("a view claimed twice stops startup, naming both, and the second panel claims nothing", "[ui]") {
    panel_routes routes;
    recording_panel first;
    recording_panel second;
    routes.add(first, {"trigpanel"}, "triggers");

    CHECK_THROWS_WITH(routes.add(second, {"echo", "trigpanel"}, "echo"),
                      Catch::Matchers::ContainsSubstring("\"trigpanel\" is claimed by both triggers and echo"));
    // Not even its other view: a panel is registered whole or not at all.
    CHECK_FALSE(routes.claimed("echo"));

    const dpp::button_click_t click;
    CHECK(routes.on_component(click, view("trigpanel"), ""));
    CHECK(first.seen.size() == 1);
    CHECK(second.seen.empty());
}
