#include "link_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "links/url_rules.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace latibot::events {
namespace {

auto fix_link(const url_rule_store& rules, const nlohmann::json& input, const capabilities::model_tool_context& context)
    -> capabilities::model_tool_result {
    const auto text = capabilities::tool_text(input, "url");
    if (!text) return capabilities::tool_refused("fix_link needs the link, or text with links in it");

    // The same reading of the text a real message gets, so the model's
    // answer and the replacement it would have had agree.
    const std::vector<url_rule> known = rules.for_guild(context.guild_id);
    const std::vector<link_verdict> verdicts = explain_links(*text, known);
    if (verdicts.empty()) return capabilities::tool_refused("there is no link in that");

    std::string out;
    for (const link_verdict& verdict : verdicts) {
        if (verdict.decision != link_decision::replaced) {
            out += std::format("- {}: left alone ({})\n", verdict.link.original_url, to_string(verdict.decision));
            continue;
        }
        out += std::format("- {} becomes {}", verdict.link.original_url, mirror_url(verdict.link, 0));
        for (std::size_t index = 1; index < verdict.link.mirrors.size(); ++index) {
            out += std::format(", or else {}", mirror_url(verdict.link, index));
        }
        out += '\n';
    }
    return capabilities::tool_done(out);
}

} // namespace

auto add_link_tools(capabilities::model_tools& tools, const url_rule_store& rules) -> void {
    tools.add(
        {.name = "fix_link",
         .description = "The same link on a site whose preview Discord shows, by this server's /links rules: for a link someone "
                        "says has no preview. Write the link it gives you into your reply. A site with no rule is left alone.",
         .input_schema = capabilities::tool_schema(
             {{"url", capabilities::tool_property("The link, or a message with links in it; at most five are fixed.")}}, {"url"}),
         .changes = false,
         .run = [&rules](nlohmann::json input, capabilities::model_tool_context context) -> dpp::task<capabilities::model_tool_result> {
             co_return fix_link(rules, input, context);
         }});
}

} // namespace latibot::events
