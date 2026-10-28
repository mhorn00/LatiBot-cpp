#include "module_tools.hpp"

#include "aliases.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <utility>

namespace latibot::llm {

module_tools::module_tools(tool_registry& tools, const ports::discord_gateway& discord, post_fn post)
    : tools_(&tools), discord_(&discord), post_(std::move(post)) {}

auto find_channel(std::string_view named, dpp::snowflake guild, const ports::discord_gateway& discord) -> std::optional<dpp::snowflake> {
    std::string_view wanted = util::trim(named);
    if (wanted.starts_with("<#") && wanted.ends_with('>')) wanted = wanted.substr(2, wanted.size() - 3);
    if (wanted.starts_with('#')) wanted.remove_prefix(1);
    if (const auto id = util::parse_snowflake(wanted)) {
        const auto found = discord.channel_details(*id);
        if (!found || found->guild_id != guild) return std::nullopt;
        return *id;
    }
    if (const auto server = discord.server_details(guild)) {
        for (const ports::channel_details& channel : server->channels) {
            if (util::equals_ignoring_case(channel.name, wanted)) return channel.id;
        }
    }
    return std::nullopt;
}

auto module_context(const tool_context& context, const ports::discord_gateway& discord, const module_tools::post_fn& post)
    -> capabilities::model_tool_context {
    people* cast = context.cast;
    return {.guild_id = context.guild_id,
            .channel_id = context.channel_id,
            .author_id = context.author_id,
            .message_id = context.message_id,
            .author_permissions = discord.member_permissions(context.guild_id, context.channel_id, context.author_id).value_or(0),
            .now = context.now,
            .user_for = [cast](std::string_view alias) { return cast == nullptr ? std::nullopt : cast->user_for(alias); },
            .shown_as = [cast](dpp::snowflake user) { return cast == nullptr ? std::string("someone") : cast->meet(user); },
            .restore = [cast](std::string_view text) { return cast == nullptr ? std::string(text) : cast->restore(text); },
            .sanitize = [cast](std::string_view text) { return cast == nullptr ? std::string(text) : cast->sanitize(text); },
            .channel_for = [&discord, guild = context.guild_id](std::string_view named) { return find_channel(named, guild, discord); },
            .hide = [cast](std::string_view text) { return cast == nullptr ? std::string("something") : cast->hide(std::string(text)); },
            .post = [post, persona = context.persona_id,
                     guild = context.guild_id](dpp::message message) { return post(std::move(message), persona, guild); }};
}

auto module_tools::add(capabilities::model_tool tool) -> void {
    tool_definition definition{.name = tool.name, .description = std::move(tool.description), .input_schema = std::move(tool.input_schema)};
    tools_->add_async(std::move(definition),
                      [this, name = tool.name, changes = tool.changes, run = std::move(tool.run)](
                          nlohmann::json input, tool_context context) -> dpp::task<tool_outcome> {
                          if (changes && context.asker_is_bot) {
                              co_return tool_outcome{.content = "this message is a bot's: tools that change something only act for people",
                                                     .is_error = true};
                          }
                          const capabilities::model_tool_result result =
                              co_await run(std::move(input), module_context(context, *discord_, post_));
                          if (changes && !result.is_error) {
                              util::log().info("the model used {} in guild {}, asked by {}", name, context.guild_id, context.author_id);
                          }
                          co_return tool_outcome{.content = result.content, .is_error = result.is_error};
                      });
}

} // namespace latibot::llm
