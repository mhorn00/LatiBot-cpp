#include "nickname_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "nicknames.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <vector>

namespace latibot::events {
namespace {

using capabilities::tool_done;
using capabilities::tool_refused;

/// How many the model is shown when it asks for none, and at most.
constexpr std::int64_t shown_by_default = 10;
constexpr std::int64_t shown_at_most = 50;

auto nickname_history(const nickname_store& store, const nlohmann::json& input, const capabilities::model_tool_context& context)
    -> capabilities::model_tool_result {
    dpp::snowflake user = context.author_id;
    if (const auto alias = capabilities::tool_text(input, "user")) {
        const auto found = context.user_for(*alias);
        if (!found) return tool_refused("there is nobody here with that alias");
        user = *found;
    }
    const auto limit = static_cast<std::size_t>(
        std::clamp(capabilities::tool_number(input, "limit").value_or(shown_by_default), std::int64_t{1}, shown_at_most));

    const std::vector<nickname_change> history = store.history(context.guild_id, user);
    if (history.empty()) return tool_done(std::format("no nickname of {}'s has been recorded here", context.shown_as(user)));

    std::string text = std::format(
        "{} has had {} nickname(s) recorded here, newest first. Each nickname is a marker: write it as it is, and it is "
        "shown as the nickname.\n",
        context.shown_as(user), history.size());
    for (std::size_t index = 0; index < history.size() && index < limit; ++index) {
        const nickname_change& change = history[index];
        const auto at = std::chrono::floor<std::chrono::minutes>(change.changed_at);
        text += std::format("- {} on {:%Y-%m-%d %H:%M} UTC", change.nickname ? context.hide(*change.nickname) : "(cleared)", at);
        if (change.changed_by) {
            text += std::format(", by {}", *change.changed_by == user ? std::string("themselves") : context.shown_as(*change.changed_by));
        } else if (change.source != nickname_source::imported) {
            text += ", by someone unknown";
        }
        text += '\n';
    }
    if (history.size() > limit) text += std::format("and {} older\n", history.size() - limit);
    return tool_done(text);
}

} // namespace

auto add_nickname_tools(capabilities::model_tools& tools, const nickname_store& store) -> void {
    tools.add(
        {.name = "nickname_history",
         .description = "Every nickname someone has had in this server, as /nicknames shows it: when, and who changed it. The "
                        "nicknames come as markers to write in your reply, since you never see names.",
         .input_schema = capabilities::tool_schema(
             {{"user", capabilities::tool_property("Their alias; whoever you are answering when left out.")},
              {"limit", capabilities::tool_property("How many, newest first, 1 to 50; 10 when left out.", "integer")}}),
         .changes = false,
         .run = [&store](nlohmann::json input, capabilities::model_tool_context context) -> dpp::task<capabilities::model_tool_result> {
             co_return nickname_history(store, input, context);
         }});
}

} // namespace latibot::events
