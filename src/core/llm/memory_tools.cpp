#include "core/llm/memory_tools.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <format>

namespace latibot::llm {
namespace {

using json = nlohmann::json;

/// How many memories `recall` hands back.
constexpr std::size_t recall_limit = 10;

auto refuse(std::string reason) -> tool_outcome {
    return {.content = std::move(reason), .is_error = true};
}

auto describe(const memory& entry) -> std::string {
    const std::string about = entry.subject ? std::format(" (about user {})", *entry.subject) : std::string{};
    return std::format("#{}{}: {}", entry.id, about, entry.content);
}

/// A Discord id the model passed, as a string or a number.
auto id_in(const json& input, const char* key) -> std::optional<dpp::snowflake> {
    const auto found = input.find(key);
    if (found == input.end()) return std::nullopt;
    if (found->is_string()) return util::parse_snowflake(found->get<std::string>());
    if (found->is_number_unsigned()) return dpp::snowflake{found->get<std::uint64_t>()};
    return std::nullopt;
}

auto remember(memory_store& store, const json& input, const tool_context& context) -> tool_outcome {
    const auto content = input.find("content");
    if (content == input.end() || !content->is_string() || util::is_blank(content->get<std::string>())) {
        return refuse("remember needs the content to save");
    }

    const std::string text(util::trim(content->get<std::string>()));
    if (util::character_count(text) > memory_length_limit) {
        return refuse(std::format("that is longer than {} characters; save something shorter", memory_length_limit));
    }
    if (store.count(context.guild_id) >= memories_per_guild) {
        return refuse(std::format("this server already has {} memories, the most it may; forget one first", memories_per_guild));
    }

    // An id that is not one is dropped rather than refused: the memory is
    // still worth having, as one about the server.
    const std::optional<dpp::snowflake> about = input.is_object() ? id_in(input, "about_user_id") : std::nullopt;
    const std::int64_t id = store.add({.id = 0,
                                       .guild_id = context.guild_id,
                                       .subject = about,
                                       .content = text,
                                       .created_by = context.author_id,
                                       .created_at = context.now});
    util::log().info("the model remembered #{} in guild {}, asked by {}: \"{}\"", id, context.guild_id, context.author_id, text);
    return {.content = std::format("saved as #{}", id), .is_error = false};
}

auto recall(const memory_store& store, const json& input, const tool_context& context) -> tool_outcome {
    const auto query = input.find("query");
    if (query == input.end() || !query->is_string()) return refuse("recall needs a query");

    const std::vector<memory> found = store.search(context.guild_id, query->get<std::string>(), recall_limit);
    if (found.empty()) return {.content = "nothing matches that", .is_error = false};

    std::string listed;
    for (const memory& entry : found) {
        listed += describe(entry);
        listed += '\n';
    }
    return {.content = listed, .is_error = false};
}

auto forget(memory_store& store, const json& input, const tool_context& context) -> tool_outcome {
    const auto id = input.find("id");
    if (id == input.end() || !id->is_number_integer()) return refuse("forget needs the memory's id, a number");

    const auto entry = store.find(id->get<std::int64_t>(), context.guild_id);
    if (!entry) return refuse("there is no memory with that id here");
    if (entry->subject != context.author_id && entry->created_by != context.author_id) {
        return refuse(
            "you may only forget memories about the person you are talking to, or ones they had you save; an admin can "
            "remove others with /memory");
    }

    store.remove(entry->id, context.guild_id);
    util::log().info("the model forgot #{} in guild {}, asked by {}: \"{}\"", entry->id, context.guild_id, context.author_id,
                     entry->content);
    return {.content = std::format("forgot #{}", entry->id), .is_error = false};
}

} // namespace

auto add_memory_tools(tool_registry& tools, memory_store& store) -> void {
    tools.add({.name = "remember",
               .description = std::format("Save a fact worth knowing in later conversations here: something about a person, or about the "
                                          "server. Keep it short and self-contained, at most {} characters. When it is about one person, "
                                          "pass their user id, shown beside their name in the conversation.",
                                          memory_length_limit),
               .input_schema = {{"type", "object"},
                                {"properties",
                                 {{"content", {{"type", "string"}, {"description", "The fact, as a sentence."}}},
                                  {"about_user_id", {{"type", "string"}, {"description", "The user id it is about, if one person."}}}}},
                                {"required", json::array({"content"})}}},
              [&store](const json& input, const tool_context& context) { return remember(store, input, context); });

    tools.add({.name = "recall",
               .description = "Search this server's saved memories by keywords. Returns each match with its id.",
               .input_schema = {{"type", "object"},
                                {"properties", {{"query", {{"type", "string"}, {"description", "Words to look for."}}}}},
                                {"required", json::array({"query"})}}},
              [&store](const json& input, const tool_context& context) { return recall(store, input, context); });

    tools.add({.name = "forget",
               .description = "Delete a saved memory by its id: one about the person you are talking to, or one they had you save.",
               .input_schema = {{"type", "object"},
                                {"properties", {{"id", {{"type", "integer"}, {"description", "The memory's id, without the #."}}}}},
                                {"required", json::array({"id"})}}},
              [&store](const json& input, const tool_context& context) { return forget(store, input, context); });
}

} // namespace latibot::llm
